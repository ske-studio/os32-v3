#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""memmap.h の地図と、生きているゲストのページ表を突き合わせる。"""
import json, struct, sys

SC = "/tmp/claude-1000/-home-hight-os32/0d4e51c0-865c-48b5-a375-eba7544fdd4a/scratchpad"
BSS_END = 0x16BF20                       # build/out/kernel.map の実値

# --- memmap.h の定数を実値で解く -----------------------------------------
K = {}
K['KHEAP_BASE']        = (BSS_END + 0xFFF) & ~0xFFF
K['KHEAP_SIZE']        = 0x30000
K['MEM_KAPI_SIZE']     = 0x1000
K['MEM_KAPI_BASE']     = K['KHEAP_BASE'] + K['KHEAP_SIZE']
K['MEM_SHM_GUARD_LO']  = K['KHEAP_BASE'] + K['KHEAP_SIZE'] + K['MEM_KAPI_SIZE']
K['MEM_SHM_BASE']      = K['MEM_SHM_GUARD_LO'] + 0x1000
K['MEM_SHM_SIZE']      = 0x40000
K['MEM_SHM_END']       = K['MEM_SHM_BASE'] + K['MEM_SHM_SIZE'] - 1
K['MEM_SHM_GUARD_HI']  = K['MEM_SHM_BASE'] + K['MEM_SHM_SIZE']
K['MEM_SHM_RESV_START']= K['MEM_SHM_GUARD_HI'] + 0x1000
K['MEM_STACK_GUARD']   = 0x1FB000
K['MEM_SHM_RESV_END']  = K['MEM_STACK_GUARD'] - 1
K['MEM_KSTACK_BASE']   = 0x1FC000
K['MEM_KSTACK_TOP']    = 0x1FFFFC
K['MEM_SHM_GUI_BASE']  = K['MEM_SHM_BASE'] + 0x30000
K['MEM_SHM_GUI_SIZE']  = 0x10000

def pte_all():
    out = {}
    for i in range(8):
        d = json.load(open("%s/pt%d.json" % (SC, i)))
        v = struct.unpack('<1024I', bytes.fromhex(d['hex']))
        for j, e in enumerate(v):
            out[(i * 1024 + j) * 0x1000] = e
    return out

def attr(e):
    return (e & 1, (e >> 1) & 1, (e >> 2) & 1)

def show(lo, hi, label, want_p=None, want_us=None):
    """[lo,hi] を見て、属性が変わる境目ごとに 1 行出す。"""
    pt = PTE
    runs, cur = [], None
    a = lo & ~0xFFF
    while a <= hi:
        e = pt.get(a)
        k = attr(e) if e is not None else None
        if cur and cur[2] == k:
            cur[1] = a
        else:
            if cur: runs.append(cur)
            cur = [a, a, k]
        a += 0x1000
    if cur: runs.append(cur)
    bad = 0
    for s, e, k in runs:
        if k is None:
            mark = "  (PT 無し)"
        else:
            p, rw, us = k
            ng = []
            if want_p is not None and p != want_p: ng.append("P=%d (期待 %d)" % (p, want_p))
            if want_us is not None and us != want_us: ng.append("US=%d (期待 %d)" % (us, want_us))
            mark = ("  **" + " / ".join(ng) + "**") if ng else ""
            if ng: bad += 1
        print("  %08x-%08x  P=%d RW=%d US=%d%s" %
              (s, e + 0xFFF, k[0], k[1], k[2], mark) if k else
              "  %08x-%08x  %s" % (s, e + 0xFFF, mark))
    return bad

PTE = pte_all()
print("=== 解いた定数 (__bss_end = 0x%06X) ===" % BSS_END)
for k in ['KHEAP_BASE','MEM_KAPI_BASE','MEM_SHM_GUARD_LO','MEM_SHM_BASE','MEM_SHM_END',
          'MEM_SHM_GUARD_HI','MEM_SHM_RESV_START','MEM_SHM_RESV_END',
          'MEM_STACK_GUARD','MEM_KSTACK_BASE','MEM_KSTACK_TOP','MEM_SHM_GUI_BASE']:
    print("  %-20s 0x%06X" % (k, K[k]))
print()
print("  MEM_SHM_RESV の範囲: 0x%06X 〜 0x%06X  %s" %
      (K['MEM_SHM_RESV_START'], K['MEM_SHM_RESV_END'],
       "**逆転している (空振り)**" if K['MEM_SHM_RESV_START'] > K['MEM_SHM_RESV_END'] else "正常"))
print("  SHM 本体がスタックガードを飲むか: %s" %
      ("**はい**" if K['MEM_SHM_BASE'] <= K['MEM_STACK_GUARD'] <= K['MEM_SHM_END'] else "いいえ"))
print("  SHM 本体がスタック本体に届くか : %s" %
      ("**はい (0x%06X まで)**" % K['MEM_SHM_END']
       if K['MEM_SHM_END'] >= K['MEM_KSTACK_BASE'] else "いいえ"))
print("  SHM 後方ガードの位置           : 0x%06X %s" %
      (K['MEM_SHM_GUARD_HI'],
       "**カーネルスタックの中**" if K['MEM_KSTACK_BASE'] <= K['MEM_SHM_GUARD_HI'] <= K['MEM_KSTACK_TOP'] else ""))
print()

total = 0
checks = [
    ("カーネル .text/.data/.bss", 0x100000, BSS_END, 1, 0),
    ("カーネルヒープ (320KB)", K['KHEAP_BASE'], K['MEM_KAPI_BASE'] - 1, 1, 0),
    ("KAPI テーブル (4KB)", K['MEM_KAPI_BASE'], K['MEM_SHM_GUARD_LO'] - 1, 1, 0),
    ("SHM 前方ガード (NP のはず)", K['MEM_SHM_GUARD_LO'], K['MEM_SHM_GUARD_LO'] + 0xFFF, 0, None),
    ("SHM 本体 (present + USER)", K['MEM_SHM_BASE'], K['MEM_SHM_END'], 1, 1),
    ("SHM 後方ガード (NP のはず)", K['MEM_SHM_GUARD_HI'], K['MEM_SHM_GUARD_HI'] + 0xFFF, 0, None),
    ("スタックガード (NP のはず)", K['MEM_STACK_GUARD'], K['MEM_STACK_GUARD'] + 0xFFF, 0, None),
    ("カーネルスタック 16KB (present, US=0)", K['MEM_KSTACK_BASE'], 0x1FFFFF, 1, 0),
    ("SQLite 帯", 0x200000, 0x2FFFFF, None, 0),
    ("常駐シェル帯", 0x300000, 0x3FFFFF, None, 0),
]
for label, lo, hi, wp, wu in checks:
    print("--- %s  (0x%06X-0x%06X)" % (label, lo, hi))
    total += show(lo, hi, label, wp, wu)
print()
print("食い違いのある区間: %d 件" % total)
