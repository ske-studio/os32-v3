#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Compare memmap.h with captured guest PTs (pt0.json ... pt7.json).

Usage: python3 tools/memmap_audit_live.py SNAPSHOT_DIR [--root DEPLOYED_TREE]
Each JSON is an emu_read_mem response with a 4096-byte `hex` member.
Use the ELF/header from the deployed build, not another worktree's artifacts.
"""
import argparse
import json
import struct
from pathlib import Path

from gen_memmap import Macros, read_defines, read_elf, layout_header_defects

ROOT = Path(__file__).resolve().parents[1]
PAGE = 4096


def pte_all(directory, count):
    out = {}
    for i in range(count):
        raw = bytes.fromhex(json.loads((directory / f'pt{i}.json').read_text())['hex'])
        for j, entry in enumerate(struct.unpack('<1024I', raw)):
            out[(i * 1024 + j) * PAGE] = entry
    return out


def checks(k, symbols):
    """Inclusive intervals; every changing address comes from this build."""
    page_end = PAGE - 1
    # exec_tramp_page_addr(): the one USER/RO page inside kernel BSS.
    tramp = (symbols['ring3_tramp_raw'] + page_end) & ~page_end
    return [
        ("カーネル (trampoline 前)", k['KERNEL_LOAD_ADDR'], tramp - 1, 1, 0),
        ("KAPI trampoline", tramp, tramp + page_end, 1, 1),
        ("カーネル (trampoline 後)", tramp + PAGE, symbols['__bss_end'] - 1, 1, 0),
        ("SQLite 帯", symbols['__sqlite_start'], symbols['__sqlite_end'] - 1, 1, 0),
        ("SQLite stack guard", k['MEM_SQLITE_STACK_GUARD'], k['MEM_SQLITE_STACK_BASE'] - 1, 0, None),
        ("SQLite stack", k['MEM_SQLITE_STACK_BASE'], k['MEM_SQLITE_STACK_BASE'] + k['MEM_SQLITE_STACK_SIZE'] - 1, 1, 0),
        ("カーネルヒープ", k['KHEAP_BASE'], k['KHEAP_BASE'] + k['KHEAP_SIZE'] - 1, 1, 0),
        ("KAPI テーブル", k['KHEAP_BASE'] + k['KHEAP_SIZE'], k['MEM_SHM_GUARD_LO'] - 1, 1, 0),
        ("SHM 前方 guard", k['MEM_SHM_GUARD_LO'], k['MEM_SHM_GUARD_LO'] + page_end, 0, None),
        ("SHM", k['MEM_SHM_BASE'], k['MEM_SHM_END'], 1, 1),
        ("SHM 後方 guard", k['MEM_SHM_GUARD_HI'], k['MEM_SHM_GUARD_HI'] + page_end, 0, None),
        ("SLACK", k['MEM_SHM_RESV_START'], k['MEM_SHM_RESV_END'], 1, 0),
        ("固定 paging guard", k['MEM_FIXED_PAGING_GUARD_BASE'], k['MEM_FIXED_PAGING_BASE'] - 1, 0, None),
        ("固定 paging 枠", k['MEM_FIXED_PAGING_BASE'], k['MEM_FIXED_PAGING_FRAME_END'] - 1, 1, 0),
        ("kernel stack guard", k['MEM_STACK_GUARD'], k['MEM_STACK_GUARD_END'], 0, None),
        ("kernel stack", k['MEM_KSTACK_BASE'], k['MEM_KSTACK_BASE'] + k['MEM_KSTACK_SIZE'] - 1, 1, 0),
        ("shell image/sbrk/exec_heap", k['MEM_SHELL_LOAD_ADDR'], k['MEM_SHELL_GUARD'] - 1, 1, 0),
        ("shell stack guard", k['MEM_SHELL_GUARD'], k['MEM_SHELL_GUARD'] + page_end, 0, None),
        ("shell stack", k['MEM_SHELL_STACK_TOP'] - k['MEM_SHELL_STACK_SIZE'], k['MEM_SHELL_BAND_END'], 1, 0),
    ]


def show(ptes, lo, hi, want_p, want_us):
    """Missing captures are failures, even for an expected NP guard."""
    runs = []
    for address in range(lo & ~(PAGE - 1), hi + 1, PAGE):
        entry = ptes.get(address)
        attr = None if entry is None else (entry & 1, (entry >> 1) & 1, (entry >> 2) & 1)
        if runs and runs[-1][2] == attr:
            runs[-1][1] = address
        else:
            runs.append([address, address, attr])
    bad = 0
    for start, end, attr in runs:
        failed = attr is None or attr[0] != want_p or (want_us is not None and attr[2] != want_us)
        bad += failed
        seen = 'PT missing' if attr is None else 'P=%d RW=%d US=%d' % attr
        print(f'  {start:08x}-{end + PAGE - 1:08x} {seen}' + (' FAIL' if failed else ''))
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot_dir', type=Path)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    elf = args.root / 'build/out/kernel.elf'
    header = args.root / 'build/out/memmap_layout.h'
    defects = layout_header_defects(args.root, elf, header)
    if defects:
        parser.error('; '.join(defects) + '; run make kernel')
    _, symbols = read_elf(elf)
    macros = Macros(read_defines(args.root / 'include/memmap.h'), symbols)
    k = {name: macros.get(name) for name in macros.defines}
    ptes = pte_all(args.snapshot_dir, k['MEM_FIXED_BOOT_PT_COUNT'])
    total = 0
    for label, lo, hi, wp, wu in checks(k, symbols):
        print(f'--- {label} ({lo:08x}-{hi:08x})')
        total += show(ptes, lo, hi, wp, wu)
    print(f'食い違いのある区間: {total} 件')
    return int(total != 0)


if __name__ == '__main__':
    raise SystemExit(main())
