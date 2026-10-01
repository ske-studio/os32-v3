"""T2d d1: real syscall dispatcher + shared caller/redirect identity.

Mutations compile then fail at runtime. Only the two C sources and fixture are
copied per variant; headers use the original read-only include paths.
"""
import argparse
import pathlib
import subprocess
import tempfile
from mutpar import run_ordered

ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTATIONS = [
    ("access", " || a.pd_phys != paging_current_cr3()", "", "entry CR3"),
    ("access", "res_owner_get() != a.app_id", "0", "entry owner"),
    ("access", "a->pd_phys == paging_current_cr3()", "1", "current CR3"),
    ("access", "a->app_id == appslot_cur()", "1", "current slot"),
    ("access", "res_owner_get() == a->app_id", "1", "current owner"),
    ("access", "slot->as->generation == a->generation", "1", "generation"),
    ("access", "slot->as->owner == a->owner", "1", "AS owner"),
    ("access", "slot->as->pd_phys == a->pd_phys", "1", "AS PD"),
    ("access", " || slot->as != a->as", "", "AS identity"),
    ("access", "slot->state == APP_STATE_ABORT_PENDING ||", "", "abort pending"),
    ("access", "slot->state == APP_STATE_FAULT_PENDING", "0", "fault pending"),
    ("access", "caller_frame.valid && redir_live(a)", "redir_live(a)", "inactive frame"),
    ("access", "*previous = caller_frame;", "*previous = (CallerAccessFrame){0};", "nested save"),
    ("access", "caller_frame = *previous;", "(void)previous;", "normal restore"),
    ("access", "if (ok) *out = *a;", "*out = *a;", "rejected output"),
    ("access", "origin != CALLER_USER && origin != CALLER_TRUSTED", "0", "unknown origin"),
    ("dispatch", "caller_access_enter(&prev_caller, CALLER_USER)", "caller_access_enter(&prev_caller, CALLER_TRUSTED)", "dispatcher origin"),
    ("dispatch", "caller_access_leave(&prev_caller);", "(void)prev_caller;", "dispatcher leave"),
    ("dispatch", "ring3_in_syscall = prev_in_syscall;", "ring3_in_syscall = 0; (void)prev_in_syscall;", "nested guard"),
]


def sources():
    s = (ROOT / "exec/exec.c").read_text()
    a = s.index("void __cdecl ring3_syscall_dispatch(u32 *frame)")
    b = s.index("\n}", a) + 2
    return {"access": (ROOT / "exec/redir_access.c").read_text(),
            "dispatch": s[a:b],
            "host": (ROOT / "tools/tests/caller_access_host.c").read_text()}


def run(src, quiet=False):
    with tempfile.TemporaryDirectory(prefix="os32-d1-") as folder:
        tmp = pathlib.Path(folder)
        (tmp / "exec").mkdir()
        (tmp / "tools/tests").mkdir(parents=True)
        (tmp / "exec/redir_access.c").write_text(src["access"])
        host = tmp / "tools/tests/caller_access_host.c"
        host.write_text(src["host"])
        (host.parent / "dispatcher.inc").write_text(src["dispatch"])
        exe = tmp / "caller"
        result = subprocess.run([
            "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-Wno-int-to-pointer-cast", "-Wno-pointer-to-int-cast", "-D__cdecl=",
            *["-I" + str(ROOT / d) for d in ("include", "kernel", "exec", "fs", "lib", "sdk/include/os32")],
            str(host), "-o", str(exe),
        ], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("compile failure (not RED):\n" + result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
        if not quiet:
            print(result.stdout + result.stderr, end="")
        return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutate", action="store_true")
    args = parser.parse_args()
    original = sources()  # Read actual source once, not a whole tree per mutant.
    if run(original):
        return 1
    if args.mutate:
        def mutant(item):
            key, old, new, name = item
            src = original.copy()
            assert src[key].count(old) == 1, (name, src[key].count(old))
            src[key] = src[key].replace(old, new)
            return name, run(src, quiet=True) != 0
        results = list(run_ordered(mutant, MUTATIONS))
        for name, red in results:
            print(f"{'RED (runtime)' if red else 'SURVIVED'}: {name}")
        print(f"MUTATIONS {sum(red for _, red in results)}/{len(results)} runtime RED")
        return 0 if all(red for _, red in results) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
