"""T1 pure recovery contracts; real source + fake bus/clock, no hardware.

--mutate covers DESIGN.final §7's T1 mutations; PTE restore belongs to T2.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = "kdebug/recover.c"
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-m32", "-march=i386",
         "-ffreestanding", "-fno-pie", "-fno-stack-protector"]


def host_cmd(exe):
    return ["gcc", *FLAGS, "-O0", "-nostdlib", "-static", "-no-pie",
            "-I" + str(ROOT / "include"),
            str(ROOT / "tools/tests/recover_host.c"), "-o", str(exe)]


MUTATIONS = [
    ("snapshot_valid_early", "r->valid = false; r->failed = false;",
     "r->valid = true; r->failed = false;"),
    ("cleanup_removed", "bool safe = !r->access_blocked && quiesce(r, true);",
     "bool safe = false;"),
    ("w1c_allowed", "(d->kind != REC_RW && d->kind != REC_COLD)",
     "(d->kind != REC_RW && d->kind != REC_COLD && d->kind != REC_W1C)"),
    ("mask_all_bits", "(actual & ~d->write_mask) | (r->saved[i] & d->write_mask)",
     "r->saved[i]"),
    ("restore_order", "const struct recover_reg *d = &r->profile->regs[i];",
     "const struct recover_reg *d = &r->profile->regs[r->saved_count - 1 - i];"),
    ("deadline_inverted", "if (r->spent >= r->budget ||",
     "if (r->spent < r->budget ||"),
    ("stale_session", "t && t->session == r->token.session &&", "t &&"),
    ("verify_ok", "fail(r, REC_VERIFY, phase, w->key); return false;",
     "return true;"),
    ("cold_replayed", "if (safe && !r->profile->cold_required)", "if (safe)"),
]


def one_mutation(item):
    name, old, new = item
    original = (ROOT / SOURCE).read_text()
    if old and original.count(old) != 1:
        return f"MUTATE {name}: missing/ambiguous anchor", 1
    with tempfile.TemporaryDirectory(prefix="recover-mut-") as td:
        exe = pathlib.Path(td) / "recover-host"
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {SOURCE: original.replace(old, new, 1) if old else original},
                [host_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError as error:
            return f"MUTATE {name}: COMPILE FAIL {error.stderr!r}", 1
        result = host32.run([str(exe)], cwd=str(tree), timeout=60, capture_output=True)
        if result.returncode == 0:
            return f"MUTATE {name}: GREEN", 1
        return f"MUTATE {name}: RED rc={result.returncode}", 0


def main():
    with tempfile.TemporaryDirectory(prefix="recover-host-") as td:
        exe = pathlib.Path(td) / "recover-host"
        subprocess.run(host_cmd(exe), cwd=ROOT, check=True)
        rc = host32.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print(f"HOST recover rc={rc}", flush=True)
        if rc:
            return 1
        cross = pathlib.Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc"
        subprocess.run([str(cross), *FLAGS, "-O2", "-I" + str(ROOT / "include"),
                        "-c", str(ROOT / SOURCE), "-o", str(pathlib.Path(td) / "recover.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 compile PASS", flush=True)
        if "--mutate" in sys.argv:
            return int(bool(mutpar.run_with_control(
                one_mutation, MUTATIONS, ("control", "", ""))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
