#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_memmap.py — メモリ地図の生成と、重なり・逆転の検出

手で書いた地図は必ず腐る。2026-09-17 時点で同じ地図が 3 か所に手書きされていて
(`include/memmap.h` の先頭コメント、`docs/02_memory.md` §2-1、`CLAUDE.md` の帯の表)、
3 つとも「カーネル本体 ~200KB」と書いていた。実測は **約 432KB**。
しかもカーネルが育った結果、共有メモリ帯がカーネルスタックに食い込んでいたのに、
どの地図も相対表記 (「+4KB - +260KB」) だったので誰も気づけなかった
(票 docs/archive/kernel_v21/TASK_KSTACK_USER.md)。

そこでこの生成器を置く。

  情報源   include/memmap.h の #define (番地の正典) と
           build/out/kernel.map の __bss_end / __sqlite_start / __sqlite_end
  出力先   docs/02_memory.md の生成ブロック **1 か所だけ**。表は全て **絶対番地**。
  検査     帯どうしの重なりと、start > end の逆転。今回の穴は両方ともこれで捕まる。

使い方:
    python3 tools/gen_memmap.py              # 表を標準出力に出す
    python3 tools/gen_memmap.py --write      # docs/02_memory.md の生成ブロックを差し替える
    python3 tools/gen_memmap.py --check      # 重なり・逆転・鮮度ずれがあれば 1 で終わる
    python3 tools/gen_memmap.py --headroom   # カーネルがあと何 KB 育つと何が壊れるか

    --root <dir>  別のツリーを見る (試験用)
    --map <path>  kernel.map の場所 (既定 <root>/build/out/kernel.map)

**`make check` にはまだ登録していない。** 登録すると今すぐ赤になる — 検出される
重なりが実在するため (票 §4 の 4 で番地を直すのはユーザー判断)。番地を直したら
`build/checks.d/check-memmap.mk` の登録に `check-memmap` を足すこと。

kernel.map が無いときは**推測しない**。__bss_end を決め打ちにすると、地図が
「それらしく」出てしまい、今回とまったく同じ嘘をもう一度書くことになる。
"""

import argparse
import difflib
import os
import re
import sys

OUT_REL = "docs/02_memory.md"
MAP_REL = "build/out/kernel.map"
HDR_REL = "include/memmap.h"

BEGIN = "<!-- 生成: tools/gen_memmap.py --write (この印の中は手で書かない) -->"
END = "<!-- /生成: tools/gen_memmap.py -->"

# kernel.map から取るリンカシンボル。ここに無いものは #define から解く。
SYMBOLS = ("__bss_end", "__sqlite_start", "__sqlite_end")

PAGE = 0x1000


# ---------------------------------------------------------------------------
#  1. 情報源を読む
# ---------------------------------------------------------------------------

class MissingMap(Exception):
    pass


def read_symbols(map_path):
    """kernel.map から __bss_end などを取る。無ければ **止まる** (推測しない)。"""
    if not os.path.isfile(map_path):
        raise MissingMap(
            "%s が無い。__bss_end はカーネルを積んだ結果でしか決まらないので、\n"
            "ここで推測すると地図がそれらしい嘘になる (票 §4-bis)。\n"
            "先に `make CROSS_DIR=... kernel` を実行してから、もう一度呼ぶこと。"
            % map_path)
    want = dict.fromkeys(SYMBOLS)
    with open(map_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"0x([0-9a-fA-F]+)\s+(\w+)\s*=", line)
            if m and m.group(2) in want and want[m.group(2)] is None:
                want[m.group(2)] = int(m.group(1), 16)
    missing = [k for k, v in want.items() if v is None]
    if missing:
        raise MissingMap("%s に %s が無い。リンカスクリプトが変わった可能性がある。"
                         % (map_path, " / ".join(missing)))
    return want


DEFINE = re.compile(r"^\s*#define\s+([A-Za-z_]\w*)\s+(\S.*?)\s*$")
IDENT = re.compile(r"\b([A-Za-z_]\w*)\b")
CASTS = re.compile(r"\((?:u8|u16|u32|int|unsigned|long)\)")
SUFFIX = re.compile(r"\b(0[xX][0-9a-fA-F]+|\d+)[uU]?[lL]*\b")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def read_defines(header_path):
    """#define NAME expr を集める (行継続をつないでからコメントを落とす)。"""
    with open(header_path, encoding="utf-8") as f:
        text = f.read()
    text = re.sub(r"\\\n", " ", text)
    out = {}
    for line in strip_comments(text).split("\n"):
        m = DEFINE.match(line)
        if m and "(" not in m.group(1):
            out[m.group(1)] = m.group(2).strip()
    return out


class Macros(object):
    """#define を必要になった順に解く。解けないものは None のまま置く。"""

    def __init__(self, defines, symbols):
        self.defines = defines
        self.symbols = symbols
        self.cache = {}
        self.busy = set()

    def get(self, name):
        if name in self.cache:
            return self.cache[name]
        if name in self.busy or name not in self.defines:
            return None
        self.busy.add(name)
        try:
            value = self._eval(self.defines[name])
        finally:
            self.busy.discard(name)
        self.cache[name] = value
        return value

    def _eval(self, expr):
        # (u32)&__bss_end のようなリンカシンボル参照を実値に置き換える
        def sym(m):
            v = self.symbols.get(m.group(1))
            return str(v) if v is not None else "None"
        expr = re.sub(r"\(\s*u32\s*\)\s*&\s*(\w+)", sym, expr)
        expr = re.sub(r"&\s*(\w+)", sym, expr)
        expr = CASTS.sub("", expr)
        expr = SUFFIX.sub(r"\1", expr)
        names = set(IDENT.findall(expr)) - {"None"}
        for n in names:
            if re.match(r"^0[xX]", n):
                continue
            v = self.get(n)
            if v is None:
                return None
            expr = re.sub(r"\b%s\b" % re.escape(n), "(%d)" % v, expr)
        expr = re.sub(r"(?<![/])/(?![/])", "//", expr)
        try:
            return int(eval(expr, {"__builtins__": {}}, {}))  # noqa: S307
        except Exception:
            return None


