#!/usr/bin/env python3
"""Measure PowerPC kernel C compilation using the existing ARM gauge.

PPC_CC or --cc overrides $PPC_CROSS_DIR/bin/powerpc-elf-gcc.
Use --no-target-flags with an i386 compiler to validate the gauge itself.
This is a measurement, deliberately separate from make check.
"""

import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_arm_compile as gauge


PPC_FLAGS = ["-mcpu=7450", "-m32", "-mbig-endian", "-msoft-float",
             "-mno-altivec", "-msdata=none"]

# Keep generation failures separate from architectural dependencies.
CAT_GENERATED = "generated"
MAKE_TIMEOUT = 300  # seconds for each generated-header make
GENERATED_RE = re.compile(r"fatal error: \w+_imports\.h:")
gauge_classify = gauge.classify


def classify(err_line):
    return CAT_GENERATED if GENERATED_RE.search(err_line) else gauge_classify(err_line)


def prepare_generated_headers():
    """Expand and run existing Python-only make rules, without building objects.

    HWDEV_GENERATED is deliberately excluded: its private layout uses i386
    DWARF, and its generator writes to the source tree rather than BUILD_OUT.
    """
    root = Path(gauge.PROJ_DIR)
    makefiles = ["build/kdebug.mk", "build/sqlite-proto.mk"]
    makefiles += sorted(str(p.relative_to(root))
                        for p in (root / "build").glob("*-module.mk"))
    names = []
    for filename in makefiles:
        names += re.findall(r"^(\w+(?:_GEN|_GENERATED))\s*:=", gauge.read(root / filename),
                            re.M)
    names = sorted(set(names) - {"KDEBUG_GEN", "SQLITE_GEN", "HWDEV_GEN", "HWDEV_GENERATED"})
    query = "include build/config.mk\n"
    query += "".join("include %s\n" % filename for filename in makefiles)
    query += ".PHONY: __ppc_headers\n__ppc_headers:\n\t@"
    query += "$(info $(filter %%_imports.h,%s)):\n" % " ".join(
        "$(%s)" % name for name in names)
    env = dict(os.environ)
    for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "GNUMAKEFLAGS"):
        env.pop(name, None)
    cmd = ["make", "--no-print-directory", "-rRsf", "-",
           "CROSS_DIR=/home/hight/opt/cross",
           "BUILD_OUT=build/out", "KDEBUG_GEN=build/out/ppc-gauge/kdebug",
           "SQLITE_GEN=build/out/ppc-gauge/sqlite"]
    records = []
    try:
        # Same make rules as a normal build, so a stale tracked kernel/module/module_schema.h
        # is regenerated here too; everything else stays under build/out.
        expanded = subprocess.run(cmd + ["__ppc_headers"], input=query, timeout=MAKE_TIMEOUT,
                                  cwd=root, env=env, text=True, capture_output=True)
        if expanded.returncode:
            raise RuntimeError(expanded.stderr.strip() or expanded.stdout.strip())
        headers = sorted(set(expanded.stdout.split()))
        if not headers:
            raise RuntimeError("make returned no generated import headers")
        for header in headers:
            if not (root / header).resolve().is_relative_to((root / "build/out").resolve()):
                raise RuntimeError("generated header outside build/out: " + header)
        for header in headers:
            proc = subprocess.run(cmd + [header], input=query, cwd=root, env=env,
                                  timeout=MAKE_TIMEOUT, text=True, capture_output=True)
            ok = proc.returncode == 0 and (root / header).is_file()
            record = {"header": header, "ok": ok}
            if not ok:
                record["reason"] = (proc.stderr.strip() or proc.stdout.strip()
                                    or "make did not create header")
                print("生成ヘッダ: %s: %s" % (header, record["reason"]), file=sys.stderr)
            records.append(record)
        includes = sorted({str(Path(r["header"]).parent) for r in records if r["ok"]})
        return ["-I" + path for path in includes], records
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print("生成ヘッダの準備失敗: %s" % exc, file=sys.stderr)
        return [], [{"ok": False, "reason": str(exc)}]


def main():
    parser = argparse.ArgumentParser(description="カーネル C の PowerPC コンパイル計測")
    parser.add_argument("--cc", default=os.environ.get("PPC_CC", os.path.join(
        # x86 builds use CROSS_DIR for i386; keep the PPC toolchain independent.
        os.environ.get("PPC_CROSS_DIR", os.path.expanduser("~/opt/cross-ppc")),
        "bin", "powerpc-elf-gcc")), help="計測用コンパイラ")
    parser.add_argument("--no-target-flags", action="store_true",
                        help="計測器の検証用: PowerPC 専用フラグを追加しない")
    parser.add_argument("--json", action="store_true", help="JSON を出す")
    parser.add_argument("--jobs", type=int, default=0, help="並列数 (既定: CPU 数)")
    args = parser.parse_args()

    gauge.classify = classify
    gauge.CAT_LABEL[CAT_GENERATED] = "(e) 生成ヘッダの不足 (x86 依存ではない)"
    gauge.CAT_ORDER.append(CAT_GENERATED)
    includes, generated = (prepare_generated_headers() if shutil.which(args.cc)
                           else ([], []))
    old_flags = gauge.KERNEL_EXTRA_FLAGS
    old_argv = sys.argv
    old_cc = os.environ.get("ARM_CC")
    output = io.StringIO()
    try:
        gauge.KERNEL_EXTRA_FLAGS = old_flags + includes + (
            [] if args.no_target_flags else PPC_FLAGS)
        os.environ["ARM_CC"] = args.cc
        sys.argv = [old_argv[0], "--jobs", str(args.jobs)]
        if args.json:
            sys.argv.append("--json")
        with contextlib.redirect_stdout(output):
            rc = gauge.main()
    finally:
        gauge.KERNEL_EXTRA_FLAGS = old_flags
        sys.argv = old_argv
        if old_cc is None:
            os.environ.pop("ARM_CC", None)
        else:
            os.environ["ARM_CC"] = old_cc

    rendered = output.getvalue()
    if args.json and rendered.strip():
        result = json.loads(rendered)
        result["generated_headers"] = generated
        rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if not args.json:
        rendered = rendered.replace("ARM コンパイル計測", "PowerPC コンパイル計測")
        rendered = rendered.replace("ARM 計測を飛ばします", "PowerPC 計測を飛ばします")
    print(rendered, end="")
    return rc


if __name__ == "__main__":
    sys.exit(main())
