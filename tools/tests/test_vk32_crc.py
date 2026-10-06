"""VK32 v2 (CRC32 表 + 完全長) の生成と、両ローダの検査・展開のホスト試験。

記録: tools/tests/vk32_crc_tdd.md
票  : docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 A-4 / §1-v3「FD ローダ」

tools/mkvmkernel.py (実物) で VK32 v2 を作り、
  mode 0  boot/vk32_boot.c の vk32_boot + boot/lz4_mini.c (HDD ローダ)
  mode 1  boot/loader_fat_new.asm の pm_vk32_boot (FD ローダ、ASM を**そのまま** 32bit で)
に同じイメージ (正常・壊したもの) を渡し、同じ VK32_ERR_* で断ること、正常なら窓の
中身が元の kernel/sqlite と一致して他は触らないことを見る。ハーネスは
tools/tests/vk32_host.c (libc なしの 32bit 静的 ELF)。

FD ローダの実モード部 (FAT チェーンの検査 fat_chain_check / fat12_next32) は
32 ビットのレジスタと番地だけで書いてあるので、同じソースを bits 32 で組んで
ホストで回す (mode 2)。2HD / 1.44MB の両方のジオメトリで組む。

CRC は zlib.crc32 と突き合わせる。

  python3 -B tools/tests/test_vk32_crc.py            # 合成データ
  python3 -B tools/tests/test_vk32_crc.py --real     # + build/out の実物と images/ の FD
  python3 -B tools/tests/test_vk32_crc.py --target   # C を i386-elf-gcc (ローダと同じ) で組む
  python3 -B tools/tests/test_vk32_crc.py --mutate   # 否定側 (写しの上で変異 → RED)
"""
import host32
import os
import pathlib
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

import lz4.block

import mutpar  # noqa: E402  (tools/tests/mutpar.py、同じディレクトリ)

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/vk32_host.c"
SCRIPT = ROOT / "tools/mkvmkernel.py"
VK32_SRC = ROOT / "boot/vk32_boot.c"
MINI_SRC = ROOT / "boot/lz4_mini.c"
ASM_SRC = ROOT / "boot/loader_fat_new.asm"
DEFS = ROOT / "boot/boot_defs.h"
BOOTINFO_H = ROOT / "include/bootinfo.h"
REAL_VMK = ROOT / "build/out/vmkernel.lz4"
FD_IMAGES = {"2hd": ROOT / "images/os32_boot.img", "144": ROOT / "images/os32_boot144.img"}

LOAD_MIN = 0x100000
LOAD_END = 0x2E8000
WINDOW = LOAD_END - LOAD_MIN
MAX_IMAGE = 508 * 1024

E = {"SIZE": -1, "MAGIC": -2, "VERSION": -3, "COUNT": -4, "HEADER": -5,
     "LENGTH": -6, "FILE_CRC": -7, "SRC": -8, "DST": -9, "DECODE": -10,
     "RAW_SIZE": -11, "ENTRY_CRC": -12}
FATCHK = {"OK": 0, "SIZE": 1, "SHORT": 2, "RANGE": 3, "LONG": 4, "START": 5}
GEOMS = {"2hd": dict(sect=1024, total=1232, data=11, fat_bytes=2 * 1024),
         "144": dict(sect=512, total=2880, data=31, fat_bytes=9 * 512)}


class Fail(Exception):
    pass


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, **kw)
    if r.returncode != 0:
        raise Fail("{} -> {}\n{}{}".format(
            " ".join(str(c) for c in cmd), r.returncode,
            r.stdout.decode(errors="replace"), r.stderr.decode(errors="replace")))
    return r


# ---------------------------------------------------------------- 切り出し

def between(text, begin, end):
    try:
        a = text.index(begin)
        b = text.index(end, a)
    except ValueError:
        raise Fail("{} 〜 {} が見つからない".format(begin, end))
    return text[a:b]


def asm_vk32(text):
    body = between(text, ";; >>> VK32_HOST_BEGIN", ";; <<< VK32_HOST_END")
    return "section .text\nglobal pm_vk32_boot\n" + body + "\n"


def geometry_block(text, geom):
    """%ifdef FD144 〜 MAX_CLUSTER の行を、ジオメトリを決めた形で返す。"""
    # 行頭の %ifdef だけを見る (注記の中の「%ifdef FD144」に当たらないように)
    blk = between(text, "\n%ifdef FD144\n", "MAX_CLUSTER EQU")
    line = re.search(r"^MAX_CLUSTER EQU.*$", text, re.MULTILINE).group(0)
    pre = "%define FD144\n" if geom == "144" else ""
    return pre + blk + line + "\n"


def asm_fat(text, geom):
    body = between(text, ";; >>> FAT_HOST_BEGIN", ";; <<< FAT_HOST_END")
    m = re.search(r"^MAX_IMAGE_SIZE\s+EQU\s+\S+.*$", text, re.MULTILINE)
    if not m:
        raise Fail("MAX_IMAGE_SIZE EQU が無い")
    wrapper = """
global fat_check_c
; int fat_check_c(u32 start, u32 size, const u8 *fat, u32 *count)
; 呼ぶ側の EBX / EDX / ESI が保たれることも見る (壊したら 99)
fat_check_c:
        push    ebp
        mov     ebp, esp
        push    ebx
        push    esi
        push    edi
        mov     eax, [ebp+8]
        mov     edx, [ebp+12]
        mov     esi, [ebp+16]
        mov     ebx, 5A5A5A5Ah
        mov     ecx, 0DEADh
        call    fat_chain_check
        mov     edi, [ebp+20]
        mov     [edi], ecx
        cmp     ebx, 5A5A5A5Ah
        jne     .clob
        cmp     edx, [ebp+12]
        jne     .clob
        cmp     esi, [ebp+16]
        jne     .clob
        jmp     .ret
.clob:
        mov     eax, 99
.ret:
        pop     edi
        pop     esi
        pop     ebx
        pop     ebp
        ret
"""
    return ("bits 32\n" + geometry_block(text, geom) + m.group(0) + "\n"
            + "section .text\n" + wrapper + body + "\n")


# ---------------------------------------------------------------- ビルド

