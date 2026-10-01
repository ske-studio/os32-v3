"""T2d d0a: same VA / distinct backing with the real fs/fd_redirect.c.

Default is the desired contract (currently RED, rc=1). The check target uses
--expect-known-bug: accept only the exact MISSING/CHANGED fingerprint as XFAIL.
Compiler errors, signals, other failures and XPASS fail the check. In d0b remove
that flag from the recipe and extend the harness for the saved-AS copy boundary.
Linux mmap replaces CR3; this does not establish real guest paging correctness.
"""
import argparse
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expect-known-bug", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="os32-d0a-") as tmp:
        exe = pathlib.Path(tmp) / "d0a"
        subprocess.run([
            "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D__cdecl=",
            "-I" + str(ROOT / "include"),
            "-I" + str(ROOT / "sdk/include/os32"),
            str(ROOT / "tools/tests/fd_redirect_d0a_host.c"), "-o", str(exe),
        ], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    print(result.stdout, end="")
    print(result.stderr, end="")
    if args.expect_known_bug:
        fingerprint = ("d0a: same_as=OK\n"
                       "d0a: parent_buffer=MISSING\n"
                       "d0a: child_value=CHANGED\n")
        if result.returncode == 1 and result.stdout == fingerprint and not result.stderr:
            print("XFAIL: registered parent buffer resolved in child AS (d0b pending)")
            return 0
        print("FAIL: unexpected result (including XPASS); revisit d0b registration")
        return 1
    return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
