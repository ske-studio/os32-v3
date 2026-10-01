#!/usr/bin/env python3
"""
os32x_hdr.py — OS32X ヘッダの生成を 1 か所にまとめた共通モジュール

`sdk/mkos32x.py` (アプリ。SDK の bin/ にも同じものを配る) と
`tools/mkshlib.py` (共有ライブラリ) の両方がここを import する。
ヘッダの並びは `sdk/include/os32/os32_kapi_shared.h` の OS32Header と一致させる。

ヘッダ v4 (T2c、旧v3の先頭48Bを保つ。票 docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md、KAPI v63):

    0x00 magic          'OS32'
    0x04 header_size    60
    0x08 version        4
    0x0C flags
    0x10 entry_offset
    0x14 text_size
    0x18 bss_size
    0x1C heap_size
    0x20 stack_size     0=256KiB、明示値はページ切上げ・最低16KiB
    0x24 min_api_ver    正典の最低機能版以上 (世代とは別)
    0x28 load_addr      (v2) ELF の .text の番地
    0x2C kapi_data_off  ELF の .os32_kapi_layout
    0x30 kapi_abi_generation
    0x34 memory_layout_generation
    0x38 shlib_protocol  0=依存なし

`kapi_data_off` は**包装時の値ではなくコードが実際に使った配置**から取る:
crt0 (`sdk/crt/crt0_c.c`) と Rust の os32api が非ロードのセクション
`.os32_kapi_layout` に `KAPI_DATA_FIELDS_OFF` を 1 語置き、ここがそれを読む。
**刻印が無ければ失敗する** (ELF を渡さない / 刻印の無いオブジェクトだけで
リンクした、のどちらも「配置が分からない」)。刻印が複数ある場合
(crt0 + os32api) は全部が同じ値でなければ失敗する。
"""

import struct

OS32X_MAGIC = 0x4F533332          # 'OS32'
OS32X_HDR_V1_SIZE = 40
OS32X_HDR_V2_SIZE = 44
OS32X_HDR_V3_SIZE = 48
from os32_generations import *
OS32X_HDR_SIZE = 60
# Address mirrors checked against include/memmap.h by gen_memmap.py.
OS32X_APP_LOAD_ADDR = 0x80100000
OS32X_SHELL_LOAD_ADDR = 0x300000
# v3 のバイナリが要求する最低 KAPI 版 (OS32X_HDR_V3_MIN_API)。
OS32X_HDR_V3_MIN_API = 63

OS32X_FLAG_GFX = 0x0001
OS32X_FLAG_RING3 = 0x0002
OS32X_FLAG_FORCE_CPL0 = 0x0004
OS32X_FLAG_SHLIB = 0x0008
OS32X_FLAG_CUI_ONLY = 0x0010
OS32X_FLAG_LAUNCHER = 0x0020

KAPI_LAYOUT_SECTION = '.os32_kapi_layout'

SHT_NOBITS = 8
SHF_ALLOC = 0x2
PT_LOAD = 1


class HeaderError(Exception):
    """ヘッダを作れない (生成器は終了コード 1 で止める)。"""


