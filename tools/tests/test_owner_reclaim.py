"""K5b-K: owner-ID reclamation closes on one app, using the real sources.

Ticket: docs/archive/gui_v13/TASK_K5B_kernel.md (host tests, 2nd item)
Design: TASK_K5_multiapp.md D3 (the `*_owned(id)` inventory, P3/P5)
Log:    tools/tests/k5b_kernel_tdd.md

Compiles the shipped fs/fd_redirect.c, fs/pipe_buffer.c and kernel/shm.c
(included verbatim by owner_reclaim_host.c) with a minimal kernel environment,
then checks that folding ID 2 frees only ID 2's redirect / pipe / SHM.

Same harness shape as test_kapi_db_owned.py: tiny shim headers in a temp dir,
one host binary, no Make / emulator / VFS.
"""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class OwnerReclaimTests(unittest.TestCase):
    def test_host_cases(self):
        with tempfile.TemporaryDirectory(prefix="os32-owner-reclaim-") as tmp:
            tmp = pathlib.Path(tmp)
            # kernel/shm.c derives every address from MEM_SHM_BASE and the
            # STATIC_ASSERTs need a constant expression, so keep the real
            # numeric shape. Nothing is dereferenced: kmemset is a recording
            # no-op and paging_map_range is stubbed in the host file.
            db_defines = "\n".join(line for line in
                (ROOT / "include/memmap.h").read_text().splitlines()
                if line.startswith("#define MEM_SHM_DB_")) + "\n"
            lease_defines = "\n".join(line for line in
                (ROOT / "include/memmap.h").read_text().splitlines()
                if line.startswith("#define MEM_LEASE_")) + "\n"
            (tmp / "memmap.h").write_text(
                "#ifndef OS32_TEST_MEMMAP_H\n"
                "#define OS32_TEST_MEMMAP_H\n"
                "#define MEM_SHM_BASE       0x00180000U\n"
                # 2026-09-17 決裁 D1 で 16 → 14 ブロック、GUI 予約は末尾 4 個。
                # 値の写しなので tools/gen_memmap.py --check が実物と照合する。
                "#define MEM_SHM_SIZE       0x038000U\n"   # 224KB = 16KB x 14
                "#define MEM_SHM_GUI_SIZE   0x10000U\n"
                "#define MEM_SHM_GUI_OFFSET (MEM_SHM_SIZE - MEM_SHM_GUI_SIZE)\n"
                "#define MEM_SHM_GUI_BASE   (MEM_SHM_BASE + MEM_SHM_GUI_OFFSET)\n"
                "#define MEM_SHM_GUARD_LO   (MEM_SHM_BASE - 0x1000U)\n"
                "#define MEM_SHM_GUARD_HI   (MEM_SHM_BASE + MEM_SHM_SIZE)\n"
                "#define GUI_SLOT_SIZE      0x4000U\n"
                "#define GUI_SLOT_MAX       4\n"
                # kernel/paging.h (included by shm.c from its own directory,
                # so the real header always wins) needs this one constant.
                "#define MEM_APP_BAND_MAX_PDES 2UL\n"
                + db_defines + lease_defines + "#endif\n")
            (tmp / "kstring.h").write_text(
                "#include <string.h>\n"
                "void test_shm_memset(void *d, int c, unsigned long n);\n"
                "#define kmemset(d,c,n) test_shm_memset((d),(c),(n))\n"
                "#define kmemcpy(d,s,n) memcpy((d),(s),(n))\n")
            (tmp / "kmalloc.h").write_text(
                "#include \"types.h\"\nvoid *kmalloc(u32 size);\nvoid kfree(void *p);\n")
            exe = tmp / "owner-reclaim"
            command = [
                "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", 
                "-D__cdecl=", "-I" + str(ROOT / "tools/tests"),
                "-I" + str(tmp),
                "-I" + str(ROOT / "sdk/include/os32"),
                "-I" + str(ROOT / "include"),
                "-I" + str(ROOT / "fs"),
                "-I" + str(ROOT / "kernel"),
                str(ROOT / "tools/tests/owner_reclaim_host.c"), "-o", str(exe),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, cwd=ROOT)
            if os.environ.get("MUTATE") == "1":
                fixture_text = (ROOT / "tools/tests/owner_reclaim_host.c").read_text()
                paths = ['fs/pipe_buffer.c', 'kernel/shm.c']
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
                            mutant_command = [str(host_path) if arg.endswith("/owner_reclaim_host.c") else arg
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
