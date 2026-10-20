#!/usr/bin/env python3
"""rshell_peek.py — ゲストの `peek` (kdebug のメモリ読み) をシリアルで回して照合する。

TASK_KPEEK §3 (段 T3)。rshell_serial.py のサブコマンド `peek` から呼ぶ:

  rshell_serial.py --port COM3 --fast 115200 peek [--elf build/out/kernel.elf]
      [--out FILE] [-m] [-w N] <記号|hex>[+off] [len]

1. 記号名は **動いているカーネルと --elf が同じ版のときだけ** 引く (K4、R1):
   `ver` の `Commit:` と ELF の `os32_build_commit` (nm の番地を含む節の
   中身を NUL まで、節の名前は見ない)、`ver` の `Image CRC:` と同じ
   ディレクトリの `vmkernel.lz4` の CRC (VK32 v2、image_crc の欄を 0 として
   zlib.crc32) の **2 つとも** 一致しなければ断る。git は見ない。
   番地 (16 進) の指定は照合しない (警告 1 行)。
2. 記号の番地・大きさは `i386-elf-nm -S --defined-only` (len 省略時は大きさ)。
3. 64 KB ごとに `peek` を送り、応答を行で照合する: 見出し、番地の連番、行の
   バイト数 (最後の行だけ端数)、`s=` の 8 bit 和、`ok` 行の len = 要求長、
   crc32 = 組み直したバイトの zlib.crc32、frames = 1 KB の要求の数。
   1 つでも違えば (または `peek: err`) 終了コード 1 で **ファイルを書かない**。
4. `--out FILE` に生バイト、標準出力に `addr len crc32 lines` の 1 行。
   「2 回読んで一致」は呼び手が 2 回回して CRC を比べる。
5. `--lan --iface IF` (T5、§3 5): AF_PACKET (tools/lanfs_host.py の NicPort) を
   開き、自分の MAC を `-l<mac>` に入れて送る。本文はゲストの kdebug が送る
   SFS_T_DUMP のフレーム (セッション ID 欄 = tag、番号 = 通し番号、ペイロード
   = [addr u32][data])。シリアルの `peek: ok len= crc32= tag= frames=` の後も
   1 秒の猶予でフレームを待ち (L5)、tag の違うものは捨て、番号で並べ直し、
   欠け・番号の重複の矛盾・番地や長さの違い・crc32 の不一致は rc 1 で
   ファイルを書かない (再送は無い、呼び手が再実行)。`peek: lan unavailable`
   の行が来たらシリアル版の受け方に切り替える。
"""
import os
import re
import shutil
import select
import socket
import struct
import subprocess
import time
import zlib

PEEK_CMD_MAX = 0x10000      # ゲストの 1 コマンドの上限 (KPEEK_CMD_MAX)
PEEK_ROW = 16               # 1 行のバイト数
PEEK_KAPI_CHUNK = 1024      # KDEBUG_PEEK_CHUNK (frames の数え方)
PEEK_DEFAULT_LEN = 0x10
COMMIT_SYMBOL = "os32_build_commit"
VK32_MAGIC = 0x32334B56
VK32_NAME = "vmkernel.lz4"
# 1 行の最大 (番地 8 + ": " + 16 x 3 + "  |s=XX" + CRLF) と 1 文字 10 bit。
PEEK_LINE_BYTES = 70
SERIAL_BITS_PER_BYTE = 10
LONG_TIMEOUT_S = 60.0       # [V3]: 長いコマンドは 60 秒以上

SHT_NOBITS = 8
SHF_ALLOC = 2

HEX_RE = re.compile(r"^(?:0[xX])?([0-9A-Fa-f]{1,8})$")
ROW_RE = re.compile(r"^([0-9A-F]{8}):((?: [0-9A-F]{2})+)  \|s=([0-9A-F]{2})$")
OK_RE = re.compile(r"^peek: ok len=([0-9A-F]{8}) crc32=([0-9A-F]{8}) frames=([0-9]+)$")
OK_LAN_RE = re.compile(r"^peek: ok len=([0-9A-F]{8}) crc32=([0-9A-F]{8}) "
                       r"tag=([0-9A-F]{8}) frames=([0-9]+)$")
