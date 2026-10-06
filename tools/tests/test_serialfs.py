"""SerialFS (シリアル越しの /host) のホスト試験 — 票 TASK_SERIAL_HOSTFS §3 の T1 と T5。

記録: tools/tests/serialfs_tdd.md
票  : docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 B (§1-v2 / §1-v3 / ユーザー決裁 2026-09-24)

5 つの段:

  C   ゲスト側の実物 (fs/sfs_proto.c・fs/sfs_client.c・fs/serialfs.c・
      userland/shell/serial_watchdog.c・userland/system/hsync_bootold.inc) を
      tools/tests/serialfs_host.c に取り込み、偽の線・仮想の時計・偽のホストで
      回す。フレーム・CRC32・番号・再送・フレーム長の境界・VFS の契約・
      障害の注入 (喪失 / 遅延 / CRC 破損 / 番号違い / セッション違い /
      ホストの停止 / ごみの連続 / ERR / 契約違反の応答)
  GATE drivers/serial.c のゲート (tools/tests/serial_gate_host.c)。セッション中に
      rshell へフレームが漏れない・出力は保留リングへ・下ろすと受信を捨てる
  SESSION fs/serialfs_session.c (tools/tests/serialfs_session_host.c)。実物の
      drivers/serial.c・fs/sfs_*.c と組み、HELLO → mount → BYE / LOG / EXIT の
      流れ、HELLO が通らなかった回も隔離してから下ろす (決定 11)、EXIT の後に
      保留へ入った文字も流す (Fable m6)、隔離の上限と NOT_QUIET
  PY  ホスト側の実物 (tools/serialfs_host.py・tools/rshell_serial.py): 振り分け
      (EOT とフレームの混在)、`..` と symlink の脱出の拒否、応答のキャッシュ
      (再送で副作用が二重にならない)、期限切れの応答を送らない、BYE の後は
      答えない、`sfs run` の行の外ではフレームを解釈しない
  XC  C と Python の相互照合 (C が組んだ列を Python が読む / 逆)、定数の照合
      (sfs_proto.h ⇔ serialfs_host.py、hsync_bootold.inc ⇔ boot/boot_defs.h)、
      結合 (C の受け手と Python のホストを実時間のパイプでつなぎ、応答の喪失と
      CRC 破損を注入して RENAME が二重に実行されないこと)

  python3 -B tools/tests/test_serialfs.py                     # 全段
  python3 -B tools/tests/test_serialfs.py --target            # + i386-elf -Werror
  python3 -B tools/tests/test_serialfs.py --mutate            # + 否定側

--mutate: 実装を 1 か所ずつ**写しの上で**壊して RED になることを見る。組めない
変異・当てはまらない変異は ERROR として数える (見逃しと同じく失敗)。何も変えない
恒等の対照を C と Python に 1 本ずつ入れ、それが GREEN であること (試験の枠が
壊れていないこと) も見る。
"""
import errno
import importlib.util
import io
import os
import pathlib
import re
import select
import shutil
import struct
import mutpar
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
HARNESS = ROOT / "tools/tests/serialfs_host.c"
GATE_HARNESS = ROOT / "tools/tests/serial_gate_host.c"
SESSION_HARNESS = ROOT / "tools/tests/serialfs_session_host.c"

C_SRCS = {
    "fs/sfs_proto.c": "sfs_proto.c",
    "fs/sfs_proto.h": "sfs_proto.h",
    "fs/sfs_client.c": "sfs_client.c",
    "fs/sfs_client.h": "sfs_client.h",
    "fs/serialfs.c": "serialfs.c",
    "userland/shell/serial_watchdog.c": "serial_watchdog.c",
    "userland/system/hsync_bootold.inc": "hsync_bootold.inc",
}
C_CASES = ["crc_and_frames", "timing_and_seq", "vfs_contract", "mount_permit",
           "fault_drop", "fault_corrupt", "fault_stray", "fault_delay",
           "fault_dead", "fault_err_stale", "fault_garbage", "fault_stray_flood",
           "cannot_wait",
           "seq_exhausted", "hello_cases", "bad_host_replies", "rshell_rules", "rshell_watchdog_junk",
           "bootold_rules"]
GATE_CASES = ["gate_tx", "hold_overflow", "gate_rx", "gate_init_refused",
              "isr_counts"]
SESSION_CASES = ["flow", "hello_fail_quiet", "hello_fail_flood", "end_late_hold",
                 "end_not_quiet"]

FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-D_DEFAULT_SOURCE", "-D__cdecl=",
         "-Wno-unused-function"]

def flags_for(mutated):
    """実物は -Werror。変異は警告 (使われなくなった引数など) を止めない —
    **組めない変異は ERROR** として数えるので、警告で ERROR にしない。"""
    if mutated is None:
        return FLAGS
    return [f for f in FLAGS if f != "-Werror"]


sys.path.insert(0, str(ROOT / "tools/tests"))
from test_serial_portc import FAKE_ARCH_IO, FAKE_PLATFORM_IO  # noqa: E402
import mutpar  # noqa: E402  (tools/tests/mutpar.py)


# ============================================================================
#  C の組み立て
# ============================================================================
def c_build(tmp, mutated=None, name="real"):
    """serialfs_host.c を組む。mutated = {rel: 中身} は写しの上で差し替える
    (写しのディレクトリを -I の先頭に置く。実物は 1 バイトも触らない)。"""
    work = pathlib.Path(tmp) / name
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    for rel, base in C_SRCS.items():
        text = (mutated or {}).get(rel, (ROOT / rel).read_text(encoding="utf-8"))
        (work / base).write_text(text, encoding="utf-8")
    exe = work / "serialfs-host"
    cmd = ["gcc", *flags_for(mutated), "-I" + str(work), "-I" + str(ROOT / "fs"),
           "-I" + str(ROOT / "lib"), "-I" + str(ROOT / "include"),
           "-I" + str(ROOT / "sdk/include/os32"),
           "-I" + str(ROOT / "userland/shell"),
           "-I" + str(ROOT / "userland/system"),
           str(HARNESS), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stderr=subprocess.DEVNULL if mutated is not None else None)
    return exe


def gate_build(tmp, mutated=None, name="gate"):
    work = pathlib.Path(tmp) / name
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    (work / "arch_io.h").write_text(FAKE_ARCH_IO, encoding="utf-8")
    (work / "platform_io.h").write_text(FAKE_PLATFORM_IO, encoding="utf-8")
    text = (mutated or {}).get("drivers/serial.c",
                               (ROOT / "drivers/serial.c").read_text(encoding="utf-8"))
    (work / "serial.c").write_text(text, encoding="utf-8")
    exe = work / "serial-gate-host"
    cmd = ["gcc", *flags_for(mutated), "-I" + str(work), "-I" + str(ROOT / "include"),
           "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "sdk/include/os32"),
           str(GATE_HARNESS), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stderr=subprocess.DEVNULL if mutated is not None else None)
    return exe


# セッションの試験は hlt で tick を進め、IF=1 で待てる (ゲートの偽物は IF=0)
FAKE_ARCH_IO_SESSION = FAKE_ARCH_IO.replace(
    "static inline int _irq_enabled(void) { return 0; }",
    "int fake_irq_enabled(void);\n"
    "static inline int _irq_enabled(void) { return fake_irq_enabled(); }").replace(
    "static inline void _halt(void) {}",
    "void fake_halt(void);\nstatic inline void _halt(void) { fake_halt(); }").replace(
    "static inline void _idle(void) {}",
    "static inline void _idle(void) { fake_halt(); }")
SESSION_SRCS = ("drivers/serial.c", "fs/sfs_proto.c", "fs/sfs_client.c",
                "fs/serialfs_session.c", "lib/crc32.c")


def session_build(tmp, mutated=None, name="session"):
    """fs/serialfs_session.c を実物の drivers/serial.c・fs/sfs_*.c と組む。
    kstring.h (glibc の string.h と衝突) と appslot.h (カーネルの構造体) は
    写しのディレクトリの小さな偽物で影にする。"""
    work = pathlib.Path(tmp) / name
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    (work / "arch_io.h").write_text(FAKE_ARCH_IO_SESSION, encoding="utf-8")
    (work / "platform_io.h").write_text(FAKE_PLATFORM_IO, encoding="utf-8")
    (work / "kstring.h").write_text(
        "#ifndef KSTRING_H\n#define KSTRING_H\n"
        "int kstrcmp(const char *a, const char *b);\n#endif\n", encoding="utf-8")
    (work / "appslot.h").write_text(
        "#ifndef APPSLOT_H\n#define APPSLOT_H\n#define APP_ID_SHELL 1\n#endif\n",
        encoding="utf-8")
    for rel in SESSION_SRCS:
        text = (mutated or {}).get(rel, (ROOT / rel).read_text(encoding="utf-8"))
        (work / pathlib.Path(rel).name).write_text(text, encoding="utf-8")
    exe = work / "serialfs-session-host"
    cmd = ["gcc", *flags_for(mutated), "-I" + str(work), "-I" + str(ROOT / "include"),
           "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "fs"),
           "-I" + str(ROOT / "lib"), "-I" + str(ROOT / "sdk/include/os32"),
           str(SESSION_HARNESS), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stderr=subprocess.DEVNULL if mutated is not None else None)
    return exe


# 変異のケースの時間の上限。実物のケースは長いもので 0.5 秒 (rshell_watchdog_junk、
# 2026-09-26)。上限を消す変異 (C16「静まるのを待つ口に上限が無い」) は止まらず、
# この上限で RED になる — 60 秒のままだと変異を並列にしても段の wall がこの 1 本で
# 決まるので、変異だけ 20 秒にする (実物の 40 倍、make -j の負荷の下でも届かない)。
MUT_CASE_TIMEOUT = 20


def run_exe_cases(exe, cases, quiet=False, tag="", first_fail=False, timeout=60):
    """落ちた数を返す。first_fail なら最初に落ちたところで打ち切る (変異用)。"""
    failed = 0
    for case in cases:
        try:
            rc = mutpar.run_timeout([str(exe), case], cwd=ROOT, timeout=timeout,
                                stdout=subprocess.DEVNULL if quiet else None,
                                stderr=subprocess.DEVNULL if quiet else None
                                ).returncode
        except subprocess.TimeoutExpired:
            rc = mutpar.timeout_red(124)
        if not quiet:
            print(f"EXIT {tag}{case}={rc}", flush=True)
        failed += rc != 0
        if failed and first_fail:
            break
    return failed


