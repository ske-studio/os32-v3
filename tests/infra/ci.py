"""Plan, run and collect registry checks in local or hosted builds."""
import argparse
from collections import Counter, defaultdict
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from tests.support.tracked import paths
from tests.support.registry import load
from tools.ownership import Inventory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from build_compare import copy_candidate
import fetch_fonts


def prerequisites(environment, cpu, config, sources):
    if environment == "python-yaml" and importlib.util.find_spec("yaml") is None:
        raise OSError("missing Python module yaml")
    if environment in ("python", "python-yaml"):
        return
    ppc = environment in ("linux-ppc-cross", "qemu-ppc") or environment == "linux-cross" and cpu == "ppc"
    directory = Path(os.environ.get("PPC_CROSS_DIR" if ppc else "CROSS_DIR",
                                    Path.home() / "opt" / ("cross-ppc" if ppc else "cross")))
    prefix = "powerpc-elf" if ppc else "i386-elf"
    tools = ["make", "python3"]
    if environment == "qemu-ppc":
        tools.append("qemu-system-ppc")
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
    (destination / ".check-index.json").write_text(json.dumps(sorted(paths(ROOT))))
    fonts = fetch_fonts.MEMBERS | fetch_fonts.INCONSOLATA_MEMBERS | fetch_fonts.JFDOT_MEMBERS
    names = [name for name, _ in fonts.values()]
    names.extend((fetch_fonts.OFL_NAME, fetch_fonts.JFDOT_LICENSE_NAME))
    for name in names:
        path = Path("assets/fonts") / name
        if (ROOT / path).is_file():
            shutil.copy2(ROOT / path, destination / path)


STATES = ("PASS", "FAIL", "ERROR", "NOT_RUN", "NOT_APPLICABLE", "DEFERRED", "REUSED")


def selection(rows, args):
    for row in rows:
        if args.check and args.check != row["id"]:
            continue
        for cpu, configs in row["arch"].items():
            if args.bundle and cpu != args.bundle:
                continue
            for config in configs:
                if not args.config or config == args.config:
                    yield row, cpu, config


def exemption(row, config, gate):
    if gate not in row["gates"]:
        return "NOT_APPLICABLE", f"gate {gate} outside registration"
    approval = row.get("deferred")
    if approval and gate in approval["gates"]:
        configs = approval["config"]
        if config not in ([configs] if isinstance(configs, str) else configs):
            return None
        return "DEFERRED", f'{approval["ledger"]}: {config} at {gate}'
    return None


def scoped_deferral(exempt):
    return exempt and exempt[0] == "DEFERRED" and exempt[1].split(":")[0] in ("P43-INCLUDE", "P43-LINK")


def manifest(plan, identity):
    inventory = Inventory(ROOT)
    ownership = {path: inventory.lookup(path)["kind"] for path in inventory.paths}
    entries = []
    for row, cpu, config in plan:
        inputs = {path: ownership.get(path) for path in row["sources"]}
        if any(path.startswith("make:") for path in inputs):
            for target in (ROOT / "build/targets").glob(f"{cpu}-*.mk"):
                path = target.relative_to(ROOT).as_posix()
                inputs[path] = ownership.get(path)

        mids = [m["id"] for m in row["mutations"] if m["config"] == config
                and identity["mutate"] and (not (exempt := exemption(row, config, identity["gate"])) or scoped_deferral(exempt))]
        entries.append(dict(id=row["id"], config=config, bundle=cpu, owner=row["subsystem"],
                            level=row["level"], environment=row["environment"], inputs=inputs,
                            mutations=mids))
    return dict(identity=identity, entries=entries)


def state(rc):
    return {0: "PASS", 1: "FAIL"}.get(rc, "ERROR")


