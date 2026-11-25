"""Local registry entry point. Each row builds in a new candidate tree."""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from tests.support.registry import load

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from build_compare import copy_candidate
import fetch_fonts


def prerequisites(environment, cpu, config, sources):
    if environment == "python":
        return
    ppc = environment == "linux-ppc-cross" or environment == "linux-cross" and cpu == "ppc"
    directory = Path(os.environ.get("PPC_CROSS_DIR" if ppc else "CROSS_DIR",
                                    Path.home() / "opt" / ("cross-ppc" if ppc else "cross")))
    prefix = "powerpc-elf" if ppc else "i386-elf"
    tools = ["make", "python3"]
    modules = []
    if not ppc:
        tools += ["gcc", "cargo", "rustc", "xorriso", "nasm"]
        modules = ["lz4", "yaml", "PIL", "fontTools"]
    elif "make:iso" in sources:
        tools += ["genisoimage", "isoinfo"]
        if config in ("sh", "link-sh", "shell"):
            modules = ["lz4", "PIL", "fontTools"]
    for module in modules:
        if importlib.util.find_spec(module) is None:
            raise OSError(f"missing Python module {module}")
    tools += [directory / "bin" / f"{prefix}-{tool}" for tool in
              ("gcc", "as", "ar", "ld", "objcopy", "objdump", "nm", "readelf")]
    for tool in tools:
        if not shutil.which(tool):
            raise OSError(f"missing tool {tool}")
    for file in ("include/stdio.h", "lib/libc.a"):
        with (directory / prefix / file).open("rb") as stream:
            if not stream.read(1):
                raise OSError(f"empty newlib {file}")


def mutate(tree, mutation):
    op = next(k for k in ("remove", "append", "replace") if k in mutation)
    path = tree / mutation[op]
    if not path.is_file() or not path.resolve().is_relative_to(tree):
        raise ValueError(f"invalid target {path}")
    if op == "remove":
        path.unlink()
    elif op == "append":
        with path.open("ab") as file:
            file.write(b"\x01")
    else:
        source = path.read_text()
        if source.count(mutation["old"]) != 1:
            raise ValueError("mutation target not unique")
        path.write_text(source.replace(mutation["old"], mutation["new"]))


def snapshot(destination):
    copy_candidate(ROOT, destination)
    fonts = fetch_fonts.MEMBERS | fetch_fonts.INCONSOLATA_MEMBERS | fetch_fonts.JFDOT_MEMBERS
    names = [name for name, _ in fonts.values()]
    names.extend((fetch_fonts.OFL_NAME, fetch_fonts.JFDOT_LICENSE_NAME))
    for name in names:
        path = Path("assets/fonts") / name
        if (ROOT / path).is_file():
            shutil.copy2(ROOT / path, destination / path)


def execute(candidate, row, cpu, config, mutations, env):
    prerequisites(row["environment"], cpu, config, row["sources"])
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp)
        shutil.copytree(candidate, tree, dirs_exist_ok=True)
        command = [sys.executable, "-B", f'tests/{row["subsystem"]}/{row["id"]}.py', config]
        for phase in ("build", "verify"):
            result = subprocess.run(command + [phase], cwd=tree, env=env)
            if result.returncode:
                return 1 if result.returncode == 1 else 2
        for mutation in mutations:
            started = time.monotonic()
            # Each mutant starts from the passing control.
            target = tree / next(mutation[k] for k in ("remove", "append", "replace") if k in mutation)
            with tempfile.TemporaryDirectory() as backup:
                saved = Path(backup) / "original"
                shutil.copy2(target, saved)
                mutate(tree, mutation)
                result = subprocess.run(command + ["verify"], cwd=tree, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                print(result.stdout, end="")
                detected = result.returncode == 1 and mutation["fail"] in result.stdout
                print(f'{mutation["id"]} {"RED" if detected else "UNDETECTED"} rc={result.returncode} {time.monotonic()-started:.1f}s', flush=True)
                if not detected:
                    return 2 if result.returncode not in (0, 1) else 1
                shutil.copy2(saved, target)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["lint", "run"])
    parser.add_argument("--mutate", action="store_true")
    parser.add_argument("--check")
    parser.add_argument("--config")
    args = parser.parse_args()
    if args.command == "lint":
        rows = load(ROOT)
        print(f"PASS registry {len(rows)} checks")
        return 0
    env = os.environ.copy()
    for key in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEOVERRIDES"):
        env.pop(key, None)
    commit, epoch = subprocess.check_output(["git", "log", "-1", "--format=%H %ct"], cwd=ROOT, text=True).split()
    env.update(OS32_BUILD_COMMIT=commit[:12], SOURCE_DATE_EPOCH=epoch, TZ="UTC", LC_ALL="C",
               PYTHONDONTWRITEBYTECODE="1")
    with tempfile.TemporaryDirectory() as tmp:
        candidate = Path(tmp)
        snapshot(candidate)
        rows = load(candidate)
        print(f"candidate={commit} (worktree inputs) mutate={args.mutate}", flush=True)
        return run_rows(candidate, rows, args, env)


def run_rows(candidate, rows, args, env):
    status = count = 0
    for row in rows:
        if args.check and args.check != row["id"]:
            continue
        for cpu, configs in row["arch"].items():
            for config in configs:
                if args.config and args.config != config:
                    continue
                mutations = [m for m in row["mutations"] if args.mutate and m["config"] == config]
                start = time.monotonic()
                try:
                    rc = execute(candidate, row, cpu, config, mutations, env)
                except (OSError, ValueError) as exc:
                    print(exc)
                    rc = 2
                print(row["id"], config, ("PASS", "FAIL", "ERROR")[rc],
                      f"{time.monotonic()-start:.1f}s", flush=True)
                status = max(status, rc)
                count += 1
    if not count:
        raise ValueError("no matching check configuration")
    print(f'{count} rows rc={status}', flush=True)
    return status


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        sys.exit(2)
