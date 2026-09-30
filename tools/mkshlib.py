#!/usr/bin/env python3
"""
mkshlib.py — 共有ライブラリ (OS32X_FLAG_SHLIB) の先頭ページを検算し、
             OS32X ヘッダを付けて `*.shlib` を出す。票 C3。

やること:

  1. **番号表の突き合わせ** (`--check` 単独でも実行可)
     `userland/rust/libos32gui/src/shlib.rs` の `global_asm!` にある
     `.long os32gui_*` の並び順と、`sdk/rust/os32api/src/gui/stub.rs` の
     `E_* = <番号>` を突き合わせる。ここがずれると、アプリは黙って別の関数へ
     飛ぶ (メモリ os32-verify-traps の「stale で沈黙」と同じ壊れ方)。

  2. **ヘッダの検算 / 穴埋め**
     `.shlib_hdr` 先頭 32B の magic / version / nfunc / data_vaddr /
     data_pages / text_pages を ELF から求めた値と突き合わせる。
     リンカスクリプト (`sdk/link/shlib.ld`) の絶対シンボルで既に埋まって
     いれば検算だけ、0 のままなら書き込む。
     さらに entry[i] が i 番目のシンボルのアドレスと一致するかを見る。

  3. **OS32X ヘッダ v3 (48B)** を付けて出力する (`OS32X_FLAG_SHLIB` を立てる)。
     組み立ては `sdk/os32x_hdr.py` (`sdk/mkos32x.py` と共通)。`kapi_data_off` は
     ELF の `.os32_kapi_layout` (os32api の刻印) から取り、無ければ失敗する。
     `min_api_ver` は 63 未満なら 63 に引き上げる (票 TASK_KAPI_DATA_FIELDS)。

使い方:
    python3 tools/mkshlib.py <in.raw> <out.shlib> --elf <lib.elf> [--api VER]
    python3 tools/mkshlib.py --check            # 番号表の突き合わせだけ
"""

import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'sdk'))
import os32x_hdr as H  # noqa: E402  (OS32X ヘッダ v3 の共通モジュール)

OS32X_FLAG_SHLIB = H.OS32X_FLAG_SHLIB

# --- OS32ShlibHeader ---
OS32_SHLIB_MAGIC = 0x42494C53     # 'SLIB'
OS32_SHLIB_HDR_SIZE = 4096
OS32_SHLIB_ENTRY_OFF = 32
OS32_SHLIB_MAX_FUNC = (OS32_SHLIB_HDR_SIZE - OS32_SHLIB_ENTRY_OFF) // 4

MEM_SHLIB_BASE = 0x00400000
PAGE = 4096

SHLIB_RS = os.path.join(REPO, 'userland', 'rust', 'libos32gui', 'src', 'shlib.rs')
STUB_RS = os.path.join(REPO, 'sdk', 'rust', 'os32api', 'src', 'gui', 'stub.rs')
PROTO_RS = os.path.join(REPO, 'sdk', 'rust', 'os32api', 'src', 'gui', 'proto.rs')


# ====================================================================
#  1. 番号表の突き合わせ
# ====================================================================

def parse_asm_table(path):
    """shlib.rs の global_asm! から (names, version, nfunc) を取り出す。"""
    with open(path, 'r', encoding='utf-8') as f:
        src = f.read()
    m = re.search(r'global_asm!\s*\(\s*r#"(.*?)"#\s*\)', src, re.S)
    if not m:
        raise SystemExit(f"mkshlib: {path} に global_asm! が見つかりません")
    body = m.group(1)
    names = re.findall(
        r'^\s*\.long\s+(os32gui_[A-Za-z0-9_]+)\s*(?:/\*.*?\*/)?\s*$', body, re.M)
    ver = re.search(r'\.long\s+(\d+)\s*/\*\s*0x04\s+version', body)
    nfn = re.search(r'\.long\s+(\d+)\s*/\*\s*0x08\s+nfunc', body)
    if not names:
        raise SystemExit(f"mkshlib: {path} の表に `.long os32gui_*` がありません")
    return names, int(ver.group(1)) if ver else None, int(nfn.group(1)) if nfn else None


def parse_stub_consts(path):
    """stub.rs から {番号: E_名} と SHLIB_NFUNC を取り出す。"""
    with open(path, 'r', encoding='utf-8') as f:
        src = f.read()
    idx = {}
    for name, val in re.findall(r'^pub const (E_[A-Z0-9_]+): usize = (\d+);', src, re.M):
        v = int(val)
        if v in idx:
            raise SystemExit(f"mkshlib: {path}: 番号 {v} が {idx[v]} と {name} で重複")
        idx[v] = name
    m = re.search(r'^pub const SHLIB_NFUNC: usize = (\d+);', src, re.M)
    return idx, int(m.group(1)) if m else None