class Elf32:
    """32bit LE の ELF の最小リーダ (セクションとシンボル)。"""

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        if len(d) < 52 or d[:4] != b'\x7fELF':
            raise HeaderError(f"{path} は ELF ではない")
        if d[4] != 1 or d[5] != 1:
            raise HeaderError(f"{path} は 32bit LE の ELF ではない")
        (self.e_entry,) = struct.unpack_from('<I', d, 24)
        (self.e_phoff,) = struct.unpack_from('<I', d, 28)
        (self.e_shoff,) = struct.unpack_from('<I', d, 32)
        (self.e_phentsize,) = struct.unpack_from('<H', d, 42)
        (self.e_phnum,) = struct.unpack_from('<H', d, 44)
        (self.e_shentsize,) = struct.unpack_from('<H', d, 46)
        (self.e_shnum,) = struct.unpack_from('<H', d, 48)
        (self.e_shstrndx,) = struct.unpack_from('<H', d, 50)
        # プログラムヘッダ (PT_LOAD の中身を .raw と突き合わせる)
        self.segments = []
        for i in range(self.e_phnum):
            off = self.e_phoff + i * self.e_phentsize
            if off + 32 > len(d):
                raise HeaderError(f"{path} のプログラムヘッダが切れている")
            (ptype, poff, pvaddr, ppaddr, pfilesz, pmemsz, pflags, palign) = \
                struct.unpack_from('<8I', d, off)
            self.segments.append(dict(type=ptype, offset=poff, vaddr=pvaddr,
                                      paddr=ppaddr, filesz=pfilesz,
                                      memsz=pmemsz))
        self.sections = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            (nm, ty, fl, addr, foff, size, link, info, align, entsz) = \
                struct.unpack_from('<10I', d, off)
            self.sections.append(dict(name_off=nm, type=ty, flags=fl, addr=addr,
                                      offset=foff, size=size, link=link,
                                      entsize=entsz))
        if self.e_shstrndx >= len(self.sections):
            raise HeaderError(f"{path} にセクション名の表が無い")
        shstr = self.sections[self.e_shstrndx]
        self.shstrtab = d[shstr['offset']:shstr['offset'] + shstr['size']]
        for s in self.sections:
            s['name'] = self._str(self.shstrtab, s['name_off'])
        self.symbols = {}
        for s in self.sections:
            if s['type'] != 2:          # SHT_SYMTAB
                continue
            strtab_s = self.sections[s['link']]
            strtab = d[strtab_s['offset']:strtab_s['offset'] + strtab_s['size']]
            n = s['size'] // 16
            for i in range(n):
                off = s['offset'] + i * 16
                (st_name, st_value, st_size, st_info, st_other, st_shndx) = \
                    struct.unpack_from('<IIIBBH', d, off)
                nm = self._str(strtab, st_name)
                if nm:
                    self.symbols[nm] = st_value

    @staticmethod
    def _str(tab, off):
        end = tab.find(b'\x00', off)
        return tab[off:end].decode('ascii', errors='replace') if end >= 0 else ''

    def section(self, name):
        for s in self.sections:
            if s['name'] == name:
                return s
        return None

    def bss_size(self):
        s = self.section('.bss')
        return s['size'] if s else 0

    def text_addr(self):
        """`.text` / `.text.startup` の最小番地 (= リンク時のロードアドレス)。"""
        addrs = [s['addr'] for s in self.sections
                 if s['name'] in ('.text', '.text.startup')]
        return min(addrs) if addrs else None

    def loadable_extent(self):
        """objcopy -O binary が書く範囲 (alloc かつ NOBITS でない非空の
        セクションの最小番地〜最大終端)。無ければ None。"""
        lo = None
        hi = None
        for s in self.sections:
            if not (s['flags'] & SHF_ALLOC) or s['type'] == SHT_NOBITS:
                continue
            if s['size'] == 0:
                continue
            a, b = s['addr'], s['addr'] + s['size']
            lo = a if lo is None else min(lo, a)
            hi = b if hi is None else max(hi, b)
        if lo is None:
            return None
        return (lo, hi)

    def section_bytes(self, s):
        return self.data[s['offset']:s['offset'] + s['size']]


def read_kapi_layout(elf):
    """ELF の刻印 (.os32_kapi_layout) から kapi_data_off を返す。

    - セクションが無い / 空 / 4 の倍数でない → HeaderError
    - 非ロード (alloc でない) でなければ HeaderError (平らなバイナリに入る)
    - 刻印が複数あって値が違えば HeaderError
    """
    s = elf.section(KAPI_LAYOUT_SECTION)
    if s is None or s['size'] == 0:
        raise HeaderError(
            f"{elf.path}: KAPI 配置の刻印 ({KAPI_LAYOUT_SECTION}) が無い — "
            "crt0 (sdk/crt/crt0_c.c) か os32api をリンクしているか、"
            "crt0 を使わないなら OS32_KAPI_LAYOUT_STAMP(); を書く "
            "(票 TASK_KAPI_DATA_FIELDS)。古い SDK / 古いリンカ台本なら作り直す")
    if s['flags'] & SHF_ALLOC:
        raise HeaderError(
            f"{elf.path}: {KAPI_LAYOUT_SECTION} がロードされるセクションになっている "
            "(リンカ台本の `(INFO)` + KEEP が要る — sdk/link/*.ld)")
    if s['type'] == SHT_NOBITS or s['size'] % 4:
        raise HeaderError(f"{elf.path}: {KAPI_LAYOUT_SECTION} の大きさが不正 ({s['size']})")
    body = elf.section_bytes(s)
    vals = [struct.unpack_from('<I', body, i)[0] for i in range(0, len(body), 4)]
    uniq = sorted(set(vals))
    if len(uniq) != 1:
        raise HeaderError(
            f"{elf.path}: KAPI 配置の刻印が食い違う "
            f"({', '.join('0x%X' % v for v in uniq)}) — 古いオブジェクトが混ざっている。"
            "make clean で作り直す")
    if uniq[0] == 0:
        raise HeaderError(f"{elf.path}: KAPI 配置の刻印が 0")
    from link_guard import check_note
    check_note(elf)
    from pathlib import Path
    import json, hashlib
    manifest = Path(elf.path).with_suffix('.inputs.json')
    if not manifest.exists():
        raise HeaderError(f"{elf.path}: missing validated link inputs; use link_guard.py")
    inputs = json.loads(manifest.read_text())
    if inputs.get('elf_sha256') != hashlib.sha256(elf.data).hexdigest():
        raise HeaderError(f"{elf.path}: ELF differs from validated link inputs")

    return uniq[0]