def build(work, srcs, target, geom):
    work = pathlib.Path(work) / geom
    work.mkdir(exist_ok=True)
    common = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
              "-fno-stack-protector", "-fno-pie", "-Wall", "-Werror"]
    if target:
        cc = "i386-elf-gcc"
        if shutil.which(cc) is None:
            raise Fail("i386-elf-gcc が PATH に無い (--target)")
    else:
        cc = "gcc"
        common = common + ["-fno-pic"]
    # ローダと同じ -Os (build/boot.mk CFLAGS_BOOT)。-I boot で
    # "../lib/crc32_core.inc" が実物を指す。
    inc = ["-I" + str(ROOT / "boot")]
    for key, obj in (("vk32", "vk32.o"), ("mini", "mini.o")):
        src = work / pathlib.Path(srcs[key]).name
        shutil.copy(srcs[key], src)
        sh([cc] + common + ["-Os"] + inc + ["-c", str(src), "-o", str(work / obj)])

    text = pathlib.Path(srcs["asm"]).read_text(encoding="utf-8")
    (work / "vk32.asm").write_text(asm_vk32(text), encoding="utf-8")
    sh(["nasm", "-f", "elf32", str(work / "vk32.asm"), "-o", str(work / "asm.o")])
    (work / "fat.asm").write_text(asm_fat(text, geom), encoding="utf-8")
    sh(["nasm", "-f", "elf32", str(work / "fat.asm"), "-o", str(work / "fat.o")])

    sh(["gcc", "-std=gnu11", "-m32", "-ffreestanding", "-fno-pic", "-fno-pie",
        "-fno-stack-protector", "-fno-builtin", "-O2", "-Wall", "-Wextra",
        "-Werror", 
        "-c", str(HARNESS), "-o", str(work / "harness.o")])
    exe = work / "vk32_host"
    sh(["ld", "-m", "elf_i386", "-static", "-e", "_start", "-o", str(exe),
        str(work / "harness.o"), str(work / "vk32.o"), str(work / "mini.o"),
        str(work / "asm.o"), str(work / "fat.o")])
    return exe


def run(exe, mode, data):
    inp = struct.pack("<2I", mode, len(data)) + data
    r = host32.run([str(exe)], input=inp, capture_output=True, timeout=120)
    if r.returncode == 3:
        raise Fail("mode {}: 窓の後ろ (番兵) を書き越した".format(mode))
    if r.returncode != 0:
        raise Fail("mode {}: harness exit {} (落ちた)".format(mode, r.returncode))
    rc, v = struct.unpack("<iI", r.stdout[:8])
    return rc, v, r.stdout[8:]


# ---------------------------------------------------------------- イメージ

def synthetic():
    rnd = random.Random(20260924)

    def rb(n):
        return bytes(rnd.getrandbits(8) for _ in range(n))
    blk = rb(3000)
    kernel = rb(2000) + bytes(40000) + blk + rb(30000) + blk + b"OS32 " * 2000
    sqlite = rb(1500) + (b"\x00\x01\x02\x03" * 3000) + rb(200) + bytes(9000) + rb(17)
    return kernel, sqlite


def make_image(script, work, kernel, sqlite, label):
    work = pathlib.Path(work)
    kp, sp, op = work / (label + "_k.bin"), work / (label + "_s.bin"), work / (label + ".lz4")
    kp.write_bytes(kernel)
    sp.write_bytes(sqlite)
    r = subprocess.run([sys.executable, "-B", str(script),
                        "--kernel", str(kp), "--kernel-addr", "0x100000",
                        "--sqlite", str(sp), "--sqlite-addr", "0x200000",
                        "--limit-header", str(DEFS), "-o", str(op)],
                       capture_output=True)
    if r.returncode != 0:
        raise Fail("[{}] mkvmkernel が失敗\n{}".format(label, r.stderr.decode(errors="replace")))
    return op.read_bytes()


def parse(img):
    magic, hsz, ver, n = struct.unpack_from("<4I", img, 0)
    ents = [list(struct.unpack_from("<4I", img, 16 + 16 * i)) for i in range(n)]
    crcs = list(struct.unpack_from("<{}I".format(n), img, 16 + 16 * n))
    isize, icrc = struct.unpack_from("<2I", img, 16 + 20 * n)
    return dict(magic=magic, hsz=hsz, ver=ver, n=n, ents=ents, crcs=crcs,
                isize=isize, icrc=icrc)


def file_crc(img, n):
    off = 16 + 20 * n + 4
    return zlib.crc32(img[:off] + b"\0\0\0\0" + img[off + 4:]) & 0xFFFFFFFF


def reseal(img):
    """image_crc を今の中身で付け直す (後ろの検査まで届かせる)。"""
    img = bytearray(img)
    n = struct.unpack_from("<I", img, 12)[0]
    if 1 <= n <= 4 and 16 + 20 * n + 8 <= len(img):
        struct.pack_into("<I", img, 16 + 20 * n + 4, file_crc(bytes(img), n))
    return bytes(img)


def put(img, off, fmt, *vals):
    img = bytearray(img)
    struct.pack_into(fmt, img, off, *vals)
    return bytes(img)


def check_format(img, kernel, sqlite, label):
    """生成物を zlib と突き合わせる。"""
    h = parse(img)
    out = []
    if h["magic"] != 0x32334B56 or h["ver"] != 2 or h["n"] != 2:
        raise Fail("[{}] 共通部 {}".format(label, h))
    if h["hsz"] != 16 + 20 * 2 + 8:
        raise Fail("[{}] header_size {} != 64".format(label, h["hsz"]))
    if h["isize"] != len(img):
        raise Fail("[{}] image_size {} != 長さ {}".format(label, h["isize"], len(img)))
    want = file_crc(img, 2)
    if h["icrc"] != want:
        raise Fail("[{}] image_crc {:08x} != zlib {:08x}".format(label, h["icrc"], want))
    for i, raw in enumerate((kernel, sqlite)):
        if h["crcs"][i] != zlib.crc32(raw) & 0xFFFFFFFF:
            raise Fail("[{}] entry_crc[{}] {:08x} != zlib {:08x}".format(
                label, i, h["crcs"][i], zlib.crc32(raw) & 0xFFFFFFFF))
        a, r, o, c = h["ents"][i]
        if r != len(raw) or lz4.block.decompress(img[o:o + c], uncompressed_size=r) != raw:
            raise Fail("[{}] entry[{}] の中身が元と違う".format(label, i))
    if h["ents"][0][2] != h["hsz"]:
        raise Fail("[{}] データが header_size の直後から始まらない".format(label))
    out.append("[{}] format: v2 hsz=64 image_size={} image_crc={:08x} entry_crc={} "
               "(zlib と一致)".format(label, len(img), h["icrc"],
                                     " ".join("{:08x}".format(c) for c in h["crcs"])))
    return out