def build_target(tmp):
    """カーネルと同じ i386-elf で新しい実物を -Werror で通す。"""
    base = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
            "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
            "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
            "-O2", "-Wall", "-Werror", 
            "-D__KERNEL_BUILD__"]
    inc = ["-I.", "-Iinclude", "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
           "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet", "-Ifs",
           "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]
    for rel in ("fs/sfs_proto.c", "fs/sfs_client.c", "fs/serialfs.c",
                "fs/serialfs_session.c", "drivers/serial.c",
                "drivers/serial_plan.c"):
        out = pathlib.Path(tmp) / (rel.replace("/", "_") + ".o")
        subprocess.run(base + inc + ["-c", rel, "-o", str(out)], cwd=ROOT,
                       check=True)
    print("TARGET i386-elf GNU11 -Werror PASS (sfs_proto / sfs_client / "
          "serialfs / serialfs_session / serial / serial_plan)", flush=True)


# ============================================================================
#  Python の実物
# ============================================================================
def load_pair(h_path=None, r_path=None):
    """serialfs_host と rshell_serial を (写しでも) 読み込む。rshell_serial の
    `import serialfs_host` が同じものを引くよう sys.modules に置いてから読む。"""
    spec = importlib.util.spec_from_file_location(
        "serialfs_host", str(h_path or TOOLS / "serialfs_host.py"))
    h = importlib.util.module_from_spec(spec)
    sys.modules["serialfs_host"] = h
    spec.loader.exec_module(h)
    spec = importlib.util.spec_from_file_location(
        "rshell_serial_under_test", str(r_path or TOOLS / "rshell_serial.py"))
    r = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(r)
    r.serialfs_host = h
    return h, r


FAILED = []


def check(cond, what):
    if not cond:
        FAILED.append(what)


def frames_of(h, data):
    dm = h.Demux()
    return [e[1] for e in dm.feed(data, 0.0) if e[0] == "frame"]


def case_demux(h, r):
    hello = h.encode(h.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 7))
    # 本文に ENQ・EOT・ESC・'SF' を含むフレーム
    tricky = h.encode(h.T_LOG, 9, 0, b"a\x04b\x05SF\x1bc\x04")
    stream = (b"> sfs run x\r\n" + hello + b"mid" + tricky + b"tail\x04after")
    dm = h.Demux()
    ev = dm.feed(stream, 0.0)
    kinds = [e[0] for e in ev]
    check(kinds == ["text", "frame", "text", "frame", "text", "eot", "text"],
          "demux: kinds %r" % kinds)
    check(ev[0][1] == b"> sfs run x\r\n", "demux: leading text")
    check(ev[3][1].payload == b"a\x04b\x05SF\x1bc\x04",
          "demux: EOT / ENQ inside the payload are payload")
    # 1 バイトずつ入れても同じ (途中のフレームは待つ)
    dm = h.Demux()
    ev2 = []
    for i in range(len(stream)):
        ev2 += dm.feed(stream[i:i + 1], 0.0)
    kinds2 = [e[0] for e in ev2 if e[0] != "text"]
    text2 = b"".join(e[1] for e in ev2 if e[0] == "text")
    check(kinds2 == ["frame", "frame", "eot"], "demux: bytewise kinds %r" % kinds2)
    check(text2 == b"> sfs run x\r\nmidtailafter", "demux: bytewise text %r" % text2)
    # CRC の壊れたフレームは**丸ごと捨てる** — 中の 0x04 で行を終えない
    bad = bytearray(h.encode(h.T_LOG, 9, 0, b"xx\x04yy"))
    bad[-1] ^= 0xFF
    dm = h.Demux()
    ev = dm.feed(bytes(bad) + b"z\x04", 0.0)
    check([e[0] for e in ev] == ["text", "eot"] and ev[0][1] == b"z",
          "demux: corrupt frame skipped whole %r" % ev)
    check(dm.bad_frames == 1, "demux: bad frame counted")
    # 印の無い ENQ は文字
    dm = h.Demux()
    ev = dm.feed(b"a\x05b\x05Sx\x04", 0.0)
    check(ev == [("text", b"a\x05b\x05Sx"), ("eot",)], "demux: bare ENQ is text %r" % ev)
    # 長さ欄が上限を越えるヘッダは文字 (フレームを待たない)
    hdr = b"\x05SF" + struct.pack("<BIHH", 0x10, 1, 1, 513)
    dm = h.Demux()
    ev = dm.feed(hdr + b"\x04", 0.0)
    check(ev and ev[-1] == ("eot",), "demux: oversize header does not swallow EOT")
    # 途中で途切れたフレームは、止まってから文字として流す
    dm = h.Demux()
    ev = dm.feed(hello[:8], 0.0)
    check(ev == [], "demux: partial frame waits")
    ev = dm.poll(0.1)
    check(ev == [], "demux: short gap still waits")
    ev = dm.poll(1.0)
    check(ev and ev[0][0] == "text", "demux: stalled partial becomes text")
    ev = dm.feed(b"\x04", 1.0)
    check(("eot",) in ev, "demux: EOT after a stalled partial")


def mk_tree(tmp):
    root = pathlib.Path(tmp) / "hostroot"
    root.mkdir()
    (root / "hello.txt").write_bytes(b"hello from the host\n" * 200)
    (root / "gone.txt").write_bytes(b"x")
    many = root / "many"
    many.mkdir()
    for i in range(150):
        (many / ("file-%03d-with-a-long-name.bin" % i)).write_bytes(b"z" * i)
    (root / "sub").mkdir()
    outside = pathlib.Path(tmp) / "outside"
    outside.mkdir()
    (outside / "secret").write_bytes(b"s")
    os.symlink(str(outside), str(root / "escape"))
    os.symlink(str(root / "hello.txt"), str(root / "inside_link"))
    return root


def expect_err(h, fn, code, what):
    try:
        fn()
        check(False, "%s: accepted" % what)
    except h.SfsError as e:
        check(e.code == code, "%s: code %d (want %d)" % (what, e.code, code))
    except OSError as e:
        got = h.ERRNO_MAP.get(e.errno, h.ERR_IO)
        check(got == code, "%s: errno %d -> %d (want %d)" % (what, e.errno, got, code))


def list_all(h, fs, wire):
    names, cookie, pages = [], 0, 0
    while True:
        st, body = fs.list(wire, cookie)
        nxt = struct.unpack_from("<I", body)[0]
        check(len(body) + 4 <= h.MAX_PAYLOAD, "list: page fits")
        pos = 4
        while pos < len(body):
            k, sz, nl = struct.unpack_from("<BIB", body, pos)
            names.append((body[pos + 6:pos + 6 + nl], k))
            pos += 6 + nl
        pages += 1
        if nxt == 0:
            return names, pages
        check(nxt > cookie, "list: cookie advances")
        cookie = nxt


def case_paths(h, r):
    """`..` と symlink の脱出、固定の作り (dir_fd + O_NOFOLLOW) とパスの作り。"""
    with tempfile.TemporaryDirectory(prefix="os32-sfs-paths-") as tmp:
        root = mk_tree(tmp)
        for secure in (True, False):
            tag = "fd" if secure else "path"
            fs = h.HostFS(str(root), allow_write=["sub", "w.bin"], secure=secure)
            check(fs.secure == secure, "%s: mode" % tag)
            for bad in (b"/../outside/secret", b"/sub/../../outside", b"/.",
                        b"/sub/..", b"relative", b"/a\\b", b"/a\x00b"):
                expect_err(h, lambda: fs.stat(bad), h.ERR_INVAL, "%s: %r" % (tag, bad))
            # symlink で根の外へ: どちらの作りでも断る
            expect_err(h, lambda: fs.stat(b"/escape/secret"), h.ERR_INVAL,
                       "%s: symlink escape stat" % tag)
            expect_err(h, lambda: fs.read(b"/escape/secret", 0, 1), h.ERR_INVAL,
                       "%s: symlink escape read" % tag)
            expect_err(h, lambda: fs.list(b"/escape", 0), h.ERR_INVAL,
                       "%s: symlink escape list" % tag)
            # 根そのものは変えない
            for op in (lambda: fs.unlink(b"/"), lambda: fs.rmdir(b"/"),
                       lambda: fs.rename(b"/", b"/x"),
                       lambda: fs.write(b"/", 0, 0, b"")):
                expect_err(h, op, h.ERR_INVAL, "%s: root modification" % tag)
            # 操作の結果
            st, body = fs.stat(b"/hello.txt")
            kind, size, mtime = struct.unpack("<BII", body)
            check(kind == h.KIND_FILE and size == 4000 and mtime > 0, "%s: stat file" % tag)
            st, body = fs.stat(b"/many")
            check(struct.unpack("<BII", body)[0] == h.KIND_DIR, "%s: stat dir" % tag)
            st, body = fs.stat(b"/")
            check(struct.unpack("<BII", body)[0] == h.KIND_DIR, "%s: stat root" % tag)
            names, pages = list_all(h, fs, b"/many")
            check(len(names) == 150 and [n for n, _ in names] == sorted(n for n, _ in names)
                  and pages > 1, "%s: list %d names in %d pages" % (tag, len(names), pages))
            n, data = fs.read(b"/hello.txt", 3990, 100)
            check(n == 10, "%s: read at EOF" % tag)
            n, data = fs.read(b"/hello.txt", 0, 5000)
            check(n == h.READ_MAX, "%s: read is clamped to READ_MAX" % tag)
            expect_err(h, lambda: fs.read(b"/many", 0, 1), h.ERR_ISDIR, "%s: read dir" % tag)
            # **書き込みは既定で禁止**。--allow-write のパスとその下だけ
            expect_err(h, lambda: fs.write(b"/hello.txt", 0, h.WF_TRUNC, b"x"),
                       h.ERR_ROFS, "%s: write outside allow" % tag)
            expect_err(h, lambda: fs.unlink(b"/gone.txt"), h.ERR_ROFS,
                       "%s: unlink outside allow" % tag)
            expect_err(h, lambda: fs.mkdir(b"/newdir"), h.ERR_ROFS,
                       "%s: mkdir outside allow" % tag)
            expect_err(h, lambda: fs.rename(b"/sub/a", b"/hello.txt"), h.ERR_ROFS,
                       "%s: rename into outside allow" % tag)
            expect_err(h, lambda: fs.write(b"/subx", 0, h.WF_TRUNC, b"x"), h.ERR_ROFS,
                       "%s: allow is per component (subx is not sub)" % tag)
            check(fs.write(b"/w.bin", 0, h.WF_TRUNC, b"abc")[0] == 3, "%s: allowed file" % tag)
            check(fs.write(b"/w.bin", 3, 0, b"de")[0] == 2 and
                  (root / "w.bin").read_bytes() == b"abcde", "%s: write at offset" % tag)
            check(fs.mkdir(b"/sub/d")[0] == 0 and fs.write(b"/sub/d/f", 0, h.WF_TRUNC, b"1")[0] == 1,
                  "%s: allowed subtree" % tag)
            check(fs.rename(b"/sub/d/f", b"/sub/g")[0] == 0, "%s: rename inside allow" % tag)
            check(fs.unlink(b"/sub/g")[0] == 0 and fs.rmdir(b"/sub/d")[0] == 0,
                  "%s: unlink / rmdir inside allow" % tag)
            expect_err(h, lambda: fs.unlink(b"/sub"), h.ERR_ISDIR, "%s: unlink dir" % tag)
            (root / "w.bin").unlink()
        # `--allow-write /` だけが根の全体を開く。壊れた指定は起動時に断る
        fs_all = h.HostFS(str(root), allow_write=["/"])
        check(fs_all.write(b"/w2.bin", 0, h.WF_TRUNC, b"q")[0] == 1 and
              fs_all.unlink(b"/w2.bin")[0] == 0, "allow /: whole root writable")
        for bad_allow in ("..", "a/../b", "a\\..\\b", "x\x00"):
            try:
                h.HostFS(str(root), allow_write=[bad_allow])
                check(False, "allow %r accepted" % bad_allow)
            except ValueError:
                pass
        # **リンク越しに --allow-write の外へ書けない** (往復 2、Codex 1)。
        # out/link -> protected、out/f2 -> protected/x。パスの作りは実体で判定して
        # ROFS、固定の作りは辿らないので INVAL。どちらも protected には何も届かない
        (root / "out").mkdir()
        (root / "out" / "a").write_bytes(b"a")
        (root / "out" / "inner").mkdir()
        (root / "protected").mkdir()
        (root / "protected" / "victim").write_bytes(b"v")
        os.symlink(str(root / "protected"), str(root / "out" / "link"))
        os.symlink(str(root / "protected" / "x"), str(root / "out" / "f2"))
        os.symlink(str(root / "out" / "inner"), str(root / "out" / "self"))
        for secure, code in ((False, h.ERR_ROFS), (True, h.ERR_INVAL)):
            tag = "fd" if secure else "path"
            fs = h.HostFS(str(root), allow_write=["out"], secure=secure)
            expect_err(h, lambda: fs.write(b"/out/link/f", 0, h.WF_TRUNC, b"x"), code,
                       "%s: write through a dir link into protected" % tag)
            expect_err(h, lambda: fs.write(b"/out/f2", 0, h.WF_TRUNC, b"x"), code,
                       "%s: write through a file link into protected" % tag)
            expect_err(h, lambda: fs.mkdir(b"/out/link/d"), code,
                       "%s: mkdir through a link" % tag)
            expect_err(h, lambda: fs.rename(b"/out/a", b"/out/link/b"), code,
                       "%s: rename into protected through a link" % tag)
            expect_err(h, lambda: fs.unlink(b"/out/link/victim"), code,
                       "%s: unlink through a link" % tag)
            # rmdir: パスの作りは実体 (protected) で ROFS、固定の作りはリンクを
            # ディレクトリとして扱わない (ENOTDIR)
            expect_err(h, lambda: fs.rmdir(b"/out/link"), h.ERR_NOTDIR if secure else code,
                       "%s: rmdir of a link" % tag)
            check(sorted(os.listdir(root / "protected")) == ["victim"] and
                  (root / "out" / "a").exists() and not (root / "protected" / "x").exists(),
                  "%s: nothing changed behind the link" % tag)
            if not secure:
                # 実体も許可の中なら通る (パスの作りは根の中のリンクを辿る)
                check(fs.write(b"/out/self/g", 0, h.WF_TRUNC, b"g")[0] == 1 and
                      (root / "out" / "inner" / "g").read_bytes() == b"g",
                      "path: link that stays inside the allowed subtree")
                (root / "out" / "inner" / "g").unlink()
            fs.close()

        # 一部しか書けない pwrite (往復 2、Codex 5): 全部書けるまでくり返す
        fs = h.HostFS(str(root), allow_write=["out"])
        real_pwrite = os.pwrite
        calls = {"n": 0}

        def short_pwrite(fd, data, off):
            calls["n"] += 1
            return real_pwrite(fd, data[:3], off)
        os.pwrite = short_pwrite
        try:
            n, _ = fs.write(b"/out/part", 0, h.WF_TRUNC, b"0123456789")
        finally:
            os.pwrite = real_pwrite
        check(n == 10 and calls["n"] == 4 and
              (root / "out" / "part").read_bytes() == b"0123456789",
              "fd: short pwrite is repeated until all is written (n=%d calls=%d)"
              % (n, calls["n"]))
        os.pwrite = lambda fd, data, off: 0
        try:
            expect_err(h, lambda: fs.write(b"/out/part", 0, h.WF_TRUNC, b"zz"), h.ERR_IO,
                       "fd: pwrite that makes no progress")
        finally:
            os.pwrite = real_pwrite
        fs.close()

        # **起点の fd は起動時に固定** (往復 2、Codex 2): 根を symlink に差し替えても
        # 元のディレクトリを見続ける。realpath を解かない pm で根が symlink なら
        # 起動時に断る (O_NOFOLLOW)
        fs = h.HostFS(str(root), allow_write=["out"])
        os.rename(str(root), str(root) + ".bak")
        os.symlink(str(pathlib.Path(tmp) / "outside"), str(root))
        try:
            st, body = fs.stat(b"/hello.txt")
            check(struct.unpack("<BII", body)[0] == h.KIND_FILE, "fd: pinned root after swap")
            expect_err(h, lambda: fs.stat(b"/secret"), h.ERR_NOTFOUND,
                       "fd: swapped root does not show the outside")
            names, _ = list_all(h, fs, b"/")
            check(b"secret" not in [n for n, _ in names] and
                  b"hello.txt" in [n for n, _ in names], "fd: list uses the pinned root")
            check(fs.write(b"/out/w3", 0, h.WF_TRUNC, b"3")[0] == 1 and
                  (pathlib.Path(str(root) + ".bak") / "out" / "w3").exists() and
                  not (pathlib.Path(tmp) / "outside" / "out").exists(),
                  "fd: write lands in the pinned root")
        finally:
            os.unlink(str(root))
            os.rename(str(root) + ".bak", str(root))
        fs.close()

        class NoRealpath(object):
            realpath = staticmethod(lambda p: p)
            join = staticmethod(os.path.join)
            normcase = staticmethod(os.path.normcase)
            commonpath = staticmethod(os.path.commonpath)
            sep = os.path.sep
        os.symlink(str(root), str(pathlib.Path(tmp) / "rootlink"))
        try:
            h.HostFS(os.fsencode(str(pathlib.Path(tmp) / "rootlink")), pm=NoRealpath,
                     secure=True)
            check(False, "fd: root given as a symlink was opened")
        except OSError as e:
            check(e.errno in (errno.ELOOP, errno.ENOTDIR), "fd: root symlink refused (%r)" % e)

        # 固定の作りは**根の中を指す symlink も辿らない** (差し替えの競合を作らない)
        fs = h.HostFS(str(root), allow_write=["sub"])
        st, body = fs.stat(b"/inside_link")
        check(struct.unpack("<BII", body)[0] == h.KIND_OTHER, "fd: symlink is OTHER")
        expect_err(h, lambda: fs.read(b"/inside_link", 0, 1), h.ERR_INVAL,
                   "fd: symlink not followed on read")
        os.symlink(str(root / "outside_target"), str(root / "sub" / "lnk"))
        expect_err(h, lambda: fs.write(b"/sub/lnk", 0, h.WF_TRUNC, b"x"), h.ERR_INVAL,
                   "fd: write through a symlink refused")
        check(not (root / "outside_target").exists(), "fd: nothing written through the link")
        # 検査と操作の間で要素を symlink に差し替える: 固定の作りは辿らない
        (root / "sub" / "real").mkdir()
        (root / "sub" / "real" / "f").write_bytes(b"in")
        fs2 = h.HostFS(str(root), allow_write=["sub"])
        comps = fs2._comps(b"/sub/real/f")
        orig_open_dir = fs2._open_dir

        def swapping_open_dir(c):
            # 最後の親を開く直前に real を根の外への symlink に差し替える
            if list(c) == [b"sub", b"real"]:
                os.rename(str(root / "sub" / "real"), str(root / "sub" / "real.bak"))
                os.symlink(str(pathlib.Path(tmp) / "outside"), str(root / "sub" / "real"))
            return orig_open_dir(c)
        fs2._open_dir = swapping_open_dir
        expect_err(h, lambda: fs2.read(b"/sub/real/f", 0, 10), h.ERR_INVAL,
                   "fd: swapped component is not followed")
        check(comps == [b"sub", b"real", b"f"], "fd: comps")


