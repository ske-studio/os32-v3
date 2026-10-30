#!/usr/bin/env python3
"""Private recipe/selection fixtures for the temporary R1(b) converter."""
import ast
import contextlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import types
import unittest
from unittest.mock import patch

import convert


ROOT = Path(__file__).resolve().parents[2]


class ConvertTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="check-convert-")
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.root = self.base / "source"
        self.output = self.base / "converted"
        self.write("tools/check_select.py", (ROOT / "tools/check_select.py").read_text())
        self.write("tools/check_map.yaml", "checks: {}\n")

    def write(self, path, text):
        dest = self.root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding="utf-8")

    def fixture(self, recipes, covers=None, extras="", policy=""):
        self.write("tools/check_map.yaml", "checks: {}\n" + policy)
        for index, (name, prerequisites, lines) in enumerate(recipes):
            target = "check-" + name
            self.write(f"build/checks.d/{target}.mk",
                       f"CHECK_PAR_ORDER += {index:03}:{target}\n{target}: {prerequisites}\n" +
                       "".join("\t" + line + "\n" for line in lines) + extras)
            paths = (covers or {}).get(name, ["src/a.c"])
            self.write(f"tools/check_map.d/{target}.yaml", json.dumps({target: paths}))

    def converted(self, tiers=None, timings=None):
        paths = convert.convert(self.root, self.output, tiers or {}, timings or {})
        result = {}
        for path in paths:
            tree = ast.parse(path.read_text())
            declaration = next(n.value for n in tree.body if isinstance(n, ast.Assign)
                               and any(isinstance(t, ast.Name) and t.id == "CHECK" for t in n.targets))
            check = {k.arg: ast.literal_eval(k.value) for k in declaration.keywords}
            result[check["name"]] = (path, check)
        return result

    def run_entry(self, path, *, mutate=False, runners=("native", "qemu"), make_vars=None, rc=None):
        module = types.ModuleType("converted_fixture")
        module.__file__ = str(path)
        exec(compile(path.read_text(), str(path), "exec"), module.__dict__)
        ctx = types.SimpleNamespace(root=self.root, mutate=mutate, runners=runners,
                                    files=(), tmp=self.base, make_vars=make_vars or {})
        with patch("subprocess.call", side_effect=rc) as call:
            status = module.run(ctx)
        return status, call.call_args_list

    def test_script_header_main_and_determinism(self):
        script = "tools/tests/test_a.py"
        source = ('#!/usr/bin/env python3\n# -*- coding: utf-8 -*-\n"""fixture"""\n'
                  'from __future__ import annotations\nimport sys\n'
                  'if __name__ == "__main__":\n    sys.exit(7)\n')
        self.write(script, source)
        self.fixture([("a", "", [f"python3 -B {script} $(MUTS)"])],
                     {"a": [script, "src/*.c", "src/a.h", "build/checks.d/check-a.mk"]})
        files = self.converted({"a": 3}, {"check-a": 1.25})
        path, check = files["a"]
        self.assertEqual(path.relative_to(self.output).as_posix(), script)
        self.assertEqual(check["tier"], 3)
        self.assertEqual(check["budget"], 2.5)
        self.assertEqual(check["covers"], ["src/*.c", "src/a.h", "build/checks.d/check-a.mk"])
        text = path.read_text()
        self.assertLess(text.index("from __future__"), text.index("CHECK ="))
        self.assertLess(text.index("CHECK ="), text.index("import sys"))
        self.assertEqual(subprocess.run(["python3", "-B", str(path)]).returncode, 7)
        status, calls = self.run_entry(path, mutate=True, rc=[0])
        self.assertEqual(status, 0)
        self.assertEqual(calls[0].args[0], ["python3", "-B", script, "--mutants"])
        self.converted({"a": 3}, {"check-a": 1.25})
        self.assertEqual(path.read_text(), text)
        self.assertEqual((self.root / script).read_text(), source)

    def test_host32_every_normal_and_only_first_mutation(self):
        for name in ("a", "b"):
            self.write(f"tools/tests/test_{name}.py", "# fixture\n")
        self.fixture([("host", "", ["$(call host32_check,test_a.py)",
                                     "$(call host32_check,test_b.py)"])])
        path, _ = self.converted()["host"]
        for mutate in (False, True):
            status, calls = self.run_entry(path, mutate=mutate, rc=[0] * 4)
            self.assertEqual(status, 0)
            self.assertEqual([c.args[0][-1] == "--mutate" for c in calls],
                             [mutate, False, mutate, False])
            self.assertEqual([c.args[0][4] for c in calls], ["native", "qemu"] * 2)
        self.assertEqual(self.run_entry(path, runners=())[0], 1)
        status, calls = self.run_entry(path, mutate=True, rc=[13])
        self.assertEqual(status, 13)
        self.assertEqual(len(calls), 1)

    def test_shared_scripts_unittest_and_existing_run_use_wrappers(self):
        self.write("tools/tests/test_shared.py", "# shared\n")
        self.write("tools/tests/test_collision.py", "def run():\n    return 19\n")
        self.fixture([
            ("a", "", ["python3 -B tools/tests/test_shared.py"]),
            ("b", "", ["python3 -B tools/tests/test_shared.py $(MUT)"]),
            ("collision", "", ["python3 -B tools/tests/test_collision.py"]),
            ("unit", "", ["python3 -B -m unittest discover -s tools/tests -p 'test_shared.py'"]),
        ])
        entries = self.converted()
        for name, (path, _) in entries.items():
            self.assertEqual(path.relative_to(self.output).as_posix(), f"tools/checks/{name}.py")
        status, calls = self.run_entry(entries["unit"][0], rc=[0])
        self.assertEqual(status, 0)
        self.assertEqual(calls[0].args[0], ["python3", "-B", "-m", "unittest", "discover",
                                          "-s", "tools/tests", "-p", "test_shared.py"])

    def test_import_time_argument_parsing_and_unguarded_scripts_use_wrappers(self):
        self.write("tools/tests/test_args.py", 'import argparse\nargs = argparse.ArgumentParser().parse_args()\n'
                   'if __name__ == "__main__":\n    pass\n')
        self.write("tools/tests/test_top.py", 'raise RuntimeError("must only execute in subprocess")\n')
        self.fixture([("args", "", ["python3 -B tools/tests/test_args.py"]),
                      ("top", "", ["python3 -B tools/tests/test_top.py"])])
        for name, (path, _) in self.converted().items():
            self.assertTrue(path.read_text().startswith("#!/usr/bin/env python3\nCHECK ="))
            self.assertEqual(path.relative_to(self.output).as_posix(), f"tools/checks/{name}.py")
            self.assertEqual(self.run_entry(path, rc=[0])[0], 0)

    def test_make_variables_env_quotes_multiple_lines_and_failure(self):
        self.fixture([("commands", "", [
            "@mkdir -p $(BUILD_OUT)",
            "@gcc $(C_STD) -o $(BUILD_OUT)/fixture tools/tests/fixture.c",
            "@$(BUILD_OUT)/fixture",
            'MUTATE=$(MUTATE) PYTHONPATH=. python3 -B tools/run_host_group.py $(MUT) '
            '--command "tools/tests/test_a.py $(if $(mut_on),--mutate-rshell)"',
            "python3 -B tools/check.py --flags '$(filter-out -MMD -MP,$(KERNEL_CFLAGS))' "
            "--includes '$(INC_KERNEL)' --cc $(CROSS_DIR)/bin/i386-elf-gcc",
            'python3 tools/check.py --directory "$(SQLITE_PROTO_OUT)" $(if $(filter 1,$(MUTATE)),--mutate)',
        ])])
        path, _ = self.converted()["commands"]
        values = dict(BUILD_OUT="private/out", C_STD="-std=gnu11 -g", CROSS_DIR="/cross",
                      KERNEL_CFLAGS='-g -MMD -MP -O2 -DNAME="test"', INC_KERNEL="-I. -Iinclude",
                      SQLITE_PROTO_OUT="private/has space")
        status, calls = self.run_entry(path, mutate=True, make_vars=values, rc=[0] * 6)
        self.assertEqual(status, 0)
        self.assertEqual(calls[1].args[0], ["gcc", "-std=gnu11", "-g", "-o", "private/out/fixture",
                                          "tools/tests/fixture.c"])
        self.assertEqual(calls[3].args[0][-1], "tools/tests/test_a.py --mutate-rshell")
        self.assertEqual(calls[3].kwargs["env"]["MUTATE"], "1")
        self.assertEqual(calls[3].kwargs["env"]["PYTHONPATH"], ".")
        self.assertEqual(calls[3].kwargs["env"]["HOST32_RUNNERS"], "native qemu")
        self.assertEqual(calls[4].args[0][3:], ["--flags", '-g -O2 -DNAME="test"', "--includes", "-I. -Iinclude",
                                             "--cc", "/cross/bin/i386-elf-gcc"])
        self.assertEqual(calls[5].args[0][-2:], ["private/has space", "--mutate"])
        status, calls = self.run_entry(path, make_vars=values, rc=[0, 23])
        self.assertEqual(status, 23)
        self.assertEqual(len(calls), 2)
        # Literal MUTATE/filter 1 expressions retain the legacy sel behavior;
        # MUT/MUTS and mut_on still follow the selected check's ctx.mutate.
        status, calls = self.run_entry(path, mutate=True, make_vars=dict(values, MUTATE="sel"), rc=[0] * 6)
        self.assertEqual(status, 0)
        self.assertEqual(calls[3].kwargs["env"]["MUTATE"], "sel")
        self.assertIn("--mutate", calls[3].args[0])
        self.assertNotIn("--mutate", calls[5].args[0])

    def test_stdout_redirection(self):
        self.fixture([("audit", "", ["python3 tools/audit.py --out $(BUILD_OUT) > $(BUILD_OUT)/audit.json"])])
        path, _ = self.converted()["audit"]
        (self.root / "out").mkdir()
        status, calls = self.run_entry(path, make_vars={"BUILD_OUT": "out"}, rc=[9])
        self.assertEqual(status, 9)
        self.assertEqual(calls[0].kwargs["stdout"].name, str(self.root / "out/audit.json"))
        self.assertTrue(calls[0].kwargs["stdout"].closed)

    def test_prerequisites_artifacts_tiers_and_excluded_alias(self):
        recipes = [
            ("a", "", ["python3 tools/a.py"]),
            ("aggregate", "check-a check-artifact", ["python3 tools/aggregate.py"]),
            ("artifact", "", ["python3 tools/artifact.py"]),
            ("sqlite", "$(if $(CHECK_TIME_CHILD),,sqlite-module-proto)", ["python3 tools/sqlite.py"]),
            ("font", "assets/fonts/font.kcgfont", ["python3 tools/font.py"]),
        ]
        self.fixture(recipes, {"artifact": ["build/out/kernel.map"], "sqlite": ["build/out/proto/**"]},
                     policy="docs_always: [check-a]\nartifact_readers: [check-artifact, check-sqlite]\n")
        self.write("build/checks.d/local.mk", "check-e10c-lifecycle: check-a\ncheck-arm-compile:\n\tunknown\n")
        entries = self.converted()
        self.assertEqual(set(entries), {r[0] for r in recipes})
        self.assertEqual(entries["a"][1]["tier"], 0)
        self.assertEqual(entries["a"][1]["budget"], 60.0)
        self.assertEqual(entries["aggregate"][1]["needs"], ["check:a", "check:artifact"])
        self.assertEqual(entries["artifact"][1]["needs"], ["make:all"])
        self.assertEqual(entries["sqlite"][1]["needs"], ["make:sqlite-module-proto"])
        self.assertEqual(entries["font"][1]["needs"], ["make:assets/fonts/font.kcgfont"])

    def test_unknown_recipe_stops_before_writing_any_entry(self):
        self.fixture([("a", "", ["python3 tools/a.py"]),
                      ("bad", "", ["$(call unknown,test_bad.py)"])])
        with self.assertRaisesRegex(ValueError, r"check-bad \(build/checks.d/check-bad.mk:2\)"):
            self.converted()
        self.assertFalse(self.output.exists())
        for line in ("python3 a.py && python3 b.py", "python3 a.py; false", "echo done",
                     "python3 a.py $(shell false)"):
            with self.assertRaises(ValueError):
                convert._command(line)

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], text=True).strip()

    def commit(self, message):
        self.git("add", ".")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", message)
        return self.git("rev-parse", "HEAD")

    def test_compare_observes_three_sets_and_only_broad_exception(self):
        self.fixture([("a", "", ["python3 tools/a.py"]),
                      ("scanner", "", ["python3 tools/scanner.py"])],
                     {"a": ["src/a.c", "src/a.h", "build/checks.d/check-a.mk",
                            "tools/check_map.d/check-a.yaml"], "scanner": ["**/*.c"]},
                     policy="broad: [check-scanner]\nfull: [Makefile, 'build/**/*.mk']\n"
                            "docs_only: ['**/*.md']\nnotest: [data/**]\nignore: [ignored/**]\n")
        self.write("src/a.c", '#include "a.h"\n')
        self.write("src/a.h", "#define A 1\n")
        self.git("init", "-q", "-b", "main")
        self.commit("base")
        frozen = self.base / "old"
        shutil.copytree(self.root, frozen, ignore=shutil.ignore_patterns(".git"))
        entries = self.converted()
        # The comparison callback uses the generated covers, not legacy stage/mut.
        covers = {name: check["covers"] for name, (_, check) in entries.items()}
        self.write("src/new.c", "int added;\n")
        first = self.commit("broad only")
        self.write("Makefile", "# build\n")
        second = self.commit("full input")
        self.git("mv", "src/a.c", "src/renamed.c")
        third = self.commit("rename")
        self.write("src/a.h", "#define A 2\n")
        fourth = self.commit("header")
        self.write("tools/check_map.d/check-a.yaml", (self.root / "tools/check_map.d/check-a.yaml").read_text() + "\n")
        fifth = self.commit("legacy registration")
        old = convert._legacy(frozen)
        seen = []

        def new_plan(files):
            seen.append(files)
            hit = frozenset(name for name, globs in covers.items()
                            if any(old.matches(f, old.compile_globs(globs)) for f in files))
            unmatched = frozenset(f for f in files if not old.matches(f, old.compile_globs(
                [g for globs in covers.values() for g in globs] + ["Makefile"])))
            return types.SimpleNamespace(hit=hit, unmatched=unmatched,
                                         full_hits=frozenset(f for f in files if f == "Makefile"))

        commits = (first, second, third, fourth, fifth)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = convert.compare_commits(self.root, frozen, commits, new_plan)
        self.assertEqual(status, 0)
        record = json.loads(output.getvalue())
        self.assertEqual(record["main"], fifth)
        self.assertEqual(record["commits"], list(commits))
        self.assertEqual(record["results"][0]["old"]["broad_relief"], ["src/new.c"])
        self.assertEqual(set(seen[2]), {"src/a.c", "src/renamed.c"})
        self.assertEqual(record["results"][1]["old"]["full_hits"], ["Makefile"])
        self.assertEqual(record["results"][3]["new"]["hit"], ["a"])
        self.assertEqual((frozen / "tools/check_select.py").read_text(),
                         (ROOT / "tools/check_select.py").read_text())
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = convert.compare_commits(self.root, frozen, (first,),
                lambda files: types.SimpleNamespace(hit=frozenset(), unmatched=frozenset({"other"}),
                                                   full_hits=frozenset({"wrong"})))
        self.assertEqual(status, 1)
        diff = json.loads(output.getvalue())["results"][0]["differences"]
        self.assertEqual({d["collection"] for d in diff}, {"hit", "unmatched", "full_hits"})
        self.assertTrue(all(d["cause"] for d in diff))

    def test_observation_keeps_legacy_return_value(self):
        self.fixture([("a", "", ["python3 tools/a.py"])], {"a": ["src/a.c"]},
                     policy="docs_only: ['**/*.md']\nfull: [Makefile]\n")
        old = convert._legacy(self.root)
        raw = types.ModuleType("legacy_unmodified")
        raw.__file__ = str(self.root / "tools/check_select.py")
        exec(compile(Path(raw.__file__).read_text(), raw.__file__, "exec"), raw.__dict__)
        self.assertEqual(old.plan(["src/a.c"]), raw.plan(["src/a.c"]))
        self.assertEqual(old.plan([]), raw.plan([]))
        self.assertEqual(old.plan(["Makefile"]), raw.plan(["Makefile"]))

    def test_actual_237_recipes_are_compilable_and_registration_unchanged(self):
        paths = convert.convert(ROOT, self.output, {}, {})
        self.assertEqual(len(paths), 237)
        names = []
        for path in paths:
            tree = ast.parse(path.read_text())
            declaration = next(n.value for n in tree.body if isinstance(n, ast.Assign)
                               and any(isinstance(t, ast.Name) and t.id == "CHECK" for t in n.targets))
            names.append(next(ast.literal_eval(k.value) for k in declaration.keywords if k.arg == "name"))
        self.assertEqual(len(set(names)), len(paths))
        self.assertTrue({"ver-info-host", "memmap-audit-host", "tools-host"} <= set(names))
        self.assertFalse({"e10c-lifecycle", "arm-compile", "gfinit-fixture-host", "host-agent"} & set(names))


if __name__ == "__main__":
    unittest.main()
