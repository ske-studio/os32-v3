"""font の read lease (T7b-b1) — 実物の fs/vfs_fd.c をホストで。

設計: ~/os32-tmp/run/t7bb12/DESIGN.final.md §2-2・§5 (b1②④⑤⑦ の VFS 部)。
S1 は VFS の内部 API (vfs_open_font / validate / read_at / close_font、公開
経路の締め出し、vfs_close_owned、FD 予算) だけ。lease 表と KAPI の門
(kernel/font_source.c) は S2 で tools/tests/font_source_host.c に足す。

  python3 -B tools/tests/test_font_source.py            # ホスト
  python3 -B tools/tests/test_font_source.py --target   # + i386-elf の -Werror
  python3 -B tools/tests/test_font_source.py --mutate   # 否定側 (写しの上で変異)
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["open_fields", "open_reject", "read_at", "validate", "close_reuse",
         "stale_close", "public_reject", "close_owned", "pinned_busy",
         "generic_regression", "budget"]
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare", "-D__cdecl="]
INC_DIRS = ("include", "fs", "drivers", "sdk/include/os32")
HOST = "tools/tests/font_source_host.c"
SRC = "fs/vfs_fd.c"

# 否定側: 実物の 1 行を壊すと RED になることを見る (写しの上で。ソースは触らない)。
# (ファイル, 置き換え前 (ちょうど 1 か所), 置き換え後, 何を崩したか)
MUTATIONS = [
    (SRC, "    if (offset > f->size || len > f->size - offset) return VFS_ERR_INVAL;\n",
     "    if (offset > f->size) return VFS_ERR_INVAL;\n",
     "b1② len > size-offset の判定を外す"),
    (SRC, "    if (offset > f->size || len > f->size - offset) return VFS_ERR_INVAL;\n",
     "    if (len > f->size - offset) return VFS_ERR_INVAL;\n",
     "b1② offset > size の判定を外す (桁あふれ)"),
    (SRC, "    return fd_read_backend(f, offset, dst, len);\n",
     "    (void)fd_read_backend(f, offset, dst, len);\n    return (int)len;\n",
     "b1② backend の戻りを len に書き換える (短読を隠す)"),
    (SRC, "    if (f->generation != l->generation) return VFS_ERR_INVAL;\n", "",
     "b1④ generation の比較を外す"),
    (SRC, "    if (f->owner != l->owner) return VFS_ERR_INVAL;\n", "",
     "validate が owner を見ない"),
    (SRC, "    if (rc != VFS_OK && rc != VFS_ERR_STALE) return VFS_ERR_INVAL;\n",
     "    if (rc != VFS_OK) return VFS_ERR_INVAL;\n",
     "close_font が失効した FD を解放しない"),
    (SRC, "        if (f->lifetime == VFS_FD_FONT) { fd_release(f); continue; }\n", "",
     "b1⑤ close_owned の lifetime==FONT 条件を外す"),
    (SRC, "    open_files[fd].pinned = (flags & VFS_FONT_OPEN_PIN) ? 1 : 0;\n",
     "    open_files[fd].pinned = 0;\n", "b1⑦ pinned=(flags&PIN) → 0"),
    (SRC, "        if (open_files[fd].lifetime == VFS_FD_FONT) return;\n        fd_release(",
     "        fd_release(", "公開 vfs_close が FONT を閉じる"),
    (SRC, "    if (f->lifetime == VFS_FD_FONT) return VFS_ERR_INVAL; /* lease 経由だけ */\n"
          "    if (f->stale) return VFS_ERR_STALE;   /* fs_ctx より先に見る */\n",
     "    if (f->stale) return VFS_ERR_STALE;   /* fs_ctx より先に見る */\n",
     "公開 vfs_read_fd が FONT を断らない"),
    (SRC, "    if (f->lifetime == VFS_FD_FONT) return VFS_ERR_INVAL; /* 位置を持たない */\n", "",
     "公開 vfs_seek が FONT を断らない"),
    (SRC, "    if (!f->in_use || f->lifetime != VFS_FD_FONT) return VFS_ERR_INVAL;\n",
     "    if (!f->in_use) return VFS_ERR_INVAL;\n", "validate が lifetime を見ない"),
]


def includes(root):
    return ["-I" + str(root / p) for p in INC_DIRS]


def build(root, exe, quiet=False):
    p = subprocess.run(["gcc", *FLAGS, *includes(root), str(root / HOST), "-o", str(exe)],
                       cwd=root, capture_output=True, text=True)
    if p.returncode != 0 and not quiet:
        sys.stdout.write(p.stdout + p.stderr)
    return p.returncode


def run_cases(root, exe, quiet=False):
    failed = 0
    for case in CASES:
        p = subprocess.run([str(exe), case], cwd=root, capture_output=True, text=True)
        if not quiet:
            sys.stdout.write(p.stdout + p.stderr)
            print("EXIT %s=%d" % (case, p.returncode), flush=True)
        failed += p.returncode != 0
    if not quiet:
        print("SUMMARY %d/%d PASS" % (len(CASES) - failed, len(CASES)), flush=True)
    return failed


def target_compile(root, tmp):
    subprocess.run(["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
                    "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
                    "-mno-red-zone", "-fcommon", "-O2", "-Wall", "-Werror",
                    "-D__KERNEL_BUILD__", *includes(root), "-c", str(root / SRC),
                    "-o", str(tmp / "vfs_fd.o")], cwd=root, check=True)


def mutate():
    red = bad = 0
    for i, (rel, old, new, why) in enumerate(MUTATIONS, 1):
        with tempfile.TemporaryDirectory(prefix="os32-font-source-mut-") as tmp:
            copy = pathlib.Path(tmp)
            for d in INC_DIRS:
                shutil.copytree(ROOT / d, copy / d, dirs_exist_ok=True)
            (copy / HOST).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / HOST, copy / HOST)
            src = copy / rel
            text = src.read_text(encoding="utf-8")
            if text.count(old) != 1:
                print("MUTATION %d SKIP (pattern not found once): %s" % (i, why))
                return 1
            src.write_text(text.replace(old, new), encoding="utf-8")
            exe = copy / "fd-host"
            if build(copy, exe, quiet=True) != 0:
                # 組めない変異は RED に数えない
                print("MUTATION %d BUILD FAIL (not RED): %s" % (i, why), flush=True)
                bad += 1
                continue
            failed = run_cases(copy, exe, quiet=True)
            state = "RED" if failed else "GREEN (bad)"
            print("MUTATION %d %s: %s" % (i, state, why), flush=True)
            red += failed != 0
    print("MUTATIONS %d/%d RED" % (red, len(MUTATIONS)))
    return 0 if red == len(MUTATIONS) and not bad else 1


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-font-source-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "fd-host"
        rc = build(ROOT, exe)
        if rc:
            sys.exit(rc)
        print("COMPILE GNU11 -Werror PASS", flush=True)
        if "--target" in sys.argv[1:]:
            target_compile(ROOT, tmp)
            print("TARGET i386-elf GNU11 -Werror PASS", flush=True)
        if run_cases(ROOT, exe):
            sys.exit(1)
    sys.exit(mutate() if "--mutate" in sys.argv[1:] else 0)