def check_good(exe, img, kernel, sqlite, label):
    h = parse(img)
    out = []
    for mode in (0, 1):
        rc, crc, win = run(exe, mode, img)
        if rc != 0:
            raise Fail("[{}] mode {} が正常なイメージを断った rc={}".format(label, mode, rc))
        if crc != h["icrc"]:
            raise Fail("[{}] mode {} out_crc {:08x} != {:08x}".format(label, mode, crc, h["icrc"]))
        spans = []
        for (a, r, o, c), raw in zip(h["ents"], (kernel, sqlite)):
            s = a - LOAD_MIN
            if win[s:s + r] != raw:
                raise Fail("[{}] mode {} 展開結果が元と違う".format(label, mode))
            spans.append((s, s + r))
        spans.sort()
        pos = 0
        for s, e in spans + [(WINDOW, WINDOW)]:
            if win[pos:s].count(0xCC) != s - pos:
                raise Fail("[{}] mode {} エントリの外 [{:#x},{:#x}) を書いた".format(
                    label, mode, pos + LOAD_MIN, s + LOAD_MIN))
            pos = e
    out.append("[{}] good: C / ASM とも 0、out_crc={:08x}、窓の中身一致、外は無傷".format(
        label, h["icrc"]))
    return out


def lz4_trunc_lit():
    """リテラル長の延長中に入力が尽きる (旧 ASM は .lz4_err でスタックを崩した)。"""
    return b"\xF0\xFF\xFF"


def lz4_trunc_match():
    """マッチ長の延長中に入力が尽きる。"""
    return b"\x1F" + b"A" + b"\x01\x00" + b"\xFF"


def corrupt_cases(img):
    """(名前, イメージ, 期待する VK32_ERR_*, 展開前に止まるか)。"""
    h = parse(img)
    n = h["n"]
    hsz = h["hsz"]
    k_addr, k_raw, k_off, k_csz = h["ents"][0]
    s_addr, s_raw, s_off, s_csz = h["ents"][1]
    L = len(img)
    e1 = 16 + 16  # entry[1] の位置
    cases = []

    def c(name, data, code, pre=True):
        cases.append((name, data, E[code], pre))

    # ---- 長さ・共通部
    c("空", b"", "SIZE")
    c("15 バイト", img[:15], "SIZE")
    c("上限 + 1 バイト", img + bytes(MAX_IMAGE + 1 - L), "SIZE")
    c("magic", put(img, 0, "<I", 0x31334B56), "MAGIC")
    c("version 1 (旧形式)", put(img, 8, "<I", 1), "VERSION")
    c("entry_count 0", put(img, 12, "<I", 0), "COUNT")
    c("entry_count 5", put(img, 12, "<I", 5), "COUNT")
    c("header_size が 1 大きい", put(img, 4, "<I", hsz + 1), "HEADER")
    c("header_size が旧形式 (16+16n)", put(img, 4, "<I", 16 + 16 * n), "HEADER")
    c("header だけ (切り詰め)", img[:hsz], "LENGTH")
    c("header の途中まで", img[:40], "HEADER")
    # ---- 完全長
    c("末尾 1 バイト切り詰め", img[:-1], "LENGTH")
    c("末尾に 1 バイト足す", img + b"\0", "LENGTH")
    c("image_size が 1 大きい (CRC は付け直す)", reseal(put(img, 16 + 20 * n, "<I", L + 1)), "LENGTH")
    # ---- ファイル全体の CRC
    for pos, what in ((k_off + 100, "kernel の圧縮データ"), (s_off + s_csz - 1, "最後のバイト"),
                      (16 + 16 * n, "entry_crc[0]"), (e1 + 0, "entry[1].load_addr"),
                      (16 + 20 * n + 4, "image_crc の欄")):
        b = bytearray(img)
        b[pos] ^= 0x01
        c("1 ビット反転: " + what, bytes(b), "FILE_CRC")
    # ---- 入力の範囲 (CRC を付け直して検査の後段まで届かせる)
    c("data_offset がファイル長", reseal(put(img, e1 + 8, "<I", L)), "SRC")
    c("data_offset がファイル長 + 1", reseal(put(img, e1 + 8, "<I", L + 1)), "SRC")
    c("data_offset が header の中", reseal(put(img, 16 + 8, "<I", hsz - 4)), "SRC")
    c("compressed_size が 1 大きい", reseal(put(img, e1 + 12, "<I", s_csz + 1)), "SRC")
    c("compressed_size = 0xFFFFFFFF (桁あふれ)", reseal(put(img, e1 + 12, "<I", 0xFFFFFFFF)), "SRC")
    c("data_offset = 0xFFFFFFF0 (桁あふれ)", reseal(put(img, e1 + 8, "<I", 0xFFFFFFF0)), "SRC")
    # ---- 展開先の範囲
    c("load_addr = 0x0FFFFF (帯の下)", reseal(put(img, 16, "<I", 0x0FFFFF)), "DST")
    c("load_addr = 0x10000 (読み込み域)", reseal(put(img, 16, "<I", 0x10000)), "DST")
    c("load_addr = 帯の上端", reseal(put(img, e1, "<I", LOAD_END)), "DST")
    c("末尾が帯の上端を 1 バイト越える", reseal(put(img, e1, "<I", LOAD_END - s_raw + 1)), "DST")
    c("load_addr + raw_size が桁あふれ", reseal(put(img, e1, "<I", 0xFFFFFF00)), "DST")
    c("sqlite が kernel に重なる", reseal(put(img, e1, "<I", k_addr + k_raw - 1)), "DST")
    c("raw_size = 0", reseal(put(img, e1 + 4, "<I", 0)), "DST")
    # 末尾がちょうど上端 (通る側の境界) は展開して RAW でなく成功する
    # ---- 展開 (decoded == raw_size)
    c("raw_size が 1 大きい", reseal(put(img, e1 + 4, "<I", s_raw + 1)), "RAW_SIZE", False)
    c("raw_size が 1 小さい (出力が溢れる)", reseal(put(img, e1 + 4, "<I", s_raw - 1)), "DECODE", False)
    # 圧縮データそのものを差し替えた (CRC は付け直す)
    for name, stream, raw in (("リテラル延長の途中で入力が尽きる", lz4_trunc_lit(), 300),
                              ("マッチ延長の途中で入力が尽きる", lz4_trunc_match(), 300),
                              ("リテラルが入力より長い", b"\x50AB", 5),
                              ("offset が出力済みより遠い", b"\x10A\x05\x00", 10),
                              ("offset = 0", b"\x10A\x00\x00", 10),
                              ("offset の途中で入力が尽きる", b"\x10A\x01", 10),
                              ("マッチが出力の残りを越える", b"\x14A\x01\x00", 5)):
        b = bytearray(img[:s_off]) + stream
        struct.pack_into("<I", b, e1 + 4, raw)
        struct.pack_into("<I", b, e1 + 12, len(stream))
        struct.pack_into("<I", b, 16 + 20 * n, len(b))
        c("LZ4: " + name, reseal(bytes(b)), "DECODE", False)
    # ---- 展開後の CRC
    c("entry_crc[1] (ファイル CRC は付け直す)", reseal(put(img, 16 + 16 * n + 4, "<I",
                                                   h["crcs"][1] ^ 1)), "ENTRY_CRC", False)
    c("entry_crc[0] = 0", reseal(put(img, 16 + 16 * n, "<I", 0)), "ENTRY_CRC", False)
    return cases