def execute(candidate, row, cpu, config, mutations, env, log, outcomes, deferred=None):
    prerequisites(row["environment"], cpu, config, row["sources"])
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp)
        shutil.copytree(candidate, tree, dirs_exist_ok=True)
        cmd = [sys.executable, "-B", f'tests/{row["subsystem"]}/{row["id"]}.py', config]
        if deferred:
            env = dict(env, OS32_LAYER_DEFERRAL=deferred[1].split(":")[0])
        else:
            env = env.copy()
            env.pop("OS32_LAYER_DEFERRAL", None)
        status = ("PASS", "build and verify passed")
        for phase in ("build", "verify"):
            rc = subprocess.run(cmd + [phase], cwd=tree, env=env, stdout=log,
                                stderr=subprocess.STDOUT).returncode
            if phase == "verify" and rc == 3 and deferred:
                status = deferred
            elif rc:
                return state(rc), f"{phase} exited {rc}"
        for m in mutations:
            start = time.monotonic()
            target = tree / next(m[k] for k in ("remove", "append", "replace") if k in m)
            with tempfile.TemporaryDirectory() as backup:
                saved = Path(backup) / "original"
                try:
                    shutil.copy2(target, saved)
                    mutate(tree, m)
                    proc = subprocess.run(cmd + ["verify"], cwd=tree, env=env, stdout=subprocess.PIPE,
                                          stderr=subprocess.STDOUT, text=True, errors="replace")
                    print(proc.stdout, file=log)
                    detected = proc.returncode == 1 and m["fail"] in proc.stdout
                    result = "RED" if detected else state(proc.returncode)
                    if result in ("PASS", "FAIL"):
                        result = "UNDETECTED"
                except (OSError, ValueError) as exc:
                    print(exc, file=log)
                    result = "ERROR"
                finally:
                    if saved.exists():
                        shutil.copy2(saved, target)
                outcomes[m["id"]] = dict(result=result, seconds=time.monotonic()-start)
                print(m["id"], result, file=log)
    return status


def run_bundle(candidate, plan, args, env, identity):
    start = time.monotonic()
    records = []
    bundle = args.output / f"bundle-{args.bundle or 'all'}.json"
    pending = bundle.with_suffix(".tmp")
    bundle.unlink(missing_ok=True)
    for row, cpu, config in plan:
        began = time.monotonic()
        name = row["id"]
        logfile = f"{name}-{config}.log"
        outcomes = {}
        with (args.output / logfile).open("w") as log:
            exempt = exemption(row, config, args.gate)
            if exempt and not scoped_deferral(exempt):
                status, reason = exempt
            else:
                mutations = [m for m in row["mutations"] if args.mutate and m["config"] == config]
                try:
                    status, reason = execute(candidate, row, cpu, config, mutations, env, log, outcomes, exempt)
                except (OSError, ValueError) as exc:
                    status, reason = "ERROR", str(exc)
                print(reason, file=log)
        seconds = time.monotonic()-began - sum(m["seconds"] for m in outcomes.values())
        records.append(dict(id=name, config=config, status=status, reason=reason, seconds=seconds,
                            log=logfile, mutations=outcomes))
        print(name, config, status, f"{seconds:.1f}s", flush=True)
        data = dict(identity=identity, records=records, seconds=time.monotonic()-start)
        pending.write_text(json.dumps(data, indent=2))
        pending.replace(bundle)


def read_bundle(path, identity):
    data = json.loads(path.read_text())
    if data["identity"] != identity:
        raise ValueError("unrelated bundle")
    def duration(value):
        return type(value) in (int, float) and math.isfinite(value) and value >= 0
    if not duration(data["seconds"]) or not isinstance(data["records"], list):
        raise ValueError("invalid bundle")
    for item in data["records"]:
        if not all(isinstance(item[k], str) and item[k]
                   for k in ("id", "config", "status", "reason", "log")):
            raise ValueError("invalid result row")
        if Path(item["log"]).name != item["log"] or not duration(item["seconds"]):
            raise ValueError("invalid result log or duration")
        if not isinstance(item["mutations"], dict):
            raise ValueError("invalid mutation results")
        for result in item["mutations"].values():
            if result["result"] not in ("RED", "UNDETECTED", "ERROR") or not duration(result["seconds"]):
                raise ValueError("invalid mutation outcome")
    return data