# ---------------------------------------------------------------------------
#  2. 帯の表 — 番地は必ず memmap.h / kernel.map から引く ([C4])
# ---------------------------------------------------------------------------
#  (節, 名前, 開始, 終端(inclusive), 属性, 注記, 親)
#  親を持つ帯は「その内側の予約」で、親との重なりは重なりと数えない。
#  size が動的で静的に決まらないものは end=None で載せ、重なり検査から外す。

CONV = "コンベンショナルメモリ (0x00000-0xFFFFF)"
KERN = "カーネル帯域 (0x100000-0x1FFFFF)"
SQL = "SQLite 帯域 (0x200000-0x2FFFFF)"
SHELL = "シェル常駐帯域 (0x300000-0x3FFFFF)"
APP = "仮想アプリ帯 (0x80000000-)"
DEV = "物理 RAM とデバイス窓"


def bands(m, sym):
    """帯の一覧を作る。値が解けなかった帯は落とさず None のまま返す。"""
    def v(name):
        return m.get(name)

    def plus(name, size_name, off=0):
        a, b = v(name), v(size_name)
        if a is None or b is None:
            return None
        return a + b + off

    rows = []

    def add(section, name, start, end, attr, note="", parent=None):
        rows.append({"section": section, "name": name, "start": start,
                     "end": end, "attr": attr, "note": note, "parent": parent})

    add(CONV, "NULL ポインタ検出ガード", 0, v("MEM_NULL_GUARD_END"), "NP",
        "ブート後は R/O (BDA 参照のため。paging.c の [DEBUG] 注記)")
    add(CONV, "フォントキャッシュ", v("MEM_FONT_CACHE_BASE"),
        (v("MEM_UNICODE_TABLE_BASE") or 0) - 1, "RW", "kcg.c がブート後に配置")
    add(CONV, "ブート情報域 (ローダ → kernel_main)", v("MEM_BOOTINFO_BASE"),
        v("MEM_BOOTINFO_END"), "RW",
        "INT 1Bh AH=84h の結果 (include/bootinfo.h)。kernel_main の最初で写した"
        "後はフォントキャッシュが上書きしてよい",
        parent="フォントキャッシュ")
    add(CONV, "Unicode-JIS 変換表", v("MEM_UNICODE_TABLE_BASE"),
        plus("MEM_UNICODE_TABLE_BASE", "MEM_UNICODE_TABLE_SIZE", -1), "RW",
        "SURFACE_BACKING/KERNEL/WB、RO lease。旧 USER RW は e11 で撤去、T3 V86_LOW 統合時に再検討")
    add(CONV, "GFX バックバッファ (4 プレーン)", v("MEM_GFX_BB_BASE"),
        plus("MEM_GFX_BB_BASE", "MEM_GFX_BB_SIZE", -1), "RW",
        "CPL=3 からは常に USER (レビュー #6)")
    add(CONV, "空き / V86 ゲスト窓の一部",
        plus("MEM_GFX_BB_BASE", "MEM_GFX_BB_SIZE"),
        (v("MEM_CONV_END") or 0) - 1, "RW",
        "MEM_AUTOPLAY_MAILBOX_BASE/SIZE は自動プレイ観測メールボックス (game/app と driver.py の対)")
    add(CONV, "VRAM (テキスト + グラフィック)", v("MEM_CONV_END"),
        (v("MEM_BIOS_ROM_START") or 0) - 1, "RW", "CPL=3 からは USER。TVRAM / B,R,G / E は PCD (UC)、CG 窓は WB")
    add(CONV, "BIOS ROM", v("MEM_BIOS_ROM_START"), v("MEM_BIOS_ROM_END"), "RO", "WB (PCD なし)")

    add(KERN, "カーネル .text/.data/.bss", v("KERNEL_LOAD_ADDR"),
        sym["__bss_end"] - 1, "RW", "kernel.map の __bss_end まで")
    add(KERN, "カーネルヒープ (kmalloc)", v("KHEAP_BASE"),
        plus("KHEAP_BASE", "KHEAP_SIZE", -1), "RW",
        "__bss_end を 4KB に切り上げた位置から")
    add(KERN, "KernelAPI テーブル", plus("KHEAP_BASE", "KHEAP_SIZE"),
        (v("MEM_SHM_GUARD_LO") or 0) - 1, "RW", "KAPI_ADDR")
    add(KERN, "SHM 前方ガード", v("MEM_SHM_GUARD_LO"),
        (v("MEM_SHM_BASE") or 0) - 1, "NP")
    add(KERN, "共有メモリ本体", v("MEM_SHM_BASE"), v("MEM_SHM_END"), "RW+USER",
        "16KB x SHM_BLOCK_COUNT。shm_init 後・最初の AS 前に boot 口で USER/WB。"
        "lock/free/回収は USER を保ち RW のみ切替 (PDE 0 は全 PD 共有)")
    add(KERN, "GUI 予約 (末尾 4 ブロック)", v("MEM_SHM_GUI_BASE"),
        plus("MEM_SHM_GUI_BASE", "MEM_SHM_GUI_SIZE", -1), "RW+USER",
        "契約 T2。SDK の GUI_SHM_OFFSET = MEM_SHM_GUI_OFFSET",
        parent="共有メモリ本体")
    guard_hi = v("MEM_SHM_GUARD_HI")
    add(KERN, "SHM 後方ガード", guard_hi,
        (guard_hi + PAGE - 1) if guard_hi is not None else None, "NP")
    add(KERN, "SHM 後方予約", v("MEM_SHM_RESV_START"), v("MEM_SHM_RESV_END"),
        "NP", "カーネルが予算いっぱいなら空になる (それは正しい)")

    add(SQL, "SQLite code+BSS", sym["__sqlite_start"], sym["__sqlite_end"] - 1,
        "RW", "kernel.map の __sqlite_start / __sqlite_end")
    add(SQL, "SQLite 代替スタック", v("MEM_SQLITE_STACK_BASE"),
        plus("MEM_SQLITE_STACK_BASE", "MEM_SQLITE_STACK_SIZE", -1), "RW")
    # カーネル予約は DMA プールで **2 つに割れる**。上下が NP のままガードに
    # なるので、割れていること自体が設計の一部 (票 TASK_HAL_WIRING §1-3)。
    add(SQL, "カーネル予約 (下)", v("MEM_KERNEL_RESV_START"),
        (v("MEM_DMA_POOL_BASE") or 0) - 1, "NP",
        "DMA プールの下側ガード")
    add(SQL, "DMA プール", v("MEM_DMA_POOL_BASE"), v("MEM_DMA_POOL_END"), "RW",
        "予約域に開けた穴。present / supervisor / R/W。**USER は立てない** "
        "(kselftest の MM 検査が MM_RW と MM_RWU を分けて見る)")
    # 予約域の上側は「4KB のガード + 8KB の台帳 backing」に割れる
    # (票 docs/archive/v3/TASK_T1_LEDGER.md §3-3、T1a)。backing を張るのは
    # FIXED 型 (低位 RAM の末尾に置けない 8MB・9MB・12MB) のときだけ。
    add(SQL, "カーネル予約 (上)", (v("MEM_DMA_POOL_END") or 0) + 1,
        (v("MEM_LEDGER_META_BASE") or 0) - 1, "NP", "DMA プールの上側ガード")
    add(SQL, "台帳 backing (FIXED 型)", v("MEM_LEDGER_META_BASE"),
        (v("MEM_LEDGER_META_END") or 0) - 1, "NP / RW",
        "metadata 1 ページ + PT workspace 1 ページ。FIXED 型のときだけ "
        "present / supervisor / R/W (USER なし)、ほかの構成では予約域 (NP) のまま")
    add(SQL, "カーネルスタックガード", v("MEM_STACK_GUARD"),
        v("MEM_STACK_GUARD_END"), "NP", "2026-09-17 に 0x1FB000 から移設 (決裁 D1)")
    add(SQL, "カーネルスタック", v("MEM_KSTACK_BASE"),
        (v("MEM_SHELL_LOAD_ADDR") or 0) - 1, "RW",
        "ESP 初期値 = MEM_KSTACK_TOP (kentry.asm / TSS.ESP0)")

    shell_guard = v("MEM_SHELL_GUARD")
    add(SHELL, "シェル .text/.data/.bss + newlib sbrk", v("MEM_SHELL_LOAD_ADDR"),
        (shell_guard or 0) - 1, "RW", "MEM_SHELL_MAX_SIZE が上限 = sbrk の天井")
    add(SHELL, "シェルスタックガード", shell_guard,
        (shell_guard + PAGE - 1) if shell_guard else None, "NP")
    stack_top = v("MEM_SHELL_STACK_TOP")
    stack_size = v("MEM_SHELL_STACK_SIZE")
    add(SHELL, "シェルスタック",
        (stack_top - stack_size) if (stack_top and stack_size) else None,
        (stack_top - 1) if stack_top else None, "RW", "下向き成長")
    add(SHELL, "シェル exec_heap (KAPI mem_alloc)", v("MEM_SHELL_HEAP_BASE"),
        plus("MEM_SHELL_HEAP_BASE", "MEM_SHELL_HEAP_SIZE", -1), "RW",
        "newlib の sbrk とは別領域 (2026-09-03)")

    add(SHELL, "固定 master PD", v("MEM_FIXED_PD_BASE"),
        (v("MEM_FIXED_BOOT_PT_BASE") or 0) - 1, "RW", "恒久FIXED / supervisor / WB")
    add(SHELL, "固定 bootstrap PT (8枚)", v("MEM_FIXED_BOOT_PT_BASE"),
        (v("MEM_FIXED_APERTURE_PT_BASE") or 0) - 1, "RW", "恒久FIXED / supervisor / WB")
    add(SHELL, "固定 device aperture PT", v("MEM_FIXED_APERTURE_PT_BASE"),
        (v("MEM_FIXED_PAGING_END") or 0) - 1, "RW", "backing は WB、MMIO PTE は PCD/PWT")
    add(SHELL, "上端残余予約", v("MEM_FIXED_PAGING_END"), v("MEM_SHELL_BAND_END"),
        "NP", "恒久FIXED、T3 の guard+kstack 用")

    add(APP, "共有ライブラリ帯 (libos32gui.shlib)", v("MEM_SHLIB_BASE"),
        (v("MEM_SHLIB_END") or 0) - 1, "RO+USER / RW+USER",
        ".text は全 PD 共有、.data/.bss はアプリごとの物理")
    add(APP, "外部プログラム空間", v("MEM_EXEC_LOAD_ADDR"), None, "RW+USER",
        "image/sbrk、exec_heap は 0x88000000、可変 stack は 0x90000000 直下。"
        "物理ページの池の下端は MEM_POOL_BASE")
    add(DEV, "集積域 (ブート時)", v("MEM_BOOT_STAGING_BASE"),
        plus("MEM_BOOT_STAGING_BASE", "MEM_BOOT_STAGING_SIZE", -1), "池",
        "圧縮画像の読み込み先 (T6b 以後)。展開が終われば池へ。T1 ではローダは未使用")
    add(DEV, "同梱域 (ブート時)", v("MEM_BOOT_BUNDLE_BASE"),
        plus("MEM_BOOT_BUNDLE_BASE", "MEM_BOOT_BUNDLE_SIZE", -1), "池",
        "ブート必須モジュールの展開先 (T5b 以後、台帳が owner=bundle で予約)。"
        "T1 ではローダは未使用")

    add(DEV, "PC-98 システム空間 (PEGC リニア窓)", v("MEM_SYSTEM_SPACE_BASE"),
        (v("MEM_SYSTEM_SPACE_END") or 0) - 1, "NP / supervisor+PCD",
        "RAM として配らない")
    ceiling = v("MEM_PHYS_RAM_CEILING")
    add(DEV, "16MB 以上の実 RAM (の置き場)", v("MEM_HIGH_RAM_BASE"),
        (ceiling - 1) if ceiling else None, "RW",
        "検出量ぶんだけ pgalloc の池に入る (K6-RAM)。上端は MEM_PHYS_RAM_CEILING (D11)")
    add(DEV, "RAM の登録上限 (2GB) 以上", v("MEM_PHYS_RAM_CEILING"), 0xFFFFFFFF,
        "NP / supervisor+PCD",
        "RAM として登録しない (D11)。PCI の BAR・デバイス窓の帯 (0xFE000000〜)・"
        "最上位の ROM / MMIO")
    return rows