def check_raw_matches_elf(elf, raw, what='raw'):
    """平らなバイナリ (.raw) がこの ELF から作られたかを**内容で**確かめる。

    旧 .raw と新 ELF の取り違えを生成工程で止める (ヘッダは ELF から作るので、
    本文だけが古いと配置の刻印が嘘になる)。大きさだけでは足りない — 旧配置の
    shm_base を読む命令と新配置を読む命令はどちらも disp32 で同じ長さなので、
    同じ大きさで中身だけ違う .raw が通ってしまう (実装レビュー R1、Codex
    blocker 1)。

    `objcopy -O binary` はロードされるセクションを最小番地から並べ、隙間を 0 で
    埋める。そこで ELF の PT_LOAD のうち**ファイルに実体のある部分**
    (p_offset..p_offset+p_filesz) を、.raw の (p_paddr - 先頭番地) から
    1 バイトずつ突き合わせる。セグメントがロード範囲の外 (ELF ヘッダ等) に
    はみ出す部分は .raw に入らないので比べない。

    raw は bytes (大きさだけの int は受けない — 内容を見ないと意味が無い)。"""
    if not isinstance(raw, (bytes, bytearray)):
        raise HeaderError(f"{what}: 内容の照合には .raw の中身が要る")
    ext = elf.loadable_extent()
    if ext is None:
        raise HeaderError(f"{elf.path}: ロードされるセクションが無い")
    base, end = ext
    want = end - base
    if len(raw) != want:
        raise HeaderError(
            f"{what} の大きさ {len(raw)} が ELF のロード範囲 {want} "
            f"(0x{base:X}..0x{end:X}) と違う — .raw と .elf の世代が違う")
    compared = 0
    for seg in elf.segments:
        if seg['type'] != PT_LOAD or seg['filesz'] == 0:
            continue
        lo = max(seg['paddr'], base)
        hi = min(seg['paddr'] + seg['filesz'], end)
        if lo >= hi:
            continue
        foff = seg['offset'] + (lo - seg['paddr'])
        n = hi - lo
        if foff + n > len(elf.data):
            raise HeaderError(f"{elf.path}: PT_LOAD がファイルの外を指す")
        a = elf.data[foff:foff + n]
        b = bytes(raw[lo - base:lo - base + n])
        if a != b:
            k = next(i for i in range(n) if a[i] != b[i])
            raise HeaderError(
                f"{what} の内容が ELF の PT_LOAD と違う (番地 0x{lo + k:X}、"
                f".raw の +0x{lo - base + k:X}: raw=0x{b[k]:02X} elf=0x{a[k]:02X}) "
                "— .raw と .elf の世代が違う。作り直す")
        compared += n
    if compared == 0:
        raise HeaderError(f"{elf.path}: .raw と突き合わせる PT_LOAD が無い")


def effective_min_api(min_api):
    """v3 のバイナリは OS32X_HDR_V3_MIN_API 以上を要求する。"""
    return max(int(min_api), OS32X_MIN_API)


def build_header(flags, entry_offset, text_size, bss_size, heap_size,
                 min_api_ver, load_addr, kapi_data_off, stack_size=0, shlib_protocol=0):
    """現行形式を返す。世代と最低機能版は生成した正典から読む。"""
    if kapi_data_off is None or kapi_data_off == 0:
        raise HeaderError("kapi_data_off が無い (刻印を読めていない)")
    hdr = struct.pack('<15I',
                      OS32X_MAGIC,
                      OS32X_HDR_SIZE,
                      OS32X_HDR_VERSION,
                      flags,
                      entry_offset,
                      text_size,
                      bss_size,
                      heap_size,
                      stack_size,
                      effective_min_api(min_api_ver),
                      load_addr,
                      kapi_data_off,
                      OS32_KAPI_ABI_GENERATION,
                      OS32_MEMORY_LAYOUT_GENERATION,
                      shlib_protocol)
    assert len(hdr) == OS32X_HDR_SIZE
    return hdr


def parse_header(blob):
    """ヘッダを辞書で返す (試験と診断用)。"""
    if len(blob) < OS32X_HDR_V1_SIZE:
        raise HeaderError("ヘッダが短い")
    names = ['magic', 'header_size', 'version', 'flags', 'entry_offset',
             'text_size', 'bss_size', 'heap_size', 'stack_size', 'min_api_ver']
    vals = struct.unpack_from('<10I', blob, 0)
    h = dict(zip(names, vals))
    if h['header_size'] >= OS32X_HDR_V2_SIZE and len(blob) >= OS32X_HDR_V2_SIZE:
        (h['load_addr'],) = struct.unpack_from('<I', blob, 40)
    if h['header_size'] >= OS32X_HDR_V3_SIZE and len(blob) >= OS32X_HDR_V3_SIZE:
        (h['kapi_data_off'],) = struct.unpack_from('<I', blob, 44)
    if h['header_size'] >= OS32X_HDR_SIZE and len(blob) >= OS32X_HDR_SIZE:
        h.update(zip(('kapi_abi_generation', 'memory_layout_generation', 'shlib_protocol'), struct.unpack_from('<3I', blob, 48)))
    return h
