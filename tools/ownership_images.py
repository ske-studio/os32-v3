"""Recover known product dependencies from the selected revision's GNU Make graph."""
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import os
import json
import shutil
import shlex
import sys
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

from tests.support.media import PPC, PPC_MAKE
from tests.support.product import sqlite_directory
from tests.support.tracked import copy_inputs


def relative(root, name):
    return os.path.relpath(name, root) if os.path.isabs(name) else os.path.normpath(name)


def compile_edges(root, lines, cache):
    jobs = {}
    for line in lines:
        if " -c " not in line or " -o " not in line:
            continue
        args = shlex.split(line)
        gcc = next((i for i, word in enumerate(args) if Path(word).name in
                    ("i386-elf-gcc", "powerpc-elf-gcc")), None)
        if gcc is None or "-c" not in args or "-o" not in args:
            continue
        args = args[gcc:]
        out = args.index("-o")
        output = relative(root, args[out + 1])
        del args[out:out+2]
        clean = []
        it = iter(args)
        for word in it:
            if word in ("-MF", "-MT", "-MQ"):
                next(it)
            elif word not in ("-c", "-MMD", "-MD", "-MP"):
                clean.append(word)
        jobs[output] = tuple(clean)

    def deps(cmd):
        if not shutil.which(cmd[0]):
            return None
        proc = subprocess.run([*cmd, "-MM", "-MG", "-MT", "unit", "-MF", "-"],
                              cwd=root, capture_output=True, text=True, errors="replace")
        if not proc.returncode and proc.stdout.startswith("unit:"):
            return {relative(root, p) for p in shlex.split(proc.stdout[5:].replace("\\\n", " "))}
        return None

    new = sorted(set(jobs.values()) - cache.keys())
    with ThreadPoolExecutor(max_workers=4) as pool:
        cache.update(zip(new, pool.map(deps, new)))
    unknown = [out for out, cmd in jobs.items() if cache[cmd] is None]
    return {out: cache[cmd] for out, cmd in jobs.items() if cache[cmd] is not None}, unknown


def cargo_inputs(root, manifest, package, cache):
    if manifest not in cache:
        try:
            proc = subprocess.run(["cargo", "metadata", "--locked", "--offline", "--format-version=1",
                                   "--manifest-path", manifest], cwd=root, capture_output=True, text=True)
        except FileNotFoundError:
            return set()
        if proc.returncode:
            print(f"{manifest}: Cargo dependency metadata unavailable", file=sys.stderr)
            cache[manifest] = None
        else:
            cache[manifest] = json.loads(proc.stdout)
    data = cache[manifest]
    if not data:
        return set()
    packages = {p["id"]: p for p in data["packages"]}
    nodes = {n["id"]: n["dependencies"] for n in data["resolve"]["nodes"]}
    workspaces = set(data["workspace_members"])
    pending = [key for key in workspaces if packages[key]["name"] == package or
               (package is None and Path(packages[key]["manifest_path"]) == root / manifest)]
    seen = set()
    inputs = set()
    while pending:
        key = pending.pop()
        if key in seen:
            continue
        seen.add(key)
        item = packages[key]
        if item["source"] is None:
            inputs.add(relative(root, item["manifest_path"]))
            for target in item["targets"]:
                if not set(target["kind"]) & {"test", "bench", "example"}:
                    inputs.add(relative(root, target["src_path"]))
        pending.extend(nodes[key])
    return inputs


def recipe_edges(root, text, graph, cargo):
    target = None
    for line in text.splitlines():
        trace = re.match(r"^.+?:\d+: (?:update target|target) '([^']+)'", line)
        if trace:
            target = relative(root, trace[1])
            continue
        if not target:
            continue
        if "cargo build " in line:
            args = shlex.split(line)
            directory = args[1] if args[0] == "cd" else "."
            manifest = str(Path(directory) / (args[args.index("--manifest-path") + 1]
                           if "--manifest-path" in args else "Cargo.toml"))
            package = args[args.index("-p") + 1] if "-p" in args else None
            graph[target].update(cargo_inputs(root, manifest, package, cargo))
        if " -o " in line and any(tool in line for tool in ("mkisofs", "genisoimage", "xorriso")):
            args = shlex.split(line)
            image = relative(root, args[args.index("-o") + 1])
            if image != target:
                graph[image].update(graph[target] - {image})
                graph[target].add(image)


def observe(root, tracked, declared_only=False):
    result = defaultdict(set)
    cache, cargo = {}, {}
    for config in ("pc98", *PPC):
        flags = [] if config == "pc98" else [*PPC_MAKE, *PPC[config][0]]
        if declared_only and config == "pc98":
            # The planner only traverses tracked prerequisites. An installed
            # cross libc is an external leaf of the Make graph.
            cross = root / ".external-toolchain"
            flags += [f"CROSS_DIR={cross}", "-o", str(cross / "i386-elf/lib/libc.a")]
        goals = ["all", "iso"] + (["kernel-r1"] if config == "pc98" else [])
        sqlite_directory(config, root)
        proc = subprocess.run(["make", "-j1", "--trace", "-kBnp", *flags, *goals],
                              cwd=root, capture_output=True, text=True, errors="replace")
        if declared_only and proc.returncode:
            raise OSError(f"{config}: incomplete Make dependency graph: {proc.stderr}")
        graph = defaultdict(set)
        output = proc.stdout.replace("\\\n", " ")
        if not declared_only:
            dependencies, unknown = compile_edges(root, output.partition("# GNU Make")[0].splitlines(), cache)
            graph.update(dependencies)
            if proc.returncode or unknown:
                print(f"{config}: incomplete dependencies; Make rc={proc.returncode}, "
                      f"unknown objects={','.join(unknown)}", file=sys.stderr)
        in_files = False
        for line in output.splitlines():
            if line == "# Files":
                in_files = True
            if line.startswith("# Finished Make data base"):
                in_files = False
            if not in_files:
                continue
            rule = re.fullmatch(r"([^\s:#%]+):(?: (.*))?", line)
            if rule and rule[2] and "=" not in rule[2]:
                graph[relative(root, rule[1])].update(relative(root, p) for p in
                                                      rule[2].split("|", 1)[0].split())
        if not declared_only:
            recipe_edges(root, output.partition("# GNU Make")[0], graph, cargo)
        if not graph:
            raise OSError(f"{config}: no Make dependency graph: {proc.stderr}")
        def closure(starts):
            pending = list(starts)
            seen = set()
            while pending:
                path = pending.pop()
                if path not in seen:
                    seen.add(path)
                    pending.extend(graph.get(path, ()))
            return seen
        for output in sorted(closure(goals)):
            if not output.lower().endswith((".elf", ".bin", ".mod", ".iso", ".img", ".d88", ".lz4", ".pkg")):
                continue
            for path in tracked.intersection(closure([output])):
                result[path].add((config, output))
    return result


def image_dependencies(inventory, declared_only=False):
    # Both modes observe clean product rules. Stale objects or depfiles in a
    # developer's worktree cannot introduce edges from a different revision.
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        if inventory.revision:
            with tempfile.TemporaryFile() as archive:
                subprocess.run(["git", "archive", inventory.revision, "--", ".",
                                ":(exclude).env", ":(exclude)docs/archive", ":(exclude)docs/hw"],
                               cwd=inventory.root, stdout=archive, check=True)
                archive.seek(0)
                with tarfile.open(fileobj=archive) as files:
                    files.extractall(root, filter="data")
        else:
            copy_inputs(inventory.root, root, inventory.paths)
        return observe(root, inventory.paths, declared_only)
