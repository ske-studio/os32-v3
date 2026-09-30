"""F2a only: compile actual FD source, never run Make or touch a filesystem backend."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["explicit_owner", "direct_close", "owned_close", "protect",
         "verified_close", "stale_reuse", "validation", "count_members",
         "quarantine", "capacity_preflight", "generation_exhaustion",
         "invalid_open", "generic_regression", "failed_open", "quarantine_capacity"]
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "fs", "drivers", "sdk/include/os32")]

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-f2a-") as tmp:
        exe = pathlib.Path(tmp) / "fd-host"
        command = ["gcc", *FLAGS, *INCLUDES,
                   str(ROOT / "tools/tests/vfs_fd_sqlite_host.c"), "-o", str(exe)]
        subprocess.run(command, cwd=ROOT, check=True)
        print("COMPILE GNU89 -Werror PASS", flush=True)
        if "--target" in sys.argv[1:]:
            target_command = [
                "i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
                "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                "-nostdlib", "-mno-red-zone", "-fcommon", "-O2", "-Wall",
                "-Werror", "-Wdeclaration-after-statement", "-D__KERNEL_BUILD__",
                *INCLUDES, "-c", str(ROOT / "fs/vfs_fd.c"),
                "-o", str(pathlib.Path(tmp) / "vfs_fd.o")]
            subprocess.run(target_command, cwd=ROOT, check=True)
            print("TARGET i386-elf GNU89 -Werror PASS", flush=True)
        failed = 0
        cases = [case for case in sys.argv[1:] if case != "--target"] or CASES
        for case in cases:
            rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