LAN_UNAVAILABLE = "peek: lan unavailable"
LAN_GRACE_S = 1.0           # ok 行の後もフレームを待つ猶予 (L5)
LAN_ADDR_LEN = 4            # ペイロード先頭の addr u32
LAN_RCVBUF = 4 << 20        # 64 フレーム × 1 KB を ok 行まで溜める


class PeekError(Exception):
    pass


# ---------------------------------------------------------------------------
#  版の照合 (K4)
# ---------------------------------------------------------------------------
def parse_ver(text):
    """`ver` の応答から (Commit, Image CRC) を引く。無ければ None。"""
    m = re.search(r"^\s*Commit:\s*(\S+)\s*$", text, re.M)
    commit = m.group(1) if m else None
    m = re.search(r"^\s*Image CRC:\s*([0-9A-Fa-f]{8})\b", text, re.M)
    crc = int(m.group(1), 16) if m else None
    return commit, crc


def parse_nm(text):
    """`nm -S --defined-only` の出力 → {名前: (番地, 大きさ or None)}。"""
    syms = {}
    for line in text.splitlines():
        f = line.split()
        try:
            if len(f) == 4:
                syms[f[3]] = (int(f[0], 16), int(f[1], 16))
            elif len(f) == 3:
                syms.setdefault(f[2], (int(f[0], 16), None))
        except ValueError:
            continue
    return syms


def nm_path(explicit=None):
    if explicit:
        return explicit
    cross = os.environ.get("CROSS_DIR")
    if cross:
        cand = os.path.join(cross, "bin", "i386-elf-nm")
        if os.path.exists(cand):
            return cand
    return shutil.which("i386-elf-nm") or "i386-elf-nm"


