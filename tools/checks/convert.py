#!/usr/bin/env python3
"""Temporary R1/R2 migration tool; contract: docs/design/tools/checks/convert.md.

--output DIR writes only converted Python files, at repository-relative paths.
Apply/review those files in R2; this command never edits the legacy registration.
--compare OLD_TREE compares against a separate, pre-conversion input snapshot;
stdout is a JSON record (including main and the complete commit list).
"""
from __future__ import annotations

import argparse
import ast
from collections import Counter
from collections.abc import Callable, Mapping
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import types


def _legacy(root: Path):
    """Instrument only the loaded snapshot, immediately before mode selection.

    The legacy plan's branches and return value remain intact. No compatibility
    projection of STAGE1/MUT1 is involved, and each load owns its dependency cache.
    """
    path = root / "tools/check_select.py"
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    plan = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "plan")
    points = [i for i, n in enumerate(plan.body) if isinstance(n, ast.If)
              and ast.unparse(n.test) == "not changed"]
    if len(points) != 1:
        raise ValueError(f"{path}: legacy plan mode boundary not found")
    plan.body[points[0]:points[0]] = ast.parse(
        "_observe(hit, unmatched, full_hits, checks, broad)").body
    module = types.ModuleType("_conversion_legacy")
    module.__file__ = str(path)
    exec(compile(ast.fix_missing_locations(tree), str(path), "exec"), module.__dict__)
    module.ROOT = str(root)
    module.MAP_PATH = str(root / "tools/check_map.yaml")
    # A frozen input-only copy need not carry git metadata. Its complete file
    # list is fixed before new entry points are generated in the other tree.
    if not (root / ".git").exists():
        module._TRACKED = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
    module._observe = lambda *args: None
    return module


def _logical_rules(root: Path, old):
    """Read prerequisite text and physical locations, not make semantics."""
    result = {}
    for relative in old.makefile_paths():
        lines = (root / relative).read_text(encoding="utf-8").splitlines()
        i = 0
        while i < len(lines):
            line, number = lines[i], i + 1
            while line.endswith("\\") and i + 1 < len(lines):
                i += 1
                line = line[:-1] + " " + lines[i].strip()
            match = re.fullmatch(r"(check[\w-]*)\s*:(?!=)(.*)", line)
            if match:
                result[match[1]] = (match[2].strip(), f"{relative}:{number}")
            i += 1
    return result


def _needs(prerequisites: str) -> list[str]:
    # The only conditional prerequisite in the current normal column.
    prerequisites = prerequisites.replace(
        "$(if $(CHECK_TIME_CHILD),,sqlite-module-proto)", "sqlite-module-proto")
    if "$" in prerequisites or any(c in prerequisites for c in "|;#"):
        raise ValueError(f"unsupported prerequisites: {prerequisites}")
    return [("check:" + p[6:] if p.startswith("check-") else "make:" + p)
            for p in prerequisites.split()]


_MAKE_VALUES = {"BUILD_OUT", "SQLITE_PROTO_OUT", "CROSS_DIR", "C_STD", "INC_KERNEL"}
_MUTATE_VALUE = "ctx.make_vars.get('MUTATE', '1' if ctx.mutate else '0')"
_SPECIAL = {
    "$(if $(mut_on),--mutate-rshell)": "('--mutate-rshell' if ctx.mutate else '')",
    "$(if $(filter 1,$(MUTATE)),--mutate)": f"('--mutate' if {_MUTATE_VALUE} == '1' else '')",
    "$(filter-out -MMD -MP,$(KERNEL_CFLAGS))":
        "' '.join(x for x in ctx.make_vars['KERNEL_CFLAGS'].split() "
        "if x not in ('-MMD', '-MP'))",
    "$(MUT)": "('--mutate' if ctx.mutate else '')",
    "$(MUTS)": "('--mutants' if ctx.mutate else '')",
    "$(MUTATE)": _MUTATE_VALUE,
}