def outside_touched(win, data):
    """展開の後に止まる場合: ヘッダのエントリ (壊した後の値) の外を書いていないか。
    書いていたら最初の番地 (帯の中の物理番地) を返す。無傷なら None。"""
    n = struct.unpack_from("<I", data, 12)[0]
    spans = []
    for i in range(min(n, 4)):
        a, r = struct.unpack_from("<2I", data, 16 + 16 * i)
        s = max(a - LOAD_MIN, 0)
        e = min(a - LOAD_MIN + r, WINDOW)
        if s < e:
            spans.append((s, e))
    spans.sort()
    pos = 0
    for s, e in spans + [(WINDOW, WINDOW)]:
        if s > pos:
            seg = win[pos:s]
            if seg.count(0xCC) != len(seg):
                k = next(i for i, b in enumerate(seg) if b != 0xCC)
                return LOAD_MIN + pos + k
        pos = max(pos, e)
    return None


def check_corrupt(exe, img, label):
    out = []
    bad = []
    post = 0
    for name, data, want, pre in corrupt_cases(img):
        got = []
        for mode in (0, 1):
            rc, crc, win = run(exe, mode, data)
            got.append(rc)
            if pre and win.count(0xCC) != WINDOW:
                bad.append("{}: mode {} が展開の前に止まらず窓を書いた".format(name, mode))
            if not pre:
                where = outside_touched(win, data)
                if where is not None:
                    bad.append("{}: mode {} がエントリの外 0x{:X} を書いた".format(name, mode, where))
        post += 0 if pre else 1
        if got != [want, want]:
            bad.append("{}: 期待 {} / C {} / ASM {}".format(name, want, got[0], got[1]))
    if bad:
        raise Fail("[{}] 壊したイメージ:\n  ".format(label) + "\n  ".join(bad))
    out.append("[{}] corrupt: {} 通りすべて C / ASM が同じ VK32_ERR_* で断った "
               "(展開の後に止まる {} 通りもエントリの外は無傷)".format(
                   label, len(corrupt_cases(img)), post))
    # 通る側の境界: 末尾がちょうど帯の上端
    h = parse(img)
    s_raw = h["ents"][1][1]
    edge = reseal(put(img, 16 + 16, "<I", LOAD_END - s_raw))
    for mode in (0, 1):
        rc, _, win = run(exe, mode, edge)
        if rc != 0:
            raise Fail("[{}] 末尾がちょうど上端のイメージを mode {} が断った rc={}".format(
                label, mode, rc))
    out.append("[{}] edge: 末尾がちょうど 0x{:X} のエントリは通る".format(label, LOAD_END))
    return out


# ---------------------------------------------------------------- FAT チェーン

def fat12_set(fat, cl, val):
    off = cl + cl // 2
    v = fat[off] | (fat[off + 1] << 8)
    if cl & 1:
        v = (v & 0x000F) | (val << 4)
    else:
        v = (v & 0xF000) | val
    fat[off] = v & 0xFF
    fat[off + 1] = (v >> 8) & 0xFF


def chain_fat(geom, chain, tail=0xFFF):
    g = GEOMS[geom]
    fat = bytearray(g["fat_bytes"])
    fat12_set(fat, 0, 0xFFE)
    fat12_set(fat, 1, 0xFFF)
    for a, b in zip(chain, chain[1:]):
        fat12_set(fat, a, b)
    if chain:
        fat12_set(fat, chain[-1], tail)
    return fat


def fat_run(exe, start, size, fat):
    rc, cnt, _ = run(exe, 2, struct.pack("<2I", start, size) + bytes(fat))
    return rc, cnt