# ---------------------------------------------------------------------------
#  3. 検査 — 重なりと逆転
# ---------------------------------------------------------------------------

def concrete(rows):
    return [r for r in rows if r["start"] is not None and r["end"] is not None]


def empty_ranges(rows):
    """start == end + 1 の帯 = **空**。予約域が使い切られただけで、異常ではない。"""
    return [r for r in concrete(rows) if r["start"] == r["end"] + 1]


def reversed_ranges(rows):
    """start > end + 1 の帯 = **逆転**。空振りした範囲指定がここに出る。

    空 (start == end + 1) と逆転を区別するのが肝心。前者は「予約が 0 バイトに
    なった」だけで、後者は「範囲の指定が壊れている」。2026-09-17 の
    MEM_SHM_RESV は 0x1FF000 > 0x1FAFFF で **4 ページぶん逆転**していた。"""
    return [r for r in concrete(rows) if r["start"] > r["end"] + 1]


def overlaps(rows):
    """帯どうしの重なり。親子 (帯の内側の予約) は数えない。"""
    good = [r for r in concrete(rows) if r["start"] <= r["end"]]
    out = []
    for i in range(len(good)):
        for j in range(i + 1, len(good)):
            a, b = good[i], good[j]
            # High app VA and physical RAM/device addresses are distinct spaces.
            if {a["section"], b["section"]} == {APP, DEV}:
                continue
            if a["parent"] == b["name"] or b["parent"] == a["name"]:
                continue
            lo = max(a["start"], b["start"])
            hi = min(a["end"], b["end"])
            if lo <= hi:
                out.append((a, b, lo, hi))
    return out