def _command(line: str) -> list[str]:
    """Compile the existing simple command forms to concrete subprocess argv.

    Make functions are consumed here, never interpreted by a generated run().
    Quotes are parsed while converting, so unittest/group string arguments and
    quoted make values each remain one argv item.
    """
    line = line.lstrip("@+")
    expressions = {}
    for literal, expr in list(_SPECIAL.items()) + [
            (f"$({name})", f"ctx.make_vars[{name!r}]") for name in sorted(_MAKE_VALUES)]:
        if literal in line:
            marker = f"__CHECK_VALUE_{len(expressions)}__"
            expressions[marker] = expr
            line = line.replace(literal, marker)
    if "$" in line or any(c in line for c in ";|&`<\n"):
        raise ValueError(f"unsupported recipe: {line}")

    def expression(token):
        parts = re.split(r"(__CHECK_VALUE_\d+__)", token)
        return " + ".join(expressions[p] if p in expressions else repr(p)
                          for p in parts if p) or "''"

    lexer = shlex.shlex(line, posix=True)
    lexer.whitespace_split = True
    lexer.commenters = ""
    tokens = []
    while True:
        start = lexer.instream.tell()
        token = lexer.get_token()
        if token is None:
            break
        raw = line[start:lexer.instream.tell()].strip()
        tokens.append((token, raw))
    if not tokens:
        raise ValueError("empty recipe")
    env = []
    while tokens and re.match(r"^[A-Za-z_][A-Za-z_0-9]*=", tokens[0][0]):
        key, value = tokens.pop(0)[0].split("=", 1)
        env.append(f"env[{key!r}] = {expression(value)}")
    if not tokens:
        raise ValueError("recipe has no command")
    executable = tokens[0][0]
    if not (executable in ("python3", "gcc", "cargo", "mkdir") or
            re.fullmatch(r"__CHECK_VALUE_\d+__/[\w./-]+", executable)):
        raise ValueError(f"unsupported command: {executable}")
    # Accept only the redirection used by hwdev-audit; no shell interpreter.
    redirect = None
    if any(t == ">" for t, _ in tokens):
        if len(tokens) < 3 or tokens[-2][0] != ">" or sum(t == ">" for t, _ in tokens) != 1:
            raise ValueError(f"unsupported redirection: {line}")
        redirect = expression(tokens[-1][0])
        tokens = tokens[:-2]
    if any(any(c in t for c in ";|&`<>") for t, _ in tokens):
        raise ValueError(f"unsupported shell operator: {line}")
    args = []
    for token, raw in tokens:
        expr = expression(token)
        # Unquoted make expansions are shell words; quoted ones stay one item.
        if any(p in token for p in expressions) and not raw.startswith(("'", '"')):
            args.append(f"*shlex.split({expr})")
        else:
            args.append(expr)
    result = ["env = dict(base_env)", *env, f"argv = [{', '.join(args)}]"]
    if redirect is None:
        result += ["rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)"]
    else:
        result += [f"with (ctx.root / ({redirect})).open('w') as output:",
                   "    rc = subprocess.call(argv, cwd=ctx.root, env=env, stdout=output, close_fds=False)"]
    return result + ["if rc:", "    return rc"]


def _run(recipe: list[str]) -> str:
    body = ["import os", "import shlex", "import subprocess",
            "base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))"]
    for line in recipe:
        host = re.fullmatch(r"\$\(call host32_check,(test_[\w]+\.py)\)", line)
        if host:
            body += ["if not ctx.runners:", "    return 1",
                     "for index, runner in enumerate(ctx.runners):",
                     "    print('HOST32_RUNNERS=' + runner, flush=True)",
                     f"    argv = ['python3', '-B', 'tools/tests/{host[1]}', '--runner', runner]",
                     "    if ctx.mutate and index == 0:", "        argv.append('--mutate')",
                     "    rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)",
                     "    if rc:", "        return rc"]
        else:
            body += _command(line)
    return "\n\ndef run(ctx) -> int:\n" + "\n".join("    " + l for l in body + ["return 0"]) + "\n"