def check_fat(exe, geom):
    g = GEOMS[geom]
    sect = g["sect"]
    max_cl = g["total"] - g["data"] + 2
    out = []
    bad = []

    def expect(name, start, size, fat, code, cnt=None):
        rc, n = fat_run(exe, start, size, fat)
        if rc != FATCHK[code] or (cnt is not None and n != cnt):
            bad.append("{}: 期待 {}/{} 実際 {}/{}".format(name, FATCHK[code], cnt, rc, n))

    size = 3 * sect + 1                      # 4 クラスタ
    good = list(range(10, 14))
    expect("連続 4 クラスタ", 10, size, chain_fat(geom, good), "OK", 4)
    expect("飛び飛び 4 クラスタ", 20, size, chain_fat(geom, [20, 7, 300, 5]), "OK", 4)
    expect("ちょうど 1 クラスタ", 9, sect, chain_fat(geom, [9]), "OK", 1)
    expect("最後の有効クラスタ", max_cl - 1, 1, chain_fat(geom, [max_cl - 1]), "OK", 1)
    expect("長さ 0", 10, 0, chain_fat(geom, good), "SIZE")
    expect("上限 + 1", 10, MAX_IMAGE + 1, chain_fat(geom, good), "SIZE")
    expect("早期終端 (3 で EOC)", 10, size, chain_fat(geom, good[:3]), "SHORT")
    expect("EOC が 0xFF8 (最小の EOC)", 10, size, chain_fat(geom, good[:3], 0xFF8), "SHORT")
    expect("長すぎる (5 つ目がある)", 10, size, chain_fat(geom, good + [14]), "LONG")
    fat = chain_fat(geom, good)
    fat12_set(fat, 13, 11)                   # 10 → 11 → 12 → 13 → 11 → ...
    expect("循環 (13 → 11)", 10, 50 * sect, fat, "LONG")
    fat = chain_fat(geom, good)
    fat12_set(fat, 10, 10)                   # 自分自身を指す
    expect("自己循環", 10, 20 * sect, fat, "LONG")
    for v, why in ((0, "0"), (1, "1"), (max_cl, "MAX_CLUSTER"), (0xFF0, "予約 0xFF0"),
                   (0xFF7, "不良 0xFF7")):
        expect("範囲外の開始 " + why, v, size, chain_fat(geom, good), "START")
        fat = chain_fat(geom, good)
        fat12_set(fat, 11, v)
        expect("範囲外の途中 " + why, 10, size, fat, "RANGE")
    # 開始クラスタが EOC (0xFF8〜) — ディレクトリの値が壊れている。早期終端とは言わない
    for v in (0xFF8, 0xFFF):
        expect("開始が EOC {:#x}".format(v), v, size, chain_fat(geom, good), "START")
    # 範囲外のクラスタの FAT 欄に EOC が書いてあっても断る (次を読んで気づくのでは遅い)
    for v in (max_cl, 0xFEF):
        fat = chain_fat(geom, [])
        if v + v // 2 + 1 < len(fat):
            fat12_set(fat, v, 0xFFF)
        expect("開始が範囲外 {:#x} (欄は EOC)".format(v), v, 1, fat, "START")
        fat = chain_fat(geom, [10])
        fat12_set(fat, 10, v)
        if v + v // 2 + 1 < len(fat):
            fat12_set(fat, v, 0xFFF)
        expect("途中が範囲外 {:#x} (欄は EOC)".format(v), 10, 2 * sect, fat, "RANGE")
    # 上限ちょうど (508KiB) は通る: 連続 (上限 / sect) クラスタ
    ncl = MAX_IMAGE // sect
    if 2 + ncl <= max_cl:
        expect("上限ちょうど", 2, MAX_IMAGE, chain_fat(geom, list(range(2, 2 + ncl))), "OK", ncl)
    else:
        bad.append("上限ちょうどのチェーンがディスクに収まらない (試験の前提)")
    if bad:
        raise Fail("[fat {}]\n  ".format(geom) + "\n  ".join(bad))
    out.append("[fat {}] 正常・長さ・開始クラスタ (0/1/MAX/0xFF0/0xFF7/EOC)・早期終端・循環・"
               "途中の範囲外を fat_chain_check が区別した (MAX_CLUSTER={})".format(geom, max_cl))
    return out


def read_fd_file(img, geom, name83):
    """mkfat12 の FD イメージからルートの 1 ファイルの (開始クラスタ, 長さ, FAT, データ)。"""
    g = GEOMS[geom]
    sect = g["sect"]
    fat_sects = g["fat_bytes"] // sect
    fat = img[sect:sect + g["fat_bytes"]]
    root = img[(1 + 2 * fat_sects) * sect:g["data"] * sect]
    for i in range(0, len(root), 32):
        e = root[i:i + 32]
        if e[0] == 0:
            break
        if e[:11] == name83:
            cl, size = struct.unpack_from("<HI", e, 26)
            data = bytearray()
            c = cl
            while c < 0xFF8 and len(data) < size + sect:
                off = (g["data"] + c - 2) * sect
                data += img[off:off + sect]
                o = c + c // 2
                v = fat[o] | (fat[o + 1] << 8)
                c = (v >> 4) if c & 1 else (v & 0xFFF)
            return cl, size, fat, bytes(data[:size])
    raise Fail("{} が {} に無い".format(name83, geom))