def sym_to_const(sym):
    """os32gui_fill_rect -> E_FILL_RECT (例外は下の表)。"""
    special = {
        'os32gui_shlib_init': 'E_SHLIB_INIT',
        'os32gui_client_init': 'E_CLIENT_INIT',
        'os32gui_client_stats': 'E_CLIENT_STATS',
        'os32gui_gfx_stats': 'E_GFX_STATS',
    }
    if sym in special:
        return special[sym]
    return 'E_' + sym[len('os32gui_'):].upper()


def parse_proto_version(path):
    """proto.rs の GUI_PROTO_VERSION。"""
    with open(path, 'r', encoding='utf-8') as f:
        m = re.search(r'^pub const GUI_PROTO_VERSION: u16 = (\d+);', f.read(), re.M)
    return int(m.group(1)) if m else None


def check_table(verbose=True):
    names, ver, nfunc = parse_asm_table(SHLIB_RS)
    consts, stub_nfunc = parse_stub_consts(STUB_RS)
    proto_ver = parse_proto_version(PROTO_RS)
    errs = []

    if proto_ver is None:
        errs.append("proto.rs から GUI_PROTO_VERSION が読めません")
    elif ver != proto_ver:
        errs.append(
            f"shlib.rs のヘッダ version={ver} が GUI_PROTO_VERSION={proto_ver} と違います "
            f"(版を上げたら global_asm! の `.long <version>` も直すこと。"
            f"ここがずれると全アプリが起動を拒みます)")

    if nfunc is not None and nfunc != len(names):
        errs.append(f"shlib.rs: nfunc={nfunc} だが .long の本数は {len(names)}")
    if stub_nfunc is not None and stub_nfunc != len(names):
        errs.append(f"stub.rs: SHLIB_NFUNC={stub_nfunc} だが表は {len(names)} 本")
    if len(names) > OS32_SHLIB_MAX_FUNC:
        errs.append(f"表が先頭ページに入りません ({len(names)} > {OS32_SHLIB_MAX_FUNC})")

    for i, sym in enumerate(names):
        want = sym_to_const(sym)
        got = consts.get(i)
        if got is None:
            errs.append(f"#{i} {sym}: stub.rs に番号 {i} の定数がありません")
        elif got != want:
            errs.append(f"#{i} {sym}: stub.rs の番号 {i} は {got} (期待 {want})")

    extra = sorted(k for k in consts if k >= len(names))
    for k in extra:
        errs.append(f"stub.rs の {consts[k]} = {k} に対応する表の項目がありません")

    if errs:
        print("mkshlib: ジャンプ表の番号がずれています —", file=sys.stderr)
        for e in errs:
            print("  " + e, file=sys.stderr)
        return None
    if verbose:
        print(f"  mkshlib: 番号表 OK ({len(names)} 本, version={ver})")
    return names, ver, len(names)


# ====================================================================
#  2. ELF (32bit LE) の最小リーダ
# ====================================================================

class Elf32(H.Elf32):
    """共通モジュールの ELF リーダ。失敗は mkshlib の終了に揃える。"""

    def __init__(self, path):
        try:
            super().__init__(path)
        except H.HeaderError as e:
            raise SystemExit(f"mkshlib: {e}")


# ====================================================================
#  3. 本体
# ====================================================================

