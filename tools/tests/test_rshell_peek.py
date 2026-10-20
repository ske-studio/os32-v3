"""tools/rshell_peek.py (rshell_serial.py の `peek`) のホスト試験 (TASK_KPEEK §4、段 T3)。

偽の応答と偽の ELF / VK32 / nm で回す (シリアルもゲストも要らない):
  - 応答の照合: 見出し・番地の連番・行のバイト数 (端数行)・8 bit 和・ok 行の
    len = 要求長・crc32・frames・`peek: err`。1 つでも違えば rc 1 でファイル無し
  - 64 KB ごとの分割と組み直し、標準出力の 1 行
  - 記号: nm の解析、os32_build_commit を節ヘッダから引く (名前は見ない、R1)、
    ELF の commit と VK32 の CRC が `ver` と **2 つとも** 合うときだけ引く (K4)

  python3 -B tools/tests/test_rshell_peek.py            # 全ケース
  python3 -B tools/tests/test_rshell_peek.py --mutate   # 否定側
"""
import argparse
import importlib.util
import os
import pathlib
import re
import struct
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "tools/rshell_peek.py"


def load(path=None):
    spec = importlib.util.spec_from_file_location("rshell_peek_under_test",
                                                  str(path or SRC))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---- ゲストの出力を独立に組む (cmd_peek.c の §2-7 の書式) -----------------
def mem(addr, n, seed=0):
    return bytes(((addr + i) * 7 + seed + ((addr + i) >> 8)) & 0xFF for i in range(n))


