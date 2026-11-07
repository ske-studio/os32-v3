#!/usr/bin/env python3
"""Measure PowerPC kernel C compilation using the existing ARM gauge.

PPC_CC or --cc overrides $PPC_CROSS_DIR/bin/powerpc-elf-gcc.
Use --no-target-flags with an i386 compiler to validate the gauge itself.
This is a measurement, deliberately separate from make check.
"""

import argparse
import contextlib
import io
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_arm_compile as gauge


PPC_FLAGS = ["-mcpu=7450", "-m32", "-mbig-endian", "-msoft-float",
             "-mno-altivec", "-msdata=none"]

# The gauge's include list has no generated-module directory, so these fall into
# its "x86 header" class although nothing about them depends on x86.
CAT_GENERATED = "generated"
GENERATED_RE = re.compile(r"fatal error: \w+_imports\.h:")
gauge_classify = gauge.classify


def classify(err_line):
    return CAT_GENERATED if GENERATED_RE.search(err_line) else gauge_classify(err_line)


def main():
    parser = argparse.ArgumentParser(description="カーネル C の PowerPC コンパイル計測")
    parser.add_argument("--cc", default=os.environ.get("PPC_CC", os.path.join(
        # CROSS_DIR is the i386 toolchain in the shell and .env: reading it would SKIP silently.
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
    old_flags = gauge.KERNEL_EXTRA_FLAGS
    old_argv = sys.argv
    old_cc = os.environ.get("ARM_CC")
    output = io.StringIO()
    try:
        gauge.KERNEL_EXTRA_FLAGS = old_flags + (
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
    if not args.json:
        rendered = rendered.replace("ARM コンパイル計測", "PowerPC コンパイル計測")
        rendered = rendered.replace("ARM 計測を飛ばします", "PowerPC 計測を飛ばします")
    print(rendered, end="")
    return rc


if __name__ == "__main__":
    sys.exit(main())