def die(msg):
    print(f"mkshlib: {msg}", file=sys.stderr)
    sys.exit(1)


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] == '--check':
        sys.exit(0 if check_table() else 1)
    if len(argv) < 2:
        print(__doc__)
        sys.exit(1)

    in_path, out_path = argv[0], argv[1]
    elf_path = None
    min_api = 1
    i = 2
    while i < len(argv):
        if argv[i] == '--elf' and i + 1 < len(argv):
            elf_path = argv[i + 1]
            i += 2
        elif argv[i] == '--api' and i + 1 < len(argv):
            min_api = int(argv[i + 1], 0)
            i += 2
        else:
            die(f"不明なオプション: {argv[i]}")
    if not elf_path:
        die("--elf <lib.elf> が要ります (ページ数と BSS をここから求めます)")

    checked = check_table()
    if checked is None:
        sys.exit(1)
    names, want_version, want_nfunc = checked

    elf = Elf32(elf_path)
    with open(in_path, 'rb') as f:
        raw = bytearray(f.read())
    # ELF との内容照合は**穴埋め (fix) の前の**中身で行う (下で先頭 32B を
    # 書き換えるので、書き換え後だと ELF と一致しなくなる)。
    raw_as_read = bytes(raw)
    if len(raw) < OS32_SHLIB_HDR_SIZE:
        die(f"{in_path} が先頭ページ (4096B) より小さい: {len(raw)}B")

    # --- ELF から本来の値を求める ---
    data_start = elf.symbols.get('__shlib_data_start')
    data_end = elf.symbols.get('__shlib_data_end')
    if data_start is None or data_end is None:
        die("__shlib_data_start / __shlib_data_end が ELF にありません "
            "(sdk/link/shlib.ld でリンクしていますか)")
    if data_start % PAGE or data_end % PAGE:
        die(f"data 範囲がページ境界にありません: {data_start:#x}..{data_end:#x}")
    hdr_addr = elf.symbols.get('__os32_shlib_header')
    if hdr_addr is not None and hdr_addr != MEM_SHLIB_BASE:
        die(f"ヘッダが {hdr_addr:#x} にあります (期待 {MEM_SHLIB_BASE:#x})")

    text_pages = (data_start - MEM_SHLIB_BASE) // PAGE
    data_pages = (data_end - data_start) // PAGE

    # --- 先頭 32B の検算 / 穴埋め ---
    magic, version, nfunc, vaddr, dpages, tpages, r0, r1 = \
        struct.unpack_from('<8I', raw, 0)
    if magic != OS32_SHLIB_MAGIC:
        die(f"magic が違います: {magic:#x} (期待 {OS32_SHLIB_MAGIC:#x})")
    if version != want_version:
        die(f"version が違います: {version} (shlib.rs の表は {want_version})")
    if nfunc != want_nfunc:
        die(f"nfunc が違います: {nfunc} (表は {want_nfunc} 本)")

    def fix(field, cur, want):
        if cur == want:
            return want
        if cur == 0:
            print(f"  mkshlib: {field} を {want} で埋めました")
            return want
        die(f"{field} が {cur} ですが ELF は {want} です")

    vaddr = fix('data_vaddr', vaddr, data_start)
    dpages = fix('data_pages', dpages, data_pages)
    tpages = fix('text_pages', tpages, text_pages)
    struct.pack_into('<8I', raw, 0, OS32_SHLIB_MAGIC, version, nfunc,
                     vaddr, dpages, tpages, r0, r1)

    # --- entry[i] が i 番目のシンボルのアドレスか ---
    bad = []
    for i, sym in enumerate(names):
        (got,) = struct.unpack_from('<I', raw, OS32_SHLIB_ENTRY_OFF + i * 4)
        want = elf.symbols.get(sym)
        if want is None:
            bad.append(f"#{i} {sym}: ELF にシンボルがありません")
        elif got != want:
            bad.append(f"#{i} {sym}: entry={got:#x} だがシンボルは {want:#x}")
        elif not (MEM_SHLIB_BASE + PAGE <= got < data_start):
            bad.append(f"#{i} {sym}: {got:#x} が共有 .text の外です")
    if bad:
        print("mkshlib: ジャンプ表の中身が壊れています —", file=sys.stderr)
        for b in bad:
            print("  " + b, file=sys.stderr)
        sys.exit(1)

    # --- 本文 (.text/.rodata/.data) が data_end を越えていないか ---
    image_end = MEM_SHLIB_BASE + len(raw)
    if image_end > data_end:
        die(f"生イメージが data 範囲を越えています ({image_end:#x} > {data_end:#x})")

    # --- OS32X ヘッダ v3 (エントリは使わない: 入口はジャンプ表) ---
    #   load_addr     = MEM_SHLIB_BASE (カーネルは見ないが v2 の欄として正しく埋める)
    #   kapi_data_off = os32api の刻印 (票 TASK_KAPI_DATA_FIELDS)
    bss = elf.bss_size()
    try:
        kapi_data_off = H.read_kapi_layout(elf)
        H.check_raw_matches_elf(elf, raw_as_read, in_path)
        header = H.build_header(OS32X_FLAG_SHLIB, 0, len(raw), bss, 0,
                                min_api, MEM_SHLIB_BASE, kapi_data_off)
    except H.HeaderError as e:
        die(str(e))
    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(raw)

    print(f"  SHLIB: {os.path.basename(out_path)} "
          f"(nfunc={nfunc}, version={version}, "
          f"text_pages={tpages}, data_vaddr={vaddr:#x}, data_pages={dpages}, "
          f"raw={len(raw)}, bss={bss}, api>={H.effective_min_api(min_api)}, "
          f"kapi_data=0x{kapi_data_off:X})")


if __name__ == '__main__':
    main()