def case_ntpath(h, r):
    """Windows のホストの判定 (ntpath)。要素単位で比べ、`C:\\hostile` を `C:\\host`
    の中と読まない。ドライブ違い・`:` (代替データストリーム) も断る。"""
    import ntpath
    fs = h.HostFS(b"C:\\host", pm=ntpath, secure=False)
    check(fs.root == b"C:\\host", "nt: root %r" % fs.root)
    check(fs.contained(b"C:\\host"), "nt: root itself")
    check(fs.contained(b"C:\\host\\a\\b"), "nt: child")
    check(fs.contained(b"c:\\HOST\\a"), "nt: case-insensitive")
    check(not fs.contained(b"C:\\hostile\\x"), "nt: prefix sibling is outside")
    check(not fs.contained(b"C:\\host\\..\\x"), "nt: .. outside")
    check(not fs.contained(b"D:\\host\\a"), "nt: other drive")
    check(fs.resolve(b"/a/b") == b"C:\\host\\a\\b", "nt: join %r" % fs.resolve(b"/a/b"))
    expect_err(h, lambda: fs.resolve(b"/a:b"), h.ERR_INVAL, "nt: drive-like colon")
    expect_err(h, lambda: fs.resolve(b"/file.txt:stream"), h.ERR_INVAL, "nt: ADS colon")
    expect_err(h, lambda: fs.resolve(b"/a\\..\\..\\x"), h.ERR_INVAL, "nt: backslash")
    check(fs.real_comps(b"C:\\host\\a\\b") == [b"a", b"b"] and
          fs.real_comps(b"c:\\HOST") == [] and fs.real_comps(b"C:\\hostile\\x") is None,
          "nt: real_comps %r" % fs.real_comps(b"C:\\host\\a\\b"))
    fw = h.HostFS(b"C:\\host", allow_write=["Out"], pm=ntpath, secure=False)
    check(fw._allowed([b"out", b"f"]) and fw._allowed([b"OUT"]) and not fw._allowed([b"outx"]),
          "nt: allow-write is case-insensitive and per component")
    top = h.HostFS(b"C:\\", pm=ntpath, secure=False)
    check(top.contained(b"C:\\x") and not top.contained(b"D:\\x"), "nt: drive root")
    # POSIX でも接頭辞の兄弟は外
    import posixpath
    px = h.HostFS(b"/srv/host", pm=posixpath, secure=False)
    check(not px.contained(b"/srv/hostile/x") and px.contained(b"/srv/host/x"),
          "posix: prefix sibling is outside")


def req(h, t, sid, seq, payload):
    f = frames_of(h, h.encode(t, sid, seq, payload))
    return f[0]


def pth(p):
    return bytes([len(p)]) + p


WRITABLE = ["renamed.txt", "hello.txt", "newdir", "gone.txt", "w.bin"]


def case_server(h, r):
    with tempfile.TemporaryDirectory(prefix="os32-sfs-server-") as tmp:
        root = mk_tree(tmp)
        sv = h.Server(h.HostFS(str(root), allow_write=WRITABLE))
        hello = req(h, h.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 42))
        r1 = sv.handle(hello)
        sid = frames_of(h, r1)[0].sid
        check(sid != 0, "server: session id")
        # HELLO の再送は同じ ID (応答をそのまま返す)
        check(sv.handle(hello) == r1 and sv.sid == sid, "server: HELLO resend cached")
        # 未知の ID には ERR (STALE)
        e = frames_of(h, sv.handle(req(h, h.T_STAT, sid ^ 1, 1, pth(b"/"))))[0]
        check(e.type == h.T_ERR and struct.unpack("<i", e.payload)[0] == h.ERR_STALE,
              "server: unknown session -> ERR STALE")
        # **再送で副作用が二重にならない**: RENAME / UNLINK / MKDIR / WRITE
        ren = req(h, h.T_RENAME, sid, 1, pth(b"/hello.txt") + pth(b"/renamed.txt"))
        a = sv.handle(ren)
        ex = sv.executed
        b = sv.handle(ren)
        check(a == b and sv.executed == ex and sv.replayed >= 1,
              "server: RENAME resend replayed, not re-executed")
        check(struct.unpack_from("<i", frames_of(h, b)[0].payload)[0] == 0,
              "server: replayed RENAME still says OK")
        check((root / "renamed.txt").exists() and not (root / "hello.txt").exists(),
              "server: rename happened once")
        mk = req(h, h.T_MKDIR, sid, 2, pth(b"/newdir"))
        a = sv.handle(mk)
        b = sv.handle(mk)
        check(a == b and struct.unpack_from("<i", frames_of(h, b)[0].payload)[0] == 0,
              "server: MKDIR resend is not EXIST")
        ul = req(h, h.T_UNLINK, sid, 3, pth(b"/gone.txt"))
        a = sv.handle(ul)
        b = sv.handle(ul)
        check(a == b and struct.unpack_from("<i", frames_of(h, b)[0].payload)[0] == 0,
              "server: UNLINK resend is not NOTFOUND")
        wr = req(h, h.T_WRITE, sid, 4,
                 struct.pack("<IB", 0, h.WF_TRUNC) + pth(b"/w.bin") + b"abc")
        ex = sv.executed
        sv.handle(wr)
        sv.handle(wr)
        check(sv.executed == ex + 1 and (root / "w.bin").read_bytes() == b"abc",
              "server: WRITE resend executed once")
        # 同じ番号でも中身 (CRC) が違えば別の要求
        wr2 = req(h, h.T_WRITE, sid, 4,
                  struct.pack("<IB", 0, h.WF_TRUNC) + pth(b"/w.bin") + b"xyz")
        sv.handle(wr2)
        check((root / "w.bin").read_bytes() == b"xyz", "server: same seq, other CRC executes")
        # TRUNC の無い書き込みは途中から (作り直さない)
        sv.handle(req(h, h.T_WRITE, sid, 9,
                      struct.pack("<IB", 3, 0) + pth(b"/w.bin") + b"123"))
        check((root / "w.bin").read_bytes() == b"xyz123", "server: WRITE without TRUNC appends at offset")
        # 許されていないパスへの書き込みは ROFS (中身は変わらない)
        rr = frames_of(h, sv.handle(req(h, h.T_WRITE, sid, 10,
                       struct.pack("<IB", 0, h.WF_TRUNC) + pth(b"/many/x") + b"no")))[0]
        check(struct.unpack_from("<i", rr.payload)[0] == h.ERR_ROFS and
              not (root / "many" / "x").exists(), "server: write outside --allow-write -> ROFS")
        # キャッシュは直前の 1 件だけ (前の番号の再送は実行し直す)
        ex = sv.executed
        sv.handle(req(h, h.T_STAT, sid, 5, pth(b"/")))
        sv.handle(ul)
        check(sv.executed == ex + 2, "server: cache holds only the last request")
        # BYE の後は何も送らない。LOG / EXIT は受け取る
        sv.handle(req(h, h.T_BYE, sid, 0, b""))
        check(sv.handle(req(h, h.T_STAT, sid, 6, pth(b"/"))) is None,
              "server: nothing after BYE")
        sv.handle(req(h, h.T_LOG, sid, 0, b"log line\n"))
        sv.handle(req(h, h.T_EXIT, sid, 0, struct.pack("<iII", 3, 0, 0)))
        check(bytes(sv.log_text) == b"log line\n" and sv.exit == (3, 0, 0),
              "server: LOG / EXIT after BYE")
        # 行ごとに結果を空にする (REPL で前の EXIT を取り違えない)
        sv.begin_line()
        check(sv.exit is None and not sv.log_text, "server: begin_line resets")
        # 新しいセッション。古い ID には答えない
        h2 = req(h, h.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 43))
        sid2 = frames_of(h, sv.handle(h2))[0].sid
        check(sid2 not in (0, sid), "server: new session")
        check(sv.handle(req(h, h.T_STAT, sid, 7, pth(b"/"))) is None,
              "server: closed session stays silent")
        # 壊れたペイロードでも落ちずに status で答える
        rr = frames_of(h, sv.handle(req(h, h.T_READ, sid2, 1, b"\x01")))[0]
        check(struct.unpack_from("<i", rr.payload)[0] == h.ERR_INVAL,
              "server: short payload -> INVAL")
        # `..` はワイヤでも断る
        rr = frames_of(h, sv.handle(req(h, h.T_STAT, sid2, 2, pth(b"/../x"))))[0]
        check(struct.unpack_from("<i", rr.payload)[0] == h.ERR_INVAL,
              "server: .. rejected on the wire")