def check_real(exe_by_geom, script, work):
    out = []
    if not REAL_VMK.is_file():
        raise Fail("[real] build/out/vmkernel.lz4 が無い (make kernel が先)")
    # kernel.bin / sqlite.bin とは突き合わせない: kapi_sys.o は毎回組み直される
    # (__DATE__ __TIME__) ので、make check の並列の中で別の目標が kernel を組み直すと
    # kernel.bin と vmkernel.lz4 が一瞬食い違う。中身は python-lz4 で展開した
    # ものを正とし、CRC はそれを zlib で計算して突き合わせる (ローダとは独立)。
    img = REAL_VMK.read_bytes()
    h = parse(img)
    k, s = (lz4.block.decompress(img[o:o + c], uncompressed_size=r)
            for (a, r, o, c) in h["ents"])
    out += check_format(img, k, s, "real")
    out += check_good(exe_by_geom["2hd"], img, k, s, "real")
    out += check_corrupt(exe_by_geom["2hd"], img, "real")
    out.append("[real] vmkernel.lz4 {} B、上限 {} まで残り {} B".format(
        len(img), MAX_IMAGE, MAX_IMAGE - len(img)))
    for geom, path in FD_IMAGES.items():
        if not path.is_file():
            raise Fail("[real] {} が無い (make all / make fd144 が先)".format(path))
        fd = path.read_bytes()
        cl, size, fat, data = read_fd_file(fd, geom, b"VMKRNL  LZ4")
        rc, cnt = fat_run(exe_by_geom[geom], cl, size, fat)
        sect = GEOMS[geom]["sect"]
        if rc != 0 or cnt != (size + sect - 1) // sect:
            raise Fail("[real] {} の VMKRNL.LZ4 のチェーンを断った rc={} cnt={}".format(geom, rc, cnt))
        # FD の VMKRNL.LZ4 そのものを両ローダに通す (build/out と同じ版とは限らない
        # — 上と同じ理由。同じかどうかは check-packages-host が見る)。
        for mode in (0, 1):
            rc, crc, _ = run(exe_by_geom[geom], mode, data)
            if rc != 0 or crc != parse(data)["icrc"]:
                raise Fail("[real] {} の VMKRNL.LZ4 を mode {} が断った rc={}".format(geom, mode, rc))
        lcl, lsize, _, ldata = read_fd_file(fd, geom, b"LOADER  BIN")
        lrc, lcnt = fat_run(exe_by_geom[geom], lcl, lsize, fat)
        boot_name = "loader_fat_new.bin" if geom == "2hd" else "loader_fat144.bin"
        want = (ROOT / "boot" / boot_name).read_bytes()
        if lrc != 0 or ldata != want:
            raise Fail("[real] {} の LOADER.BIN のチェーン rc={} / 中身一致={}".format(
                geom, lrc, ldata == want))
        out.append("[real] {}: VMKRNL.LZ4 {} クラスタを fat_chain_check が通し、C / ASM が検査を通した。"
                   "LOADER.BIN {} B = {} クラスタのチェーンも一致 (IPL が辿る形)".format(
                       geom, cnt, lsize, lcnt))
    return out


# ---------------------------------------------------------------- 写しの一致

def c_values(header, names, extra_inc=()):
    """ヘッダを host gcc で読んで名前ごとの値を得る。"""
    with tempfile.TemporaryDirectory(prefix="vk32_vals_") as d:
        src = pathlib.Path(d) / "v.c"
        body = "".join('printf("{0}=%lu\\n", (unsigned long)({0}));\n'.format(n) for n in names)
        src.write_text('#include <stdio.h>\n#include "{}"\nint main(void){{\n{}return 0;}}\n'.format(
            header, body), encoding="utf-8")
        exe = pathlib.Path(d) / "v"
        sh(["gcc", "-std=gnu11", "-w"] + ["-I" + str(i) for i in extra_inc]
           + [str(src), "-o", str(exe)])
        r = sh([str(exe)])
    return {k: int(v) for k, v in (ln.split("=") for ln in r.stdout.decode().split())}


def asm_equs(text):
    out = {}
    for m in re.finditer(r"^([A-Z_][A-Z0-9_]*)\s+EQU\s+(-?[0-9A-Fa-fx]+h?)\s*(?:;.*)?$",
                         text, re.MULTILINE):
        v = m.group(2)
        if v.lower().startswith("0x") or v.lower().startswith("-0x"):
            n = int(v, 16)
        elif v.lower().endswith("h"):
            n = int(v[:-1], 16)
        else:
            n = int(v)
        out[m.group(1)] = n & 0xFFFFFFFF
    return out


MIRROR_ASM = ["VK32_MAGIC", "VK32_VERSION", "VK32_MAX_ENTRIES", "VK32_LOAD_MIN",
              "VK32_LOAD_END", "MAX_IMAGE_SIZE", "VK32_ERR_SIZE", "VK32_ERR_MAGIC",
              "VK32_ERR_VERSION", "VK32_ERR_COUNT", "VK32_ERR_HEADER", "VK32_ERR_LENGTH",
              "VK32_ERR_FILE_CRC", "VK32_ERR_SRC", "VK32_ERR_DST", "VK32_ERR_DECODE",
              "VK32_ERR_RAW_SIZE", "VK32_ERR_ENTRY_CRC", "VK32_ERR_MIN"]
MIRROR_BI = ["BI_OFF_IMG_CRC", "BI_OFF_IMG_SIZE", "BI_OFF_IMG_CHECK", "BOOTINFO_IMG_KEY"]


def check_mirror(asm_text):
    defs = c_values(str(DEFS), MIRROR_ASM + MIRROR_BI + ["BOOTINFO_BASE"])
    bad = []
    equ = asm_equs(asm_text)
    for n in MIRROR_ASM:
        if n not in equ:
            bad.append("ASM に {} が無い".format(n))
        elif equ[n] != defs[n] & 0xFFFFFFFF:
            bad.append("{}: ASM 0x{:X} / boot_defs.h 0x{:X}".format(n, equ[n], defs[n] & 0xFFFFFFFF))
    if defs["MAX_IMAGE_SIZE"] != MAX_IMAGE or defs["VK32_LOAD_MIN"] != LOAD_MIN or \
            defs["VK32_LOAD_END"] != LOAD_END:
        bad.append("試験の前提 (MAX_IMAGE / LOAD_MIN / LOAD_END) が boot_defs.h と違う")
    for n, v in E.items():
        if defs["VK32_ERR_" + n] & 0xFFFFFFFF != v & 0xFFFFFFFF:
            bad.append("試験の VK32_ERR_{} が boot_defs.h と違う".format(n))
    bi = c_values(str(BOOTINFO_H), MIRROR_BI, (ROOT / "include",))
    for n in MIRROR_BI:
        if bi[n] != defs[n]:
            bad.append("{}: include/bootinfo.h 0x{:X} / boot_defs.h 0x{:X}".format(n, bi[n], defs[n]))
    return bad


# ---------------------------------------------------------------- 全体

def run_all(srcs, script, target, real):
    lines = []
    asm_text = pathlib.Path(srcs["asm"]).read_text(encoding="utf-8")
    bad = check_mirror(asm_text)
    if bad:
        raise Fail("[mirror]\n  " + "\n  ".join(bad))
    lines.append("[mirror] ASM の VK32_* / MAX_IMAGE_SIZE と boot_defs.h、"
                 "boot_defs.h のイメージ欄と include/bootinfo.h が名前ごとに一致")
    with tempfile.TemporaryDirectory(prefix="vk32_crc_") as work:
        exes = {g: build(work, srcs, target, g) for g in GEOMS}
        k, s = synthetic()
        img = make_image(script, work, k, s, "synthetic")
        lines += check_format(img, k, s, "synthetic")
        lines += check_good(exes["2hd"], img, k, s, "synthetic")
        lines += check_corrupt(exes["2hd"], img, "synthetic")
        for g in GEOMS:
            lines += check_fat(exes[g], g)
        if real:
            lines += check_real(exes, script, work)
    return lines


