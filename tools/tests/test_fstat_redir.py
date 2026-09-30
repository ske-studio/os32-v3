"""F4 / F5: `fstat` がリダイレクトに従い、`isatty` と食い違わない。

票:   docs/archive/test/TASK_FSTAT_REDIR.md の受入 F4 / F5
記録: tools/tests/fstat_redir_tdd.md

tools/tests/fstat_redir_host.c が実物の fs/vfs.c + fs/vfs_fd.c +
**fs/fd_redirect.c** をそのまま #include し、境界 (kstring / kmalloc /
コンソール / キーボード) だけを同義の C で置く。リダイレクトは贋物を置かない
— この票の主題そのものなので実物を通す。

押さえる規則:
  F4  リダイレクトの有無で `st_mode` が変わる。ファイルへ向いているなら
      **その実体** (種別・大きさ・時刻・inode・st_dev) を答える。
  F5  **`fstat` が S_IFCHR ⇔ `isatty` が 1**。状態 (コンソール / ファイル /
      パイプ) × fd 0/1/2 の総当たり。F4 だけだと「片方だけ直した版」が
      通ってしまう。

  python3 -B tools/tests/test_fstat_redir.py [--target] [--mutate]

--target は実機と同じ i386-elf クロスコンパイラでも fs/vfs_fd.c /
fs/fd_redirect.c が -Werror で通ることの確認 ([C1] GNU11)。

--mutate は**否定側**。票 §3-1 の根は「判定が 2 か所にあった」ことなので、
(1) fstat だけリダイレクトを見ない版 (2026-09-17 の不具合そのもの)、
(2) isatty だけリダイレクトを見ない版 (**片方だけ直した形**)、
(3) パイプを S_IFCHR と答える版、(4) ファイルの実体ではなく作り話を返す版、
の 4 つで確かに落ちることを見る。

静的には「判定の管理元が 1 か所」([C4]) を突き合わせる — `vfs_isatty` と
`vfs_fstat` がどちらも `fd_redirect_ifmt()` から引き、`fd_is_redirected()` も
そこから導いていること。

make・エミュレータ・実配備には一切触れない。
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-Wno-sign-compare",
              "-D__cdecl="]
HOST_INC = ["-I" + str(ROOT / p)
            for p in ("include", "fs", "lib", "kernel", "drivers",
                      "sdk/include/os32")]

TARGET_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-Wno-sign-compare", "-Wno-unused-parameter",
                "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
                "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
                "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet",
                "-Ifs", "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]


def body_of(text, signature):
    """`signature` で始まる関数の本体を素朴に切り出す (波括弧の対応で数える)。"""
    i = text.index(signature)
    i = text.index("{", i)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise SystemExit("関数の本体を切り出せない: %s" % signature)


def check_single_source():
    """[C4]: fd 0/1/2 の種別を決める場所が 1 つであること。

    票 §3-1 の根は「`isatty` と `fstat` が 2 か所で別々に判定していた」こと。
    実行時の試験 (F5) は食い違いを捕まえるが、**2 か所に分かれたこと自体**は
    形で押さえておく — 分かれた瞬間に落ちるほうが、直す人に早く届く。
    """
    vfs_fd = (ROOT / "fs/vfs_fd.c").read_text(encoding="utf-8")
    redir = (ROOT / "fs/fd_redirect.c").read_text(encoding="utf-8")

    isatty = body_of(vfs_fd, "int vfs_isatty(int fd)")
    fstat = body_of(vfs_fd, "int vfs_fstat(int fd, OS32_Stat *buf)")
    is_redir = body_of(redir, "int fd_is_redirected(int fd)")
    ifmt = body_of(redir, "u16 fd_redirect_ifmt(int fd, int *out_file_fd)")

    for name, body in (("vfs_isatty", isatty), ("vfs_fstat", fstat),
                       ("fd_is_redirected", is_redir)):
        if "fd_redirect_ifmt(" not in body:
            raise SystemExit(
                "[C4] %s が fd_redirect_ifmt() から引いていない "
                "(判定が 2 か所に分かれると票 §3-1 の食い違いが再発する)" % name)
    if "fd_is_redirected(" in isatty or "fd_is_redirected(" in fstat:
        raise SystemExit(
            "[C4] vfs_isatty / vfs_fstat が fd_is_redirected() を直に見ている "
            "— 種別は fd_redirect_ifmt() 1 本から引くこと")
    if "target_type" in is_redir:
        raise SystemExit(
            "[C4] fd_is_redirected が redir_table を直に読んでいる "
            "— fd_redirect_ifmt() から導くこと")
    for want in ("OS_S_IFCHR", "OS_S_IFREG", "OS_S_IFIFO"):
        if want not in ifmt:
            raise SystemExit("fd_redirect_ifmt が %s を返していない" % want)

    # S_IFIFO は POSIX と同じ値で、S_IFMT の中で他とぶつからない
    shared = (ROOT / "sdk/include/os32/os32_kapi_shared.h").read_text(
        encoding="utf-8")
    vals = dict((m.group(1), int(m.group(2), 0)) for m in re.finditer(
        r"#define\s+OS_(S_IF[A-Z]+)\s+(0x[0-9A-Fa-f]+|\d+)", shared))
    if vals.get("S_IFIFO") != 0x1000:
        raise SystemExit("OS_S_IFIFO が POSIX の 0x1000 ではない: %r"
                         % vals.get("S_IFIFO"))
    kinds = [(k, v) for k, v in vals.items() if k != "S_IFMT"]
    for k, v in kinds:
        if v & ~vals["S_IFMT"]:
            raise SystemExit("OS_%s が S_IFMT の外に出ている" % k)
    if len(set(v for _, v in kinds)) != len(kinds):
        raise SystemExit("OS_S_IF* の値が重なっている: %r" % vals)

    print("C4 SINGLE-SOURCE PASS (isatty / fstat / is_redirected はすべて "
          "fd_redirect_ifmt() 由来、OS_S_IFIFO=0x1000)", flush=True)


def build_host(tmp, name):
    exe = tmp / name
    subprocess.run(["gcc", *HOST_FLAGS, *HOST_INC,
                    str(ROOT / "tools/tests/fstat_redir_host.c"),
                    "-o", str(exe)], cwd=ROOT, check=True)
    print("HOST GNU11 -Werror COMPILE PASS "
          "(real fs/vfs.c + fs/vfs_fd.c + fs/fd_redirect.c)", flush=True)
    return exe


MUTATIONS = [
    # 変異 1: **2026-09-17 の不具合そのもの**。fstat だけリダイレクトを見ず、
    # fd 0/1/2 を無条件で S_IFCHR と答える。F4 も F5 も落ちなければならない。
    ("fstat_always_chr", "fs/vfs_fd.c",
     "        u16 ifmt = fd_redirect_ifmt(fd, &file_fd);",
     "        u16 ifmt = OS_S_IFCHR;"),
    # 変異 2: **片方だけ直した形** (票 §4 の F5 が狙う的)。fstat は正しく
    # 実体を答えるのに isatty が「fd 0/1/2 は常に端末」に戻る。F4 は通って
    # しまうので、F5 が無ければ素通りする。
    ("isatty_always_tty", "fs/vfs_fd.c",
     "        return (fd_redirect_ifmt(fd, (int *)0) == OS_S_IFCHR) ? 1 : 0;",
     "        return 1;"),
    # 変異 3: パイプをキャラクタデバイスと答える版。管理元が 1 本なので
    # isatty も一緒に「端末だ」と言い出す = 食い違いはしないが、**パイプを
    # 端末と呼ぶ**のは嘘なので、パイプの節が落ちる。
    ("pipe_is_chr", "fs/fd_redirect.c",
     "        return OS_S_IFIFO;",
     "        return OS_S_IFCHR;"),
    # 変異 4: 種別だけ合わせて中身は作り話を返す版。F4 の「直に開いた FD の
    # fstat と 1 バイトも違わない」が止める。
    ("file_stat_invented", "fs/vfs_fd.c",
     "            return vfs_fstat(file_fd, buf);",
     "            {\n"
     "                int mi; u8 *mp = (u8 *)buf;\n"
     "                for (mi = 0; mi < (int)sizeof(OS32_Stat); mi++) mp[mi] = 0;\n"
     "                buf->st_mode = (u16)(OS_S_IFREG | 0644);\n"
     "                buf->st_nlink = 1;\n"
     "                return VFS_OK;\n"
     "            }"),
]


def host_cmd(exe):
    return ["gcc", *HOST_FLAGS, *HOST_INC,
            str(ROOT / "tools/tests/fstat_redir_host.c"), "-o", str(exe)]


def one_mutation(item):
    """変異 1 本を一時ディレクトリの写しで組んで回す。(印字, 見逃し) を返す。"""
    name, relpath, old, new = item
    original = (ROOT / relpath).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-20s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-fstat-redir-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {relpath: original.replace(old, new, 1)},
                [host_cmd(exe)])
        except subprocess.CalledProcessError:
            return "MUTATE %-20s RED (コンパイルが通らない)" % name, 0
        head = ("HOST GNU11 -Werror COMPILE PASS "
                "(real fs/vfs.c + fs/vfs_fd.c + fs/fd_redirect.c)\n")
        rc = subprocess.run([str(exe)], cwd=str(tree), timeout=120,
                            capture_output=True).returncode
    if rc == 0:
        return (head + "MUTATE %-20s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    return head + "MUTATE %-20s RED (期待どおり落ちた)" % name, 0


def run_mutations(tmp):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    実物のソースは読むだけ — check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS, ("control", "fs/vfs_fd.c", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-fstat-redir-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        check_single_source()

        exe = build_host(tmp, "fstat-redir")
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT fstat_redir_host=%d" % rc, flush=True)
        failed += rc != 0

        if "--target" in sys.argv:
            for src in ("fs/vfs_fd.c", "fs/fd_redirect.c"):
                subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", src,
                                "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                               cwd=ROOT, check=True)
                print("TARGET i386-elf -Werror COMPILE PASS (%s)" % src,
                      flush=True)

        if "--mutate" in sys.argv:
            failed += run_mutations(tmp)

        sys.exit(1 if failed else 0)