class VClock(object):
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


class ScriptedLine(object):
    """偽の線 (仮想の時計)。ゲストの台本は、ホストが書いた応答を見て次のバイトを
    **届く時刻つきで**積む。serve_line の port と rx の両方を兼ねる。"""

    def __init__(self, h, clock, first, step):
        self.h = h
        self.clock = clock
        self.first = first
        self.step = step
        self.arrivals = []       # (t, bytes)
        self.writes = []
        self.dm = h.Demux()

    def at(self, dt, data):
        self.arrivals.append((self.clock.t + dt, bytes(data)))
        self.arrivals.sort(key=lambda a: a[0])

    # port
    def reset_input_buffer(self):
        pass

    def flush(self):
        pass

    def write(self, data):
        self.writes.append(bytes(data))
        if len(self.writes) == 1:
            self.first(self, data)
            return
        for e in self.dm.feed(data, self.clock.t):
            if e[0] == "frame":
                self.step(self, e[1])

    # rx
    def get(self, timeout):
        """届いた分を返す。無ければ (timeout > 0 なら) 次の到着まで時計を進める。"""
        if timeout > 0 and not any(t <= self.clock.t for t, _ in self.arrivals):
            if self.arrivals:
                self.clock.t = max(self.clock.t, self.arrivals[0][0])
            else:
                self.clock.t += timeout
        out = [(t, d) for t, d in self.arrivals if t <= self.clock.t]
        self.arrivals = [(t, d) for t, d in self.arrivals if t > self.clock.t]
        return out


def guest_script(h, log=b"hsync: ok\nsfs: exit=0\n", code=0):
    state = {"sid": 0}

    def first(line, data):
        line.at(0.01, b"> " + data.rstrip(b"\n") + b"\n" +
                h.encode(h.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 99)))

    def step(line, fr):
        if fr.type == (h.T_HELLO | h.T_RESP):
            state["sid"] = fr.sid
            line.at(0.01, h.encode(h.T_STAT, fr.sid, 1, pth(b"/hello.txt")))
        elif fr.type == (h.T_STAT | h.T_RESP):
            sid = state["sid"]
            line.at(0.01, h.encode(h.T_BYE, sid, 0) + h.encode(h.T_LOG, sid, 0, log) +
                    h.encode(h.T_EXIT, sid, 0, struct.pack("<iII", code, 0, 0)) +
                    b"\x04")
    return first, step


class Args(object):
    timeout = 30.0


def serve(h, line, sv, clock, text="sfs run hsync boot", timeout=30.0):
    return h.serve_line(line, text, sv, timeout, io.StringIO(), now=clock,
                        sleep=lambda s: None, rx=line)


def case_serve_line(h, r):
    with tempfile.TemporaryDirectory(prefix="os32-sfs-serve-") as tmp:
        root = mk_tree(tmp)
        # 正常: HELLO → STAT → BYE / LOG / EXIT → EOT
        clock = VClock()
        first, step = guest_script(h)
        line = ScriptedLine(h, clock, first, step)
        sv = h.Server(h.HostFS(str(root)), now=clock)
        out = io.StringIO()
        res = h.serve_line(line, "sfs run hsync boot", sv, 30.0, out, now=clock,
                           sleep=lambda s: None, rx=line)
        check(res["eot"] and res["exit"] == (0, 0, 0), "serve: exit frame %r" % res)
        check(res["sent"] == 2, "serve: two responses sent")
        check("> sfs run hsync boot" in out.getvalue() and
              "hsync: ok" in out.getvalue(), "serve: echo + log shown")
        check(sv.sid == 0, "serve: session closed at EOT")
        # 次の行 (REPL): EXIT が来なければ exit は None (前の行の結果を持ち越さない)
        clock2 = VClock()

        def first_noexit(ln, data):
            ln.at(0.01, b"> sfs run x\nsfs: session not started (-1)\n\x04")
        line2 = ScriptedLine(h, clock2, first_noexit, lambda ln, fr: None)
        res2 = serve(h, line2, sv, clock2, "sfs run x")
        check(res2["eot"] and res2["exit"] is None, "serve: REPL result reset %r" % res2)
        check(b"session not started" in res2["text"],
              "serve: raw text between the line and EOT is output (HELLO failed)")

        # **20 秒止まった後に、滞留した再送と BYE** (Codex 1)。STAT の処理が
        # 20 秒かかるあいだに、ゲストは再送を 3 回・BYE・LOG・EXIT・EOT を送る。
        # 到着時刻で数えるので、止まっていた処理の応答も再送への応答も送らない。
        clock3 = VClock()
        sv3 = h.Server(h.HostFS(str(root)), now=clock3)
        slow = {"armed": False}
        real_stat = sv3.fs.stat

        def slow_stat(wire):
            if slow["armed"]:
                slow["armed"] = False
                clock3.t += 20.0
            return real_stat(wire)
        sv3.fs.stat = slow_stat
        st3 = {"sid": 0}

        def first3(ln, data):
            ln.at(0.01, b"> sfs run hsync boot\n" +
                  h.encode(h.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 7)))

        def step3(ln, fr):
            if fr.type == (h.T_HELLO | h.T_RESP):
                sid = st3["sid"] = fr.sid
                q = h.encode(h.T_STAT, sid, 1, pth(b"/hello.txt"))
                slow["armed"] = True
                ln.at(0.01, q)
                for k in (1, 2, 3):
                    ln.at(0.01 + 2.8 * k, q)      # 再送 (同じ番号)
                ln.at(12.0, h.encode(h.T_BYE, sid, 0) +
                      h.encode(h.T_LOG, sid, 0, b"sfs: line declared dead\n") +
                      h.encode(h.T_EXIT, sid, 0, struct.pack("<iII", 1, 0, h.XF_DEAD)) +
                      b"\x04")
        line3 = ScriptedLine(h, clock3, first3, step3)
        res3 = serve(h, line3, sv3, clock3)
        check(len(line3.writes) == 2, "stall: only the command line and HELLO_R written "
              "(%d writes)" % len(line3.writes))
        check(res3["eot"] and res3["exit"] == (1, 0, h.XF_DEAD), "stall: EXIT %r" % res3)
        check(res3["late"] >= 3 and sv3.executed == 1 and sv3.replayed == 0,
              "stall: stale resends not even replayed %r executed=%d replayed=%d"
              % (res3, sv3.executed, sv3.replayed))
        check(res3["after_bye"] >= 1, "stall: the slow answer is dropped (BYE ahead)")

        # 処理が期限 (2 秒) を越えた: BYE が無くてもその応答は送らない。
        # ゲストの再送 (到着は新しい) には保存した応答で答える
        clock7 = VClock()
        sv7 = h.Server(h.HostFS(str(root)), now=clock7)
        real7 = sv7.fs.stat
        slow7 = {"armed": False}

        def slow_stat7(wire):
            if slow7["armed"]:
                slow7["armed"] = False
                clock7.t += 2.5
            return real7(wire)
        sv7.fs.stat = slow_stat7
        st7 = {"sid": 0, "resent": False}

        def step7(ln, fr):
            if fr.type == (h.T_HELLO | h.T_RESP):
                sid = st7["sid"] = fr.sid
                q = h.encode(h.T_STAT, sid, 1, pth(b"/hello.txt"))
                slow7["armed"] = True
                ln.at(0.01, q)
                ln.at(2.6, q)                           # 期限切れの後の再送
            elif fr.type == (h.T_STAT | h.T_RESP):
                sid = st7["sid"]
                ln.at(0.01, h.encode(h.T_BYE, sid, 0) +
                      h.encode(h.T_EXIT, sid, 0, struct.pack("<iII", 0, 0, 0)) +
                      b"\x04")
        line7 = ScriptedLine(h, clock7, first3, step7)
        res7 = serve(h, line7, sv7, clock7)
        check(len(line7.writes) == 3 and res7["late"] >= 1 and sv7.replayed == 1,
              "slow: late answer dropped, resend answered from the cache %r writes=%d"
              % (res7, len(line7.writes)))

        # 期限の直前に BYE が届いていたら、期限内でも送らない
        clock4 = VClock()
        sv4 = h.Server(h.HostFS(str(root)), now=clock4)
        real4 = sv4.fs.stat

        def slow4(wire):
            clock4.t += 0.5
            return real4(wire)
        sv4.fs.stat = slow4

        def step4(ln, fr):
            if fr.type == (h.T_HELLO | h.T_RESP):
                sid = fr.sid
                ln.at(0.01, h.encode(h.T_STAT, sid, 1, pth(b"/hello.txt")))
                ln.at(0.2, h.encode(h.T_BYE, sid, 0) + b"\x04")
        line4 = ScriptedLine(h, clock4, first3, step4)
        res4 = serve(h, line4, sv4, clock4)
        check(len(line4.writes) == 2 and res4["after_bye"] == 1,
              "bye-ahead: response not sent after a queued BYE %r" % res4)

        # run_line (rshell_serial) の結果: 子が 0 以外なら 1
        clock5 = VClock()
        first5, step5 = guest_script(h, code=2)
        line5 = ScriptedLine(h, clock5, first5, step5)
        sv5 = h.Server(h.HostFS(str(root)), now=clock5)
        saved = h.serve_line
        h.serve_line = (lambda p, l, s, t, o: saved(p, l, s, t, o, now=clock5,
                                                    sleep=lambda x: None, rx=line5))
        try:
            rc, text = r.run_line(Args(), line5, "sfs run hsync boot", sv5,
                                  out=io.StringIO())
        finally:
            h.serve_line = saved
        check(rc == 1, "run_line: nonzero child -> rc 1")
        # --serve-host 無しの `sfs run` は送らずに断る (Fable m4)
        line6 = ScriptedLine(h, VClock(), first5, step5)
        rc, _ = r.run_line(Args(), line6, "sfs run hsync boot", None, out=io.StringIO())
        check(rc == 2 and line6.writes == [], "run_line: sfs run without --serve-host not sent")


