"""S3I2-I: `install` (無印) の通常インストール経路のホスト TDD。

実 `userland/system/install.c` の main を `#define main` で取り込み、KAPI の
贋物 (FAT のように大文字を返す `sys_ls`、96 B を書く `ide_identify`、read の
負 / short write / mkdir / sys_ls / format / mount / sync / ide_write の失敗
注入) の上で回す。ホストのファイルシステム・配備・エミュレータには触らない。
記録は tools/tests/s3i2_tdd.md 節 I。

  python3 -B tools/tests/test_install_fresh.py [--target] [--sanitize] [case ...]

`--target` は i386-elf-gcc で install.c を単体コンパイル ([C1] C89 / -Werror)。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

CASES = [
    "nokernel",   # (1) /kernel.bin を読まない / (6b) 生カーネルの書込みが無い
    "vmkernel",   # (3) /hd0/boot/vmkernel.lz4 の長さ一致
    "lower",      # (4) 宛先が小文字 / (5) profile は写らない
    "precheck",   # (2) Phase 0 の欠損・空で 1 バイトも書かない
    "decline",    # 承認しない (何も書かず 0)
    "boot_fail",  # (6) IPL / PT / ローダ / format / mount の失敗で 1
    "copy_fail",  # (6) read の負 / short write / mkdir / sys_ls の負 (頭・途中・末尾)
    "mkdir_init", # B1 初期ディレクトリ作成の失敗も終了 1
    "bounds",     # 列挙 64 / 65 件、再帰の深さ 4 / 5
    "srcname",    # ソース名の綴り保持 / 正常 EOF での長さ不一致
    "sync_fail",  # (6) vfs_sync の失敗で 1
    "idetype",    # (7) IdeInfo は 96 B の実型
    # 段 2 (票 TASK_HDD_INSTALL): 共有部 inst_hdd.c / inst_disk.c と組んで回す
    "geom817",    # 8/17: 区画表の開始 1632・IPL [8]/[9] = 8/17・format の範囲・順序
    "geom1663",   # 16/63 (実機): 開始 2016・IPL 16/63・256MiB を 1008 で切り下げ
    "modes",      # 空・再作成 (標準 / 旧配置 8/17)・未知・2 項目・開始違い・55AA・壊れ・旧配置 16/63
    "preflight",  # 大きさ (IPL / ローダ / vmkernel)・容量・幾何・ルート・マウントで 1 セクタも書かない
    "incomplete", # format / 区画表の読み戻し / マウント / ローダ / IPL / コピー / sync の失敗は INCOMPLETE
    "rerun",      # 途中で止まった hd0 を入れ直せる
]

INC = ["-I" + str(ROOT / p) for p in
       ("include", "sdk/include", "sdk/include/os32", "userland/lib")] + ["-I" + str(ROOT)]
# 段 2: install.c が繋ぐ共有部 (build/programs.mk の INST_OBJ と同じ顔ぶれ)。
# 写さずに実物を別の翻訳単位として組む。
SHARED = ["userland/system/inst_hdd.c", "userland/system/inst_disk.c",
          "userland/shell/hdprep_plan.c", "drivers/pc98pt.c", "fs/ext2_layout.c"]


def build(tmp, sanitize, extra=(), root=ROOT):
    exe = str(tmp / "installfresh")
    san = (["-fsanitize=address", "-fno-omit-frame-pointer"]
           if sanitize else [])
    subprocess.run(["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                    "-Wdeclaration-after-statement", "-Wno-unused-function",
                    "-Wno-pointer-to-int-cast", "-D__cdecl=",
                    "-D__OS32_USERLAND__", "-O0",
                    *san, *extra, "-I" + str(root / "userland/system"),
                    "-I" + str(root), *INC,
                    str(ROOT / "tools/tests/install_fresh_host.c"),
                    *[str(root / f) for f in SHARED],
                    "-o", exe], check=True)
    print("HOST GNU89 -Werror compile PASS (real install.c normal path)",
          flush=True)
    return exe


TARGET_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2", "-Wall", "-Wextra",
                "-Werror", "-Wno-unused-function",
                "-Wdeclaration-after-statement", "-D__OS32_USERLAND__",
                "-I.", "-Iinclude", "-Isdk/include", "-Isdk/include/os32",
                "-Iuserland/lib", "-I/usr/local/cross/i386-elf/include"]

# ホストは LP64 (u32 = unsigned long = 8 B) なので 96 / 92 という数そのものは
# 再現できない。実機と同じ i386 のコンパイル時表明で固定する (票 §1 の (7))。
LAYOUT_ASSERT = """#define main install_main
#include "userland/system/install.c"
#undef main
typedef char ide_info_is_96[(sizeof(IdeInfo) == 96) ? 1 : -1];
typedef char ide_info_tail_at_92[
    (__builtin_offsetof(IdeInfo, phys_sector_size) == 92) ? 1 : -1];
"""


def target_compile(tmp):
    """i386-elf でも同じソースが通ること ([C1] C89、-Werror)。

    `-Wno-unused-function` は install.c に元からある死んだ静的関数
    (`str_endswith_ci`) の分。あわせて IdeInfo が実型 (96 B、末尾 92) で
    あることをコンパイル時に表明する。
    """
    subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c",
                    "userland/system/install.c",
                    "-o", str(tmp / "install.o")], check=True, cwd=ROOT)
    print("TARGET i386-elf GNU89 compile PASS (install.c)", flush=True)
    src = tmp / "ide_layout_assert.c"
    src.write_text(LAYOUT_ASSERT)
    subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", str(src),
                    "-o", str(tmp / "layout.o")], check=True, cwd=ROOT)
    print("TARGET IdeInfo layout PASS (sizeof == 96, phys_sector_size @ 92)",
          flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-s3i2-") as tmp:
        tmp = pathlib.Path(tmp)
        # RED の記録用: -DIdeInfo=IdeInfoTemp で修正前の型に当てて回せる
        extra = [a[2:] for a in sys.argv[1:] if a.startswith("-D")]
        extra = ["-D" + a for a in extra]
        exe = build(tmp, "--sanitize" in sys.argv, extra)
        if "--target" in sys.argv:
            target_compile(tmp)
        cases = [x for x in sys.argv[1:]
                 if not x.startswith("--") and not x.startswith("-D")] or CASES
        failed = 0
        for case in cases:
            rc = subprocess.run([exe, case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
