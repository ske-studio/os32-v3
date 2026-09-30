#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""userland/cmds/tar.c (票 S6) のホスト試験。

実物の `userland/cmds/tar.c` を `-DHOST_TEST` で gcc ビルドする。I/O
コールバックだけが POSIX に切り替わり、ustar の読み書きは実機と同じ
vendor 済み `lib/microtar/microtar.c` (rxi、MIT) が行う。

見るのは 5 つ:
  (a) `tar c` の出力をホストの Python `tarfile` が開けて、名前・サイズ・
      中身が一致する (DESIGN.md §6b の「ホスト側は tarfile で読める」)
  (b) Python `tarfile` が書いた ustar を `tar x` が展開でき、`tar t` が
      同じ一覧を出す
  (c) ustar の name 欄 (100B) に入らない名前を拒否して非ゼロ終了する
  (d) 存在しない入力・存在しないアーカイブでエラーを出して非ゼロ終了する
  (e) ディレクトリを再帰でたどる (往復して中身が一致する)

エミュレータ・実機・NHD には一切触れない。記録は tools/tests/s6_tdd.md。
"""
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "userland/cmds/tar.c"
MICROTAR = ROOT / "lib/microtar/microtar.c"
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement",
         "-DHOST_TEST", "-DMTAR_NO_STDIO"]
INCLUDES = ["-I" + str(ROOT / "lib/microtar")]

CASES = ["create_readable_by_python",
         "extract_python_ustar",
         "list_python_ustar",
         "reject_long_name",
         "missing_input",
         "directory_recursion",
         "strip_leading_slash",
         "name_exactly_100"]

failures = []
current_case = [""]
failed_cases = set()


def build(tmp):
    exe = tmp / "tar"
    subprocess.run(["gcc", *FLAGS, *INCLUDES, str(SRC), str(MICROTAR),
                    "-o", str(exe)], cwd=ROOT, check=True)
    print("HOST GNU89 -Werror COMPILE PASS", flush=True)
    return exe


def build_target(tmp):
    """実機と同じフラグでクロスコンパイルも通ること ([C1] C89/GNU89)。

    vendor の microtar.c は i386-elf でも 1 行も直さずに通る必要がある
    (README.OS32 の改変点はこの制約のためにある)。
    """
    flags = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
             "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
             "-O2", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-sign-compare",
             "-Wdeclaration-after-statement",
             "-D__OS32_USERLAND__", "-DMTAR_NO_STDIO"]
    includes = ["-I" + str(ROOT / p) for p in
                (".", "include", "sdk/include", "sdk/include/os32",
                 "userland/lib", "lib/microtar")]
    includes.append("-I/usr/local/cross/i386-elf/include")
    for src in (MICROTAR, SRC):
        subprocess.run(["i386-elf-gcc", *flags, *includes, "-c", str(src),
                        "-o", str(tmp / (src.stem + ".o"))],
                       cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror COMPILE PASS", flush=True)


def run(exe, cwd, *args):
    p = subprocess.run([str(exe), *args], cwd=cwd,
                       capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def check(case, cond, detail=""):
    if cond:
        print(f"  ok   {case}: {detail}" if detail else f"  ok   {case}")
    else:
        print(f"  FAIL {case}: {detail}", flush=True)
        failures.append(f"{current_case[0]} / {case}: {detail}")
        failed_cases.add(current_case[0])


def fresh(tmp, name):
    d = tmp / name
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    return d


# --- (a) tar c の出力を Python tarfile が読める ------------------------
def case_create_readable_by_python(exe, tmp):
    d = fresh(tmp, "a")
    (d / "one.txt").write_bytes(b"hello os32\n")
    (d / "two.bin").write_bytes(bytes(range(256)) * 7)   # 1792B: 512 の倍数でない
    (d / "empty.txt").write_bytes(b"")
    # TAR_CHUNK (8192B) を 2 回またぐ = 写しのループが 3 周する
    big = bytes((i * 7 + 3) & 0xFF for i in range(20000))
    (d / "big.bin").write_bytes(big)

    rc, out, err = run(exe, d, "c", "out.tar",
                       "one.txt", "two.bin", "empty.txt", "big.bin")
    check("create", rc == 0, f"rc={rc} out={out!r} err={err!r}")
    if rc != 0:
        return

    with tarfile.open(d / "out.tar", "r") as tf:
        members = tf.getmembers()
        names = [m.name for m in members]
        check("create.names",
              names == ["one.txt", "two.bin", "empty.txt", "big.bin"],
              f"names={names}")
        by = {m.name: m for m in members}
        check("create.size", by["two.bin"].size == 1792,
              f"size={by['two.bin'].size}")
        check("create.size0", by["empty.txt"].size == 0,
              f"size={by['empty.txt'].size}")
        check("create.body1", tf.extractfile("one.txt").read() == b"hello os32\n")
        check("create.body2",
              tf.extractfile("two.bin").read() == bytes(range(256)) * 7)
        check("create.body3", tf.extractfile("empty.txt").read() == b"")
        check("create.body4.chunked", tf.extractfile("big.bin").read() == big,
              "TAR_CHUNK を 2 回またぐ 20000B")
        check("create.isreg", all(m.isreg() for m in members),
              f"types={[m.type for m in members]}")


# --- (b) Python が書いた ustar を tar x が展開できる --------------------
def _make_python_archive(path):
    with tarfile.open(path, "w", format=tarfile.USTAR_FORMAT) as tf:
        for name, body in (("alpha.txt", b"A" * 10),
                           ("nest/beta.txt",
                            bytes((i * 5 + 1) & 0xFF for i in range(20000))),
                           ("nest/deep/gamma.txt", b"")):
            info = tarfile.TarInfo(name)
            info.size = len(body)
            info.mtime = 0
            tf.addfile(info, __import__("io").BytesIO(body))


def case_extract_python_ustar(exe, tmp):
    d = fresh(tmp, "b")
    _make_python_archive(d / "in.tar")
    (d / "dest").mkdir()

    rc, out, err = run(exe, d, "x", "in.tar", "-C", "dest")
    check("extract", rc == 0, f"rc={rc} out={out!r} err={err!r}")
    if rc != 0:
        return
    check("extract.alpha", (d / "dest/alpha.txt").read_bytes() == b"A" * 10)
    check("extract.beta.chunked",
          (d / "dest/nest/beta.txt").read_bytes()
          == bytes((i * 5 + 1) & 0xFF for i in range(20000)),
          "TAR_CHUNK を 2 回またぐ 20000B")
    check("extract.gamma", (d / "dest/nest/deep/gamma.txt").read_bytes() == b"")

    # -C 無し = カレントディレクトリへ
    d2 = fresh(tmp, "b2")
    _make_python_archive(d2 / "in.tar")
    rc, out, err = run(exe, d2, "x", "in.tar")
    check("extract.cwd", rc == 0 and (d2 / "alpha.txt").read_bytes() == b"A" * 10,
          f"rc={rc} out={out!r}")

    # 既存ファイルは上書きする
    (d2 / "alpha.txt").write_bytes(b"stale stale stale")
    rc, out, err = run(exe, d2, "x", "in.tar")
    check("extract.overwrite",
          rc == 0 and (d2 / "alpha.txt").read_bytes() == b"A" * 10,
          f"rc={rc} body={(d2 / 'alpha.txt').read_bytes()!r}")


def case_list_python_ustar(exe, tmp):
    d = fresh(tmp, "c")
    _make_python_archive(d / "in.tar")
    rc, out, err = run(exe, d, "t", "in.tar")
    got = [line for line in out.splitlines() if line.strip()]
    check("list", rc == 0, f"rc={rc} err={err!r}")
    check("list.names",
          got == ["alpha.txt", "nest/beta.txt", "nest/deep/gamma.txt"],
          f"got={got}")


# --- (c) 100B を超える名前は拒否 ----------------------------------------
def case_reject_long_name(exe, tmp):
    d = fresh(tmp, "d")
    long_name = "L" * 150
    (d / long_name).write_bytes(b"x")
    rc, out, err = run(exe, d, "c", "out.tar", long_name)
    check("longname.rc", rc != 0, f"rc={rc}")
    check("longname.msg", "tar: " + long_name in out,
          f"out={out[:160]!r}")

    # 99B ちょうどは通る (境界)
    d2 = fresh(tmp, "d2")
    ok_name = "K" * 99
    (d2 / ok_name).write_bytes(b"y")
    rc, out, err = run(exe, d2, "c", "out.tar", ok_name)
    check("longname.boundary", rc == 0, f"rc={rc} out={out!r}")
    if rc == 0:
        with tarfile.open(d2 / "out.tar", "r") as tf:
            check("longname.boundary.name",
                  tf.getnames() == [ok_name], f"names={tf.getnames()}")


# --- (d) 存在しない入力 --------------------------------------------------
def case_missing_input(exe, tmp):
    d = fresh(tmp, "e")
    rc, out, err = run(exe, d, "c", "out.tar", "nope.txt")
    check("missing.create.rc", rc != 0, f"rc={rc}")
    check("missing.create.msg", "tar: nope.txt:" in out, f"out={out!r}")

    rc, out, err = run(exe, d, "t", "nope.tar")
    check("missing.list.rc", rc != 0, f"rc={rc}")
    check("missing.list.msg", "tar: nope.tar:" in out, f"out={out!r}")

    rc, out, err = run(exe, d, "x", "nope.tar")
    check("missing.extract.rc", rc != 0, f"rc={rc}")

    rc, out, err = run(exe, d, "c")
    check("missing.usage.rc", rc != 0, f"rc={rc}")

    # 1 つ壊れていても残りは束ね、終了コードは非ゼロ
    (d / "good.txt").write_bytes(b"good\n")
    rc, out, err = run(exe, d, "c", "part.tar", "nope.txt", "good.txt")
    check("missing.partial.rc", rc != 0, f"rc={rc}")
    if (d / "part.tar").exists():
        with tarfile.open(d / "part.tar", "r") as tf:
            check("missing.partial.names", tf.getnames() == ["good.txt"],
                  f"names={tf.getnames()}")


# --- (e) ディレクトリの再帰 ---------------------------------------------
def case_directory_recursion(exe, tmp):
    d = fresh(tmp, "f")
    (d / "tree/sub/deeper").mkdir(parents=True)
    (d / "tree/top.txt").write_bytes(b"top\n")
    (d / "tree/sub/mid.bin").write_bytes(b"\x00\xff" * 3000)
    (d / "tree/sub/deeper/leaf.txt").write_bytes(b"leaf\n")
    (d / "tree/emptydir").mkdir()

    rc, out, err = run(exe, d, "c", "tree.tar", "tree")
    check("recurse.create", rc == 0, f"rc={rc} out={out!r} err={err!r}")
    if rc != 0:
        return

    with tarfile.open(d / "tree.tar", "r") as tf:
        names = sorted(tf.getnames())
        want = sorted(["tree", "tree/top.txt", "tree/sub", "tree/sub/mid.bin",
                       "tree/sub/deeper", "tree/sub/deeper/leaf.txt",
                       "tree/emptydir"])
        check("recurse.names", names == want, f"got={names}")
        dirs = sorted(m.name for m in tf.getmembers() if m.isdir())
        check("recurse.dirtype",
              dirs == sorted(["tree", "tree/sub", "tree/sub/deeper",
                              "tree/emptydir"]), f"dirs={dirs}")

    (d / "back").mkdir()
    rc, out, err = run(exe, d, "x", "tree.tar", "-C", "back")
    check("recurse.extract", rc == 0, f"rc={rc} out={out!r}")
    if rc != 0:
        return
    check("recurse.top", (d / "back/tree/top.txt").read_bytes() == b"top\n")
    check("recurse.mid",
          (d / "back/tree/sub/mid.bin").read_bytes() == b"\x00\xff" * 3000)
    check("recurse.leaf",
          (d / "back/tree/sub/deeper/leaf.txt").read_bytes() == b"leaf\n")
    check("recurse.emptydir", (d / "back/tree/emptydir").is_dir())


# --- (f) 先頭の '/' を落とす (GNU tar と同じ) ---------------------------
def case_strip_leading_slash(exe, tmp):
    d = fresh(tmp, "g")
    (d / "abs.txt").write_bytes(b"abs\n")
    abs_path = str(d / "abs.txt")
    if len(abs_path) > 99:
        check("strip", True, f"SKIP: 一時ディレクトリが長すぎる ({len(abs_path)}B)")
        return
    rc, out, err = run(exe, d, "c", "abs.tar", abs_path)
    check("strip.rc", rc == 0, f"rc={rc} out={out!r}")
    if rc != 0:
        return
    with tarfile.open(d / "abs.tar", "r") as tf:
        names = tf.getnames()
        check("strip.name", names == [abs_path.lstrip("/")], f"names={names}")


# --- (g) name 欄ちょうど 100B は NUL 終端できない --------------------------
# 上流 microtar は strcpy で 100 バイトの宛先を溢れさせる (README.OS32 の
# 改変点 3)。黙って壊すのではなく、きれいに断って非ゼロ終了すること。
def case_name_exactly_100(exe, tmp):
    d = fresh(tmp, "h")
    import io as _io
    name100 = "N" * 100
    with tarfile.open(d / "in.tar", "w", format=tarfile.USTAR_FORMAT) as tf:
        info = tarfile.TarInfo(name100)
        info.size = 3
        info.mtime = 0
        tf.addfile(info, _io.BytesIO(b"abc"))
    raw = (d / "in.tar").read_bytes()
    check("name100.fixture", raw[:100] == name100.encode(),
          "name 欄が 100B 使い切っていること")

    rc, out, err = run(exe, d, "t", "in.tar")
    check("name100.list.rc", rc != 0, f"rc={rc} out={out!r}")
    check("name100.list.nocrash", "Segmentation" not in err and err == "",
          f"err={err!r}")

    rc, out, err = run(exe, d, "x", "in.tar")
    check("name100.extract.rc", rc != 0, f"rc={rc} out={out!r}")
    check("name100.extract.nofile", not (d / name100).exists(),
          "壊れた名前で書き出さないこと")


if __name__ == "__main__":
    wanted = sys.argv[1:] or CASES
    with tempfile.TemporaryDirectory(prefix="os32-tar-") as t:
        tmp = pathlib.Path(t)
        exe = build(tmp)
        for case in wanted:
            print(f"CASE {case}", flush=True)
            current_case[0] = case
            globals()["case_" + case](exe, tmp)
        build_target(tmp)
    total = len(wanted)
    print(f"SUMMARY {total - len(failed_cases)}/{total} cases PASS, "
          f"{len(failures)} assertion failures", flush=True)
    if failures:
        for f in failures:
            print("FAILED " + f)
    sys.exit(1 if failures else 0)