class FakeSerial(object):
    """pyserial の read の振る舞いの模型: read(n) は n バイト揃えば即座に、
    揃わなければ timeout まで待ってから有るだけ返す。**来ているのに待った**
    回数 (0 < 有る < n) を stalls に数える (Fable M1 の 200ms)。"""

    def __init__(self, reply):
        self.buf = bytearray()
        self.reply = reply
        self.writes = []
        self.stalls = 0
        self.timeout = 0.2
        import threading as _th
        self.lock = _th.Lock()

    @property
    def in_waiting(self):
        return len(self.buf)

    def reset_input_buffer(self):
        self.buf = bytearray()

    def flush(self):
        pass

    def write(self, d):
        self.writes.append(bytes(d))
        more = self.reply(bytes(d))
        with self.lock:
            self.buf += more
        return len(d)

    def read(self, n):
        import time as _t
        with self.lock:
            if 0 < len(self.buf) < n:
                self.stalls += 1
            out = bytes(self.buf[:n])
            del self.buf[:n]
        if not out:
            _t.sleep(0.001)          # 実物は timeout まで待つ (空回りしない)
        return out


def case_read_size(h, r):
    port = FakeSerial(lambda d: b"> ver\nOS32\n  Build: x\n\x04")
    for _ in range(5):
        text, ok = r.send_cmd(port, "ver", 1.0)
        check(ok and "Build" in text, "send_cmd: reply")
    check(port.stalls == 0, "send_cmd: no per-request wait (stalls=%d)" % port.stalls)
    port = FakeSerial(lambda d: b"")
    port.buf += b"x" * 37
    rx = h.PortReceiver.__new__(h.PortReceiver)
    rx.port = port
    rx.now = lambda: 1.0
    import queue as _q
    rx.q = _q.Queue()
    got = rx.step()
    check(got == b"x" * 37 and port.stalls == 0, "receiver: reads what is waiting")
    check(h.read_size(port) == 1, "read_size: 1 when nothing waits")

    # **終わった行の受信スレッドが次の行の応答を奪わない** (往復 2、Codex 4)。
    # read が 10 秒待つポート (AidebugPort の作り) でも close は cancel_read で
    # 起こし、スレッドが終わったことを確かめてから戻る
    import threading as _th
    import time as _tm

    class BlockingPort(object):
        def __init__(self):
            self.ev = _th.Event()
            self.buf = bytearray()
            self.waiting = 0
            self.cancelled = False
            self.in_waiting = 0

        def read(self, n):
            self.waiting += 1
            try:
                self.ev.wait(10.0)
                self.ev.clear()
                if self.cancelled:
                    self.cancelled = False
                    return b""
                out = bytes(self.buf[:n])
                del self.buf[:n]
                return out
            finally:
                self.waiting -= 1

        def cancel_read(self):
            self.cancelled = True
            self.ev.set()

        def push(self, d):
            self.buf += d
            self.ev.set()
    bp = BlockingPort()
    rx = h.PortReceiver(bp)
    for _ in range(100):
        if bp.waiting:
            break
        _tm.sleep(0.01)
    check(bp.waiting == 1, "receiver: thread is blocked in read")
    t0 = _tm.monotonic()
    stopped = rx.close()
    check(stopped and rx.stopped() and _tm.monotonic() - t0 < 2.0,
          "receiver: close wakes the read and confirms the thread ended (%.2fs)"
          % (_tm.monotonic() - t0))
    check(bp.waiting == 0, "receiver: no reader left on the port after close")
    bp.push(b"next-line")
    check(bp.read(9) == b"next-line" and rx.q.empty(),
          "receiver: the next reader gets the bytes, the old thread does not")

    # AidebugPort.read は cancel_read で待ちを切る
    class FakeResp(object):
        def __init__(self):
            self.body = b'{"hex": ""}'

        def read(self):
            return self.body

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False
    ap = r.AidebugPort("aidebug:http://x", timeout=3.0, urlopen=lambda *a, **k: FakeResp())
    got = {}
    th = _th.Thread(target=lambda: got.setdefault("d", ap.read(16)))
    t0 = _tm.monotonic()
    th.start()
    _tm.sleep(0.05)
    ap.cancel_read()
    th.join(2.5)
    check(not th.is_alive() and got.get("d") == b"" and _tm.monotonic() - t0 < 1.5,
          "aidebug: cancel_read returns the waiting read (%.2fs)" % (_tm.monotonic() - t0))
    ap.pending += b"kept"
    check(ap.read(4) == b"kept", "aidebug: pending survives a cancel")
    # 実物の PortReceiver (スレッド) で serve_line を回し、要求ごとに待たない
    with tempfile.TemporaryDirectory(prefix="os32-sfs-rs-") as tmp:
        root = mk_tree(tmp)
        sv = h.Server(h.HostFS(str(root)))
        dm = h.Demux()
        st = {"sid": 0, "n": 0}

        def reply(d):
            if d.endswith(b"\n") and d.startswith(b"sfs"):
                return (b"> " + d + h.encode(h.T_HELLO, 0, 0,
                                             struct.pack("<HHI", 1, 512, 3)))
            out = b""
            for e in dm.feed(d, 0.0):
                if e[0] != "frame":
                    continue
                fr = e[1]
                if fr.type == (h.T_HELLO | h.T_RESP):
                    st["sid"] = fr.sid
                if st["n"] < 20:
                    st["n"] += 1
                    out += h.encode(h.T_STAT, st["sid"], st["n"], pth(b"/hello.txt"))
                else:
                    sid = st["sid"]
                    out += (h.encode(h.T_BYE, sid, 0) +
                            h.encode(h.T_EXIT, sid, 0, struct.pack("<iII", 0, 0, 0)) +
                            b"\x04")
            return out
        port = FakeSerial(reply)
        res = h.serve_line(port, "sfs run ls /host", sv, 10.0, io.StringIO())
        check(res["eot"] and res["sent"] == 21, "receiver: 20 requests answered %r" % res)
        check(port.stalls == 0, "receiver: no per-request wait (stalls=%d)" % port.stalls)


def case_outside_session(h, r):
    """`sfs run` の行の外ではフレームを解釈しない (cat の本文に ENQ / EOT)。"""
    with tempfile.TemporaryDirectory(prefix="os32-sfs-outside-") as tmp:
        root = mk_tree(tmp)
        sv = h.Server(h.HostFS(str(root)))
        hello = h.encode(h.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 5))
        stat = h.encode(h.T_STAT, 0x1234, 1, pth(b"/hello.txt"))

        class CatPort(object):
            def __init__(self):
                self.writes = []
                self.q = bytearray()

            def reset_input_buffer(self):
                pass

            def flush(self):
                pass

            def write(self, d):
                self.writes.append(bytes(d))
                if len(self.writes) == 1:
                    self.q += b"> cat f\n" + hello + stat + b"body\x04"

            def read(self, n):
                d = bytes(self.q[:n])
                del self.q[:n]
                return d
        port = CatPort()
        rc, text = r.run_line(Args(), port, "cat f", sv, out=io.StringIO())
        check(len(port.writes) == 1, "outside: host answered a frame in a cat body")
        check(sv.executed == 0 and sv.sid == 0, "outside: server untouched")
        check(rc == 0 and text.startswith("> cat f"), "outside: normal reply")
        # 行の判定は rsh_sfs_child と同じ
        for line, want in (("sfs run hsync boot", "hsync boot"),
                           ("  sfs\trun  ls /host", "ls /host"),
                           ("sfs run", None), ("sfsrun x", None),
                           ("echo x && sfs run y", None), ("sfs runx y", None)):
            check(h.sfs_child(line) == want, "sfs_child(%r)" % line)


def case_aidebug_port(h, r):
    """NP21/W の COM1 を HTTP で読み書きする口 (T2 用、ini は変えない)。"""
    import json

    class Resp(object):
        def __init__(self, body):
            self.body = body

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

        def read(self):
            return self.body
    state = {"tx": [b"\x05SF" + b"a" * 10, b"", b"\x04"], "written": b""}

    def urlopen(req, timeout=0):
        url = req if isinstance(req, str) else req.full_url
        if url.endswith("/api/serial/read"):
            chunk = state["tx"].pop(0) if state["tx"] else b""
            return Resp(json.dumps({"ok": True, "len": len(chunk),
                                    "hex": chunk.hex()}).encode())
        if url.endswith("/api/serial/write"):
            state["written"] += bytes.fromhex(req.data.decode())
            return Resp(b'{"ok":true,"written":1}')
        raise AssertionError(url)
    port = r.AidebugPort("http://x:8025/", timeout=0.05, urlopen=urlopen)
    check(port.read(4) == b"\x05SFa", "aidebug: read splits a chunk")
    check(port.read(100) == b"a" * 9, "aidebug: rest of the chunk")
    check(port.read(10) == b"\x04", "aidebug: keeps polling past an empty chunk")
    check(port.read(10) == b"", "aidebug: nothing within the timeout")
    port.write(b"\x05SF\x00\xff")
    check(state["written"] == b"\x05SF\x00\xff", "aidebug: write hex")
    state["tx"] = [b"old", b"older", b""]
    port.reset_input_buffer()
    check(state["tx"] == [] and port.read(1) == b"", "aidebug: reset drains")
    check(isinstance(r.open_port("aidebug:http://127.0.0.1:8025", 9600),
                     r.AidebugPort), "aidebug: open_port prefix")


PY_CASES = {
    "aidebug_port": case_aidebug_port,
    "ntpath": case_ntpath,
    "read_size": case_read_size,
    "demux": case_demux,
    "paths": case_paths,
    "server": case_server,
    "serve_line": case_serve_line,
    "outside_session": case_outside_session,
}


def run_py(h, r, names, quiet=False, first_fail=False):
    global FAILED
    bad = 0
    for name in names:
        FAILED = []
        try:
            PY_CASES[name](h, r)
        except Exception as e:  # noqa: BLE001
            FAILED.append("raised %r" % (e,))
        rc = 1 if FAILED else 0
        if not quiet:
            for f in FAILED:
                print(f"  FAIL {name}: {f}", flush=True)
            print(f"EXIT py:{name}={rc}", flush=True)
        bad += rc
        if bad and first_fail:
            break
    return bad


# ============================================================================
#  C ⇔ Python の照合と結合
# ============================================================================
def cross_check(exe, h):
    bad = 0
    out = mutpar.run_timeout([str(exe), "vectors"], capture_output=True, text=True,
                         check=True).stdout.split()
    pl = bytes((i * 31 + 7) & 0xFF for i in range(512))
    want = [h.encode(h.T_HELLO, 0, 0, pl[:8]),
            h.encode(h.T_READ, 0xDEADBEEF, 1, b""),
            h.encode(h.T_WRITE | h.T_RESP, 0x01020304, 0xFFFE, pl),
            h.encode(h.T_ERR, 0x80000001, 0x8000, pl[:4])]
    for i, w in enumerate(want):
        if bytes.fromhex(out[i]) != w:
            print(f"  FAIL xc: C frame {i} != Python frame", flush=True)
            bad += 1
    # Python の列を C の受信器で読む (ごみ・壊れたフレームを挟む)
    bad_frame = bytearray(want[1])
    bad_frame[-2] ^= 1
    stream = b"\x41\x05" + want[2] + bytes(bad_frame) + want[3]
    res = mutpar.run_timeout([str(exe), "decode"], input=stream.hex() + "\n",
                         capture_output=True, text=True, check=True).stdout
    lines = res.strip().split("\n")
    if lines[-1] != "bad_crc=1 bad_len=0 frames=2":
        print(f"  FAIL xc: C decode summary {lines[-1]!r}", flush=True)
        bad += 1
    if not lines[0].startswith("93 01020304 fffe 512 " + pl[:4].hex()):
        print(f"  FAIL xc: C decode frame {lines[0][:40]!r}", flush=True)
        bad += 1
    print(f"EXIT xc:frames={int(bool(bad))}", flush=True)
    return bad