def escaped_children(rows):
    """親の外へはみ出した子。"""
    by_name = {r["name"]: r for r in concrete(rows)}
    out = []
    for r in concrete(rows):
        p = by_name.get(r["parent"]) if r["parent"] else None
        if p and not (p["start"] <= r["start"] and r["end"] <= p["end"]):
            out.append((r, p))
    return out


def budget(m, sym):
    """カーネル本体に使える最大 (予算) と、実際の大きさ。

    予算の式は **include/memmap.h の MEM_KERNEL_IMAGE_MAX が正典**。ここで
    引き算をやり直すと 2 か所目の定義になるので、マクロの値をそのまま使う。
    超過の判定だけは __bss_end が要るのでここでやる (同じ判定を
    build/os32.ld の ASSERT がリンク時にもやる)。
    """
    load = m.get("KERNEL_LOAD_ADDR")
    limit = m.get("MEM_KERNEL_IMAGE_MAX")
    if load is None or limit is None:
        return None
    return limit, sym["__bss_end"] - load


# (ファイル, 正規表現, memmap.h のマクロ名) — 値を二重に持つ場所。
# C のヘッダを読めない相手 (リンカスクリプト / NASM) と、SDK として外へ出る
# 写しにだけ許し、一致は必ずここで見る ([C4])。
MIRRORS = (
    ("sdk/os32x_hdr.py", r"OS32X_APP_LOAD_ADDR = (0x[0-9A-Fa-f]+)", "MEM_EXEC_LOAD_ADDR"),
    ("sdk/os32x_hdr.py", r"OS32X_SHELL_LOAD_ADDR = (0x[0-9A-Fa-f]+)", "MEM_SHELL_LOAD_ADDR"),
    ("tools/mkshlib.py", r"MEM_SHLIB_BASE = (0x[0-9A-Fa-f]+)", "MEM_SHLIB_BASE"),
    ("sdk/link/app.ld", r"\. = (0x[0-9A-Fa-f]+);", "MEM_EXEC_LOAD_ADDR"),
    ("sdk/link/shlib.ld", r"\. = (0x[0-9A-Fa-f]+);", "MEM_SHLIB_BASE"),
    ("sdk/rust/os32api/src/gui/stub.rs", r"MEM_SHLIB_BASE: u32 = (0x[0-9A-Fa-f_]+);", "MEM_SHLIB_BASE"),

    ("build/os32.ld", r"^\s*MEM_FIXED_PAGING_BASE\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_FIXED_PAGING_BASE"),
    ("build/os32.ld", r"^\s*MEM_FIXED_PD_BASE\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_FIXED_PD_BASE"),
    ("build/os32.ld", r"^\s*MEM_FIXED_BOOT_PT_BASE\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_FIXED_BOOT_PT_BASE"),
    ("build/os32.ld", r"^\s*MEM_FIXED_BOOT_PT_COUNT\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_FIXED_BOOT_PT_COUNT"),
    ("build/os32.ld", r"^\s*MEM_FIXED_APERTURE_PT_BASE\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_FIXED_APERTURE_PT_BASE"),
    ("build/os32.ld", r"^\s*MEM_FIXED_PAGING_END\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_FIXED_PAGING_END"),
    ("build/os32.ld", r"^\s*MEM_SHELL_HEAP_BASE\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_SHELL_HEAP_BASE"),
    ("build/os32.ld", r"^\s*MEM_SHELL_HEAP_END\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_SHELL_HEAP_END"),
    ("build/os32.ld", r"^\s*MEM_SHELL_HEAP_SIZE\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_SHELL_HEAP_SIZE"),
    ("build/os32.ld", r"^\s*MEM_SHELL_BAND_END\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_SHELL_BAND_END"),
    ("build/os32.ld", r"^\s*MEM_POOL_BASE\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_POOL_BASE"),
    ("build/os32.ld", r"^\s*MEM_SHELL_STACK_TOP\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_SHELL_STACK_TOP"),
    ("build/os32.ld", r"^\s*MEM_SHELL_LOAD_ADDR\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_SHELL_LOAD_ADDR"),
    ("build/os32.ld", r"^\s*MEM_KERNEL_BAND_END\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_KERNEL_BAND_END"),
    ("build/os32.ld", r"^\s*MEM_LEDGER_META_END\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_LEDGER_META_END"),
    ("build/os32.ld", r"^\s*MEM_STACK_GUARD\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_STACK_GUARD"),
    ("build/os32.ld", r"^\s*MEM_DMA_POOL_END\s*=\s*(0x[0-9A-Fa-f]+)\s*;", "MEM_DMA_POOL_END"),

    ("build/os32.ld", r"^\s*KERNEL_LOAD_ADDR\s*=\s*(0x[0-9A-Fa-f]+)\s*;",
     "KERNEL_LOAD_ADDR"),
    ("build/os32.ld", r"^\s*MEM_KSTACK_TOP\s*=\s*(0x[0-9A-Fa-f]+)\s*;",
     "MEM_KSTACK_TOP"),
    ("build/os32.ld", r"^\s*MEM_KERNEL_IMAGE_MAX\s*=\s*(0x[0-9A-Fa-f]+)\s*;",
     "MEM_KERNEL_IMAGE_MAX"),
    # SQLite の伸び代を **リンク時に** 止めるための 2 つ (票 TASK_HAL_WIRING
    # §1-3)。ld の ASSERT が
    #   __sqlite_end + MEM_SQLITE_STACK_SIZE + 0x1000 <= MEM_DMA_POOL_BASE
    # を見るので、C 側とずれると「重なっていないはずの帯が重なる」。
    ("build/os32.ld", r"^\s*MEM_SQLITE_STACK_SIZE\s*=\s*(0x[0-9A-Fa-f]+)\s*;",
     "MEM_SQLITE_STACK_SIZE"),
    ("build/os32.ld", r"^\s*MEM_DMA_POOL_BASE\s*=\s*(0x[0-9A-Fa-f]+)\s*;",
     "MEM_DMA_POOL_BASE"),
    ("sdk/include/os32/os32_gui_shared.h",
     r"^#define\s+GUI_SHM_OFFSET\s+(0x[0-9A-Fa-f]+)UL",
     "MEM_SHM_GUI_OFFSET"),
    ("sdk/rust/os32api/src/gui/proto.rs",
     r"^pub const GUI_SHM_OFFSET:\s*u32\s*=\s*(0x[0-9A-Fa-f]+)\s*;",
     "MEM_SHM_GUI_OFFSET"),
    # ホスト試験が kernel/shm.c を組むための偽 memmap.h。実物は
    # (u32)&__bss_end を含んでホストで使えないので写しを持つしかないが、
    # **2026-09-17 に実際にずれた** — D1 で 16 → 14 ブロックにしたとき、
    # ここだけ 16 のままで shm.c の表明が落ちた (make check が捕まえた)。
    # ローダ (NASM) はヘッダを読めないので番地を写して持つ
    # (票 TASK_HDD_INSTALL 段 0。オフセットの一致は tools/tests/test_bootinfo.py)。
    ("boot/bootinfo.inc", r"^MEM_BOOTINFO_BASE\s+EQU\s+(0x[0-9A-Fa-f]+)\s*$",
     "MEM_BOOTINFO_BASE"),
    ("tools/tests/test_owner_reclaim.py",
     r'"#define MEM_SHM_SIZE\s+(0x[0-9A-Fa-f]+)U',
     "MEM_SHM_SIZE"),
    # VK32 の展開先の帯と、ローダ (C) が書くブート情報域 (票 TASK_SERIAL_HOSTFS
    # A-4)。HDD ローダ (boot/boot_defs.h) と FD ローダ (NASM) がそれぞれ写しを持つ。
    ("boot/boot_defs.h", r"^#define\s+VK32_LOAD_MIN\s+(0x[0-9A-Fa-f]+)UL",
     "KERNEL_LOAD_ADDR"),
    ("boot/boot_defs.h", r"^#define\s+VK32_LOAD_END\s+(0x[0-9A-Fa-f]+)UL",
     "MEM_DMA_POOL_BASE"),
    ("boot/boot_defs.h", r"^#define\s+BOOTINFO_BASE\s+(0x[0-9A-Fa-f]+)UL",
     "MEM_BOOTINFO_BASE"),
    ("boot/loader_fat_new.asm", r"^VK32_LOAD_MIN\s+EQU\s+(0x[0-9A-Fa-f]+)\s*$",
     "KERNEL_LOAD_ADDR"),
    ("boot/loader_fat_new.asm", r"^VK32_LOAD_END\s+EQU\s+(0x[0-9A-Fa-f]+)\s*$",
     "MEM_DMA_POOL_BASE"),
)

