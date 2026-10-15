#!/usr/bin/env python3
"""LB1 real driver injection. Record: tools/tests/e82557_drv_tdd.md"""
import os
import pathlib
import subprocess
import sys
import tempfile
import host32
ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/e82557_drv_host.c"
TARGET_SRCS = ["drivers/e82557.c"]
CASES = ["pass", "kill", "mkdir", "same", "other", "seq_missing_old", "seq_ahead", "monotonic", "try_read", "seq_read", "try_write", "seq_write", "try_short", "seq_short", "try_back", "seq_back", "sync", "write_zero", "remove", "crc_missing", "bad_bar", "io_disabled", "reset_fail", "pre_wait", "post_wait", "ru_post_wait", "no_trace", "ee_fallback", "ee_badmac", "ee_detect_fail"] + [f"bad_try{i}" for i in range(9)] + [f"bad_seq{i}" for i in range(7)]
FLAGS = ["-m32", "-static", "-no-pie", "-fno-pie", "-fno-stack-protector", "-ffreestanding", "-nostdlib", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "drivers", "sdk/include/os32", "fs", "kernel", "lib", "arch/x86", "platform/pc98")] + ["-I" + str(ROOT)]
MIRROR = ["tools/tests/e82557_drv_host.c", "drivers/e82557.c", "drivers/e82557.h", "drivers/e82557_regs.h", "drivers/e82557_eeprom.c", "drivers/e82557_eeprom.h", "drivers/pci_decode.c", "fs/vfs.h", "kernel/module.h", "include/bootinfo.h", "include/config.h"]
MUTATIONS = [('drivers/e82557.c',
  'outpd(base + E82557_PORT, E82557_PORT_SOFTWARE);',
  '/* software reset removed */',
  'software reset omitted'),
 ('drivers/e82557.c',
  'outpd(base + E82557_PORT, E82557_PORT_SELECTIVE);',
  '/* selective reset removed */',
  'selective reset omitted'),
 ('drivers/e82557.c',
  'cpu_delay_us(E82557_RESET_US);\n    scb_mask',
  'cpu_delay_us(1);\n    scb_mask',
  'selective reset delay shortened'),
 ('drivers/e82557.c',
  'cpu_delay_us(E82557_RESET_US);\n    outpd',
  'cpu_delay_us(1);\n    outpd',
  'software reset delay shortened'),
 ('drivers/e82557.c', 'scb_mask(base, 1);', 'scb_mask(base, 0);', 'interrupt mask cleared'),
 ('drivers/e82557.c',
  'outp(base + E82557_SCB_ACK, (u8)((status & E82557_STAT_MASK) >> 8));',
  'outp(base + E82557_SCB_ACK, 0xff);',
  'ack fixed ff'),
 ('drivers/e82557.c',
  'if (status & (E82557_CUS_MASK | E82557_RUS_MASK))',
  'if (0 && (status & (E82557_CUS_MASK | E82557_RUS_MASK)))',
  'idle guard removed'),
 ('drivers/e82557.c',
  'issued = scb_issue(base, E82557_CU_BASE, 0, 0, 1, &after);',
  'issued = ISSUE_OK;',
  'CU base omitted'),
 ('drivers/e82557.c',
  'issued = scb_issue(base, 0, E82557_RU_BASE, 0, 1, &after);',
  'issued = ISSUE_OK;',
  'RU base omitted'),
 ('drivers/e82557.c',
  'outp(base + E82557_SCB_COMMAND, (u8)((cuc << 4) | ruc));',
  'outpw(base + E82557_SCB_COMMAND, (u16)((cuc << 4) | ruc));',
  '16bit command'),
 ('drivers/e82557.c',
  'if (!scb_wait(base)) return ISSUE_TIMEOUT;',
  'if (0 && !scb_wait(base)) return ISSUE_TIMEOUT;',
  'pre acceptance omitted'),
 ('drivers/e82557.c',
  'return scb_wait(base) ? ISSUE_OK : ISSUE_TIMEOUT;',
  'return ISSUE_OK;',
  'post acceptance omitted'),
 ('drivers/e82557.c',
  'if (rc == VFS_ERR_NOTFOUND) return TRY_NONE;',
  'if (rc < 0) return TRY_NONE;',
  'read IO treated missing'),
 ('drivers/e82557.c',
  'if (decision == TRY_SAME_CRC) {',
  'if (0 && decision == TRY_SAME_CRC) {',
  'same crc retried'),
 ('drivers/e82557.c',
  'if (seq < oldseq) seq = oldseq;',
  'if (0 && seq < oldseq) seq = oldseq;',
  'old mark sequence ignored'),
 ('drivers/e82557.c', 'seq++;', 'seq = 1;', 'persistent seq reset'),
 ('drivers/e82557.c',
  'if (seq == E82557_SEQ_MAX) return "seq-overflow";',
  'if (0 && seq == E82557_SEQ_MAX) return "seq-overflow";',
  'sequence wrap'),
 ('drivers/e82557.c',
  'if ((rc != 0 && rc != len) || vfs_sync() < 0) return 0;',
  'if (rc < 0 || vfs_sync() < 0) return 0;',
  'short write accepted'),
 ('drivers/e82557.c',
  'if ((rc != 0 && rc != len) || vfs_sync() < 0) return 0;',
  'if ((rc != 0 && rc != len) || (0 && vfs_sync() < 0)) return 0;',
  'sync omitted'),
 ('drivers/e82557.c',
  'return rc == len && kstrncmp(buf, back, (u32)len) == 0;',
  'return rc == len && (1 || kstrncmp(buf, back, (u32)len) == 0);',
  'readback mismatch accepted'),
 ('drivers/e82557.c',
  'if (sysconfig_get_int(SYS_SYSTEM_CFG, "E82557", 1) == 0) return "cfg";',
  'if (0 && sysconfig_get_int(SYS_SYSTEM_CFG, "E82557", 1) == 0) return "cfg";',
  'kill switch ignored')]
def mutation_text(rel, old, new, label):
    text = (ROOT / rel).read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"mutation anchor count {text.count(old)}: {label}")
    return text.replace(old, new, 1)

def host_build(tmp, mutated=None):
    root = ROOT
    if mutated:
        root = pathlib.Path(tmp) / "tree"
        for rel in MIRROR:
            dst = root / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(mutated[1] if rel == mutated[0] else (ROOT / rel).read_text())
    exe = pathlib.Path(tmp) / "drv-host"
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
        hits = [case for case in CASES if run_case(exe, case).returncode]
        print(f"MUTATION {i} {'RED' if hits else 'SURVIVED'} {label}: {','.join(hits[:3])}", flush=True)
        failed += not bool(hits)
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
