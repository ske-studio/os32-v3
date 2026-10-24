"""S0-D: hsync が /etc/settings.db* を切り詰めないことの字句判定を実物のソースで見る。

票:   docs/archive/settings/TASK_S0.md §2 (S0-D)、契約は S0_FOUNDATION.md §5 (D0)
記録: tools/tests/s0_tdd.md 節 D

hsync は HostDrv (C:\\os32) の中身を NHD の / へ O_CREAT|O_TRUNC で写す。HostDrv に
古い etc/settings.db が 1 つ残っているだけで、ゲストが書いた設定 DB が切り詰められる。
userland/system/hsync_protect.inc の純関数 (字句正規化 + 名前規則) をホストで走らせ、
同じソースが外部プログラムと同じ i386-elf-gcc / PROGRAM_FLAGS でも通ることを別に見る
([C1] GNU11)。実体規則は hsync.c の sys_stat と dev/inode の比較を字句判定で確認する。
比較を除いた固定入力が拒否されることも確認する。エミュレータ・実配備・make には一切触れない。
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CROSS_DIR = os.environ.get('CROSS_DIR', '/usr/local/cross')

HOST_FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter"]
# build/config.mk の PROGRAM_FLAGS (= USER_CFLAGS + インクルードパス) と同じ
TARGET_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2", "-Wall",
                "-D__OS32_USERLAND__"]
TARGET_INCLUDES = ["-I" + str(ROOT), "-I" + str(ROOT / "include"),
                   "-I" + str(ROOT / "sdk/include"),
                   "-I" + str(ROOT / "sdk/include/os32"),
                   "-I" + str(ROOT / "userland/lib"),
                   "-I" + os.path.join(CROSS_DIR, "i386-elf/include")]

HOST_SRC = ROOT / "tools/tests/hsync_protect_host.c"
GUEST_SRC = ROOT / "userland/system/hsync.c"

def check_inode_guard(source):
    # コメントに残った比較を誤って合格にしない。
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    match = re.search(r"static int is_same_as_protected\([^)]*\)\s*\{(.*?)^\}",
                      source, flags=re.S | re.M)
    assert match, "is_same_as_protected is missing"
    body = match.group(1)
    assert re.search(r"rc\s*=\s*api->sys_stat\(dst_path,\s*&here\)", body), \
        "destination sys_stat is missing"
    assert re.search(
        r"if\s*\(\s*here\.st_dev\s*==\s*g_prot_dev\[i\]\s*&&\s*"
        r"here\.st_ino\s*==\s*g_prot_ino\[i\]\s*\)\s*return\s+1\s*;", body), \
        "device/inode equality must protect the destination"


def test_inode_guard():
    source = GUEST_SRC.read_text()
    check_inode_guard(source)
    for comparison in ("here.st_ino == g_prot_ino[i]", "here.st_dev == g_prot_dev[i]"):
        assert comparison in source
        try:
            check_inode_guard(source.replace(comparison, "1"))
        except AssertionError:
            pass
        else:
            raise AssertionError("missing comparison was accepted: " + comparison)
    print("INODE guard + missing-comparison rejection PASS", flush=True)


if __name__ == "__main__":
    test_inode_guard()
    with tempfile.TemporaryDirectory(prefix="os32-hsync-protect-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "hsync-protect"
        subprocess.run(["gcc", *HOST_FLAGS,
                        "-I" + str(ROOT / "userland/system"),
                        str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST GNU11 -Werror COMPILE PASS", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=30).returncode
        # 実物と同じフラグで hsync.c ごとクロスコンパイルも通ること
        subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, *TARGET_INCLUDES,
                        "-c", str(GUEST_SRC), "-o", str(tmp / "hsync.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 COMPILE PASS", flush=True)
        print("EXIT hsync_protect_host=%d" % rc, flush=True)
        sys.exit(rc)