def header_consts(text, names):
    got = {}
    for n in names:
        m = re.search(r"#define\s+%s\s+\(?([0-9A-Fa-fx]+)" % n, text)
        got[n] = int(m.group(1).rstrip("UL"), 0) if m else None
    return got


def const_check(h):
    bad = 0
    proto = (ROOT / "fs/sfs_proto.h").read_text(encoding="utf-8")
    pairs = {"SFS_ENQ": h.ENQ, "SFS_VERSION": h.VERSION, "SFS_HDR_LEN": h.HDR_LEN,
             "SFS_CRC_LEN": h.CRC_LEN, "SFS_MAX_PAYLOAD": h.MAX_PAYLOAD,
             "SFS_T_HELLO": h.T_HELLO, "SFS_T_BYE": h.T_BYE,
             "SFS_T_STAT": h.T_STAT, "SFS_T_LIST": h.T_LIST,
             "SFS_T_READ": h.T_READ, "SFS_T_WRITE": h.T_WRITE,
             "SFS_T_MKDIR": h.T_MKDIR, "SFS_T_RMDIR": h.T_RMDIR,
             "SFS_T_UNLINK": h.T_UNLINK, "SFS_T_RENAME": h.T_RENAME,
             "SFS_T_LOG": h.T_LOG, "SFS_T_EXIT": h.T_EXIT,
             "SFS_T_RESP": h.T_RESP, "SFS_T_ERR": h.T_ERR,
             "SFS_WF_TRUNC": h.WF_TRUNC, "SFS_KIND_FILE": h.KIND_FILE,
             "SFS_KIND_DIR": h.KIND_DIR, "SFS_KIND_OTHER": h.KIND_OTHER,
             "SFS_XF_NOT_QUIET": h.XF_NOT_QUIET, "SFS_XF_DEAD": h.XF_DEAD,
             "SFS_PATH_MAX": h.PATH_MAX, "SFS_FIRST_BYTE_MS": int(h.FIRST_BYTE_S * 1000),
             "SFS_GAP_MS": int(h.GAP_S * 1000)}
    got = header_consts(proto, pairs)
    for n, v in pairs.items():
        if got[n] != v:
            print(f"  FAIL const: {n} C={got[n]} Python={v}", flush=True)
            bad += 1
    shared = (ROOT / "sdk/include/os32/os32_kapi_shared.h").read_text(encoding="utf-8")
    for n, v in (("OS32_ERR_IO", h.ERR_IO), ("OS32_ERR_NOTFOUND", h.ERR_NOTFOUND),
                 ("OS32_ERR_EXIST", h.ERR_EXIST), ("OS32_ERR_NOTDIR", h.ERR_NOTDIR),
                 ("OS32_ERR_NOTEMPTY", h.ERR_NOTEMPTY), ("OS32_ERR_ISDIR", h.ERR_ISDIR),
                 ("OS32_ERR_INVAL", h.ERR_INVAL), ("OS32_ERR_STALE", h.ERR_STALE),
                 ("OS32_ERR_NOSPC", h.ERR_NOSPC),
                 ("OS32_ERR_NAMETOOLONG", h.ERR_NAMETOOLONG)):
        m = re.search(r"#define\s+%s\s+(-?\d+)" % n, shared)
        if not m or int(m.group(1)) != v:
            print(f"  FAIL const: {n}", flush=True)
            bad += 1
    boot = (ROOT / "boot/boot_defs.h").read_text(encoding="utf-8")
    inc = (ROOT / "userland/system/hsync_bootold.inc").read_text(encoding="utf-8")
    for a, b in (("VK32_MAGIC", "HBO_VK32_MAGIC"), ("VK32_VERSION", "HBO_VK32_VERSION"),
                 ("VK32_MAX_ENTRIES", "HBO_VK32_MAX_ENTRIES"),
                 ("VK32_COMMON_SIZE", "HBO_VK32_COMMON_SIZE"),
                 ("VK32_ENTRY_SIZE", "HBO_VK32_ENTRY_SIZE")):
        va = header_consts(boot, [a])[a]
        vb = header_consts(inc, [b])[b]
        if va is None or va != vb:
            print(f"  FAIL const: {a}={va} vs {b}={vb}", flush=True)
            bad += 1
    # image_crc の位置の式が boot_defs.h と同じ (16 + 20n + 4)
    if "VK32_OFF_IMAGE_CRC(n)  (VK32_OFF_IMAGE_SIZE(n) + 4UL)" not in boot or \
            "(VK32_COMMON_SIZE + (u32)(n) * (VK32_ENTRY_SIZE + 4UL))" not in boot:
        print("  FAIL const: boot_defs.h の image_crc の位置の式が変わった", flush=True)
        bad += 1
    print(f"EXIT xc:consts={int(bool(bad))}", flush=True)
    return bad


