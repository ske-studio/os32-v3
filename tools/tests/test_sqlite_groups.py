"""F2b scoped host/target compile; no Make, network, deployment or guest."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["registration", "register_retry", "capacity", "owned_open", "lifecycle",
         "busy_partial", "busy_zero", "orphan", "sticky_close", "scoped_open",
         "diagnostics", "null_open", "edge_opens", "stale_group", "repeated_lifetimes",
         "real_normal", "real_stale", "path_delete", "path_access", "path_fullpath",
         "uri_callback"]
CASES += ["callback_" + name for name in
          ("read", "write", "truncate", "sync", "size", "lock", "unlock",
           "reserved", "control", "sector", "device")]
INC = ["-I" + str(ROOT / p) for p in
       ("include", "fs", "drivers", "sdk/include/os32", "lib", "lib/sqlite3")]
CONFIG = str(ROOT / "lib/sqlite3/os32_sqlite_config.h")
if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-f2b-") as tmp:
        obj = str(pathlib.Path(tmp) / "sqlite.o")
        exe = str(pathlib.Path(tmp) / "groups")
        san = ["-fsanitize=address", "-fno-omit-frame-pointer"] if "--sanitize" in sys.argv else []
        subprocess.run(["gcc", "-std=gnu89", "-O0", *san, "-include", CONFIG,
                        "-c", str(ROOT / "lib/sqlite3/sqlite3.c"), "-o", obj], check=True)
        subprocess.run(["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                        "-Wdeclaration-after-statement", "-Wno-unused-parameter",
                        "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
                        "-Wno-missing-field-initializers", "-D__cdecl=",
                        *san, *INC, str(ROOT / "tools/tests/sqlite_groups_host.c"), obj,
                        "-o", exe], check=True)
        print("HOST GNU89 -Werror compile PASS (bundled SQLite)", flush=True)
        if "--target" in sys.argv:
            subprocess.run(["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
                            "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                            "-nostdlib", "-msoft-float", "-Os", "-Wall", "-Werror",
                            "-Wdeclaration-after-statement", "-D__KERNEL_BUILD__",
                            *INC, "-c", str(ROOT / "lib/sqlite3/os32_sqlite_vfs.c"),
                            "-o", str(pathlib.Path(tmp) / "vfs.o")], check=True)
            print("TARGET i386-elf GNU89 -Werror compile PASS", flush=True)
        cases = [x for x in sys.argv[1:] if x not in ("--target", "--sanitize")] or CASES
        failed = 0
        for case in cases:
            rc = subprocess.run([exe, case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
