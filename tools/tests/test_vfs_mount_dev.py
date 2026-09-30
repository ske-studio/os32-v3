"""ext2 スーパーブロック巻き戻し (2026-09-10) の回帰試験。

実物の fs/vfs.c と fs/ext2_vfs.c をそのまま取り込み、ext2 本体と kmalloc だけを
差し替えて mount 経路を動かす。実デバイス・実イメージには一切触れない。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["encode", "ext2_rejects_non_hd", "boot_sequence_has_one_ext2",
         "duplicate_device_refused", "stat_dev_identifies_mount",
         "stat_dev_on_synth_root"]
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "fs", "lib", "kernel", "drivers", "sdk/include/os32")]

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-vfsmount-") as tmp:
        exe = pathlib.Path(tmp) / "vfs-mount-host"
        subprocess.run(["gcc", *FLAGS, *INCLUDES,
                        str(ROOT / "tools/tests/vfs_mount_dev_host.c"),
                        "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("COMPILE GNU89 -Werror PASS", flush=True)
        failed = 0
        cases = sys.argv[1:] or CASES
        for case in cases:
            rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