def integration(exe, h):
    """C の受け手 (実物) と Python のホスト (実物) を実時間のパイプでつなぐ。

    RENAME の 1 回目の応答を落とし (ゲストは同じ番号で再送 → ホストは
    **実行し直さずに**保存した応答を返す。実行し直すと NOTFOUND になる)、
    READ の 1 回目の応答の CRC を壊す。
    """
    import time
    bad = []
    with tempfile.TemporaryDirectory(prefix="os32-sfs-int-") as tmp:
        root = mk_tree(tmp)
        (root / "hello.txt").write_bytes(bytes(range(256)) * 20)
        sv = h.Server(h.HostFS(str(root), allow_write=[
            "out.bin", "out2.bin", "newdir", "gone.txt"]))
        p = subprocess.Popen([str(exe), "pipe"], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        os.set_blocking(p.stdout.fileno(), False)
        dm = h.Demux()
        drop = {h.T_RENAME: 1}
        corrupt = {h.T_READ: 1}
        dropped = corrupted = 0
        t_end = time.monotonic() + 60
        while p.poll() is None and time.monotonic() < t_end:
            rd, _, _ = select.select([p.stdout], [], [], 0.05)
            data = b""
            if rd:
                try:
                    data = os.read(p.stdout.fileno(), 4096)
                except BlockingIOError:
                    data = b""
            for e in dm.feed(data, time.monotonic()):
                if e[0] != "frame":
                    continue
                resp = sv.handle(e[1])
                if resp is None:
                    continue
                t = e[1].type
                if drop.get(t):
                    drop[t] -= 1
                    dropped += 1
                    continue
                if corrupt.get(t):
                    corrupt[t] -= 1
                    corrupted += 1
                    resp = bytearray(resp)
                    resp[-1] ^= 0x55
                    resp = bytes(resp)
                try:
                    p.stdin.write(resp)
                    p.stdin.flush()
                except BrokenPipeError:
                    break
        if p.poll() is None:
            p.kill()
            bad.append("child did not finish")
        err = p.stderr.read().decode()
        vals = dict()
        for line in err.splitlines():
            parts = line.split()
            if parts:
                vals[parts[0]] = dict(kv.split("=", 1) for kv in parts[1:] if "=" in kv)
        hello = (root / "hello.txt").read_bytes()
        exp = {
            ("stat", "rc"): "0", ("stat", "size"): str(len(hello)),
            ("read", "rc"): str(len(hello)),
            ("read", "crc"): "%08x" % (zlib.crc32(hello) & 0xFFFFFFFF),
            ("list", "rc"): "0", ("list", "n"): "150",
            ("write", "rc"): "3000", ("rename", "rc"): "0",
            ("mkdir", "rc"): "0", ("unlink", "rc"): "0",
            ("escape", "rc"): str(h.ERR_INVAL), ("nope", "rc"): str(h.ERR_NOTFOUND),
        }
        for (k, f), v in exp.items():
            got = vals.get(k, {}).get(f)
            if got != v:
                bad.append("%s.%s = %r (want %r)" % (k, f, got, v))
        if (root / "out.bin").exists() or not (root / "out2.bin").exists():
            bad.append("rename not applied exactly once")
        elif (root / "out2.bin").read_bytes() != bytes((i * 3) & 0xFF for i in range(3000)):
            bad.append("written content differs")
        if not (root / "newdir").is_dir() or (root / "gone.txt").exists():
            bad.append("mkdir / unlink not applied")
        if dropped != 1 or corrupted != 1 or sv.replayed < 1:
            bad.append("faults: dropped=%d corrupted=%d replayed=%d" %
                       (dropped, corrupted, sv.replayed))
        st = vals.get("stats", {})
        if int(st.get("resends", 0)) < 2 or int(st.get("bad_crc", 0)) < 1:
            bad.append("guest stats %r" % st)
    for b in bad:
        print(f"  FAIL xc:integration: {b}", flush=True)
    print(f"EXIT xc:integration={int(bool(bad))}", flush=True)
    return len(bad)


# ============================================================================
#  変異
# ============================================================================
IDENTITY = "IDENTITY"
C_MUTATIONS = [
    ("fs/sfs_proto.c", r"(    /\* CRC は種別 \(off 3\) からペイロードの末尾まで)",
     r"\1", IDENTITY),
    ("fs/sfs_proto.c", r"crc = sfs_crc32\(out \+ 3, \(u32\)\(SFS_HDR_LEN - 3 \+ len\)\);",
     "crc = sfs_crc32(out + 4, (u32)(SFS_HDR_LEN - 4 + len));",
     "CRC の範囲から種別を外す (Python と食い違う)"),
    ("fs/sfs_proto.c", r"        if \(len > SFS_MAX_PAYLOAD\) \{\n            d->bad_len\+\+;",
     "        if (len > 0xFFFF) {\n            d->bad_len++;",
     "受信器が長さ欄を上限と照合しない"),
    ("fs/sfs_proto.c", r"    if \(len > SFS_MAX_PAYLOAD\) return 0;\n    out\[0\]",
     "    out[0]", "組み立てが上限を越えるペイロードを断らない"),
    ("fs/sfs_proto.c", r"if \(\*seq >= SFS_SEQ_LAST\) return -1;",
     "if (*seq >= 0xFFFFu) *seq = 0;", "番号を周回させる"),
    ("fs/sfs_proto.c", r"\(u32\)SFS_FIRST_BYTE_MS \+ frame_ms \+ \(u32\)SFS_SLACK_MS",
     "(u32)SFS_SLACK_MS", "最初のバイトまでの 2 秒を期限に入れない"),
    ("fs/sfs_client.c", r"            if \(sfs_match\(c, &f, type, seq, hello, nonce\)\) \{\n"
                        r"                \*got = f;\n                return 0;\n            \}\n"
                        r"            c->stray\+\+;",
     "            if (sfs_match(c, &f, type, seq, hello, nonce)) {\n"
     "                *got = f;\n                return 0;\n            }\n"
     "            c->stray++;\n            start = t;",
     "受け取らなかったフレームで期限を延ばす"),
    ("fs/sfs_client.c", r"    if \(f->sid != c->sid \|\| f->seq != seq\) return 0;",
     "    if (f->sid != c->sid) return 0;", "番号違いの応答を取る"),
    ("fs/sfs_client.c", r"    if \(f->sid != c->sid \|\| f->seq != seq\) return 0;",
     "    if (f->seq != seq) return 0;", "セッション違いの応答を取る"),
    ("fs/sfs_client.c", r"        if \(sfs_get32\(f->payload \+ 8\) != nonce\) return 0;\n",
     "", "HELLO の nonce を見ない (前のセッションの遅れた HELLO を取る)"),
    ("fs/sfs_client.c", r"    if \(c->fails >= SFS_DEAD_AFTER && c->dead == SFS_DEAD_NONE\)",
     "    if (0)", "連続失敗で線を死んだと見なさない"),
    ("fs/sfs_client.c", r"        c->dead = SFS_DEAD_STALE;\n", "",
     "ERR (未知のセッション) で止まらない"),
    ("fs/sfs_client.c", r"    for \(attempt = 0; attempt <= SFS_RETRIES; attempt\+\+\) \{",
     "    for (attempt = 0; attempt < 1; attempt++) {", "再送しない"),
    ("fs/sfs_client.c", r"        if \(c->io->put\(c->ctx, c->tx, flen\) != 0\) \{\n            c->timeouts\+\+;",
     "        if (attempt > 0) sfs_encode(c->tx, type, c->sid, (u16)(seq + attempt), c->tx + SFS_HDR_LEN, (u16)(flen - SFS_HDR_LEN - SFS_CRC_LEN));\n"
     "        if (c->io->put(c->ctx, c->tx, flen) != 0) {\n            c->timeouts++;",
     "再送で番号を変える (ホストのキャッシュが効かず副作用が二重になる)"),
    ("fs/sfs_client.c", r"    if \(!c->io->can_wait\(c->ctx\)\) return OS32_ERR_IO;\n    /\* 番号は",
     "    /* 番号は", "IF=0 でも待つ"),
    ("fs/sfs_client.c", r"        if \(\(u32\)\(t - start\) >= limit\) return 0;",
     "", "静まるのを待つ口に上限が無い"),
    ("fs/serialfs.c", r"    if \(!g_permit_cli \|\| g_mounted\) return \(void \*\)0;",
     "    if (g_mounted) return (void *)0;", "セッションの外からマウントできる"),
    ("fs/serialfs.c", r"    if \(VFS_MOUNT_DEV_TYPE\(dev\) != VFS_DEV_SERIAL\) return \(void \*\)0;",
     "", "デバイス種別を見ない"),
    ("fs/serialfs.c", r"if \(\(u32\)rc > want \|\| \(u32\)rc != blen\) return OS32_ERR_IO;",
     "if ((u32)rc != blen) return OS32_ERR_IO;", "要求より多く返す応答を通す"),
    ("fs/serialfs.c", r"        if \(\(u16\)rc < want\) break;          /\* 短い = EOF \*/",
     "", "短い応答を EOF と見ない"),
    ("fs/serialfs.c", r"                if \(ch == 0 \|\| ch == '/'\) return OS32_ERR_IO;",
     "", "名前の '/' を通す"),
    ("fs/serialfs.c", r"        if \(next <= cookie\) return OS32_ERR_IO;",
     "", "cookie が進まない相手で回り続ける"),
    ("fs/serialfs.c", r"    if \(kind == SFS_KIND_DIR\) return OS32_ERR_ISDIR;",
     "", "get_file_size がディレクトリにサイズを返す"),
    ("fs/serialfs.c", r"        pl\[4\] = \(u8\)\(\(first && first_trunc\) \? SFS_WF_TRUNC : 0\);",
     "        pl[4] = (u8)(first_trunc ? SFS_WF_TRUNC : 0);",
     "write_file の 2 回目以降も TRUNC (前の塊を消す)"),
    ("fs/serialfs.c", r"    if \(size == 0 && !first_trunc\) return 0;\n", "",
     "0 バイトの write_stream が要求を出す"),
    ("fs/serialfs.c", r"    default:\n        return OS32_ERR_IO;\n    \}\n\}",
     "    default:\n        return st;\n    }\n}", "知らない status を畳まない"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(at_line_start && !followed\) return RSH_ESC_EXIT;",
     "    if (at_line_start) return RSH_ESC_EXIT;",
     "行頭の ESC は後ろに続きがあっても閉じる"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(at_line_start && !followed\) return RSH_ESC_EXIT;",
     "    return RSH_ESC_EXIT;", "シリアルの ESC はどこでも閉じる (旧挙動)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(p\[0\] != 'r' \|\| p\[1\] != 'u' \|\| p\[2\] != 'n' \|\| !rsh_is_space\(p\[3\]\)\)",
     "    if (p[0] != 'r' || p[1] != 'u' || p[2] != 'n')",
     "`sfs runx` を受ける"),
    ("userland/system/hsync_bootold.inc",
     r"\*off = HBO_VK32_COMMON_SIZE \+ cnt \* \(HBO_VK32_ENTRY_SIZE \+ 4UL\) \+ 4UL;",
     "*off = HBO_VK32_COMMON_SIZE + cnt * HBO_VK32_ENTRY_SIZE + 4UL;",
     "image_crc の位置をエントリの CRC 表抜きで数える"),
    ("userland/system/hsync_bootold.inc",
     r"    if \(booted_crc != disk_crc\) return HBO_REFUSE_DIFF;\n", "",
     "起動していない版でも .old を作る"),
    ("userland/system/hsync_bootold.inc",
     r"    if \(stored_crc != disk_crc\) return HBO_REFUSE_CORRUPT;\n", "",
     "image_crc 欄が壊れた版でも .old を作る (起動しない .old)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(!l->junk\) return RSH_LINE_DONE;\n",
     "    return RSH_LINE_DONE;\n", "拒否した行が受信の間で解ける (残りが次の行として走る)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(!l->junk\) return RSH_LINE_DONE;\n    /\* 拒否した行は沈黙では閉じない",
     "    if (!l->junk) return RSH_LINE_DONE;\n    { static int n; if (++n > 200) return RSH_LINE_DONE; }\n    /* 拒否した行は沈黙では閉じない",
     "拒否した行が 2 秒の沈黙で解ける (往復 2、Codex 3)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(ch == '\\n' \|\| ch == '\\r'\) return RSH_LINE_DONE;",
     r"    if ((ch == '\\n' || ch == '\\r') && from_serial) return RSH_LINE_DONE;",
     "本体キーボードの Enter で拒否した行を閉じられない (回復の口が無い)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(cls == RSH_ESC_EXIT\) return RSH_LINE_EXIT;\n    l->bytes\+\+;",
     "    l->bytes++;\n    if (cls == RSH_ESC_EXIT) return RSH_LINE_EXIT;",
     "行頭の単独 ESC を数える (閉じるときに余分な EOT を返す)"),
    ("userland/shell/serial_watchdog.c",
     r"        if \(l->junk && w &&\n            serial_watchdog_poll",
     "        if (0 && l->junk && w &&\n            serial_watchdog_poll",
     "拒否した行の行末を待つ間は番犬を見ない (期限で旧速度へ戻らない、Codex P2)"),
    ("userland/shell/serial_watchdog.c",
     r"            rsh_line_begin\(l, l->buf, l->cap\);\n            return RSH_LINE_REVERT;",
     "            return RSH_LINE_REVERT;",
     "番犬で戻したときに拒否していた行を捨てない"),
    ("userland/shell/serial_watchdog.c",
     r"        if \(l->junk && w &&",
     "        if (w &&",
     "拒否していない行の途中でも番犬で切る (ack の行を途中で捨てる)"),
    ("userland/system/hsync_bootold.inc",
     r"    if \(!have_info\) return HBO_REFUSE_INFO;\n", "",
     "起動したイメージの記録が無くても .old を作る"),
    ("userland/system/hsync_bootold.inc",
     r"            if \(k > len - i\) k = len - i;\n            state = crc32_core_update\(state, zero4, k\);",
     "            if (k > len - i) k = len - i;\n            state = crc32_core_update(state, chunk + i, k);",
     "image_crc 欄を 0 にせずに CRC を取る"),
]
GATE_MUTATIONS = [
    ("drivers/serial.c", r"    if \(s_gate\) \{\n        ser_hold_push\(\(u8\)c\);",
     "    if (0) {\n        ser_hold_push((u8)c);",
     "ゲート中の serial_putchar が線へ出る (console の複写がフレームに混ざる)"),
    ("drivers/serial.c", r"    if \(s_gate\) return -1;         /\* rshell / kbd.c / KAPI にフレームを渡さない \*/\n",
     "", "ゲート中の serial_trygetchar が受信を渡す (rshell にフレームが漏れる)"),
    ("drivers/serial.c", r"    ser_head = 0;\n    ser_tail = 0;\n    ser_count = 0;\n    s_gate = on",
     "    if (on) { ser_head = 0; ser_tail = 0; ser_count = 0; }\n    s_gate = on",
     "下ろすときに受信リングを空にしない (遅れた応答が rshell に入る)"),
    ("drivers/serial.c", r"        s_hold_head = \(s_hold_head \+ 1\) % \(u32\)SER_HOLD_SIZE;\n        s_hold_count--;\n        s_hold_dropped\+\+;\n    \}\n    s_hold\[",
     "        s_hold_dropped++;\n        irq_restore(f);\n        return;\n    }\n    s_hold[",
     "保留リングが溢れたら新しい方を捨てる (結果行と exit が消える)"),
    ("drivers/serial.c", r"    if \(s_gate\) \{\n        kprintf\(0x0E, \"\[ser\] refuse serial_init during SerialFS session\\n\"\);\n        return;\n    \}",
     "", "セッション中に速度を変えられる"),
    ("drivers/serial.c", r"            if \(sts & serial_oe_mask\(s_setup.mode\)\) ser_err_oe\+\+;\n            if \(sts & serial_fe_mask",
     "            if (sts & serial_fe_mask", "ISR が OE を数えない"),
    ("drivers/serial.c", r"            if \(!\(sts & s_mask_rxrdy\)\) break;\n            \(void\)inp\(s_port_data\);",
     "            break;", "ゲートの上げ下げで UART / FIFO の残りを読み捨てない"),
]
SESSION_MUTATIONS = [
    ("fs/serialfs_session.c", r"(static SfsClient g_cli;)", r"\1", IDENTITY),
    ("fs/serialfs_session.c",
     r"    quiet = sfs_client_quiesce\(&g_cli, SFS_QUIET_MS, SFS_QUIET_MAX_MS\);",
     "    quiet = g_cli.sid ? sfs_client_quiesce(&g_cli, SFS_QUIET_MS, SFS_QUIET_MAX_MS) : 1;",
     "HELLO が通らなかった回は隔離せずに下ろす (遅れて届く相手の応答が rshell に入る)"),
    ("fs/serialfs_session.c", r"    sfs_flush_hold_raw\(\);\n    g_cli.sid = 0;",
     "    if (g_cli.sid == 0) sfs_flush_hold_raw();\n    g_cli.sid = 0;",
     "EXIT からゲートを下ろすまでに保留へ入った文字を捨てる"),
    ("fs/serialfs_session.c",
     r"    if \(g_cli.sid != 0\) \(void\)sfs_client_oneway\(&g_cli, SFS_T_BYE, 0, 0\);\n",
     "", "BYE を送らない"),
    ("fs/serialfs_session.c", r"        flags \|= SFS_XF_NOT_QUIET;\n", "",
     "隔離の上限に達しても EXIT の flags に出さない"),
    ("fs/serialfs_session.c",
     r"    if \(vfs_fstype\(SERIALFS_PREFIX\)\[0\] != '\\0'\) return OS32_ERR_BUSY;\n", "",
     "/host が使われていても (HostDrv) 始める"),
    ("fs/serialfs_session.c", r"    if \(!_irq_enabled\(\)\) return OS32_ERR_INVAL;\n", "",
     "IF=0 のまま始める (tick が進まず期限が来ない)"),
]
PY_MUTATIONS = [
    ("tools/serialfs_host.py", r"(    def feed\(self, data, now\):)", r"\1", IDENTITY),
    ("tools/serialfs_host.py", r"            if r\[0\] == \"bad\":\n                self.bad_frames \+= 1\n",
     "            if r[0] == \"bad\":\n                self.bad_frames += 1\n                i += 1\n                continue\n",
     "CRC の壊れたフレームを文字として読み直す (中の 0x04 で行が終わる)"),
    ("tools/serialfs_host.py", r"        if key == self.cache_key:\n            self.replayed \+= 1\n            return self.cache_resp\n",
     "", "応答のキャッシュが無い (再送で RENAME / MKDIR / UNLINK が二重に走る)"),
    ("tools/serialfs_host.py", r"        key = \(fr.sid, fr.seq, fr.crc\)\n        if key == self.cache_key:",
     "        key = (fr.sid, fr.seq)\n        if key == self.cache_key:",
     "キャッシュの一致に要求の CRC を見ない"),
    ("tools/serialfs_host.py", r"            if c in \(b\"\.\", b\"\.\.\"\) or",
     "            if c in (b\".\",) or", "`..` を通す"),
    ("tools/serialfs_host.py", r"        if not self.contained\(path\):\n            raise SfsError\(ERR_INVAL\)",
     "        pass", "パスの作りで realpath の検査をしない (symlink で脱出)"),
    ("tools/serialfs_host.py", r"            common = self.pm.commonpath\(\[nc\(self.root\), nc\(real\)\]\)",
     "            common = nc(self.root) if nc(real).startswith(nc(self.root)) else b\"\"",
     "根の判定を文字列の前方一致に戻す (C:\\hostile を C:\\host の中と読む)"),
    ("tools/serialfs_host.py", r"                    nfd = os.open\(c, os.O_RDONLY \| os.O_DIRECTORY \|\n                                  os.O_NOFOLLOW, dir_fd=fd\)",
     "                    nfd = os.open(c, os.O_RDONLY | os.O_DIRECTORY, dir_fd=fd)",
     "固定の作りで symlink の要素を辿る (差し替えで根の外へ)"),
    ("tools/serialfs_host.py", r"            if self.pm.sep == \"\\\\\" and b\":\" in c:\n                raise SfsError\(ERR_INVAL\)\n",
     "", "Windows で `:` (代替データストリーム) を通す"),
    ("tools/serialfs_host.py", r"        if not self._allowed\(comps\):\n            raise SfsError\(ERR_ROFS\)",
     "        pass", "書き込みの既定の禁止が無い"),
    ("tools/serialfs_host.py", r"        if not self._allowed\(rc\):\n            raise SfsError\(ERR_ROFS\)\n        return path",
     "        return path", "パスの作りで、解決した実体に許可を当てない (リンク越しに --allow-write の外へ書ける)"),
    ("tools/serialfs_host.py", r"                fd = os.open\(comps\[-1\], fl, 0o644, dir_fd=pfd\)",
     "                fd = os.open(comps[-1], fl & ~os.O_NOFOLLOW, 0o644, dir_fd=pfd)",
     "固定の作りの書き込みが最後の要素の symlink を辿る (リンク越しに --allow-write の外へ書ける)"),
    ("tools/serialfs_host.py", r"        fd = os.dup\(self.root_fd\)",
     "        fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY)",
     "起点を操作のたびにパスで開き直す (根を差し替えられると外へ出る)"),
    ("tools/serialfs_host.py", r"            self.root_fd = os.open\(self.root, os.O_RDONLY \| os.O_DIRECTORY \|\n                                   os.O_NOFOLLOW\)",
     "            self.root_fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY)",
     "起点の open に O_NOFOLLOW が無い"),
    ("tools/serialfs_host.py", r"                n = self._pwrite_all\(fd, data, offset\)",
     "                n = len(data); os.pwrite(fd, data, offset)",
     "pwrite の戻り値を見ずに全量成功として返す"),
    ("tools/serialfs_host.py", r"            if n <= 0:\n                raise SfsError\(ERR_IO\)\n",
     "            if n < 0:\n                raise SfsError(ERR_IO)\n            if n == 0:\n                return done\n",
     "進まない pwrite を成功として返す"),
    ("tools/serialfs_host.py", r"        while self.thread.is_alive\(\):\n            if cancel is not None:",
     "        self.thread.join(0.2)\n        return True\n        while self.thread.is_alive():\n            if cancel is not None:",
     "close が終わりを確かめずに戻る (終わった行の受信スレッドが次の行の応答を奪う)"),
    ("tools/rshell_serial.py", r"        self._cancel = True\n",
     "        pass\n", "AidebugPort の cancel_read が read を起こさない"),
    ("tools/serialfs_host.py", r"                if fr.type in REQUEST_TYPES and now\(\) - t > FIRST_BYTE_S:",
     "                if False:", "到着から期限を過ぎた要求も実行する (再送が二重に走る)"),
    ("tools/serialfs_host.py", r"                pump\(0\)                         # 処理中に届いた分\n",
     "", "送る前に、処理中に届いた BYE / EOT を取り込まない"),
    ("tools/serialfs_host.py", r"                if fr.sid in server.closed or closed_ahead\(fr.sid\):",
     "                if fr.sid in server.closed:", "先に届いている BYE を見ない"),
    ("tools/serialfs_host.py", r"                if now\(\) - t > FIRST_BYTE_S:\n                    stats\[\"late\"\] \+= 1\n                    continue\n                port.write",
     "                port.write", "送る直前に期限を見ない"),
    ("tools/serialfs_host.py", r"    return n if n > 0 else 1\n\n\nclass PortReceiver",
     "    return MAX_FRAME\n\n\nclass PortReceiver", "受信で MAX_FRAME を待つ (要求ごとに 200ms 止まる)"),
    ("tools/serialfs_host.py", r"        self.log_text = bytearray\(\)\n        self.exit = None\n\n    # 要求",
     "        pass\n\n    # 要求", "行ごとに結果を空にしない (REPL で前の EXIT を取り違える)"),
    ("tools/rshell_serial.py", r"        chunk = port.read\(read_size\(port\)\)",
     "        chunk = port.read(256)", "send_cmd が 256 バイト待つ (要求ごとに 200ms)"),
    ("tools/rshell_serial.py", r"    if is_sfs and server is None:",
     "    if False:", "--serve-host 無しでも `sfs run` を送る"),
    ("tools/serialfs_host.py", r"        if fr.sid in self.closed:\n            return None ",
     "        if False:\n            return None ", "BYE の後も答える"),
    ("tools/serialfs_host.py", r"        if fr.sid == 0 or fr.sid != self.sid:\n            self.stale \+= 1\n",
     "        if False:\n            self.stale += 1\n", "未知のセッションに普通に答える"),
    ("tools/serialfs_host.py", r"        if flags & WF_TRUNC:\n            mode = \"wb\"",
     "        if True:\n            mode = \"wb\"", "TRUNC の無い書き込みでも作り直す"),
    ("tools/rshell_serial.py", r"    if is_sfs:\n        r = serialfs_host.serve_line",
     "    if server is not None:\n        r = serialfs_host.serve_line",
     "`sfs run` の行の外でもフレームを解釈する"),
    ("tools/serialfs_host.py", r"    if not s.startswith\(\"run\"\) or s\[3:4\] not in \(\" \", \"\\t\"\):",
     "    if not s.startswith(\"run\"):", "ホスト側の行の判定が `sfs runx` を受ける"),
]


def _mutate_c_one(item):
    """C の変異 1 本。tmp/mut<i> (自分専用) に写して組み、最初に落ちたケースで打ち切る。
    並列に呼ばれる。(missed, error, 行) を返す。"""
    tmp, i, (rel, pattern, repl, why) = item
    original = (ROOT / rel).read_text(encoding="utf-8")
    text, n = re.subn(pattern, repl, original, count=1)
    if n != 1:
        return 0, 1, f"MUTATION C{i} ERROR (not applicable): {why}"
    # 変異を当てたファイル → 組むハーネス (ほかのハーネスはこのファイルを含まないか、
    # 含んでも変異の的ではない — 従来どおりの対応)
    if rel == "drivers/serial.c":
        build, cases = gate_build, GATE_CASES
    elif rel == "fs/serialfs_session.c":
        build, cases = session_build, SESSION_CASES
    else:
        build, cases = c_build, C_CASES
    name = "mut%d" % i
    try:
        exe = build(tmp, {rel: text}, name=name)
    except subprocess.CalledProcessError:
        return 0, 1, f"MUTATION C{i} ERROR (compile): {why}"
    try:
        hits = run_exe_cases(exe, cases, quiet=True, first_fail=True,
                             timeout=MUT_CASE_TIMEOUT)
    finally:
        shutil.rmtree(pathlib.Path(tmp) / name, ignore_errors=True)
    if why == IDENTITY:
        status = "GREEN (control)" if not hits else "**RED (control broken)**"
        missed = int(bool(hits))
    else:
        status = "RED" if hits else "**GREEN (見逃し)**"
        missed = int(not hits)
    return missed, 0, f"MUTATION C{i} {status} ({hits} 件): {why}"


def _mutate_py_one(item):
    """Python の変異 1 本。tmp/pymut<i> に写して読み込み直し、最初に落ちたケースで
    打ち切る。モジュールの差し替え (sys.modules) と FAILED を使うので、別プロセスで
    呼ばれる (mutpar processes=True)。(missed, error, 行) を返す。"""
    tmp, i, (rel, pattern, repl, why) = item
    mdir = pathlib.Path(tmp) / ("pymut%d" % i)
    mdir.mkdir(parents=True, exist_ok=True)
    files = {"tools/serialfs_host.py": mdir / "serialfs_host.py",
             "tools/rshell_serial.py": mdir / "rshell_serial.py"}
    for src, dst in files.items():
        shutil.copy(ROOT / src, dst)
    original = (ROOT / rel).read_text(encoding="utf-8")
    text, n = re.subn(pattern, repl, original, count=1)
    if n != 1:
        return 0, 1, f"MUTATION P{i} ERROR (not applicable): {why}"
    files[rel].write_text(text, encoding="utf-8")
    try:
        h, r = load_pair(files["tools/serialfs_host.py"],
                         files["tools/rshell_serial.py"])
    except Exception:  # noqa: BLE001
        return 0, 1, f"MUTATION P{i} ERROR (import): {why}"
    hits = run_py(h, r, list(PY_CASES), quiet=True, first_fail=True)
    if why == IDENTITY:
        status = "GREEN (control)" if not hits else "**RED (control broken)**"
        missed = int(bool(hits))
    else:
        status = "RED" if hits else "**GREEN (見逃し)**"
        missed = int(not hits)
    return missed, 0, f"MUTATION P{i} {status} ({hits} 件): {why}"


def mutate(tmp):
    """C の変異はスレッド、Python の変異は別プロセスで並列に回し (mutpar、
    OS32_MUT_JOBS)、結果は番号順に出す。どちらも最初に落ちたケースで打ち切る
    (「件」は打ち切るまでに落ちた数 = 1)。"""
    bad = 0
    errors = 0
    items = [(tmp, i, m) for i, m in enumerate(
        C_MUTATIONS + GATE_MUTATIONS + SESSION_MUTATIONS, 1)]
    for missed, err, line in mutpar.run_ordered(_mutate_c_one, items):
        print(line, flush=True)
        bad += missed
        errors += err
    items = [(str(tmp), i, m) for i, m in enumerate(PY_MUTATIONS, 1)]
    for missed, err, line in mutpar.run_ordered(_mutate_py_one, items, processes=True):
        print(line, flush=True)
        bad += missed
        errors += err
    load_pair()                 # 実物へ戻す (OS32_MUT_JOBS=1 ならこのプロセスで読み込み直している)
    total = (len(C_MUTATIONS) + len(GATE_MUTATIONS) + len(SESSION_MUTATIONS)
             + len(PY_MUTATIONS))
    print(f"MUTATION SUMMARY total={total} errors={errors} missed={bad}",
          flush=True)
    return bad + errors


def main():
    args = sys.argv[1:]
    only = [a for a in args if not a.startswith("--")]
    rc = 0
    with tempfile.TemporaryDirectory(prefix="os32-serialfs-") as tmp:
        exe = c_build(tmp)
        gexe = gate_build(tmp)
        sexe = session_build(tmp)
        print("HOST GNU11 -Werror compile PASS (real fs/sfs_*.c, fs/serialfs.c, "
              "fs/serialfs_session.c, drivers/serial.c)", flush=True)
        if "--target" in args:
            build_target(tmp)
        rc += run_exe_cases(exe, [c for c in C_CASES if not only or c in only])
        rc += run_exe_cases(gexe, [c for c in GATE_CASES if not only or c in only],
                            tag="gate:")
        rc += run_exe_cases(sexe, [c for c in SESSION_CASES if not only or c in only],
                            tag="session:")
        h, r = load_pair()
        rc += run_py(h, r, [c for c in PY_CASES if not only or c in only])
        if not only:
            rc += cross_check(exe, h)
            rc += const_check(h)
            rc += integration(exe, h)
        if "--mutate" in args:
            rc += mutate(tmp)
    print(f"RESULT {'PASS' if rc == 0 else 'FAIL'} ({rc})", flush=True)
    return 1 if rc else 0


if __name__ == "__main__":
    sys.exit(main())