# ASM は値ではなくシンボルで引くこと。数値直書きに戻したらここで気づく。
ASM_SYMBOLIC = (("kernel/kentry.asm", "mov     esp, MEM_KSTACK_TOP",
                 "ESP の初期値を数値で書かず build/os32.ld の "
                 "MEM_KSTACK_TOP を extern で引くこと"),)


def mirrors(root, m):
    """memmap.h の値を写している場所が食い違っていないか ([C4])。"""
    out = []
    for rel, pattern, macro in MIRRORS:
        path = os.path.join(root, rel)
        want = m.get(macro)
        if want is None:
            out.append("写し: %s の基準 %s が memmap.h から解けない" % (rel, macro))
            continue
        if not os.path.isfile(path):
            out.append("写し: %s が無い" % rel)
            continue
        with open(path, encoding="utf-8") as f:
            found = re.search(pattern, f.read(), re.M)
        if not found:
            out.append("写し: %s に %s の写しが見つからない" % (rel, macro))
            continue
        got = int(found.group(1), 16)
        if got != want:
            out.append("写しのずれ: %s = 0x%X だが memmap.h の %s は 0x%X"
                       % (rel, got, macro, want))
    for rel, needle, why in ASM_SYMBOLIC:
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            out.append("写し: %s が無い" % rel)
            continue
        with open(path, encoding="utf-8") as f:
            if needle not in f.read():
                out.append("写し: %s に `%s` が無い — %s" % (rel, needle, why))
    return out