def guest_reply(addr, data, mmio=False, width=1, echo=None, crc=None, frames=None,
                ok_len=None):
    lines = []
    if echo:
        lines.append("> " + echo)
    lines.append("peek %s %08X w%d len=%08X" % ("m" if mmio else "k", addr, width,
                                                len(data)))
    for off in range(0, len(data), 16):
        row = data[off:off + 16]
        lines.append("%08X:%s  |s=%02X" % (addr + off, "".join(" %02X" % b for b in row),
                                           sum(row) & 0xFF))
    lines.append("peek: ok len=%08X crc32=%08X frames=%d" % (
        len(data) if ok_len is None else ok_len,
        zlib.crc32(data) & 0xFFFFFFFF if crc is None else crc,
        (len(data) + 1023) // 1024 if frames is None else frames))
    return "\r\n".join(lines) + "\r\n"


class FakeGuest(object):
    """send(line, timeout) の偽物。peek の行を解いて guest_reply を返す。"""

    def __init__(self, ver_text="", corrupt=None):
        self.ver_text = ver_text
        self.corrupt = corrupt      # (text) -> text、n 回目だけ壊す等
        self.sent = []

    def __call__(self, line, timeout):
        self.sent.append((line, timeout))
        if line == "ver":
            return "> ver\r\n" + self.ver_text, None
        f = line.split()
        assert f[0] == "peek"
        mmio = "-m" in f
        width = 1
        for x in f:
            if x.startswith("-w"):
                width = int(x[2:])
        addr, n = int(f[-2], 16), int(f[-1], 16)
        text = guest_reply(addr, mem(addr, n), mmio, width, echo=line)
        if self.corrupt:
            text = self.corrupt(len(self.sent), text)
        return text, None


def make_args(target, length=None, elf=None, out=None, m=False, w=0, timeout=15.0):
    return argparse.Namespace(target=target, len=length, elf=elf, out=out, m=m, w=w,
                              timeout=timeout, nm=None)


def make_elf(commit, sym_addr=0x2010):
    """節 3 つ: null / NOBITS (同じ番地を含む、飛ばすべき) / PROGBITS。名前は
    わざと出鱈目 (名前を見ないこと)。"""
    payload = bytearray(0x40)
    off = sym_addr - 0x2000
    payload[off:off + len(commit) + 1] = commit.encode() + b"\0"
    data_off = 52
    shoff = data_off + len(payload)
    hdr = bytearray(52)
    hdr[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<I", hdr, 0x20, shoff)
    struct.pack_into("<HH", hdr, 0x2E, 40, 3)
    sh = bytearray(40 * 3)
    struct.pack_into("<IIIIII", sh, 40, 99, 8, 3, 0x2000, 0, 0x100)        # NOBITS
    struct.pack_into("<IIIIII", sh, 80, 77, 1, 3, 0x2000, data_off, 0x40)  # PROGBITS
    return bytes(hdr + payload + sh)


def make_vk32(body=b"lz4 body", field=None):
    header_size = 16 + 20 + 8
    img = bytearray(struct.pack("<4I", 0x32334B56, header_size, 2, 1))
    img += bytes(16) + bytes(4) + struct.pack("<II", header_size + len(body), 0)
    img += body
    crc = zlib.crc32(bytes(img)) & 0xFFFFFFFF
    struct.pack_into("<I", img, header_size - 4, crc if field is None else field)
    return bytes(img), crc


NM_TEXT = ("00002010 00000018 D os32_build_commit\n"
           "00186060 0000006c B kdebug\n"
           "00100000 T _start\n")


def ver_text(commit, crc):
    return ("PC-9801 OS32 v3\r\n  Build: x\r\n  Commit: %s\r\n"
            "  Image CRC: %08x (1234 bytes, HDD loader)\r\n" % (commit, crc))


FAILED = []


def check(cond, what):
    if not cond:
        FAILED.append(what)
        print("FAIL:", what, flush=True)


def expect_error(mod, text, addr, n, what, match=None, **kw):
    try:
        mod.parse_peek_output(text, addr, n, kw.get("mmio", False), kw.get("width", 1))
    except mod.PeekError as e:
        check(match is None or match == str(e), what + " (message %r)" % str(e))
        return
    check(False, what)


# ---- ケース -------------------------------------------------------------
def case_parse(mod):
    for n in (1, 0x10, 0x24, 0x401, 0x10000):
        d = mem(0x100000, n)
        got = mod.parse_peek_output(guest_reply(0x100000, d, echo="peek x"), 0x100000,
                                    n, False, 1)
        check(got == d, "parse round trip n=%x" % n)
    d = mem(0xFE000000, 8)
    check(mod.parse_peek_output(guest_reply(0xFE000000, d, True, 4), 0xFE000000, 8,
                                True, 4) == d, "parse -m -w4")
    check(mod.peek_command(0x1000, 0x20, False, 0) == "peek 00001000 20", "command k")
    check(mod.peek_command(0xFE000000, 8, True, 4) == "peek -m -w4 FE000000 8",
          "command m")


def case_reject(mod):
    a, n = 0x100000, 0x30
    d = mem(a, n)
    good = guest_reply(a, d)
    lines = good.split("\r\n")
    # 連番の欠け (2 行目を消し、ok 行までのバイト数も合わない)
    expect_error(mod, "\r\n".join(lines[:2] + lines[3:]), a, n, "missing row")
    # 番地の飛び (同じ長さのまま 2 行目の番地を書き換え)
    expect_error(mod, good.replace("%08X:" % (a + 16), "%08X:" % (a + 32), 1), a, n,
                 "row address jump")
    # 和の不一致
    bad = lines[1][:-2] + ("%02X" % ((int(lines[1][-2:], 16) + 1) & 0xFF))
    expect_error(mod, "\r\n".join([lines[0], bad] + lines[2:]), a, n, "sum mismatch")
    # バイトの化け (和も合わせて直す → CRC で捕まる)
    row = bytearray(d[0:16]); row[0] ^= 1
    fixed = "%08X:%s  |s=%02X" % (a, "".join(" %02X" % b for b in row), sum(row) & 0xFF)
    expect_error(mod, "\r\n".join([lines[0], fixed] + lines[2:]), a, n, "crc mismatch")
    expect_error(mod, guest_reply(a, d, ok_len=n - 1), a, n, "ok len mismatch")
    # ok 行の len と行が揃って短い (要求長と違う、レビュー (5))
    expect_error(mod, guest_reply(a, d[:0x20]), a, n, "short reply with own ok len")
    expect_error(mod, guest_reply(a, d, frames=2), a, n, "frames mismatch")
    expect_error(mod, guest_reply(a, d, crc=0), a, n, "crc field mismatch")
    expect_error(mod, "> peek\r\npeek: err NOMAP at 00101000\r\n", a, n, "err line",
                 match="peek: err NOMAP at 00101000")
    expect_error(mod, good + "junk\r\n", a, n, "trailing junk")
    expect_error(mod, good.replace("peek k", "peek m", 1), a, n, "header space")
    # 端数行が 16 B に化ける (最後の行のバイト数)
    d2 = mem(a, 0x14)
    r2 = guest_reply(a, d2).split("\r\n")
    r2[2] = "%08X:%s  |s=%02X" % (a + 16, "".join(" %02X" % b for b in d2[16:] + b"\0" * 12),
                                  sum(d2[16:]) & 0xFF)
    expect_error(mod, "\r\n".join(r2), a, 0x14, "tail row padded")
    # 同じ化けで ok 行の crc も化けた側で計算されている (len は要求どおり)
    padded = d2 + b"\0" * 12
    r2[3] = "peek: ok len=%08X crc32=%08X frames=1" % (0x14, zlib.crc32(padded))
    expect_error(mod, "\r\n".join(r2), a, 0x14, "tail row padded, crc of padded")
    expect_error(mod, "", a, n, "empty")


def case_run_address(mod):
    with tempfile.TemporaryDirectory() as tmp:
        out_path = os.path.join(tmp, "dump.bin")
        g = FakeGuest()
        printed = []
        rc = mod.run(make_args("00100000", "10010", out=out_path), g, 115200,
                     out=printed.append)
        check(rc == 0, "run address rc")
        check([s[0] for s in g.sent] == ["peek 00100000 10000", "peek 00110000 10"],
              "split at 64 KB: %r" % [s[0] for s in g.sent])
        check(all(t >= 60.0 for _, t in g.sent), "long timeout ([V3])")
        data = mem(0x100000, 0x10000) + mem(0x110000, 0x10)
        check(open(out_path, "rb").read() == data, "file bytes")
        check(printed[-1] == "00100000 00010010 %08X %d" % (zlib.crc32(data), 0x1001),
              "summary line %r" % printed[-1:])
        check(any("not checked" in p for p in printed), "address warning")

        # 2 つ目の応答の 1 行が欠ける → rc 1、ファイルは書かない
        out2 = os.path.join(tmp, "dump2.bin")

        def drop(i, text):
            if i == 2:
                return text.replace("00110000:", "00110001:")
            return text
        rc = mod.run(make_args("100000", "10010", out=out2), FakeGuest(corrupt=drop),
                     115200, out=lambda s: None)
        check(rc == 1 and not os.path.exists(out2), "corrupt piece: rc 1, no file")
        rc = mod.run(make_args("0x1000", "8", m=True), FakeGuest(), out=lambda s: None)
        check(rc == 2, "-m without -w refused")


def case_symbol(mod):
    with tempfile.TemporaryDirectory() as tmp:
        elf = os.path.join(tmp, "kernel.elf")
        commit = "abc1234-dirty"
        with open(elf, "wb") as f:
            f.write(make_elf(commit))
        vk, crc = make_vk32()
        with open(os.path.join(tmp, "vmkernel.lz4"), "wb") as f:
            f.write(vk)
        mod.run_nm = lambda path, nm=None: NM_TEXT
        syms = mod.parse_nm(NM_TEXT)
        check(syms["kdebug"] == (0x186060, 0x6c) and syms["_start"] == (0x100000, None),
              "parse_nm")
        check(mod.elf_cstring(make_elf(commit), 0x2010) == commit,
              "elf_cstring by section address (NOBITS skipped)")
        check(mod.vk32_image_crc(vk) == crc, "vk32 crc")
        try:
            mod.vk32_image_crc(make_vk32(field=1)[0])
            check(False, "vk32 broken field")
        except mod.PeekError:
            pass

        out = os.path.join(tmp, "k.bin")
        g = FakeGuest(ver_text(commit, crc))
        rc = mod.run(make_args("kdebug", elf=elf, out=out), g, 115200, out=lambda s: None)
        check(rc == 0, "symbol rc")
        check([s[0] for s in g.sent] == ["ver", "peek 00186060 6C"], "symbol sent %r" % g.sent)
        check(open(out, "rb").read() == mem(0x186060, 0x6c), "symbol bytes")
        g = FakeGuest(ver_text(commit, crc))
        rc = mod.run(make_args("kdebug+10", "4", elf=elf), g, out=lambda s: None)
        check(rc == 0 and g.sent[-1][0] == "peek 00186070 4", "symbol+off")

        for vt, what in ((ver_text("abc1234", crc), "commit differs (-dirty)"),
                         (ver_text(commit, crc ^ 1), "image crc differs"),
                         ("  Commit: %s\r\n  Image CRC: none (loader did not record)\r\n"
                          % commit, "no image crc")):
            out3 = os.path.join(tmp, "no.bin")
            g = FakeGuest(vt)
            rc = mod.run(make_args("kdebug", elf=elf, out=out3), g, out=lambda s: None)
            check(rc == 1 and [s[0] for s in g.sent] == ["ver"] and
                  not os.path.exists(out3), "symbol refused: " + what)
        rc = mod.run(make_args("nosuch", elf=elf), FakeGuest(), out=lambda s: None)
        check(rc == 2, "unknown symbol")


class FakePort(object):
    """rshell_serial.send_cmd が使う口だけ。書かれた行に FakeGuest で答える。"""

    def __init__(self, guest, echo_override=None):
        self.guest, self.pending, self.echo_override = guest, b"", echo_override
        self.in_waiting = 0

    def reset_input_buffer(self):
        self.pending = b""

    def write(self, data):
        line = data.decode().rstrip("\n")
        text, _ = self.guest(line, 0)
        if self.echo_override:
            text = text.replace("> " + line, self.echo_override, 1)
        self.pending = text.encode() + b"\x04"
        self.in_waiting = len(self.pending)

    def flush(self):
        pass

    def read(self, n):
        out, self.pending = self.pending[:n], self.pending[n:]
        self.in_waiting = len(self.pending)
        return out


def case_serial_glue(mod):
    """rshell_serial.py の `peek` サブコマンド: 引数と、EOT・エコーを確かめた本文。"""
    spec = importlib.util.spec_from_file_location("rshell_serial_glue",
                                                  str(ROOT / "tools/rshell_serial.py"))
    rs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rs)
    rs.rshell_peek = mod
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode")
    mod.add_arguments(sub.add_parser("peek"))
    args = ap.parse_args(["peek", "-m", "-w", "4", "FE000000", "8"])
    args.timeout = 15.0
    printed = []
    mod_print = mod.run
    mod.run = lambda a, send, baud=9600, out=print: mod_print(a, send, baud, printed.append)
    try:
        rc = rs.run_peek(args, FakePort(FakeGuest()), 115200)
        check(rc == 0 and printed[-1].startswith("FE000000 00000008 "), "glue ok %r" % printed)
        rc = rs.run_peek(args, FakePort(FakeGuest(), echo_override="> peek other"), 115200)
        check(rc == 1 and "desync" in printed[-1], "glue desync %r" % printed[-1:])
    finally:
        mod.run = mod_print


CASES = [case_parse, case_reject, case_run_address, case_symbol, case_serial_glue]

# 否定側 (パターン, 置換, 説明)
MUTATIONS = [
    (r"if int\(m\.group\(1\), 16\) != want_addr:", "if False:", "連番 (番地) の検査を消す"),
    (r"if sum\(row\) & 0xFF != int\(m\.group\(3\), 16\):", "if False:", "行の和を見ない"),
    (r"if int\(m\.group\(1\), 16\) != length:", "if False:", "ok 行の len を要求長と比べない"),
    (r"if int\(m\.group\(2\), 16\) != crc:", "if False:", "crc32 を比べない"),
    (r"if int\(m\.group\(3\)\) != frames:", "if False:", "frames を比べない"),
    (r"if len\(row\) != want_n:", "if False:", "行のバイト数 (端数行) を見ない"),
    (r"if elf_commit != commit:", "if False:", "ELF の commit を比べない"),
    (r"if lz4_crc != image_crc:", "if False:", "VK32 の CRC を比べない"),
    (r"if sh_type == SHT_NOBITS or not flags & SHF_ALLOC:", "if False:",
     "NOBITS の節から文字列を読む"),
    (r"if i != len\(lines\) - 1:", "if False:", "ok 行の後ろの余りを見ない"),
    (r"    if args\.out:\n        with open\(args\.out, \"wb\"\) as f:\n            f\.write\(bytes\(data\)\)\n",
     "", "ファイルを書かない"),
    (r"(    for off in range\(0, length, PEEK_CMD_MAX\):\n)",
     r"    if args.out:\n        open(args.out, 'wb').close()\n\1",
     "照合の前にファイルを作る"),
    (r"for l in lines:\n        if l\.startswith\(\"peek: err\"\):\n            raise PeekError\(l\)",
     "pass", "err 行を見ない"),
]


def run_all(mod, quiet=False):
    failed_before = len(FAILED)
    for c in CASES:
        try:
            c(mod)
        except Exception as e:  # noqa: BLE001
            FAILED.append("%s raised %r" % (c.__name__, e))
            if not quiet:
                print("FAIL: %s raised %r" % (c.__name__, e), flush=True)
    return len(FAILED) - failed_before


def mutate():
    bad = 0
    original = SRC.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="os32-rshell-peek-") as tmp:
        for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
            text, n = re.subn(pattern, repl, original, count=1)
            if n != 1:
                print("MUTATION %d NOT APPLICABLE: %s" % (i, why), flush=True)
                bad += 1
                continue
            path = pathlib.Path(tmp) / ("m%d.py" % i)
            path.write_text(text, encoding="utf-8")
            saved = list(FAILED)
            out = sys.stdout
            sys.stdout = open(os.devnull, "w")
            try:
                hits = run_all(load(path), quiet=True)
            finally:
                sys.stdout.close()
                sys.stdout = out
                FAILED[:] = saved
            print("MUTATION %d %s (%d 件): %s" % (i, "RED" if hits else "**GREEN (見逃し)**",
                                                  hits, why), flush=True)
            bad += not hits
    return bad


if __name__ == "__main__":
    mod = load()
    run_all(mod)
    print("SUMMARY %s" % ("PASS" if not FAILED else "%d FAIL" % len(FAILED)), flush=True)
    rc = bool(FAILED)
    if "--mutate" in sys.argv[1:]:
        rc = rc or bool(mutate())
    sys.exit(int(rc))