# ---------------------------------------------------------------- 変異

MUTATIONS = [
    # (対象キー, パターン, 置換, 何回目 (0 始まり、-1 = すべて), 説明)
    ("none", "", "", 0, "対照: 何も変えない (GREEN であること)"),
    ("vk32", "    if (rd32(file + VK32_OFF_IMAGE_SIZE(n)) != file_size) return VK32_ERR_LENGTH;\n",
     "", 0, "C: 完全長を見ない"),
    ("vk32", "        return VK32_ERR_FILE_CRC;", "        ;", 0, "C: ファイル全体の CRC を見ない"),
    ("vk32", "            return VK32_ERR_ENTRY_CRC;", "            ;", 0, "C: 展開後の CRC を見ない"),
    ("vk32", "raw > VK32_LOAD_END - addr", "0", 0, "C: 末尾が帯を越えるのを見ない"),
    ("vk32", "if (addr < a2 + r2 && a2 < addr + raw)", "if (0 && addr < a2 + r2 && a2 < addr + raw)", 0,
     "C: エントリ同士の重なりを見ない"),
    ("vk32", "        if ((u32)decoded != raw) return VK32_ERR_RAW_SIZE;\n", "", 0,
     "C: decoded == raw_size を見ない"),
    ("vk32", "if (n < 1 || n > (u32)VK32_MAX_ENTRIES)", "if (n > (u32)VK32_MAX_ENTRIES)", 0,
     "C: entry_count 0 を通す"),
    ("vk32", " || csz > file_size - off", " || (0 && csz > file_size - off)", 0,
     "C: compressed_size の範囲を見ない"),
    ("vk32", "if (off < hsz || ", "if (", 0, "C: data_offset が header の中を通す"),
    ("vk32", "s = crc32_core_update(s, zero4, 4);",
     "(void)zero4; s = crc32_core_update(s, file + crc_off, 4);", 0,
     "C: CRC の欄を 0 として計算しない"),
    ("mini", "if (lit_len > (int)(op_end - op)) return -2;", ";", 0,
     "lz4_mini: リテラルの出力境界を見ない"),
    ("mini", "if (match_len > (int)(op_end - op)) return -2;", ";", 0,
     "lz4_mini: マッチの出力境界を見ない (エントリの外を書く)"),
    ("asm", "jne     .e_file_crc", "nop", 0, "ASM: ファイル全体の CRC を見ない"),
    ("asm", "jne     .e_entry_crc", "nop", 0, "ASM: 展開後の CRC を見ない"),
    ("asm", "jne     .e_raw_size", "nop", 0, "ASM: decoded == raw_size を見ない"),
    ("asm", "jne     .e_length", "nop", 0, "ASM: 完全長を見ない"),
    ("asm", "jb      .e_dst", "nop", 1, "ASM: エントリ同士の重なりを見ない"),
    ("asm", "ja      .e_dst", "nop", 0, "ASM: 末尾が帯を越えるのを見ない"),
    ("asm", "ja      .e_src", "nop", 1, "ASM: compressed_size の範囲を見ない"),
    ("asm", "jz      .e_count", "nop", 0, "ASM: entry_count 0 を通す"),
    ("asm", "ja      .lz4_err_out", "nop", 0, "ASM lz4: リテラルの出力境界を見ない"),
    ("asm", "ja      .lz4_err_out", "nop", 1, "ASM lz4: マッチの出力境界を見ない"),
    ("asm", "        cmp     eax, ecx\n        ja      .lz4_err_in\n        mov     ecx, edx",
     "        cmp     eax, ecx\n        nop\n        mov     ecx, edx", 0,
     "ASM lz4: リテラルの入力境界を見ない"),
    ("asm", "        cmp     eax, ecx\n        ja      .lz4_err_in\n        mov     [ebp-28], eax",
     "        cmp     eax, ecx\n        nop\n        mov     [ebp-28], eax", 0,
     "ASM lz4: offset が出力済みより遠いのを見ない"),
    ("asm_old_lz4", "", "", 0,
     "ASM lz4: 基点 3e22825 の旧 pm_lz4_decode に戻す (延長読みの終端で .lz4_err が"
     "スタックを崩す・境界検査なし)"),
    ("asm", "        cld\n", "", -1, "ASM: cld しない (DF=1 で呼ばれると逆向きに写す)"),
    ("asm", "        cmp     eax, MAX_CLUSTER\n        jae     .out",
     "        cmp     eax, MAX_CLUSTER\n        nop", 0, "FAT: 開始クラスタが MAX_CLUSTER 以上 (EOC 含む) を通す"),
    ("asm", "        cmp     eax, MAX_CLUSTER\n        jae     .out",
     "        cmp     eax, MAX_CLUSTER\n        nop", 1, "FAT: 途中のクラスタが MAX_CLUSTER 以上を通す"),
    ("asm", "        mov     ebp, FATCHK_LONG\n        cmp     eax, 0FF8h\n        jb      .out",
     "        mov     ebp, FATCHK_LONG\n        cmp     eax, 0FF8h\n        nop", 0,
     "FAT: 必要な数の次が EOC でなくても通す (循環を見逃す)"),
    ("asm", "        cmp     eax, 2\n        jb      .out", "        cmp     eax, 2\n        nop", 0,
     "FAT: 開始クラスタ 0 / 1 を通す"),
    ("asm", "        cmp     eax, 2\n        jb      .out", "        cmp     eax, 2\n        nop", 1,
     "FAT: 途中のクラスタ 0 / 1 を通す"),
    ("asm", "        mov     ebp, FATCHK_START\n", "        mov     ebp, FATCHK_RANGE\n", 0,
     "FAT: 開始クラスタの壊れを範囲外と同じ文言にする"),
    ("asm", "        shr     edx, 4\n.even:", "        nop\n.even:", 0,
     "FAT: 奇数クラスタの上位 12 ビットを取らない"),
    ("asm", "        jz      .out\n        cmp     edx, MAX_IMAGE_SIZE",
     "        nop\n        cmp     edx, MAX_IMAGE_SIZE", 0, "FAT: 長さ 0 を通す"),
    ("script", "hdr += struct.pack('<II', offset, 0)", "hdr += struct.pack('<II', offset, 1)", 0,
     "mkvmkernel: CRC の欄を 0 にせずに計算する"),
    ("script", "hdr += struct.pack('<II', offset, 0)", "hdr += struct.pack('<II', offset + 1, 0)", 0,
     "mkvmkernel: image_size を 1 ずらす"),
    ("script", "raw_crcs.append(zlib.crc32(raw_data) & 0xFFFFFFFF)",
     "raw_crcs.append(zlib.crc32(raw_data[:-1]) & 0xFFFFFFFF)", 0,
     "mkvmkernel: エントリの CRC を最後の 1 バイト抜きで計算する"),
    ("script", "header_size = common_hdr_size + entry_count * (16 + 4) + 8",
     "header_size = common_hdr_size + entry_count * (16 + 4) + 12", 0,
     "mkvmkernel: header_size を 4 多く書く"),
]