def defects(rows, m=None, sym=None, root=None):
    """人が読む 1 行ずつの障害報告。空なら地図に矛盾は無い。"""
    lines = []
    if m is not None and sym is not None:
        b = budget(m, sym)
        if b and b[1] > b[0]:
            lines.append("予算超過: カーネル本体 %s > 予算 %s (超過 %s)。"
                         "KHEAP_BASE から上が丸ごと押し上げられる"
                         % (human(b[1]), human(b[0]), human(b[1] - b[0])))
    if m is not None:
        names = ("MEM_FIXED_PAGING_BASE", "MEM_FIXED_PD_BASE", "MEM_FIXED_BOOT_PT_BASE",
                 "MEM_FIXED_BOOT_PT_COUNT", "MEM_FIXED_APERTURE_PT_BASE", "MEM_FIXED_PAGING_END",
                 "MEM_SHELL_HEAP_BASE", "MEM_SHELL_HEAP_END", "MEM_SHELL_HEAP_SIZE",
                 "MEM_SHELL_BAND_END", "MEM_POOL_BASE", "MEM_SHELL_STACK_TOP")
        v = {n: m.get(n) for n in names}
        if any(x is None for x in v.values()):
            lines.append("固定paging: 境界定数を解けない")
        else:
            b, pd, pt, count, apt, end, hb, he, size, band, pool, stack = (v[n] for n in names)
            if any(a % PAGE for a in (b, pd, pt, apt, end, hb, he, band + 1, pool, stack)):
                lines.append("固定paging: 境界が4KB非整列")
            if not (pd == b and pt == pd + PAGE and count == 8 and apt == pt + count * PAGE
                    and end == apt + PAGE and end - b == 10 * PAGE):
                lines.append("固定paging: 連続1/8/1枚ではない")
            if not (stack <= hb < he == b and hb + size == he and end <= band + 1 == pool
                    and pool - end == 5 * PAGE):
                lines.append("固定paging: shell heap/上端予約の境界不一致")
    for r in reversed_ranges(rows):
        lines.append("逆転: %s  開始 0x%06X > 終端 0x%06X "
                     "(範囲指定が空振りする)" % (r["name"], r["start"], r["end"]))
    for a, b, lo, hi in overlaps(rows):
        lines.append("重なり: %s [0x%06X-0x%06X] と %s [0x%06X-0x%06X] が "
                     "0x%06X-0x%06X (%s) で重なる"
                     % (a["name"], a["start"], a["end"],
                        b["name"], b["start"], b["end"], lo, hi,
                        human(hi - lo + 1)))
    if root is not None and m is not None:
        lines.extend(mirrors(root, m))
    for r, p in escaped_children(rows):
        lines.append("はみ出し: %s [0x%06X-0x%06X] が親 %s [0x%06X-0x%06X] の外にある"
                     % (r["name"], r["start"], r["end"],
                        p["name"], p["start"], p["end"]))
    return lines


# ---------------------------------------------------------------------------
#  4. カーネルがあと何 KB 育つと何が壊れるか
# ---------------------------------------------------------------------------