def run_nm(elf, nm=None):
    r = subprocess.run([nm_path(nm), "-S", "--defined-only", elf],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise PeekError("nm failed on %s: %s" % (elf, r.stderr.strip()))
    return r.stdout


def elf_cstring(data, addr):
    """ELF32 の中で番地 addr を含む節 (ALLOC かつ NOBITS でない) を節ヘッダから
    探し、sh_offset + (addr - sh_addr) から NUL までを返す (R1、節の名前は使わない)。"""
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise PeekError("not a little-endian ELF32")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    for i in range(shnum):
        base = shoff + i * shentsize
        _, sh_type, flags, sh_addr, sh_off, sh_size = struct.unpack_from(
            "<IIIIII", data, base)
        if sh_type == SHT_NOBITS or not flags & SHF_ALLOC:
            continue
        if sh_addr <= addr < sh_addr + sh_size:
            start = sh_off + (addr - sh_addr)
            end = data.find(b"\0", start, sh_off + sh_size)
            if end < 0:
                raise PeekError("no NUL for %08X in its section" % addr)
            return data[start:end].decode("ascii", errors="replace")
    raise PeekError("no section holds %08X" % addr)


def vk32_image_crc(data):
    """vmkernel.lz4 (VK32 v2) のファイル全体の CRC32 (image_crc の欄を 0 として、
    tools/mkvmkernel.py と同じ計算)。欄の値と合わなければ壊れたファイル。"""
    if len(data) < 16:
        raise PeekError("%s too short" % VK32_NAME)
    magic, header_size = struct.unpack_from("<II", data, 0)
    if magic != VK32_MAGIC or not 24 <= header_size <= len(data):
        raise PeekError("%s is not VK32" % VK32_NAME)
    stored, = struct.unpack_from("<I", data, header_size - 4)
    image = bytearray(data)
    struct.pack_into("<I", image, header_size - 4, 0)
    crc = zlib.crc32(bytes(image)) & 0xFFFFFFFF
    if crc != stored:
        raise PeekError("%s image_crc field %08x != computed %08x"
                        % (VK32_NAME, stored, crc))
    return crc


def check_identity(ver_text, elf_data, syms, lz4_data):
    """K4: 2 つとも合えば None、合わなければ理由の文字列。"""
    commit, image_crc = parse_ver(ver_text)
    if commit is None or image_crc is None:
        return "ver did not report Commit and Image CRC"
    if COMMIT_SYMBOL not in syms:
        return "%s not in nm output" % COMMIT_SYMBOL
    elf_commit = elf_cstring(elf_data, syms[COMMIT_SYMBOL][0])
    if elf_commit != commit:
        return "commit mismatch: elf %s, running %s" % (elf_commit, commit)
    lz4_crc = vk32_image_crc(lz4_data)
    if lz4_crc != image_crc:
        return "image CRC mismatch: %s %08x, running %08x" % (VK32_NAME, lz4_crc,
                                                            image_crc)
    return None


# ---------------------------------------------------------------------------
#  応答の照合 (§3 3)
# ---------------------------------------------------------------------------
def peek_command(addr, length, mmio, width, lan_mac=None):
    parts = ["peek"]
    if mmio:
        parts.append("-m")
    if width:
        parts.append("-w%d" % width)
    if lan_mac is not None:
        parts.append("-l" + ":".join("%02x" % b for b in lan_mac))
    parts += ["%08X" % addr, "%X" % length]
    return " ".join(parts)


def parse_peek_output(text, addr, length, mmio, width):
    """ゲストの応答 (エコー込み) を照合して生バイトを返す。違えば PeekError。"""
    lines = [l.rstrip("\r") for l in text.split("\n")]
    lines = [l for l in lines if l and not l.startswith("> ")]
    for l in lines:
        if l.startswith("peek: err"):
            raise PeekError(l)
    if not lines:
        raise PeekError("empty reply")
    head = "peek %s %08X w%d len=%08X" % ("m" if mmio else "k", addr, width or 1,
                                          length)
    if lines[0] != head:
        raise PeekError("header %r, want %r" % (lines[0], head))
    data = bytearray()
    i = 1
    while len(data) < length:
        if i >= len(lines):
            raise PeekError("reply ends at %08X" % (addr + len(data)))
        m = ROW_RE.match(lines[i])
        if not m:
            raise PeekError("bad row %r" % lines[i])
        want_addr = addr + len(data)
        if int(m.group(1), 16) != want_addr:
            raise PeekError("row address %s, want %08X" % (m.group(1), want_addr))
        row = bytes(int(x, 16) for x in m.group(2).split())
        want_n = min(PEEK_ROW, length - len(data))
        if len(row) != want_n:
            raise PeekError("row %08X has %d bytes, want %d" % (want_addr, len(row),
                                                                want_n))
        if sum(row) & 0xFF != int(m.group(3), 16):
            raise PeekError("row %08X sum mismatch" % want_addr)
        data += row
        i += 1
    if i != len(lines) - 1:
        raise PeekError("want exactly one ok line after the rows")
    m = OK_RE.match(lines[i])
    if not m:
        raise PeekError("bad ok line %r" % lines[i])
    if int(m.group(1), 16) != length:
        raise PeekError("ok len %s, requested %08X" % (m.group(1), length))
    crc = zlib.crc32(bytes(data)) & 0xFFFFFFFF
    if int(m.group(2), 16) != crc:
        raise PeekError("crc32 %s, rebuilt %08X" % (m.group(2), crc))
    frames = (length + PEEK_KAPI_CHUNK - 1) // PEEK_KAPI_CHUNK
    if int(m.group(3)) != frames:
        raise PeekError("frames %s, want %d" % (m.group(3), frames))
    return bytes(data)


# ---------------------------------------------------------------------------
#  LAN の出口 (§3 5、T5)
# ---------------------------------------------------------------------------
def _lan_modules():
    import lanfs_host
    import serialfs_host
    return lanfs_host, serialfs_host


def parse_lan_reply(text, addr, length, mmio, width):
    """`peek -l` のシリアルの応答。('serial', 生バイト) (lan unavailable の後の
    シリアル版) か ('lan', tag, crc32, frames) を返す。違えば PeekError。"""
    lines = [l.rstrip("\r") for l in text.split("\n")]
    lines = [l for l in lines if l and not l.startswith("> ")]
    for l in lines:
        if l.startswith("peek: err"):
            raise PeekError(l)
    if lines and lines[0].startswith(LAN_UNAVAILABLE):
        return ("serial", parse_peek_output("\n".join(lines[1:]), addr, length,
                                            mmio, width))
    head = "peek %s %08X w%d len=%08X" % ("m" if mmio else "k", addr, width or 1,
                                          length)
    if len(lines) != 2 or lines[0] != head:
        raise PeekError("lan reply %r, want header and ok line" % lines[:3])
    m = OK_LAN_RE.match(lines[1])
    if not m:
        raise PeekError("bad lan ok line %r" % lines[1])
    if int(m.group(1), 16) != length:
        raise PeekError("ok len %s, requested %08X" % (m.group(1), length))
    frames = (length + PEEK_KAPI_CHUNK - 1) // PEEK_KAPI_CHUNK
    if int(m.group(4)) != frames:
        raise PeekError("frames %s, want %d" % (m.group(4), frames))
    return ("lan", int(m.group(3), 16), int(m.group(2), 16), frames)


def dump_frame(packet, own_mac):
    """自分宛ての SFS_T_DUMP なら Frame、それ以外は None (L2: LAN の上限で解く)。"""
    lanfs, sfs = _lan_modules()
    if (len(packet) < lanfs.ETH_HDR + len(lanfs.MARK) or
            packet[12:14] != struct.pack(">H", lanfs.ETHERTYPE) or
            packet[14:17] != lanfs.MARK or packet[:6] != own_mac):
        return None
    parsed = sfs.try_frame(packet, lanfs.ETH_HDR, sfs.LAN_MAX_PAYLOAD)
    if parsed[0] != "frame" or parsed[2].type != sfs.T_DUMP:
        return None
    return parsed[2]


class DumpCollector(object):
    """1 コマンド (tag) のフレームを番号で集めて組み直す。"""

    def __init__(self, tag, addr, length):
        self.tag = tag
        self.addr = addr
        self.length = length
        self.got = {}

    def feed(self, fr):
        if fr.sid != self.tag:
            return                       # 別のコマンドの残り
        if fr.seq in self.got:
            if self.got[fr.seq] != fr.payload:
                raise PeekError("frame seq %d repeated with other bytes" % fr.seq)
            return
        self.got[fr.seq] = fr.payload

    def complete(self, frames):
        return all(i in self.got for i in range(frames))

    def assemble(self, frames, crc):
        missing = [i for i in range(frames) if i not in self.got]
        if missing:
            raise PeekError("lan frames missing: %s of %d" % (
                ",".join(str(i) for i in missing[:8]), frames))
        extra = sorted(set(self.got) - set(range(frames)))
        if extra:
            raise PeekError("lan frame seq %d beyond frames=%d" % (extra[0], frames))
        data = bytearray()
        for seq in range(frames):
            payload = self.got[seq]
            want_addr = self.addr + seq * PEEK_KAPI_CHUNK
            want_n = min(PEEK_KAPI_CHUNK, self.length - seq * PEEK_KAPI_CHUNK)
            if len(payload) < LAN_ADDR_LEN:
                raise PeekError("lan frame seq %d too short" % seq)
            got_addr, = struct.unpack_from("<I", payload, 0)
            if got_addr != want_addr or len(payload) - LAN_ADDR_LEN != want_n:
                raise PeekError("lan frame seq %d holds %08X+%X, want %08X+%X" % (
                    seq, got_addr, len(payload) - LAN_ADDR_LEN, want_addr, want_n))
            data += payload[LAN_ADDR_LEN:]
        if zlib.crc32(bytes(data)) & 0xFFFFFFFF != crc:
            raise PeekError("crc32 %08X, rebuilt from lan %08X" % (
                crc, zlib.crc32(bytes(data)) & 0xFFFFFFFF))
        return bytes(data)


def collect_frames(port, collector, frames, grace=LAN_GRACE_S):
    """ok 行の後、揃うか猶予が切れるまでフレームを読む (L5)。揃った後も既に
    届いている分は読み切る (番号の重複の矛盾を見落とさない)。時計は port.now。"""
    now = port.now
    deadline = now() + grace

    def feed(packets):
        for packet in packets:
            fr = dump_frame(packet, port.mac)
            if fr is not None:
                collector.feed(fr)
    while not collector.complete(frames):
        remaining = deadline - now()
        if remaining <= 0:
            break
        feed(port.poll(remaining))
    while True:
        packets = port.poll(0)
        if not packets:
            break
        feed(packets)


class LanPort(object):
    """AF_PACKET の受け口 (lanfs_host.NicPort、同じ IF の lanfs_host と並べてよい)。"""

    def __init__(self, iface):
        lanfs, _ = _lan_modules()
        self.nic = lanfs.NicPort(iface, lanfs.ETHERTYPE)
        try:
            self.nic.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, LAN_RCVBUF)
        except OSError:
            pass
        self.mac = self.nic.mac
        self.now = time.monotonic

    def poll(self, timeout):
        if not select.select([self.nic], [], [], max(0.0, timeout))[0]:
            return []
        return self.nic.recv_frames()

    def close(self):
        self.nic.close()


