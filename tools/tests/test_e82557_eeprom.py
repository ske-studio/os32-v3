#!/usr/bin/env python3
"""82557 EEPROM contract, clocked model and independent T/S oracles.
Record: tools/tests/e82557_eeprom_tdd.md
"""
import os
import pathlib
import subprocess
import sys
import tempfile

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/e82557_eeprom_host.c"
SRC = ROOT / "drivers/e82557_eeprom.c"
TARGET_SRCS = ["drivers/e82557_eeprom.c"]
CASES = ([f"window{i}" for i in range(10)] + ["frame", "auto6rise", "auto6fall",
    "auto8rise", "auto8fall", "nowin", "width1", "width7", "read6rise", "read6fall",
    "read8rise", "read8fall", "erased", "empty", "stuck1fixed", "stuck0fixed", "interpret"])
FLAGS = ["-m32", "-static", "-no-pie", "-fno-pie", "-fno-stack-protector",
         "-ffreestanding", "-nostdlib", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "drivers", "sdk/include/os32")] + ["-I" + str(ROOT)]
MIRROR = ["tools/tests/e82557_eeprom_host.c", "drivers/e82557_eeprom.c",
          "drivers/e82557_eeprom.h", "drivers/e82557_regs.h", "drivers/pci_decode.c"]
# source, exact text, replacement, label; no mutation counts compile failure as RED.
MUTATIONS = [
    ("drivers/e82557_regs.h", "#define E82557_EE_READ         6u", "#define E82557_EE_READ         5u", "opcode WRITE"),
    ("drivers/e82557_eeprom.c", "(bits >> i)", "(bits >> (n - 1 - i))", "LSB first"),
    ("drivers/e82557_eeprom.c", "        p->write(p->ctx, pin);\n        p->delay(p->ctx, p->step_us); /* D: command fall */\n        if (i < w)",
     "        if (i < w)", "dummy at rising edge"),
    ("drivers/e82557_eeprom.c", "*width = w - i;", "*width = w - i + 1;", "width off by one"),
    ("drivers/e82557_eeprom.c", "((i & 1) * 8)", "((1 - (i & 1)) * 8)", "MAC endian"),
    ("drivers/e82557_eeprom.c", "p->write(p->ctx, 0); /* CS cleanup */", "/* no CS cleanup */", "CS left selected"),
    ("drivers/e82557_eeprom.c", "for (i = n - 1; i >= 0; i--)", "for (i = n - 1; i >= 0 || mode == E82557_EE_AUTO; i--)", "unbounded detection"),
    ("drivers/e82557_regs.h", "E82557_EE_STEP_US      4u", "E82557_EE_STEP_US      1u", "T step 1"),
    ("drivers/e82557_regs.h", "E82557_EE_STEP_US      4u", "E82557_EE_STEP_US      0u", "T step 0"),
]
for label in ("initial", "select", "command low", "command high", "command fall", "data high", "data low", "finish"):
    MUTATIONS.append(("drivers/e82557_eeprom.c",
        f"p->delay(p->ctx, p->step_us); /* D: {label} */", f"/* D removed: {label} */", f"S delete {label}"))
MUTATIONS += [
    ("drivers/e82557_eeprom.c", "b > E82557_PORT_LIMIT - E82557_CSR_LEN", "b >= E82557_PORT_LIMIT - E82557_CSR_LEN", "BAR end exclusive"),
    ("drivers/e82557_eeprom.c", "b > E82557_PORT_LIMIT - E82557_CSR_LEN", "b + E82557_CSR_LEN > E82557_PORT_LIMIT", "BAR addition overflow"),
    ("drivers/e82557_eeprom.c", "b & (E82557_CSR_LEN - 1)", "0 && (b & (E82557_CSR_LEN - 1))", "BAR alignment removed"),
    ("drivers/e82557_eeprom.c", "pci_bar_kind(raw) != PCI_BAR_IO", "0 && pci_bar_kind(raw) != PCI_BAR_IO", "BAR kind removed"),
]
# The rising-edge mutation moves the fall AFTER the sample, preserving every D.
# It is intentionally a full executable mutation, not just an S omission.
def mutation_text(rel, old, new, label):
    text = (ROOT / rel).read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"mutation anchor count {text.count(old)}: {label}")
    text = text.replace(old, new, 1)
    if label == "dummy at rising edge":
        old_end = "        }\n    }\n    if (mode == E82557_EE_AUTO)"
        new_end = ("        }\n        p->write(p->ctx, pin);\n"
                   "        p->delay(p->ctx, p->step_us); /* late fall */\n"
                   "    }\n    if (mode == E82557_EE_AUTO)")
        # break must also execute the late fall, so model still closes a clock.
        text = text.replace("                break;", "                p->write(p->ctx, pin);\n"
                            "                p->delay(p->ctx, p->step_us);\n                break;", 1)
        if text.count(old_end) != 1:
            raise RuntimeError("late-fall mutation anchor")
        text = text.replace(old_end, new_end, 1)
    return text