def collect(plan, args, identity, plan_errors=()):
    plan = list(plan)
    owners = {(row["id"], config): cpu for row, cpu, config in plan}
    records = {}
    errors = list(plan_errors)
    durations = {}
    paths = ([args.output / f"bundle-{args.bundle or 'all'}.json"] if getattr(args, "command", "") == "run"
             else sorted(args.output.glob("bundle-*.json")))
    for path in paths:
        try:
            data = read_bundle(path, identity)
        except (ValueError, KeyError, TypeError, OSError) as exc:
            errors.append(f"{path.name}: {exc}")
            continue
        durations[path.stem] = data["seconds"]
        for item in data["records"]:
            key = item["id"], item["config"]
            if key in owners and path.stem not in ("bundle-all", f"bundle-{owners[key]}"):
                errors.append(f"{path.name}: misplaced {key}")
            if key in records:
                errors.append(f"duplicate {key}")
            records[key] = item
    table = []
    totals = defaultdict(Counter)
    rc = 0
    failed = []
    print("| Check | Configuration | Status | Seconds | Reason | Log |")
    print("|---|---|---|---:|---|---|")
    for row, cpu, config in plan:
        name = row["id"]
        item = records.pop((name, config), None)
        if item is None:
            item = dict(id=name, config=config, status="NOT_RUN", reason="missing row",
                        seconds=0, log="", mutations={})
        status = item["status"]
        exempt = exemption(row, config, args.gate)
        applicable = exempt is None or scoped_deferral(exempt)
        allowed = ("PASS", "FAIL", "ERROR", "NOT_RUN") if applicable else (exempt[0], "NOT_RUN")
        if scoped_deferral(exempt):
            allowed += ("DEFERRED",)
        if status not in allowed or (exempt and status == exempt[0] and item["reason"] != exempt[1]):
            item.update(status="ERROR", reason=f"unjustified status {status}")
        status = item["status"]
        rc = max(rc, 2 if status == "ERROR" else int(status in ("FAIL", "NOT_RUN")))
        table.append(item)
        reason = item["reason"].replace("|", "&#124;").replace("\n", " ")
        print(f'| {name} | {config} | {status} | {item["seconds"]:.1f} | {reason} | {item["log"]} |')
        required = {m["id"] for m in row["mutations"] if m["config"] == config and applicable}
        stats = totals[name]
        stats["declared"] += len(required)
        observed = item["mutations"]
        if observed.keys() - (required if args.mutate else set()):
            errors.append(f"unexpected mutations {name} {config}")
        stats["executed"] += len(observed)
        for outcome in observed.values():
            stats["red"] += outcome["result"] == "RED"
            stats["seconds"] += outcome["seconds"]
            if outcome["result"] == "ERROR":
                rc = 2
        missing = required - observed.keys() if args.mutate else set()
        item["missing_mutations"] = sorted(missing)
        broken = missing or any(m["result"] != "RED" for m in observed.values())
        if broken:
            rc = max(rc, 1)
        if broken or status in ("FAIL", "ERROR", "NOT_RUN"):
            failed.append(item)
    if records:
        errors.append(f"unexpected rows {list(records)}")
    if errors:
        rc = 2
        print("\n" + "\n".join(errors))
    counts = Counter(item["status"] for item in table)
    print("\n" + ", ".join(f"{s} {counts[s]}" for s in STATES))
    print("\n| Check | Declared | Executed | RED | Mutation seconds |")
    print("|---|---:|---:|---:|---:|")
    for name, stats in totals.items():
        print(f'| {name} | {stats["declared"]} | {stats["executed"]} | {stats["red"]} | {stats["seconds"]:.1f} |')
    for item in failed:
        print(f'\n{item["id"]} {item["config"]} — {item["reason"]}')
        if item["missing_mutations"]:
            print("Missing mutations:", ", ".join(item["missing_mutations"]))
        logfile = args.output / item["log"]
        if logfile.is_file():
            print("\n".join("    " + line for line in logfile.read_text(errors="replace").splitlines()[-12:]))
    print()
    for item in sorted(table, key=lambda item: -item["seconds"])[:5]:
        print("slow", item["id"], item["config"], f'{item["seconds"]:.1f}s')
    print("mode", "with mutations" if args.mutate else "normal", "bundle seconds", durations)
    print(f"{len(table)} rows rc={rc}")
    (args.output / "summary.json").write_text(json.dumps(dict(identity=identity, rows=table,
        mutations=totals, durations=durations, errors=errors, rc=rc), indent=2))
    return rc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["lint", "plan", "run", "collect"])
    parser.add_argument("--mutate", action="store_true")
    parser.add_argument("--check")
    parser.add_argument("--config")
    parser.add_argument("--bundle")
    parser.add_argument("--gate", choices=["push", "main"], default="main")
    parser.add_argument("--output", type=Path, default=Path("build/out/check-results"))
    parser.add_argument("--plan-file", type=Path)
    args = parser.parse_args()
    rows = load(ROOT)
    plan = list(selection(rows, args))
    if not plan:
        raise ValueError("no matching check configuration")
    if args.command == "lint":
        print(f"PASS registry {len(rows)} checks")
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    commit, epoch = subprocess.check_output(["git", "log", "-1", "--format=%H %ct"], cwd=ROOT, text=True).split()
    identity = dict(sha=commit, kapi_base=os.getenv("OS32_KAPI_BASE"), run=os.getenv("GITHUB_RUN_ID", "local"),
                    attempt=os.getenv("GITHUB_RUN_ATTEMPT", "1"), mutate=args.mutate, gate=args.gate)
    all_args = argparse.Namespace(check=None, config=None, bundle=None)
    expected = manifest(selection(rows, all_args), identity)
    plan_errors = []
    if args.plan_file:
        try:
            if json.loads(args.plan_file.read_text()) != expected:
                raise ValueError("plan differs from candidate registration or run identity")
        except (OSError, ValueError) as exc:
            if args.command != "collect":
                raise
            plan_errors.append(str(exc))
    if args.command == "plan":
        (args.output / "plan.json").write_text(json.dumps(expected, ensure_ascii=False, indent=2))
        print(json.dumps(sorted({entry["bundle"] for entry in expected["entries"]})))
        return 0
    if not args.plan_file:
        (args.output / "plan.json").write_text(json.dumps(expected, ensure_ascii=False, indent=2))
    if args.command == "collect":
        return collect(plan, args, identity, plan_errors)
    env = os.environ.copy()
    cross_bin = Path(env.get("CROSS_DIR", Path.home() / "opt/cross")) / "bin"
    env["PATH"] = str(cross_bin) + os.pathsep + env.get("PATH", os.defpath)
    for key in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEOVERRIDES"):
        env.pop(key, None)
    short_sha = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT, text=True).strip()
    env.update(OS32_CHECK_REPOSITORY=str(ROOT), OS32_BUILD_COMMIT=short_sha, SOURCE_DATE_EPOCH=epoch, TZ="UTC", LC_ALL="C",
               PYTHONDONTWRITEBYTECODE="1")
    with tempfile.TemporaryDirectory() as tmp:
        candidate = Path(tmp)
        snapshot(candidate)
        run_bundle(candidate, plan, args, env, identity)
    return collect(plan, args, identity)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ImportError, ValueError, KeyError, TypeError) as exc:
        print(exc, file=sys.stderr)
        sys.exit(2)