def piece_timeout(length, baud, base):
    lines = (length + PEEK_ROW - 1) // PEEK_ROW + 2
    wire = lines * PEEK_LINE_BYTES * SERIAL_BITS_PER_BYTE / float(baud or 9600)
    return max(base, LONG_TIMEOUT_S + 2 * wire)


def parse_int_hex(s):
    m = HEX_RE.match(s)
    return int(m.group(1), 16) if m else None


def resolve(target, length, syms):
    """(番地, 長さ, 記号か) を返す。記号は nm にある名前、無ければ 16 進。"""
    name, plus, off_s = target.partition("+")
    off = 0
    if plus:
        off = parse_int_hex(off_s)
        if off is None:
            raise PeekError("bad offset %r" % off_s)
    if syms is not None and name in syms:
        base, size = syms[name]
        if length is None:
            if size is None or size <= off:
                raise PeekError("%s has no size; give len" % name)
            length = size - off
        return base + off, length, True
    base = parse_int_hex(name)
    if base is None:
        raise PeekError("unknown symbol %r (no --elf or not in nm)" % name)
    return base + off, PEEK_DEFAULT_LEN if length is None else length, False


def run(args, send, baud=9600, out=print, lan_port=None):
    """send(line, timeout) -> (text, err or None)。終了コードを返す。
    lan_port は --lan の受け口 (試験は偽物、無ければ --iface で開く)。"""
    lan = getattr(args, "lan", False)
    if lan and lan_port is None:
        if not getattr(args, "iface", None):
            out("[rshell_peek] --lan needs --iface")
            return 2
        try:
            lan_port = LanPort(args.iface)
        except Exception as e:  # noqa: BLE001 (BridgeError / OSError)
            out("[rshell_peek] --lan: %s" % e)
            return 1
        try:
            return _run(args, send, baud, out, lan_port)
        finally:
            lan_port.close()
    return _run(args, send, baud, out, lan_port if lan else None)