def replace_nth(text, pat, rep, nth):
    if nth < 0:
        return text.replace(pat, rep) if pat in text else None
    idx = -1
    for _ in range(nth + 1):
        idx = text.find(pat, idx + 1)
        if idx < 0:
            return None
    return text[:idx] + rep + text[idx + len(pat):]


OLD_BASE = "3e22825"
# 旧版の pm_lz4_decode の写し (os32 v2.x の OLD_BASE から切り出し)。os32-v3 は
# 履歴を切って fork したので git show OLD_BASE が引けない。
OLD_LZ4 = ROOT / "tools/tests/vk32_old_lz4_3e22825.asm"


def with_old_lz4(text):
    """今の pm_lz4_decode を基点の旧版 (OLD_LZ4 の写し) に差し替えた ASM を返す。"""
    old = OLD_LZ4.read_text(encoding="utf-8")
    a = old.index("pm_lz4_decode:")
    b = len(old)
    na = text.index("pm_lz4_decode:")
    nb = text.index(";; CRC-32 nibble 表", na)
    return text[:na] + old[a:b] + text[nb:]


def _mutation_one(item):
    """変異 1 本を自分専用の一時ディレクトリで組んで回す。run_all は最初の Fail で
    止まる (= 最初に落ちたところで打ち切り)。並列に呼ばれる。(種別, 行) を返す。"""
    target, (key, pat, rep, nth, desc) = item
    base = {"vk32": VK32_SRC, "mini": MINI_SRC, "asm": ASM_SRC, "script": SCRIPT}
    with tempfile.TemporaryDirectory(prefix="vk32_mut_") as mw:
        srcs = dict(base)
        script = SCRIPT
        if key == "asm_old_lz4":
            src = base["asm"]
            new = with_old_lz4(src.read_text(encoding="utf-8"))
            mpath = pathlib.Path(mw) / src.name
            mpath.write_text(new, encoding="utf-8")
            srcs["asm"] = mpath
        elif key != "none":
            src = base[key]
            new = replace_nth(src.read_text(encoding="utf-8"), pat, rep, nth)
            if new is None:
                return "error", "MUTATION ERROR (当たらない): {}".format(desc)
            mpath = pathlib.Path(mw) / src.name
            mpath.write_text(new, encoding="utf-8")
            srcs[key] = mpath
            if key == "script":
                script = mpath
        try:
            run_all(srcs, script, target, False)
        except Fail as e:
            msg = str(e)
            if " -> " in msg.splitlines()[0] and ("gcc" in msg or "nasm" in msg or "ld " in msg):
                return "error", "MUTATION ERROR (組めない): {} -- {}".format(
                    desc, msg.splitlines()[0][:100])
            if key == "none":
                return "error", "CONTROL RED (対照が落ちた — 試験が壊れている): {}".format(
                    msg.splitlines()[0][:120])
            return "red", "MUTATION RED (期待どおり): {} -- {}".format(
                desc, msg.splitlines()[-1].strip()[:100])
        if key == "none":
            return "green", "CONTROL GREEN (期待どおり): {}".format(desc)
        return "error", "MUTATION 生き残り: {}".format(desc)


def run_mutations(target):
    """並列に回し (mutpar、OS32_MUT_JOBS)、結果は変異の順に出す。"""
    n = {"red": 0, "green": 0, "error": 0}
    # 変異 1 本の中で像を組んで 43 通りに壊す Python の計算が重いので、プロセスで並べる
    for kind, line in mutpar.run_ordered(_mutation_one, [(target, m) for m in MUTATIONS],
                                         processes=True):
        print(line, flush=True)
        n[kind] += 1
    total = len(MUTATIONS)
    print("MUTATE 内訳: RED {} / 対照 GREEN {} / 生き残り・ERROR {} (全 {})".format(
        n["red"], n["green"], n["error"], total))
    return n["error"]


def main(args):
    target = "--target" in args
    real = "--real" in args
    if real:
        sys.path.insert(0, str(ROOT / 'tools'))
        from check_artifacts import require_fresh
        require_fresh(ROOT, ['build/out/vmkernel.lz4', 'images/os32_boot.img',
                            'images/os32_boot144.img'],
                      targets={'images/os32_boot.img': 'images/os32_boot.d88'})
    srcs = {"vk32": VK32_SRC, "mini": MINI_SRC, "asm": ASM_SRC}
    try:
        for ln in run_all(srcs, SCRIPT, target, real):
            print(ln)
    except Fail as e:
        print("FAIL:", e)
        return 1
    print("vk32_crc PASS ({}{})".format("i386-elf" if target else "host gcc -m32",
                                       ", real" if real else ""), flush=True)
    if "--mutate" in args:
        if run_mutations(target):
            print("MUTATE FAIL")
            return 1
        print("MUTATE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