def headroom(m, sym):
    """__bss_end が伸びると KHEAP_BASE 以降が芋づるで動く。次に何が起きるか。"""
    bss = sym["__bss_end"]
    kheap = m.get("KHEAP_BASE")
    b = budget(m, sym)
    if kheap is None or b is None:
        return []
    limit, used = b
    out = [("KHEAP_BASE が 1 ページ上がる", kheap - bss,
            "0x%06X → 0x%06X。以降の KAPI / SHM / ガードが全部 4KB 動く"
            % (kheap, kheap + PAGE))]
    if used <= limit:
        out.append(("**build/os32.ld の ASSERT がリンクを止める** "
                    "(予算 MEM_KERNEL_IMAGE_MAX 超過)", limit - used + 1,
                    "止めるのが目的。超えたぶんだけ SHM 帯が "
                    "カーネル帯域 0x%06X を突き抜ける"
                    % (m.get("MEM_KERNEL_BAND_END") or 0)))
    else:
        out.append(("**いま既に予算を超えている** — リンクが通らないはず", 0,
                    "os32.ld の MEM_KERNEL_IMAGE_MAX が memmap.h と "
                    "ずれていないか確かめること"))
    return out


# ---------------------------------------------------------------------------
#  5. 表を描く
# ---------------------------------------------------------------------------

def width(text):
    """表示幅。日本語は 1 文字 2 桁なので len() では桁が揃わない。"""
    import unicodedata
    n = 0
    for ch in text:
        n += 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
    return n


def pad(text, n):
    return text + " " * max(1, n - width(text))