def _run(args, send, baud, out, lan_port):
    length = None
    if args.len is not None:
        length = parse_int_hex(args.len)
        if length is None or length == 0:
            out("[rshell_peek] bad len %r" % args.len)
            return 2
    if args.m and not args.w:
        out("[rshell_peek] -m needs -w 1|2|4")
        return 2
    syms = elf_data = None
    if args.elf:
        if not os.path.exists(args.elf):
            out("[rshell_peek] --elf %s not found" % args.elf)
            return 2
        try:
            syms = parse_nm(run_nm(args.elf, getattr(args, "nm", None)))
        except (PeekError, OSError) as e:
            out("[rshell_peek] %s" % e)
            return 1
    try:
        addr, length, is_sym = resolve(args.target, length, syms)
    except PeekError as e:
        out("[rshell_peek] %s" % e)
        return 2
    if addr + length > 1 << 32:
        out("[rshell_peek] range crosses 4 GB")
        return 2
    if is_sym:
        text, err = send("ver", args.timeout)
        if err:
            out("[rshell_peek] ver: %s" % err)
            return 1
        try:
            with open(args.elf, "rb") as f:
                elf_data = f.read()
            with open(os.path.join(os.path.dirname(args.elf) or ".", VK32_NAME),
                      "rb") as f:
                lz4_data = f.read()
            why = check_identity(text, elf_data, syms, lz4_data)
        except (PeekError, OSError) as e:
            why = str(e)
        if why:
            out("[rshell_peek] symbol refused: %s" % why)
            return 1
    else:
        out("[rshell_peek] address given: build identity not checked")
    data = bytearray()
    lines = 0
    for off in range(0, length, PEEK_CMD_MAX):
        n = min(PEEK_CMD_MAX, length - off)
        line = peek_command(addr + off, n, args.m, args.w,
                            lan_port.mac if lan_port is not None else None)
        text, err = send(line, piece_timeout(n, baud, args.timeout or 0))
        if err:
            out("[rshell_peek] %s: %s" % (line, err))
            return 1
        try:
            if lan_port is None:
                data += parse_peek_output(text, addr + off, n, args.m, args.w)
            else:
                reply = parse_lan_reply(text, addr + off, n, args.m, args.w)
                if reply[0] == "serial":
                    out("[rshell_peek] lan unavailable on the guest: serial reply")
                    data += reply[1]
                else:
                    _, tag, crc, frames = reply
                    got = DumpCollector(tag, addr + off, n)
                    collect_frames(lan_port, got, frames)
                    data += got.assemble(frames, crc)
        except PeekError as e:
            out("[rshell_peek] %s: %s" % (line, e))
            return 1
        lines += (n + PEEK_ROW - 1) // PEEK_ROW
    if args.out:
        with open(args.out, "wb") as f:
            f.write(bytes(data))
    out("%08X %08X %08X %d" % (addr, length, zlib.crc32(bytes(data)) & 0xFFFFFFFF,
                               lines))
    return 0


def add_arguments(p):
    """rshell_serial.py の `peek` サブコマンドの引数。"""
    p.add_argument("--elf", default=None,
                   help="記号を引く kernel.elf (同じディレクトリの vmkernel.lz4 と組)。"
                        "記号名を使うときは必須 (番地だけなら要らない)")
    p.add_argument("--nm", default=None, help="i386-elf-nm (既定 $CROSS_DIR/bin)")
    p.add_argument("--out", default=None, help="生バイトを書くファイル (全部合ったときだけ)")
    p.add_argument("-m", action="store_true", help="MMIO (許可リストの PCI BAR)")
    p.add_argument("-w", type=int, choices=(1, 2, 4), default=0, help="読みの幅")
    p.add_argument("--lan", action="store_true",
                   help="本文を LAN (82557、SFS_T_DUMP) で受ける。--iface 必須、root か CAP_NET_RAW")
    p.add_argument("--iface", default=None, help="--lan の受け口 (例 enp1s0)")
    p.add_argument("target", help="記号[+off] か 16 進の番地[+off]")
    p.add_argument("len", nargs="?", default=None, help="16 進の長さ")
