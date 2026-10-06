"""Host-test the real DB wrapper; SQLite fault injection, no OS/VFS access."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class DbOwnedTests(unittest.TestCase):
    def test_host_cases(self):
        with tempfile.TemporaryDirectory(prefix="os32-db-f1-") as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "memmap.h").write_text(
                "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n#define MEM_SHM_DB_OFFSET 0UL\n#define MEM_SHM_DB_BASE (MEM_SHM_BASE + MEM_SHM_DB_OFFSET)\n")
            (tmp / "kstring.h").write_text(
                "#include <string.h>\n#define kstrncpy(d,s,n) strncpy(d,s,n)\n"
                "char *host_strlcat(char *d, const char *s, unsigned long n);\n"
                "#define kstrncat(d,s,n) host_strlcat(d,s,n)\n"
                "#define kstrlen(s) ((u32)strlen(s))\n"
                "#define kstrcmp(a,b) strcmp(a,b)\n"
                "#define kstrncmp(a,b,n) strncmp(a,b,n)\n"
                "#define kmemcpy(d,s,n) memcpy(d,s,n)\n")
            # v50: kapi_db.c は exec 側のポインタ検証と vfs_stat を使う。
            # どちらもこの票 (F1) の範囲外なので最小の模型で置く。
            (tmp / "exec.h").write_text(
                "int ring3_user_range_ok(u32 p, u32 len);\n")
            (tmp / "kprintf.h").write_text("int kprintf(int color, const char *fmt, ...);\n")
            exe = tmp / "db-owned"
            command = [
                "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-D__cdecl=", "-I" + str(ROOT / "tools/tests"),
                "-I" + str(tmp), "-I" + str(ROOT / "sdk/include/os32"),
                "-I" + str(ROOT / "include"), "-I" + str(ROOT / "fs"),
                "-I" + str(ROOT / "lib/sqlite3"), "-I" + str(ROOT / "kapi"),
                str(ROOT / "tools/tests/kapi_db_owned_host.c"), "-o", str(exe)
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, cwd=ROOT)
            if os.environ.get("MUTATE") == "1":
                fixture_text = (ROOT / "tools/tests/kapi_db_owned_host.c").read_text()
                paths = ['kapi/kapi_db.c']
                for path in paths:
                    source = (ROOT / path).read_text()
                    # Mutate each public guard independently: omitted, inverted,
                    # or incorrectly applied to TRUSTED calls.
                    guards = []
                    offset = 0
                    for line in source.splitlines(keepends=True):
                        if "ring3_call_from_user() &&" in line:
                            guards.append((offset, line.rstrip("\n")))
                        offset += len(line)
                    for index, (begin, guard) in enumerate(guards):
                        for label, new in (
                            ("removed", guard.replace("ring3_call_from_user() &&", "0 &&")),
                            ("inverted", guard.replace("!= res_owner_get()", "== res_owner_get()")),
                            ("trusted-denied", guard.replace("ring3_call_from_user() &&", "")),
                        ):
                            mutated = tmp / pathlib.Path(path).name
                            # Replace only this occurrence (SHM/DB guards repeat).
                            offset = begin + len(guard)
                            mutated.write_text(source[:begin] + new + source[offset:])
                            host = fixture_text
                            for included in paths:
                                target = mutated if included == path else ROOT / included
                                host = host.replace("../../" + included, str(target))
                            host = host.replace('"../../', '"' + str(ROOT) + '/')
                            host_path = tmp / "mutant_host.c"
                            host_path.write_text(host)
                            mutant_command = [str(host_path) if arg.endswith("/kapi_db_owned_host.c") else arg
                                              for arg in command]
                            compiled = subprocess.run(mutant_command, cwd=ROOT,
                                                      capture_output=True, text=True)
                            self.assertEqual(compiled.returncode, 0, compiled.stderr)
                            result = subprocess.run([str(exe)], cwd=ROOT,
                                                    capture_output=True, text=True)
                            self.assertNotEqual(result.returncode, 0,
                                                f"surviving {path}:{index} {label}")
                            self.assertTrue("FAIL" in result.stdout or "Assertion" in result.stderr,
                                            result.stdout + result.stderr)
                            print(f"RED {path}:{index} {label}")



if __name__ == "__main__":
    unittest.main()
