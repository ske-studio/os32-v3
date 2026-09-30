"""票 TASK_EXT2_EMPTY_NAME: 長さ 0 の名前を ext2 の媒体に載せない。

票:   docs/archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md
記録: tools/tests/ext2_empty_name_tdd.md

tools/tests/ext2_empty_name_host.c が実物の fs/ext2_*.c / vfs.c / vfs_fd.c と
userland/lib/rt/pkg.c を取り込み、RAM 上の 8MB の ext2 で

  vfs    … /hd0 に載せて、マウント点・末尾 "/"・"//"・"." を VFS 経由で渡す
  root   … "/" に載せて mkdir "/" ほか
  ext2   … ext2 の入口を直に (空・"."・".."・"/" 入り・256 文字の名前)
  synth  … 合成ドライバで、VFS の断りがドライバまで届かないこと
  legacy … 以前の版が作った名前の無い項目が在る媒体で、それを "" で掴まないこと
  cdinst … cdinst と同じ mkdir の並び + 実物の pkg 展開 (PKG は実物の
            tools/mkpkg.py でここで作る。無圧縮 1 本 + LZSS 1 本)

を回し、どの段も最後に**全ディレクトリの生の項目**を検査して像を書き出す。
像はここで本物の `e2fsck -fn` に当て、**終了コード 0 (clean) 以外は失敗**。
e2fsck が無い環境では `E2FSCK SKIP` と明示して自前の検査だけで通す ([V4])。

  python3 -B tools/tests/test_ext2_empty_name.py [--target] [--mutants] [case]

--target  触った fs/*.c を i386-elf-gcc で -Werror 単体コンパイル ([C1])。
--mutants fs/ の**写し**に変異を 1 つずつ当てて組み直し、どれも落ちることを見る。
          実物のソースは書き換えないので、並列の check-par に置いてよい。
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
              "-fno-stack-protector", "-nostdlib", "-static", "-O1",
              "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-Wno-sign-compare",
              "-D__cdecl="]
SRC = ROOT / "tools/tests/ext2_empty_name_host.c"
TARGET_SRCS = ["fs/ext2_dir.c", "fs/ext2_file.c", "fs/ext2_vfs.c", "fs/vfs.c"]
TARGET_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-Wno-sign-compare", "-Wno-unused-parameter",
                "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
                "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
                "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet",
                "-Ifs", "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]

# 変異: (名前, ファイル, 置き換え前, 置き換え後)。どれも**落ちなければならない**。
MUTANTS = [
    ("vfs_mkdir がマウント点を断らない", "vfs.c",
     "if (vfs_rel_is_root(rel_path)) return VFS_ERR_EXIST;   /* mkdir */",
     "/* mutated */"),
    ("ext2_mkdir が名前を検査しない", "ext2_dir.c",
     "ret = ext2_name_check(name);   /* mkdir */",
     "ret = 0;"),
    ("ext2_create が名前を検査しない", "ext2_file.c",
     "ret = ext2_name_check(name);   /* create */",
     "ret = 0;"),
    ("ext2_add_entry が名前を検査しない", "ext2_dir.c",
     "ret = ext2_name_check(name);   /* add_entry */",
     "ret = 0;"),
    ("rename の新しい名前を検査しない", "ext2_dir.c",
     "ret = ext2_name_check(new_name);   /* rename */",
     "ret = 0;"),
    ("rmdir の名前を検査しない", "ext2_dir.c",
     "ret = ext2_name_check(name);   /* rmdir */",
     "ret = 0;"),
    ("unlink の名前を検査しない", "ext2_file.c",
     "ret = ext2_name_check(name);   /* unlink */",
     "ret = 0;"),
    ("長さの上限を見ない", "ext2_dir.c",
     "if (n > EXT2_NAME_LEN) return EXT2_ERR_INVAL;",
     "/* mutated */"),
    ("\"..\" を通す", "ext2_dir.c",
     "if (n == 2 && name[0] == '.' && name[1] == '.') return EXT2_ERR_INVAL;",
     "/* mutated */"),
    ("\"/\" 入りを通す", "ext2_dir.c",
     "if (name[i] == '/') return EXT2_ERR_INVAL;",
     "/* mutated */"),
    ("ext2_vfs_mkdir がルートを EXIST にしない", "ext2_vfs.c",
     "if (ext2_path_is_root(path)) return VFS_ERR_EXIST;",
     "if (0 && ext2_path_is_root(path)) return VFS_ERR_EXIST;"),
    ("vfs_write がマウント点を断らない", "vfs.c",
     "if (vfs_rel_is_root(rel_path)) return VFS_ERR_ISDIR;\n    return ops->write_file",
     "return ops->write_file"),
    ("vfs_rmdir がマウント点を断らない", "vfs.c",
     "if (vfs_rel_is_root(rel_path)) return VFS_ERR_INVAL;   /* マウント中のルート */",
     "/* mutated */"),
    ("find_entry が長さ 0 の名前で探す", "ext2_dir.c",
     "if (name_len == 0 || name_len > EXT2_NAME_LEN) return EXT2_ERR_NOTFOUND;\n\n    ret = ext2_read_inode(ctx, dir_ino, &inode);",
     "\n    ret = ext2_read_inode(ctx, dir_ino, &inode);"),
]


def find_e2fsck():
    for cand in ("/usr/sbin/e2fsck", "/sbin/e2fsck"):
        if os.access(cand, os.X_OK):
            return cand
    return shutil.which("e2fsck")


def make_pkgs(tmp):
    """実物の mkpkg.py で PKG を 2 本作る (無圧縮 = MINIMAL 相当、LZSS = NORMAL 相当)"""
    files = tmp / "files"
    files.mkdir()
    blobs = {"vmk": 3000, "uni": 1500, "db": 700, "more": 1200, "cdi": 900,
             "keep": 11, "man": 400, "x": 2100}
    for name, size in blobs.items():
        (files / name).write_bytes(bytes((i * 7 + len(name)) & 0xFF for i in range(size)))
    p0 = tmp / "P0.PKG"
    p1 = tmp / "P1.PKG"
    mk = [sys.executable, "-B", str(ROOT / "tools/mkpkg.py"), "--base", str(ROOT)]
    subprocess.run(mk + ["--name", "minimal", "-o", str(p0),
                         f"/boot/vmkernel.lz4={files / 'vmk'}",
                         f"/sys/unicode.bin={files / 'uni'}",
                         f"/etc/settings.db={files / 'db'}",
                         f"/bin/more.bin={files / 'more'}",
                         f"/sbin/cdinst.bin={files / 'cdi'}"],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(mk + ["--name", "normal", "--lzss", "-o", str(p1),
                         f"/db/keep.txt={files / 'keep'}",
                         f"/usr/man/ls.1={files / 'man'}",
                         f"/bin/x.bin={files / 'x'}"],
                   check=True, stdout=subprocess.DEVNULL)
    return p0, p1


def build(tmp, fsdir, exe_name="ename"):
    inc = ["-I" + str(ROOT / "tools/tests/mtar_freestanding"), "-I" + str(fsdir)]
    inc += ["-I" + str(ROOT / p) for p in
            ("include", "lib", "kernel", "drivers", "sdk/include/os32",
             "userland/lib")]
    exe = tmp / exe_name
    res = subprocess.run(["gcc", *HOST_FLAGS, *inc, str(SRC), "-o", str(exe)],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.returncode != 0:
        return None, res.stdout.decode("utf-8", "replace")
    return exe, ""


def run(exe, imgdir, pkgs, case=None, quiet=False, e2fsck=None):
    """戻り値 (ok, 出力)。e2fsck が非 0 なら ok = False"""
    for p in imgdir.glob("*.img"):
        p.unlink()
    argv = [str(exe), str(imgdir), str(pkgs[0]), str(pkgs[1])]
    if case:
        argv.append(case)
    res = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=120)
    out = res.stdout.decode("utf-8", "replace")
    ok = res.returncode == 0
    lines = []
    for line in out.splitlines():
        if line.startswith("@@IMG "):
            path = line[6:].strip()
            if e2fsck is None:
                lines.append(f"  E2FSCK SKIP {os.path.basename(path)}")
                continue
            fr = subprocess.run([e2fsck, "-fn", path], stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=120)
            name = os.path.basename(path)
            if fr.returncode == 0:
                lines.append(f"  e2fsck -fn {name}: clean")
            else:
                ok = False
                lines.append(f"  e2fsck -fn {name}: rc={fr.returncode} FAIL")
                for l in fr.stdout.decode("utf-8", "replace").splitlines():
                    if l.startswith("e2fsck ") or l.startswith("Pass "):
                        continue
                    lines.append("    | " + l)
        else:
            lines.append(line)
    text = "\n".join(lines)
    if not quiet:
        print(text)
    return ok, text


def target_compile(tmp):
    cc = shutil.which("i386-elf-gcc")
    if not cc:
        print("TARGET SKIP (i386-elf-gcc not in PATH)")
        return True
    for src in TARGET_SRCS:
        subprocess.run([cc, *TARGET_FLAGS, "-c", src,
                        "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                       cwd=ROOT, check=True)
    print("TARGET i386-elf -Werror compile PASS (%s)" % ", ".join(TARGET_SRCS))
    return True


def mutants(tmp, pkgs, e2fsck):
    survivors = []
    for i, (name, fname, before, after) in enumerate(MUTANTS):
        mdir = tmp / f"mut{i}"
        shutil.copytree(ROOT / "fs", mdir)
        path = mdir / fname
        text = path.read_text(encoding="utf-8")
        if text.count(before) != 1:
            print(f"MUTANT {i} ({name}): 置き換え元が {text.count(before)} 件 — 変異表が古い")
            survivors.append(name)
            continue
        path.write_text(text.replace(before, after), encoding="utf-8")
        exe, err = build(tmp, mdir, f"ename_m{i}")
        if exe is None:
            print(f"MUTANT {i} ({name}): build failed (killed by compiler?)\n{err}")
            survivors.append(name)
            continue
        imgdir = tmp / f"img_m{i}"
        imgdir.mkdir()
        ok, _ = run(exe, imgdir, pkgs, quiet=True, e2fsck=e2fsck)
        print(f"MUTANT {i} ({name}): {'SURVIVED' if ok else 'killed'}")
        if ok:
            survivors.append(name)
    return survivors


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    case = args[0] if args else None
    e2fsck = find_e2fsck()
    with tempfile.TemporaryDirectory(prefix="os32-ext2-ename-") as t:
        tmp = pathlib.Path(t)
        pkgs = make_pkgs(tmp)
        exe, err = build(tmp, ROOT / "fs")
        if exe is None:
            print(err)
            print("BUILD FAIL")
            return 1
        print("HOST GNU11 -m32 -Werror compile PASS (real ext2 + vfs + rt/pkg.c)")
        imgdir = tmp / "img"
        imgdir.mkdir()
        ok, _ = run(exe, imgdir, pkgs, case=case, e2fsck=e2fsck)
        if e2fsck is None:
            print("E2FSCK SKIP (e2fsck not found; media scan only)")
        if not ok:
            print("ext2_empty_name: FAIL")
            return 1
        if "--target" in sys.argv:
            target_compile(tmp)
        if "--mutants" in sys.argv:
            surv = mutants(tmp, pkgs, e2fsck)
            if surv:
                print("MUTANTS SURVIVED: " + "; ".join(surv))
                return 1
            print(f"MUTANTS all {len(MUTANTS)} killed")
        print("ext2_empty_name: PASS")
        return 0


if __name__ == "__main__":
    sys.exit(main())