def _scripts(recipe):
    result = set(re.findall(r"tools/[\w/]+\.py", "\n".join(recipe)))
    for line in recipe:
        for script in re.findall(r"(?:host32_check,|['\"])(test_[\w]+\.py)", line):
            result.add("tools/tests/" + script)
    return result


def _insert(source: str, declaration: str) -> str:
    tree = ast.parse(source)
    end = len(source.splitlines()) if not tree.body else 0
    for i, node in enumerate(tree.body):
        if (i == 0 and isinstance(node, ast.Expr) and
                isinstance(node.value, ast.Constant) and isinstance(node.value.value, str)) or (
                isinstance(node, ast.ImportFrom) and node.module == "__future__"):
            end = node.end_lineno
        else:
            # Keep shebang, encoding and leading comments above CHECK too.
            end = max(end, node.lineno - 1)
            break
    lines = source.splitlines(keepends=True)
    prefix = "".join(lines[:end])
    if prefix and not prefix.endswith("\n"):
        prefix += "\n"
    return prefix + declaration + "".join(lines[end:])


def _standalone(source: str) -> bool:
    """Scripts that run on import need an independent wrapper entry as well."""
    nodes = ast.parse(source).body
    guarded = [n for n in nodes if isinstance(n, ast.If) and
               ast.unparse(n.test) == "__name__ == '__main__'"]
    if not guarded:
        return False
    for node in nodes:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            if node.name == "run":
                return False
        elif node not in guarded:
            if any(isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute) and
                   n.func.attr in ("parse_args", "parse_known_args") for n in ast.walk(node)):
                return False
            if any(isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store) and
                   n.id in ("run", "CHECK") for n in ast.walk(node)):
                return False
    return True


def convert(root: Path, output: Path, tiers: Mapping[str, int],
            timings: Mapping[str, float]) -> list[Path]:
    root, output = root.resolve(), output.resolve()
    if output == root or root in output.parents:
        raise ValueError("conversion output must be a separate work tree")
    old = _legacy(root)
    rules, variables = old.read_makefiles()
    normal, mapping = old.check_lists(variables), old.load_map()
    locations = _logical_rules(root, old)
    owners = Counter(script for recipe in rules.values() for script in _scripts(recipe))
    converted = []
    # Compile every recipe before writing, so unsupported input is never hidden
    # by a partially generated successful-looking registry.
    for target in normal:
        prerequisites, location = locations[target]
        try:
            recipe = rules[target]
            if not recipe:
                raise ValueError("no recipe")
            run = _run(recipe)
            needs = _needs(prerequisites)
            covers = list(mapping["checks"][target])
            if not any(n.startswith("make:") for n in needs) and (
                    target in mapping.get("artifact_readers", []) or
                    any(g.startswith(tuple(d + "/" for d in old.ARTIFACT_DIRS)) for g in covers)):
                needs.append("make:all")
            name = target.removeprefix("check-")
            tier = tiers.get(name, tiers.get(target, 0 if target in mapping["docs_always"] else 1))
            seconds = timings.get(name, timings.get(target))
            budget = 60.0 if seconds is None else float(seconds) * 2
            if type(tier) is not int or not 0 <= tier <= 4 or not math.isfinite(budget) or budget < 0:
                raise ValueError("invalid tier or timing")
            scripts = _scripts(recipe)
            path = Path("tools/checks") / (name + ".py")
            source = "#!/usr/bin/env python3\n"
            if len(recipe) == len(scripts) == 1:
                script = next(iter(scripts))
                # Only standalone scripts have an unambiguous __main__ entry.
                simple = re.fullmatch(r"@?python3 (?:-B )?tools/tests/test_[\w]+\.py(?: .*)?", recipe[0])
                host = re.fullmatch(r"\$\(call host32_check,test_[\w]+\.py\)", recipe[0])
                if owners[script] == 1 and (simple or host):
                    candidate = (root / script).read_text(encoding="utf-8")
                    if _standalone(candidate):
                        path, source = Path(script), candidate
            covers = [g for g in covers if g != path.as_posix()]
            declaration = (f"CHECK = dict(name={name!r}, tier={tier!r},\n"
                           f"    covers={covers!r},\n    needs={needs!r}, budget={budget!r})\n\n")
            text = _insert(source, declaration).rstrip() + "\n" + run
            compile(text, str(path), "exec")
            converted.append((output / path, text))
        except (ValueError, SyntaxError, KeyError) as error:
            raise ValueError(f"{target} ({location}): {error}") from error
    for path, text in converted:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return [path for path, _ in converted]