def host_build(tmp, mutated=None):
    root = ROOT
    if mutated:
        root = pathlib.Path(tmp) / "tree"
        for rel in MIRROR:
            dst = root / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(mutated[1] if rel == mutated[0] else (ROOT / rel).read_text())
    exe = pathlib.Path(tmp) / "ee-host"
    subprocess.run(["gcc", *FLAGS, *INCLUDES, str(root / HARNESS.relative_to(ROOT)), "-o", str(exe)], check=True)
    return exe


def run_case(exe, case, only=0):
    return host32.run([str(exe), case, str(only)], capture_output=True, text=True, timeout=10)


def target_build(tmp):
    cross = pathlib.Path(os.environ.get("CROSS_DIR", "/home/hight/opt/cross")) / "bin/i386-elf-gcc"
    for rel in TARGET_SRCS:
        subprocess.run([str(cross), "-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
            "-fno-pie", "-fno-stack-protector", "-nostdlib", "-Os", "-Wall", "-Wextra", "-Werror",
            "-D__KERNEL_BUILD__", *INCLUDES,
            *["-I" + str(ROOT / p) for p in ("arch/x86", "platform/pc98", "lib", "kernel")],
            "-c", str(ROOT / rel), "-o", str(pathlib.Path(tmp) / (pathlib.Path(rel).stem + ".o"))], check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


def mutate(tmp):
    failed = 0
    for i, (rel, old, new, label) in enumerate(MUTATIONS, 1):
        exe = host_build(tmp, (rel, mutation_text(rel, old, new, label)))
        hits = []
        for case in CASES:
            result = run_case(exe, case)
            if result.returncode:
                hits.append((case, result.stderr))
        red = bool(hits)
        oracle = hits[0][1].splitlines()[0] if hits else "none"
        if label.startswith("S delete") or label.startswith("T step"):
            t = run_case(exe, "read6rise", 1).returncode != 0
            s = run_case(exe, "read6rise", 2).returncode != 0
            oracle = f"T={'RED' if t else 'GREEN'} S={'RED' if s else 'GREEN'}"
            if label.startswith("S delete"):
                red = red and s  # ALL eight deletions MUST independently fail S.
            else:
                red = red and t
        if label == "CS left selected":
            red = red and run_case(exe, "read6rise", 1).returncode != 0
        if label == "dummy at rising edge":
            # A falling-edge part is the required counterexample.
            red = red and run_case(exe, "auto6fall").returncode != 0
        print(f"MUTATION {i} {'RED' if red else 'SURVIVED'} {label}: {oracle}", flush=True)
        failed += not red
    print(f"MUTATIONS {len(MUTATIONS)-failed}/{len(MUTATIONS)} RED", flush=True)
    return failed


def main():
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-ee-") as tmp:
        exe = host_build(tmp)
        if "--target" in args:
            target_build(tmp)
        cases = [a for a in args if not a.startswith("--")] or CASES
        failed = 0
        for case in cases:
            result = run_case(exe, case)
            print(f"CASE {case} rc={result.returncode}", flush=True)
            if result.returncode:
                print(result.stderr, end="", file=sys.stderr)
                failed += 1
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        if "--mutate" in args:
            failed += mutate(tmp)
        return bool(failed)


if __name__ == "__main__":
    sys.exit(main())
