"""B6: vfs_path_kind() が「読めなかったディレクトリ」をファイルと判定しない。

票:   docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md (H1) / Codex 実装レビュー 往復 3 の B6
記録: tools/tests/h1_tdd.md

tools/tests/vfs_kind_host.c が実物の fs/vfs.c をそのまま #include し、
境界 (kstring / kmalloc) だけを同義の C で置く。FS ドライバは合成した
VfsOps で、stat / list_dir / get_file_size の戻り値を 1 つずつ指定できる。

直す前の反例:
  stat が IO -> 「stat 未対応」とみなしてプローブへ -> list_dir が FULL ->
  get_file_size は**ディレクトリでも成功する** -> VFS_KIND_FILE。
  結果 `cd` が NOTDIR になり、sys_open のディレクトリ拒否をすり抜ける。

`stat` を持つドライバ (ext2 / FAT / ISO9660 / HostDrv の 4 つとも持つ) の
判定が変わらないことも同じ合成ドライバで押さえる。

**実物の fs/vfs_fd.c も同じ翻訳単位に取り込み、`vfs_open()` まで通す**
(票 H1 / 往復 4 の B7)。`vfs_path_kind` の戻り値までしか見ていなかったことが
B7 の検出漏れの原因だったので、種別の判定を**消費する側**まで試験する。

  python3 -B tools/tests/test_vfs_kind.py [--target]

--target を付けると i386-elf クロスコンパイラでも fs/vfs.c が -Werror で
通ることを確かめる。make・エミュレータ・実配備には一切触れない。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-Wno-sign-compare",
              "-Wdeclaration-after-statement", "-D__cdecl="]
HOST_INC = ["-I" + str(ROOT / p)
            for p in ("include", "fs", "lib", "kernel", "drivers",
                      "sdk/include/os32")]

TARGET_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-Wdeclaration-after-statement",
                # fs/vfs_fd.c は元から出る 2 件 (vfs_fstat の sizeof 比較と
                # vfs_sys_compat_shell_print の attr) なのでそこだけ外す
                "-Wno-sign-compare", "-Wno-unused-parameter",
                # arch/x86 + platform/pc98: include/io.h は契約だけで、実装は
                # 固定名 arch_io.h / platform_io.h を引く (順序 3)。
                "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
                "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
                "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet",
                "-Ifs", "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-vfs-kind-") as tmp:
        tmp = pathlib.Path(tmp)

        exe = tmp / "vfs-kind"
        subprocess.run(["gcc", *HOST_FLAGS, *HOST_INC,
                        str(ROOT / "tools/tests/vfs_kind_host.c"),
                        "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST GNU89 -Werror COMPILE PASS (real fs/vfs.c)", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT vfs_kind_host=%d" % rc, flush=True)

        if "--target" in sys.argv:
            for src in ("fs/vfs.c", "fs/vfs_fd.c"):
                subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", src,
                                "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                               cwd=ROOT, check=True)
                print("TARGET i386-elf -Werror COMPILE PASS (%s)" % src,
                      flush=True)

        sys.exit(rc)