def _git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def compare_commits(root: Path, frozen_old: Path, commits: tuple[str, ...],
                    new_plan: Callable[[tuple[str, ...]], "Plan"]) -> int:
    root, frozen_old = root.resolve(), frozen_old.resolve()
    if root == frozen_old:
        raise ValueError("old and new input trees must be separate")
    old = _legacy(frozen_old)
    observed = {}

    def observe(hit, unmatched, full_hits, checks, broad):
        relieved = {f for f in unmatched if any(old.matches(f, [(g, r)])
                    for name, globs in checks.items() if name in broad
                    for g, r in globs if "**" in g)}
        observed.update(hit={t.removeprefix("check-") for t in hit},
                        unmatched=set(unmatched) - relieved, full_hits=set(full_hits),
                        raw_unmatched=set(unmatched), broad_relief=relieved)

    old._observe = observe
    record = dict(main=_git(root, "rev-parse", "main"), commits=list(commits),
                  frozen_old=str(frozen_old), environment={k: os.environ.get(k)
                  for k in ("CROSS_DIR", "HOST32_RUNNERS")}, results=[])
    failed = False
    for commit in commits:
        parents = _git(root, "rev-list", "--parents", "-n", "1", commit).split()
        if len(parents) < 2:
            raise ValueError(f"{commit}: no first parent")
        data = subprocess.check_output(["git", "-C", str(root), "diff-tree", "--no-commit-id",
                                       "--name-only", "-r", "--no-renames", "-z", parents[1], parents[0]])
        files = tuple(p.decode('utf-8') for p in data.split(b'\0') if p)
        observed.clear()
        old.plan(list(files), base=None)
        new = new_plan(files)
        differences = []
        for key in ("hit", "unmatched", "full_hits"):
            before, after = observed[key], set(getattr(new, key))
            if before != after:
                failed = True
                differences.append(dict(collection=key, old_only=sorted(before - after),
                                        new_only=sorted(after - before),
                                        cause="covers/dependency or input classification differs"))
        record["results"].append(dict(commit=parents[0], files=list(files),
            old={k: sorted(v) for k, v in observed.items()},
            new={k: sorted(getattr(new, k)) for k in ("hit", "unmatched", "full_hits")},
            differences=differences))
    record["rc"] = int(failed)
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return int(failed)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--output", type=Path)
    action.add_argument("--compare", type=Path, metavar="FROZEN_OLD_TREE")
    parser.add_argument("--tiers", type=Path, help="JSON object: check name -> tier")
    parser.add_argument("--timings", type=Path, help="normal check_times.tsv (name, seconds, rc)")
    parser.add_argument("--commits", nargs="+", help="default: latest 200 non-merge commits on main")
    args = parser.parse_args(argv)
    try:
        if args.output:
            tiers = json.loads(args.tiers.read_text()) if args.tiers else {}
            timings = {}
            if args.timings:
                for row in args.timings.read_text().splitlines():
                    name, seconds, rc = row.split("\t")
                    if int(rc) != 0:
                        raise ValueError(f"{name}: timing is not a successful normal run")
                    timings[name] = float(seconds)
            paths = convert(args.root, args.output, tiers, timings)
            print(f"converted {len(paths)} checks into {args.output}")
            return 0
        path = args.root / "tools/checks/run.py"
        spec = importlib.util.spec_from_file_location("_conversion_new", path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        checks = module.discover(args.root)
        commits = tuple(args.commits or _git(args.root, "rev-list", "--no-merges", "-n", "200", "main").splitlines())
        return compare_commits(args.root, args.compare, commits,
                               lambda files: module.plan(args.root, checks, files, base=None))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"convert: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