def human(n):
    if n is None:
        return "-"
    if n >= 1024 * 1024 and n % (1024 * 1024) == 0:
        return "%dMB" % (n // (1024 * 1024))
    if n >= 1024:
        return "%.1fKB" % (n / 1024.0) if n % 1024 else "%dKB" % (n // 1024)
    return "%dB" % n


def addr(a):
    return "0x%06X" % a if a is not None else "        "


def table(rows, defect_pairs):
    """絶対番地だけの表。相対表記 (+4KB) は使わない — 暗算させると穴が見えなくなる。"""
    bad = set()
    for a, b, _lo, _hi in defect_pairs["overlaps"]:
        bad.add(a["name"])
        bad.add(b["name"])
    for r in defect_pairs["reversed"]:
        bad.add(r["name"])

    out = []
    order = [CONV, KERN, SQL, SHELL, APP, DEV]
    for section in order:
        mine = [r for r in rows if r["section"] == section]
        if not mine:
            continue
        out.append("[ %s ]" % section)
        cursor = None
        for r in sorted(mine, key=lambda x: (x["start"] is None,
                                             x["start"] or 0)):
            s, e = r["start"], r["end"]
            if s is not None and e is not None and s <= e:
                if cursor is not None and s > cursor + 1:
                    out.append((pad("%s - %s" % (addr(cursor + 1),
                                                 addr(s - 1)), 18) +
                                pad(human(s - cursor - 1), 9) +
                                "空き").rstrip())
                cursor = e if cursor is None else max(cursor, e)
            mark = "  <<< " if r["name"] in bad else ""
            if s is not None and e is not None and s > e:
                span = "**逆転**"
                rng = "%s - %s" % (addr(s), addr(e))
            elif s is None:
                rng = "%17s" % ("- %s" % addr(e))
                span = "-"
            elif e is None:
                rng = "%s -      " % addr(s)
                span = "動的"
            else:
                rng = "%s - %s" % (addr(s), addr(e))
                span = human(e - s + 1)
            name = r["name"] + (("  (" + r["note"] + ")") if r["note"] else "")
            out.append((pad(rng, 18) + pad(span, 9) + pad(name, 62) +
                        r["attr"] + mark).rstrip())
        out.append("")
    return "\n".join(out).rstrip()


def render(m, sym, rows, map_path, root):
    d = {"reversed": reversed_ranges(rows), "overlaps": overlaps(rows)}
    lines = defects(rows, m, sym, root)
    parts = []
    parts.append(BEGIN)
    parts.append("")
    parts.append("番地の定義の正典は [`include/memmap.h`](../include/memmap.h)。"
                 "この表はそこと")
    parts.append("`%s` の `__bss_end` から "
                 "[`tools/gen_memmap.py`](../tools/gen_memmap.py) が起こす。"
                 % MAP_REL)
    parts.append("手で直しても次の `--write` で消える。**表記は全て絶対番地** — "
                 "相対表記 (`+4KB`) は")
    parts.append("読み手に暗算させ、2026-09-17 の「SHM がカーネルスタックに"
                 "食い込んでいた」穴を隠していた。")
    parts.append("")
    parts.append("```")
    parts.append("__bss_end      = 0x%06X   (カーネル本体 %s)"
                 % (sym["__bss_end"],
                    human(sym["__bss_end"] - (m.get("KERNEL_LOAD_ADDR") or 0))))
    parts.append("__sqlite_start = 0x%06X" % sym["__sqlite_start"])
    parts.append("__sqlite_end   = 0x%06X   (SQLite 本体 %s)"
                 % (sym["__sqlite_end"],
                    human(sym["__sqlite_end"] - sym["__sqlite_start"])))
    parts.append("")
    parts.append(table(rows, d))
    parts.append("")
    parts.append("  属性: RW=読み書き / RO=読み取り専用 / NP=Not-Present (ガード)")
    parts.append("  USER=CPL=3 から見える。`<<<` の行は下の「地図の矛盾」に出る帯。")
    top = m.get("MEM_APP_BAND_MAX_TOP")
    if top is not None:
        parts.append("  アプリ固有 PDE (0x%06X から 4MB 単位) は最大 0x%06X まで伸びる。"
                     % (m.get("MEM_APP_BAND_BASE") or 0, top))
    parts.append("```")
    parts.append("")
    if lines:
        parts.append("**地図の矛盾 (%d 件)** — "
                     "`python3 tools/gen_memmap.py --check` が同じものを出す。" % len(lines))
        parts.append("")
        for x in lines:
            parts.append("- " + x)
        parts.append("")
        parts.append("直し方は票 "
                     "[`archive/kernel_v21/TASK_KSTACK_USER.md`](archive/kernel_v21/TASK_KSTACK_USER.md) §4 の 4。")
        parts.append("")
    else:
        parts.append("**地図の矛盾: 0 件** "
                     "(重なりも逆転も無い。`--check` が毎回確かめる)")
        parts.append("")
    b = budget(m, sym)
    if b:
        parts.append("**カーネル本体の予算**: %s 中 %s を使用 (残り %s)。"
                     % (human(b[0]), human(b[1]),
                        human(b[0] - b[1]) if b[0] >= b[1]
                        else "**%s 超過**" % human(b[1] - b[0])))
        parts.append("")
    hr = headroom(m, sym)
    if hr:
        parts.append("**カーネルがあと何 KB 育つと何が壊れるか** "
                     "(`__bss_end` が伸びると `KHEAP_BASE` 以降が芋づるで動く)")
        parts.append("")
        for label, need, note in hr:
            head = ("**いま既にそうなっている** — " if need == 0
                    else "`__bss_end` +%s で " % human(need))
            parts.append("- %s%s%s"
                         % (head, label, ("。" + note) if note else ""))
        parts.append("")
    parts.append(END)
    return "\n".join(parts) + "\n"


# ---------------------------------------------------------------------------
#  6. docs/02_memory.md への差し込み
# ---------------------------------------------------------------------------

def splice(doc, block):
    i = doc.find(BEGIN)
    j = doc.find(END)
    if i < 0 or j < 0:
        return None
    return doc[:i] + block + doc[j + len(END) + 1:]


def load(root, map_path):
    sym = read_symbols(map_path)
    m = Macros(read_defines(os.path.join(root, HDR_REL)), sym)
    return m, sym, bands(m, sym)


def main():
    ap = argparse.ArgumentParser(description="メモリ地図の生成と重なり・逆転の検出")
    ap.add_argument("--write", action="store_true",
                    help="docs/02_memory.md の生成ブロックを差し替える")
    ap.add_argument("--check", action="store_true",
                    help="重なり・逆転・鮮度ずれがあれば 1 で終わる")
    ap.add_argument("--headroom", action="store_true",
                    help="カーネルがあと何 KB 育つと何が壊れるかだけ出す")
    ap.add_argument("--root", default=None, help="別のツリーを見る (試験用)")
    ap.add_argument("--map", default=None, help="kernel.map の場所")
    args = ap.parse_args()

    root = args.root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    map_path = args.map or os.path.join(root, MAP_REL)

    try:
        m, sym, rows = load(root, map_path)
    except MissingMap as e:
        sys.stderr.write("gen_memmap: %s\n" % e)
        return 2

    lines = defects(rows, m, sym, root)
    block = render(m, sym, rows, map_path, root)

    if args.headroom:
        for label, need, note in headroom(m, sym):
            head = ("**いま既にそうなっている**" if need == 0
                    else "__bss_end +%s" % human(need))
            print("%-28s %s%s" % (head, label, ("。" + note) if note else ""))
        return 0

    out_path = os.path.join(root, OUT_REL)
    doc = None
    if os.path.isfile(out_path):
        with open(out_path, encoding="utf-8") as f:
            doc = f.read()

    if args.write:
        if doc is None:
            sys.stderr.write("gen_memmap: %s が無い\n" % OUT_REL)
            return 2
        fresh = splice(doc, block)
        if fresh is None:
            sys.stderr.write("gen_memmap: %s に生成ブロックの印が無い。\n"
                             "  %s\n  %s\n をこの順で置くこと。\n"
                             % (OUT_REL, BEGIN, END))
            return 2
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(fresh)
        print("%s の生成ブロックを書き出した (%d 行)" % (OUT_REL, block.count("\n")))
        for x in lines:
            sys.stderr.write("gen_memmap: 警告: %s\n" % x)
        return 0

    if args.check:
        rc = 0
        for x in lines:
            print("gen_memmap: %s" % x)
        if lines:
            print("")
            print("地図に矛盾がある (%d 件)。番地の正典は include/memmap.h。" % len(lines))
            rc = 1
        if doc is None:
            print("gen_memmap: %s が無い" % OUT_REL)
            return 1
        current = splice(doc, block)
        if current is None:
            print("gen_memmap: %s に生成ブロックの印が無い "
                  "(--write が差し込めない)" % OUT_REL)
            return 1
        if current != doc:
            print("")
            print("gen_memmap: %s の生成ブロックが古い。"
                  "`python3 tools/gen_memmap.py --write` を実行せよ。" % OUT_REL)
            diff = list(difflib.unified_diff(
                doc.split("\n"), current.split("\n"),
                fromfile=OUT_REL, tofile="生成器の出力", lineterm="", n=1))
            for line in diff[:40]:
                print("  " + line)
            if len(diff) > 40:
                print("  ... (差分 %d 行のうち先頭 40 行)" % len(diff))
            rc = 1
        if rc == 0:
            print("地図に矛盾なし / %s は最新" % OUT_REL)
        return rc

    sys.stdout.write(block)
    return 0


if __name__ == "__main__":
    sys.exit(main())
