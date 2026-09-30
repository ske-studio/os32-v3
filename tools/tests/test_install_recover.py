"""S3-I: `install --recover-settings` / `--revert-settings` のホスト TDD。

実 `userland/system/install_recover.inc` + 実 SQLite + 実 os32 SQLite VFS +
実 FD 表 + 実 `kapi/kapi_db.c` を RAM のバックエンドに載せて回す。ホストの
ファイルシステム・sudo・mount・配備・エミュレータには一切触らない。
記録は tools/tests/s3_tdd.md 節 I。

  python3 -B tools/tests/test_install_recover.py [--target] [--sanitize] [case ...]
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

CASES = [
    "media",        # 1a 起動媒体 / 対象デバイス (受入 I1)
    "scan",         # 1b 三値 / UNKNOWN / inode 共有 / .new 残骸
    "gate",         # 1b 印の phase による門と印の往復
    "master",       # 1c マスタの検査と close 失敗
    "approve",      # 1d 承認しない (受入 I5)
    "happy",        # 1e/1f 正常系 (受入 I2 / I3 / I4)
    "backup_fail",  # 1e 退避の各失敗 (元の対は不変)
    "newfail",      # 1f .new のコピー / 検証 / close 失敗
    "switch",       # 1f journal 除去と rename の 2 つの中途半端 (受入 I6)
    "record",       # 1f(7) sync / reopen / close の記録
    "revert",       # 1g revert の正常系 (受入 I7)
    "revert_fail",  # 1g revert の途中失敗 (phase は reverting のまま)
    "chain",        # 1h 連鎖
    "pure",         # 印の書式・コピー・バイト比較の純関数
]

INC = ["-I" + str(ROOT / p) for p in
       ("include", "fs", "drivers", "sdk/include", "sdk/include/os32",
        "lib", "lib/sqlite3", "userland/lib")]
CONFIG = str(ROOT / "lib/sqlite3/os32_sqlite_config.h")

SHIMS = {
    "memmap.h": "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n",
    "exec.h": "int ring3_user_range_ok(u32 p, u32 len);\n",
}


def build(tmp, sanitize):
    for name, text in SHIMS.items():
        (tmp / name).write_text(text)
    obj = str(tmp / "sqlite.o")
    exe = str(tmp / "recoverhost")
    san = (["-fsanitize=address", "-fno-omit-frame-pointer"]
           if sanitize else [])
    subprocess.run(["gcc", "-std=gnu89", "-O0", *san, "-include", CONFIG,
                    "-c", str(ROOT / "lib/sqlite3/sqlite3.c"), "-o", obj],
                   check=True)
    subprocess.run(["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                    "-Wdeclaration-after-statement", "-Wno-unused-parameter",
                    "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
                    "-Wno-missing-field-initializers", "-D__cdecl=",
                    "-D__OS32_USERLAND__",
                    *san, "-I" + str(tmp), *INC,
                    str(ROOT / "tools/tests/install_recover_host.c"), obj,
                    "-o", exe], check=True)
    print("HOST GNU89 -Werror compile PASS "
          "(real install_recover.inc + real kapi_db.c + bundled SQLite)",
          flush=True)
    return exe


def target_compile(tmp):
    """i386-elf でも同じソースが通ること (票 §3)。

    `-Wno-unused-function` は install.c に元からある死んだ静的関数
    (`str_endswith_ci`) の分。回復モードのコード自体はホスト側のビルドが
    `-Wall -Wextra -Werror` (抑制なし) で見ている。
    """
    flags = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
             "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
             "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             "-Wdeclaration-after-statement", "-D__OS32_USERLAND__",
             "-I.", "-Iinclude", "-Isdk/include", "-Isdk/include/os32",
             "-Iuserland/lib", "-I/usr/local/cross/i386-elf/include"]
    subprocess.run(["i386-elf-gcc", *flags, "-c", "userland/system/install.c",
                    "-o", str(tmp / "install.o")], check=True, cwd=ROOT)
    print("TARGET i386-elf GNU89 compile PASS (install.c + install_recover.inc)",
          flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-s3i-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = build(tmp, "--sanitize" in sys.argv)
        if "--target" in sys.argv:
            target_compile(tmp)
        cases = [x for x in sys.argv[1:] if not x.startswith("--")] or CASES
        failed = 0
        for case in cases:
            rc = subprocess.run([exe, case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
