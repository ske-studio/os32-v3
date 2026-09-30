"""MMIO 登録と検証済み資源レコード (T1d) — 実ソース ILP32 ハーネス、装置 I/O なし。

  python3 -B tools/tests/test_device_reservation.py            # 肯定側 (unittest)
  python3 -B tools/tests/test_device_reservation.py --mutate   # 否定側 + 肯定側

票 docs/tasks/v3/TASK_T1_LEDGER.md §4-4 (T1d、D33・X4)。旧 DEVICE_RESERVATION の
核 (pgalloc_device_reserve / sys_device_reserve_core) を台帳の ledger_reserve_set
に載せ直したので、その試験をここで流用・拡張する。kernel/pgalloc.c と
kernel/ledger_pci.c を丸ごと、drivers/pci.c からは読み口 pci_get だけを
切り出して取り込む (特権命令の irq_save / irq_restore だけを贋物にし、
irq_restore の度に L1 / L2 の不変条件を全ページ検査する — test_ledger.py と同じ
足場)。見るもの:

  - X4 の 6 項目: 第二 span の衝突、台帳満杯 (区間の表・資源の表)、同 owner の
    部分一致の拒否、丸めによる衝突、map / probe 失敗後の永久保持、モジュール
    回収後の予約保持
  - BACKGROUND (15〜16MB、2GB〜4GiB) に重なる予約は通り、FIXED / STAGING /
    BUNDLE / DMA / SURFACE_BACKING に重なる予約は拒否 (P3)
  - B3: PEGC と Xe10 の実範囲を同 owner で 1 回に入れると併合されて通り、
    別 owner だと 2 本目が拒否される
  - B10: span ごとの資源照合 (正規化の前)。gfx の 3 レコード / 3 span、併合後
    の res_mask、同じ組の再呼び出しは冪等、付け替えは拒否、Xe10 を 1 レコード
    で表すと片方の窓が decode 外で拒否
  - Trident 型 (資源 4 本・1 owner) と 82557 型 (資源 2 本) の合成要求
  - RAW / PROBE_UNVERIFIED は予約権限にならない
  - 合成 g_pci からの取り込み (メモリ BAR だけ、I/O と 0 を除く、64 ビットの
    上位、ブリッヂの 18h 以降、溢れは res_overflow、RAW は予約に使えない、B4)
  - 失敗時は L1・L2・区間の表・資源の表・会計が全部不変 (表は中身まで比べる)
  - 実 paging + sys + pgalloc の段つき起動で RAM + 窓の一括予約 (段の試験)

--mutate は予約・照合・取り込みの要の行を写しで壊し、どれかの試験が実行時に
RED になることを見る。実物のソースは書き換えない。コンパイルの失敗は RED に
数えない。
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import test_ledger as L  # noqa: E402

ROOT = L.ROOT
SOURCES = ('kernel/pgalloc.c', 'tools/tests/pgalloc_host_fixture.h',
           'kernel/ledger_pci.c', 'drivers/pci.c', 'kernel/paging.c', 'kernel/sys.c')


def load(mutation=None):
    texts = {rel: (ROOT / rel).read_text() for rel in SOURCES}
    if mutation:
        _, rel, old, new = mutation
        if old not in texts[rel]:
            raise SystemExit('変異 %s の当て先が見つからない: %s' % (mutation[0], rel))
        texts[rel] = texts[rel].replace(old, new, 1)
    return texts


def pci_reader(pci_c):
    """drivers/pci.c から読み口 pci_get の本体だけを切り出す (I/O を触らない)。"""
    head = 'int pci_get(u32 idx, void *out)'
    body = pci_c.split(head, 1)[1]
    return head + body.split('\n}\n', 1)[0] + '\n}\n'


# 合成 g_pci・切り出した列挙・台帳の取り込み・試験の足場 (ファイル有効範囲)
EXTRA = r'''
#include "pci.h"
static struct pci_dev g_pci[PCI_MAX_DEVS];
static int g_pci_count;
@PCI_READER@
@LEDGER_PCI@
static struct ledger_region snap_reg[LEDGER_MAX_REGIONS];
static struct ledger_resource snap_res[LEDGER_MAX_RESOURCES];
static void snap2(void) {
    u32 i;
    snap();
    for (i = 0; i < LEDGER_MAX_REGIONS; i++) snap_reg[i] = ledger_regions[i];
    for (i = 0; i < LEDGER_MAX_RESOURCES; i++) snap_res[i] = ledger_resources[i];
}
static int same2(void) {
    const u8 *a, *b;
    u32 i;
    a = (const u8 *)snap_reg; b = (const u8 *)ledger_regions;
    for (i = 0; i < sizeof(snap_reg); i++) if (a[i] != b[i]) return 0;
    a = (const u8 *)snap_res; b = (const u8 *)ledger_resources;
    for (i = 0; i < sizeof(snap_res); i++) if (a[i] != b[i]) return 0;
    return same();
}
/* 資源レコードを 1 本足して番号を返す (失敗は 0xFF)。 */
static u32 host_res(u32 bus, u32 wb, u32 first, u32 end, u32 mf, u32 me) {
    struct ledger_resource r = {0};
    u32 rid;
    r.bus = (u8)bus; r.width_basis = (u8)wb;
    r.decode_first = first; r.decode_end = end; r.map_first = mf; r.map_end = me;
    return ledger_resource_add(&r, &rid) ? rid : 0xFFU;
}
static void host_span(struct ledger_span *s, u32 first, u32 end, u32 kind, u32 res) {
    s->first = first; s->end = end; s->kind = kind; s->res = res;
}
/* 起動時の固定用途と背景 (8MB の池 = RAM [1024, 2048)、limit 2048)。 */
static int host_regions(void) {
    return ledger_register_region(LEDGER_R_FIXED, K, 0x100, 0x400, LEDGER_CACHE_WB, LEDGER_RF_PERMANENT) &&
        ledger_register_region(LEDGER_R_SURFACE_BACKING, LEDGER_OWNER_BOOT, 0x6A, 0x8A, LEDGER_CACHE_WB, LEDGER_RF_PERMANENT) &&
        ledger_register_region(LEDGER_R_STAGING, LEDGER_OWNER_STAGING, 1100, 1110, LEDGER_CACHE_WB, 0) &&
        ledger_register_region(LEDGER_R_BUNDLE, LEDGER_OWNER_BUNDLE, 1120, 1130, LEDGER_CACHE_WB, 0) &&
        ledger_register_region(LEDGER_R_DMA, K, 1140, 1150, LEDGER_CACHE_WB, LEDGER_RF_PERMANENT) &&
        ledger_register_region(LEDGER_R_BACKGROUND, K, 0xF00, 0x1000, LEDGER_CACHE_UC, LEDGER_RF_PERMANENT) &&
        ledger_register_region(LEDGER_R_BACKGROUND, K, 0x80000, PHYSMEM_MAX_PFN, LEDGER_CACHE_UC, LEDGER_RF_PERMANENT);
}
static const struct ledger_region *host_find(u32 first, u32 end) {
    u32 i;
    for (i = 0; i < ledger_region_count; i++)
        if (ledger_regions[i].first == first && ledger_regions[i].end == end)
            return &ledger_regions[i];
    return 0;
}
static u32 host_resources(void) {
    u32 i, n = 0;
    for (i = 0; i < LEDGER_MAX_RESOURCES; i++) n += ledger_resources[i].bus != 0;
    return n;
}
#define GFX LEDGER_OWNER_GFX
#define MMIO LEDGER_SPAN_MMIO
'''

BODIES = {}

# B3 / B10: gfx の候補群 (PEGC / Xe10-bank / Xe10-linear) を 1 owner・1 回で。
BODIES['gfx_candidates'] = r'''
    u32 pe, bk, li, set;
    const struct ledger_region *lo, *hi;
    struct ledger_span sp[3], t;
    host_pool_boot(8192);
    CHECK(host_regions());
    pe = host_res(LEDGER_BUS_FIXED, LEDGER_WB_DATASHEET, 0xF00, 0xF80, 0xF00, 0xF80);
    bk = host_res(LEDGER_BUS_CBUS, LEDGER_WB_GLUE_CONST, 0xF60, 0xF68, 0, 0);
    li = host_res(LEDGER_BUS_CBUS, LEDGER_WB_GLUE_CONST, 0xFE000, 0xFE400, 0xFE000, 0xFE200);
    CHECK(pe == 0 && bk == 1 && li == 2);
    host_span(&sp[0], 0xF00, 0xF80, MMIO, pe);
    host_span(&sp[1], 0xF60, 0xF68, MMIO, bk);
    host_span(&sp[2], 0xFE000, 0xFE400, MMIO, li);
    snap2();
    /* 付け替えは拒否 (B10): PEGC 窓を Xe10-bank で、リニア窓を Xe10-bank /
     * PEGC で許さない */
    sp[0].res = bk;
    CHECK(!ledger_reserve_set(GFX, sp, 3) && same2());
    sp[0].res = pe; sp[2].res = bk;
    CHECK(!ledger_reserve_set(GFX, sp, 3) && same2());
    sp[2].res = pe;
    CHECK(!ledger_reserve_set(GFX, sp, 3) && same2());
    sp[2].res = li;
    CHECK(ledger_reserve_set(GFX, sp, 3));
    /* 正規化: PEGC + 銀行窓が 1 区間、リニア窓が 1 区間 */
    CHECK(ledger_region_count == snap_regions + 2);
    lo = host_find(0xF00, 0xF80);
    hi = host_find(0xFE000, 0xFE400);
    CHECK(lo && lo->type == LEDGER_R_DEVICE && lo->owner == GFX);
    CHECK(lo->res_mask == ((1U << pe) | (1U << bk)));
    CHECK(lo->cache == LEDGER_CACHE_UC);
    CHECK(lo->flags == (LEDGER_RF_PERMANENT | LEDGER_RF_OUTSIDE));
    CHECK(hi && hi->type == LEDGER_R_DEVICE && hi->owner == GFX && hi->res_mask == (1U << li));
    CHECK(hi->flags == (LEDGER_RF_PERMANENT | LEDGER_RF_OUTSIDE));
    set = lo->span_set;
    CHECK(set && hi->span_set == set);
    CHECK(ledger_selfcheck("gfx"));
    /* 同じ組の再呼び出しは冪等 (順不同、何も変えない) */
    snap2();
    t = sp[0]; sp[0] = sp[2]; sp[2] = t;
    CHECK(ledger_reserve_set(GFX, sp, 3) && same2());
    /* 同 owner の部分一致は拒否 (リニア窓 + 銀行窓だけ、リニア窓だけ) */
    CHECK(!ledger_reserve_set(GFX, sp, 2) && same2());
    CHECK(!ledger_reserve_set(GFX, sp, 1) && same2());
    /* 区間は同じでも根拠が違えば別の集合 (PEGC 窓を PEGC だけで + リニア窓) */
    host_span(&sp[1], 0xF00, 0xF80, MMIO, pe);
    CHECK(!ledger_reserve_set(GFX, sp, 2) && same2());
    /* 永久保持: 返却も回収もできない */
    CHECK(!ledger_owner_retire(GFX) && !ledger_reclaim_owner(GFX, 0) && same2());
'''

# 3 回目の反例: Xe10 を decode 1 組のレコード 1 本で表すと片方が decode 外。
BODIES['xe10_single_record'] = r'''
    u32 xe;
    struct ledger_span sp[2];
    host_pool_boot(8192);
    CHECK(host_regions());
    xe = host_res(LEDGER_BUS_CBUS, LEDGER_WB_GLUE_CONST, 0xF60, 0xF68, 0, 0);
    host_span(&sp[0], 0xF60, 0xF68, MMIO, xe);
    host_span(&sp[1], 0xFE000, 0xFE400, MMIO, xe);
    snap2();
    CHECK(!ledger_reserve_set(GFX, sp, 2) && same2());
    CHECK(ledger_reserve_set(GFX, sp, 1));
'''

# B3 の反例 (別 owner だと 2 本目が拒否) と X4 の「第二 span の衝突」。
BODIES['other_owner_and_second_span'] = r'''
    u32 pe, bk, li, d2;
    struct ledger_span sp[2];
    host_pool_boot(8192);
    CHECK(host_regions());
    pe = host_res(LEDGER_BUS_FIXED, LEDGER_WB_DATASHEET, 0xF00, 0xF80, 0, 0);
    bk = host_res(LEDGER_BUS_CBUS, LEDGER_WB_GLUE_CONST, 0xF60, 0xF68, 0, 0);
    li = host_res(LEDGER_BUS_CBUS, LEDGER_WB_GLUE_CONST, 0xFE000, 0xFE400, 0, 0);
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 1, "pegc", &d2));
    host_span(&sp[0], 0xF00, 0xF80, MMIO, pe);
    CHECK(ledger_reserve_set(d2, sp, 1));
    /* 1 本目 (リニア窓) は通るが 2 本目 (銀行窓) が他 owner と重なる → 全体不変 */
    host_span(&sp[0], 0xFE000, 0xFE400, MMIO, li);
    host_span(&sp[1], 0xF60, 0xF68, MMIO, bk);
    snap2();
    CHECK(!ledger_reserve_set(GFX, sp, 2) && same2());
    CHECK(!host_find(0xFE000, 0xFE400));
    CHECK(ledger_reserve_set(GFX, sp, 1));
    /* 他 owner の区間との完全一致も拒否 (冪等は同 owner だけ) */
    host_span(&sp[0], 0xF00, 0xF80, MMIO, pe);
    snap2();
    CHECK(!ledger_reserve_set(GFX, sp, 1) && same2());
    /* DEVICE でない owner は予約できない */
    CHECK(!ledger_reserve_set(K, sp, 1) && same2());
'''

# P3: 背景には重ねてよい、固定用途には重ねない。
BODIES['fixed_vs_background'] = r'''
    static const u32 bad[][2] = {{0x3FF, 0x401}, {0x70, 0x71}, {1105, 1106},
                                 {1129, 1131}, {1149, 1150}};
    u32 i, r, o2;
    struct ledger_span sp;
    host_pool_boot(8192);
    CHECK(host_regions());
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        r = host_res(LEDGER_BUS_FIXED, LEDGER_WB_DATASHEET, bad[i][0], bad[i][1], 0, 0);
        CHECK(r != 0xFFU);
        host_span(&sp, bad[i][0], bad[i][1], MMIO, r);
        snap2();
        CHECK(!ledger_reserve_set(GFX, &sp, 1) && same2());
    }
    /* 背景の 2 本 (15〜16MB、2GB〜4GiB 端) */
    r = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0xF00, 0x1000, 0, 0);
    host_span(&sp, 0xF10, 0xF20, MMIO, r);
    CHECK(ledger_reserve_set(GFX, &sp, 1));
    r = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, PHYSMEM_MAX_PFN - 16, PHYSMEM_MAX_PFN, 0, 0);
    host_span(&sp, PHYSMEM_MAX_PFN - 16, PHYSMEM_MAX_PFN, MMIO, r);
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 2, "rom", &o2));
    CHECK(ledger_reserve_set(o2, &sp, 1));
    CHECK(host_find(PHYSMEM_MAX_PFN - 16, PHYSMEM_MAX_PFN)->flags ==
          (LEDGER_RF_PERMANENT | LEDGER_RF_OUTSIDE));
'''

# Trident 型 (memory BAR 3 本 + ROM、1 owner) と 82557 型 (memory BAR 2 本)。
BODIES['trident_and_82557'] = r'''
    u32 r[6], tri, nic, n0;
    struct ledger_span sp[4];
    const struct ledger_region *g;
    host_pool_boot(8192);
    CHECK(host_regions());
    r[0] = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x20000, 0x20400, 0x20000, 0x20200);
    r[1] = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x20400, 0x20410, 0x20400, 0x20410);
    r[2] = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x20800, 0x20C00, 0, 0);
    r[3] = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x20C00, 0x20C10, 0, 0);
    r[4] = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x20410, 0x20411, 0, 0);
    r[5] = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x20500, 0x20600, 0, 0);
    CHECK(r[5] == 5);
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 3, "trident", &tri));
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 4, "82557", &nic));
    host_span(&sp[0], 0x20000, 0x20400, MMIO, r[0]);
    host_span(&sp[1], 0x20400, 0x20410, MMIO, r[1]);
    host_span(&sp[2], 0x20800, 0x20C00, MMIO, r[2]);
    host_span(&sp[3], 0x20C00, 0x20C10, MMIO, r[3]);
    n0 = ledger_region_count;
    CHECK(ledger_reserve_set(tri, sp, 4));
    /* 接する同種の span は併合 (根拠は和): [0x20000, 0x20410) と [0x20800, 0x20C10) */
    CHECK(ledger_region_count == n0 + 2);
    g = host_find(0x20000, 0x20410);
    CHECK(g && g->res_mask == ((1U << r[0]) | (1U << r[1])));
    g = host_find(0x20800, 0x20C10);
    CHECK(g && g->res_mask == ((1U << r[2]) | (1U << r[3])));
    host_span(&sp[0], 0x20410, 0x20411, MMIO, r[4]);
    host_span(&sp[1], 0x20500, 0x20600, MMIO, r[5]);
    CHECK(ledger_reserve_set(nic, sp, 2));
    CHECK(ledger_reserve_set(nic, sp, 2));
    CHECK(ledger_region_count == n0 + 4 && ledger_selfcheck("pci"));
'''

# RAW / PROBE_UNVERIFIED は予約権限にならない。資源の表の検査と満杯。
BODIES['resource_authority_and_full'] = r'''
    u32 raw, probe, i, rid, n;
    struct ledger_resource rec = {0};
    struct ledger_span sp;
    host_pool_boot(8192);
    CHECK(host_regions());
    raw = host_res(LEDGER_BUS_PCI, LEDGER_WB_RAW, 0x30000, 0x30100, 0, 0);
    probe = host_res(LEDGER_BUS_PCI, LEDGER_WB_PROBE_UNVERIFIED, 0x30100, 0x30200, 0, 0);
    CHECK(raw == 0 && probe == 1);
    host_span(&sp, 0x30000, 0x30100, MMIO, raw);
    snap2();
    CHECK(!ledger_reserve_set(GFX, &sp, 1) && same2());
    host_span(&sp, 0x30100, 0x30200, MMIO, probe);
    CHECK(!ledger_reserve_set(GFX, &sp, 1) && same2());
    /* 空きのレコード・範囲外の番号 */
    sp.res = LEDGER_MAX_RESOURCES - 1;
    CHECK(!ledger_reserve_set(GFX, &sp, 1) && same2());
    sp.res = LEDGER_MAX_RESOURCES;
    CHECK(!ledger_reserve_set(GFX, &sp, 1) && same2());
    /* レコードの検査: bus・根拠・decode・写像範囲 */
    rec.bus = LEDGER_BUS_PCI; rec.width_basis = LEDGER_WB_SIZING;
    rec.decode_first = 0x40000; rec.decode_end = 0x40010;
    rec.map_first = 0x40000; rec.map_end = 0x40010;
    CHECK(!ledger_resource_add(&rec, 0) && !ledger_resource_add(0, &rid));
    rec.bus = 0; CHECK(!ledger_resource_add(&rec, &rid));
    rec.bus = LEDGER_BUS_FIXED + 1; CHECK(!ledger_resource_add(&rec, &rid));
    rec.bus = LEDGER_BUS_PCI; rec.width_basis = 0; CHECK(!ledger_resource_add(&rec, &rid));
    rec.width_basis = LEDGER_WB_PROBE_UNVERIFIED + 1; CHECK(!ledger_resource_add(&rec, &rid));
    rec.width_basis = LEDGER_WB_SIZING;
    rec.decode_first = 0x40011; CHECK(!ledger_resource_add(&rec, &rid));
    rec.decode_first = 0x40000; rec.decode_end = PHYSMEM_MAX_PFN + 1;
    CHECK(!ledger_resource_add(&rec, &rid));
    rec.decode_end = 0x40010; rec.map_end = 0x40011; CHECK(!ledger_resource_add(&rec, &rid));
    rec.map_end = 0x40010; rec.map_first = 0x3FFFF; CHECK(!ledger_resource_add(&rec, &rid));
    rec.map_first = 0x40005; rec.map_end = 0x40004; CHECK(!ledger_resource_add(&rec, &rid));
    CHECK(same2() && ledger_res_overflow == 0);
    /* 満杯: 16 本まで入り、17 本目は溢れを数えて断る (表は不変) */
    rec.map_first = rec.map_end = 0;
    n = host_resources();
    for (i = n; i < LEDGER_MAX_RESOURCES; i++) CHECK(ledger_resource_add(&rec, &rid) && rid == i);
    snap2();
    CHECK(!ledger_resource_add(&rec, &rid) && ledger_res_overflow == 1 && same2());
'''

# 丸めによる衝突: バイトでは接するだけの 2 窓が PFN への切り上げで重なる。
BODIES['rounding_collision'] = r'''
    u32 a, b, o2;
    struct ledger_span sp;
    host_pool_boot(8192);
    CHECK(host_regions());
    /* A = [0x30000000, 0x30000800)、B = [0x30000800, 0x30001000) をバイトで持つ
     * glue が PFN に直すと、どちらも [0x30000, 0x30001) になる */
    a = host_res(LEDGER_BUS_CBUS, LEDGER_WB_DATASHEET, 0x30000000UL / PAGE_SIZE,
                 (0x30000800UL + PAGE_SIZE - 1) / PAGE_SIZE, 0, 0);
    b = host_res(LEDGER_BUS_CBUS, LEDGER_WB_DATASHEET, 0x30000800UL / PAGE_SIZE,
                 (0x30001000UL + PAGE_SIZE - 1) / PAGE_SIZE, 0, 0);
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 5, "b", &o2));
    host_span(&sp, 0x30000, 0x30001, MMIO, a);
    CHECK(ledger_reserve_set(GFX, &sp, 1));
    sp.res = b;
    snap2();
    CHECK(!ledger_reserve_set(o2, &sp, 1) && same2());
'''

# 区間の表の満杯: 空きが 1 本のとき 2 本の要求は全体が断られ、1 本なら通る。
BODIES['region_table_full'] = r'''
    u32 r, i;
    struct ledger_span sp[2];
    host_pool_boot(8192);
    CHECK(host_regions());
    for (i = 0; ledger_region_count < LEDGER_MAX_REGIONS - 1; i++)
        CHECK(ledger_register_region(LEDGER_R_FIXED, K, 0x50000 + 2 * i,
                                     0x50001 + 2 * i, LEDGER_CACHE_WB, 0));
    r = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x60000, 0x60100, 0, 0);
    host_span(&sp[0], 0x60000, 0x60010, MMIO, r);
    host_span(&sp[1], 0x60020, 0x60030, MMIO, r);
    snap2();
    CHECK(!ledger_reserve_set(GFX, sp, 2) && same2());
    CHECK(ledger_reserve_set(GFX, sp, 1) && ledger_region_count == LEDGER_MAX_REGIONS);
    CHECK(ledger_reserve_set(GFX, sp, 1));      /* 満杯でも完全一致は冪等 */
'''

# map / probe 失敗後の永久保持と、モジュール回収後の予約保持。
BODIES['permanent_after_failure_and_reclaim'] = r'''
    u32 r, m, pfn, got, total;
    struct ledger_span sp;
    host_pool_boot(8192);
    CHECK(host_regions());
    CHECK(ledger_owner_new(LEDGER_KIND_MODULE, 1, "mod", &m));
    CHECK(pgalloc_alloc_n_owner(m, 4, 1500, 1600, LEDGER_BOTTOM_UP, &pfn));
    r = host_res(LEDGER_BUS_FIXED, LEDGER_WB_DATASHEET, 1600, 1700, 1600, 1650);
    host_span(&sp, 1600, 1700, MMIO, r);
    total = pgalloc_total_pages();
    CHECK(ledger_reserve_set(GFX, &sp, 1));
    CHECK(pgalloc_total_pages() == total - 100);
    /* 写像 (paging_map_phys) や probe が失敗したとしても予約を戻す口は無い:
     * 区間は残り、再試行は完全一致で冪等 */
    snap2();
    CHECK(ledger_reserve_set(GFX, &sp, 1) && same2());
    CHECK(!pgalloc_alloc_n_owner(K, 1, 1600, 1700, LEDGER_BOTTOM_UP, &pfn));
    CHECK(ledger_claim_fixed(K, 1600, 1700) && ledger_owner_pages(K) == 0);
    /* モジュールの一括回収は DEVICE の区間と eligible の落としを消さない */
    CHECK(ledger_reclaim_owner(m, &got) && got == 4);
    CHECK(host_find(1600, 1700) && host_find(1600, 1700)->owner == GFX);
    CHECK(pgalloc_total_pages() == total - 100 && pgalloc_free_pages() == total - 100);
    CHECK(ledger_owner_retire(m));
    CHECK(!ledger_reclaim_owner(GFX, &got));
    CHECK(ledger_reserve_set(GFX, &sp, 1) && ledger_selfcheck("reclaim"));
'''

# RAM 種別と使用中・永久予約との衝突 (旧 broker の RAM / live 衝突の流用)。
BODIES['ram_and_live_collision'] = r'''
    u32 r, big, low, pfn, total, free, d2;
    struct ledger_span sp[2];
    host_pool_boot(8192);
    CHECK(host_regions());
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 6, "bb", &d2));
    r = host_res(LEDGER_BUS_FIXED, LEDGER_WB_DATASHEET, 1800, 1900, 0, 0);
    big = host_res(LEDGER_BUS_FIXED, LEDGER_WB_DATASHEET, 2000, 2100, 0, 0);
    host_span(&sp[0], 1800, 1810, LEDGER_SPAN_RAM, r);
    host_span(&sp[1], 1850, 1860, MMIO, r);
    CHECK(pgalloc_alloc_n_owner(K, 1, 1855, 1856, LEDGER_BOTTOM_UP, &pfn));
    total = pgalloc_total_pages(); free = pgalloc_free_pages();
    snap2();
    CHECK(!ledger_reserve_set(d2, sp, 2) && same2());   /* MMIO が使用中に重なる */
    CHECK(pgalloc_free_n_owner(K, pfn, 1));
    CHECK(ledger_reserve_set(d2, sp, 2));
    CHECK(pgalloc_total_pages() == total - 20 && pgalloc_free_pages() == free + 1 - 20);
    CHECK(host_find(1800, 1810)->cache == LEDGER_CACHE_WB);
    CHECK(host_find(1850, 1860)->cache == LEDGER_CACHE_UC);
    CHECK(!pgalloc_alloc_n_owner(K, 1, 1800, 1810, LEDGER_BOTTOM_UP, &pfn));
    CHECK(!pgalloc_free_n_owner(K, 1800, 10));
    /* 種別を変えた同じ区間は別の集合 (部分一致で拒否) */
    sp[0].kind = MMIO;
    CHECK(!ledger_reserve_set(d2, sp, 2));
    /* 管理範囲の外にかかる RAM、永久予約に重なる RAM / MMIO */
    host_span(&sp[0], 2040, 2060, LEDGER_SPAN_RAM, big);
    snap2();
    CHECK(!ledger_reserve_set(GFX, sp, 1) && same2());
    CHECK(pgalloc_reserve_pfn(K, 2010, 2011));
    host_span(&sp[0], 2005, 2015, LEDGER_SPAN_RAM, big);
    snap2();
    CHECK(!ledger_reserve_set(GFX, sp, 1) && same2());
    sp[0].kind = MMIO;
    CHECK(!ledger_reserve_set(GFX, sp, 1) && same2());
    /* RAM でないページ (L0 で RESERVED、区間の表に無い) を RAM としては取れない */
    low = host_res(LEDGER_BUS_FIXED, LEDGER_WB_DATASHEET, 0x10, 0x20, 0, 0);
    host_span(&sp[0], 0x10, 0x20, LEDGER_SPAN_RAM, low);
    snap2();
    CHECK(!ledger_reserve_set(GFX, sp, 1) && same2());
    /* 異種の重なりは正規化で拒否 */
    host_span(&sp[0], 2020, 2030, MMIO, big);
    host_span(&sp[1], 2025, 2035, LEDGER_SPAN_RAM, big);
    CHECK(!ledger_reserve_set(GFX, sp, 2) && same2());
    /* 空いた RAM への MMIO は eligible を落とすだけ (L2 は付かない) */
    total = pgalloc_total_pages();
    CHECK(ledger_reserve_set(GFX, sp, 1) && pgalloc_total_pages() == total - 10);
    CHECK(owner_map[2020] == 0 && ledger_owner_pages(GFX) == 0);
'''

# 引数と時期 (ONLINE・起動時だけ)。
BODIES['arguments_and_context'] = r'''
    u32 r;
    struct ledger_span sp[LEDGER_MAX_SPANS + 1];
    r = host_res(LEDGER_BUS_PCI, LEDGER_WB_SIZING, 0x70000, 0x70100, 0, 0);
    host_span(&sp[0], 0x70000, 0x70010, MMIO, r);
    CHECK(!ledger_reserve_set(GFX, sp, 1));          /* 初期化前 */
    host_pool_boot(8192);
    CHECK(host_regions());
    online = 0;
    CHECK(!ledger_reserve_set(GFX, sp, 1));          /* BOOTSTRAP */
    online = 1;
    host_boot_ctx = 0;
    CHECK(!ledger_reserve_set(GFX, sp, 1));          /* live AS あり */
    host_boot_ctx = 1;
    snap2();
    CHECK(!ledger_reserve_set(0, sp, 1) && !ledger_reserve_set(LEDGER_MAX_OWNERS, sp, 1));
    CHECK(!ledger_reserve_set(LEDGER_OWNER_BOOT, sp, 1) && !ledger_reserve_set(40, sp, 1));
    CHECK(!ledger_reserve_set(GFX, 0, 1) && !ledger_reserve_set(GFX, sp, 0));
    CHECK(!ledger_reserve_set(GFX, sp, LEDGER_MAX_SPANS + 1));
    sp[0].kind = 0; CHECK(!ledger_reserve_set(GFX, sp, 1));
    sp[0].kind = 3; CHECK(!ledger_reserve_set(GFX, sp, 1));
    sp[0].kind = MMIO; sp[0].end = sp[0].first;
    CHECK(!ledger_reserve_set(GFX, sp, 1));
    sp[0].end = 0x70101; CHECK(!ledger_reserve_set(GFX, sp, 1));
    sp[0].first = 0x6FFFF; sp[0].end = 0x70001; CHECK(!ledger_reserve_set(GFX, sp, 1));
    CHECK(same2());
    /* 16 本の上限いっぱい (重複と接しを含む) は通り、1 区間に併合される */
    for (r = 0; r < LEDGER_MAX_SPANS; r++)
        host_span(&sp[r], 0x70000 + (r / 2) * 4, 0x70004 + (r / 2) * 4, MMIO, 0);
    CHECK(ledger_reserve_set(GFX, sp, LEDGER_MAX_SPANS));
    CHECK(host_find(0x70000, 0x70020) != 0 && ledger_region_count == snap_regions + 1);
'''

# B4: 合成 g_pci からの取り込み (⑥-0)。
BODIES['import_pci'] = r'''
    u32 i, rid;
    const struct ledger_resource *q;
    struct ledger_span sp;
    struct ledger_resource rec = {0};
    host_pool_boot(8192);
    CHECK(host_regions());
    /* 0:8.0 (Type 0): I/O・32 ビット・0・64 ビット (上位 0)・最後の 64 ビット (上位無し) */
    g_pci[0].dev = 8; g_pci[0].vendor = 0x1023; g_pci[0].device = 0x9660;
    g_pci[0].bar[0] = 0xE001; g_pci[0].bar[1] = 0x20000000UL; g_pci[0].bar[2] = 0;
    g_pci[0].bar[3] = 0x20400004UL; g_pci[0].bar[4] = 0; g_pci[0].bar[5] = 0x20800004UL;
    /* 0:2.0 (ブリッヂ、マルチファンクション): BAR0 だけ。18h 以降は BAR ではない */
    g_pci[1].dev = 2; g_pci[1].header = 0x81; g_pci[1].vendor = 0x1033; g_pci[1].device = 0x0001;
    g_pci[1].bar[0] = 0x20900000UL; g_pci[1].bar[2] = 0x00010100UL; g_pci[1].bar[4] = 0xFE00FC00UL;
    /* 0:11.1: 上位が 0 でない 64 ビット・番地 0 のメモリ・予約種別・32 ビット */
    g_pci[2].dev = 11; g_pci[2].fn = 1; g_pci[2].vendor = 0x8086; g_pci[2].device = 0x1229;
    g_pci[2].bar[0] = 0x30000004UL; g_pci[2].bar[1] = 1; g_pci[2].bar[2] = 0x8;
    g_pci[2].bar[3] = 0x20A00006UL; g_pci[2].bar[4] = 0x20410000UL;
    g_pci_count = 3;
    CHECK(ledger_resource_import_pci() == 4 && host_resources() == 4 && ledger_res_overflow == 0);
    q = &ledger_resources[0];
    CHECK(q->bus == LEDGER_BUS_PCI && q->width_basis == LEDGER_WB_RAW && q->bar == 1);
    CHECK(q->raw_bar == 0x20000000UL && q->decode_first == 0x20000 && q->decode_end == 0x20000);
    CHECK(q->map_first == 0 && q->map_end == 0);
    CHECK(q->bdf == (8 << 3) && q->vendor == 0x1023 && q->device == 0x9660);
    q = &ledger_resources[1];
    CHECK(q->bar == 3 && q->raw_bar == 0x20400004UL && q->decode_first == 0x20400);
    q = &ledger_resources[2];
    CHECK(q->bar == 0 && q->bdf == (2 << 3) && q->decode_first == 0x20900);
    q = &ledger_resources[3];
    CHECK(q->bar == 4 && q->bdf == ((11 << 3) | 1) && q->vendor == 0x8086 && q->decode_first == 0x20410);
    /* RAW は予約に使えない (幅が未確定) */
    host_span(&sp, 0x20000, 0x20001, MMIO, 0);
    snap2();
    CHECK(!ledger_reserve_set(GFX, &sp, 1) && same2());
    /* 溢れ: 空きが 1 本なら 1 本だけ載り、残り 3 本を数える */
    rec.bus = LEDGER_BUS_FIXED; rec.width_basis = LEDGER_WB_DATASHEET;
    for (i = host_resources(); i < LEDGER_MAX_RESOURCES - 1; i++)
        CHECK(ledger_resource_add(&rec, &rid));
    CHECK(ledger_resource_import_pci() == 1 && ledger_res_overflow == 3);
    CHECK(host_resources() == LEDGER_MAX_RESOURCES);
    /* PCI が無い (NP21/W) なら 0 本 */
    g_pci_count = 0;
    CHECK(ledger_resource_import_pci() == 0 && ledger_res_overflow == 3);
'''


def run_c(texts, body):
    """本体を組んで実行し、(成功, 出力) を返す。"""
    extra = (EXTRA.replace('@PCI_READER@', pci_reader(texts['drivers/pci.c']))
             .replace('@LEDGER_PCI@', texts['kernel/ledger_pci.c']))
    post = L.POST.replace('static int test(void) {', extra + 'static int test(void) {', 1)
    with tempfile.TemporaryDirectory(prefix='os32-devres-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'pgalloc_host_fixture.h').write_text(texts['tools/tests/pgalloc_host_fixture.h'])
        src = texts['kernel/pgalloc.c'].replace('#include "io.h"', '')
        (tmp / 'test.c').write_text(L.PRE + src + post + body + L.END)
        cmd = ['gcc'] + L.FLAGS + ['-I' + str(tmp)]
        cmd += ['-I' + str(ROOT / p) for p in ('include', 'kernel', 'lib', 'drivers', 'sdk/include/os32')]
        build = subprocess.run(cmd + [str(tmp / 'test.c'), str(ROOT / 'kernel/physmem.c'),
                                      str(ROOT / 'drivers/pci_decode.c'),
                                      '-o', str(tmp / 'test')], capture_output=True, text=True)
        if build.returncode:
            return False, 'compile\n' + build.stderr
        out = subprocess.run([str(tmp / 'test')], capture_output=True, text=True, timeout=120)
        return out.returncode == 0, 'rc=%d %s%s' % (out.returncode, out.stdout, out.stderr)


def stage_run(texts):
    """実 paging + pgalloc + sys の段つき起動 (device_reservation_stage_host.c)。"""
    with tempfile.TemporaryDirectory(prefix='os32-device-stage-') as tmp:
        tmp = pathlib.Path(tmp)
        for unit in ('paging', 'pgalloc', 'sys'):
            source = texts['kernel/%s.c' % unit]
            source = source.replace('irq_save()', 'host_irq_save()').replace('irq_restore(flags)', 'host_irq_restore(flags)')
            (tmp / f'{unit}_host_source.c').write_text(source)
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', '-DPHYSMEM_HOST_TEST=1']
        # arch/x86 + platform/pc98: include/io.h / include/cpu.h は契約
        # だけで、実装は固定名 arch_io.h / arch_cpu.h / platform_io.h を
        # 引く (順序 3・5)。CR0 / CR3 を触る arch_cpu.h だけは、ホストでは
        # tools/tests/host_arch/ の実装が先に見つかるようにする。
        cmd += ['-I' + str(ROOT / 'tools/tests/host_arch')]
        cmd += ['-I' + str(ROOT / p) for p in ('include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'drivers', 'sdk/include/os32')] + ['-I' + str(tmp)]
        build = subprocess.run(cmd + [str(ROOT / 'tools/tests/device_reservation_stage_host.c'),
                                      str(ROOT / 'kernel/physmem.c'), '-o', str(tmp / 'test')],
                               capture_output=True, text=True)
        if build.returncode:
            return False, 'compile\n' + build.stderr
        out = subprocess.run([str(tmp / 'test')], capture_output=True, text=True, timeout=60)
        return out.returncode == 0, 'rc=%d %s%s' % (out.returncode, out.stdout, out.stderr)


def all_checks(texts):
    out = []
    for name, body in BODIES.items():
        ok, log = run_c(texts, body)
        out.append((name, ok, log))
    ok, log = stage_run(texts)
    out.append(('stage', ok, log))
    return out


class DeviceReservation(unittest.TestCase):
    texts = None

    @classmethod
    def setUpClass(cls):
        cls.texts = load()

    def run_body(self, name):
        ok, log = run_c(self.texts, BODIES[name])
        self.assertTrue(ok, log)

    def test_gfx_candidates(self):
        self.run_body('gfx_candidates')

    def test_xe10_single_record(self):
        self.run_body('xe10_single_record')

    def test_other_owner_and_second_span(self):
        self.run_body('other_owner_and_second_span')

    def test_fixed_vs_background(self):
        self.run_body('fixed_vs_background')

    def test_trident_and_82557(self):
        self.run_body('trident_and_82557')

    def test_resource_authority_and_full(self):
        self.run_body('resource_authority_and_full')

    def test_rounding_collision(self):
        self.run_body('rounding_collision')

    def test_region_table_full(self):
        self.run_body('region_table_full')

    def test_permanent_after_failure_and_reclaim(self):
        self.run_body('permanent_after_failure_and_reclaim')

    def test_ram_and_live_collision(self):
        self.run_body('ram_and_live_collision')

    def test_arguments_and_context(self):
        self.run_body('arguments_and_context')

    def test_import_pci(self):
        self.run_body('import_pci')

    def test_real_online_stage(self):
        ok, log = stage_run(self.texts)
        self.assertTrue(ok, log)

    def test_old_broker_is_gone(self):
        # 旧 broker (pgalloc_device_reserve / sys_device_reserve_core) は T1d で
        # ledger_reserve_set に載せ直した。生の {first, end} を予約権限にする口を残さない。
        for rel in ('kernel/pgalloc.h', 'include/sys.h', 'kernel/pgalloc.c', 'kernel/sys.c'):
            text = (ROOT / rel).read_text()
            for name in ('pgalloc_device_reserve', 'sys_device_reserve_core',
                         'sys_device_span', 'device_claim'):
                self.assertFalse(name in text, '%s に %s が残っている' % (rel, name))

    def test_import_is_before_bind(self):
        # ⑥-0 は pci_bind_all の直前 (B4)、台帳 (memory_boot_init) より後。
        k = (ROOT / 'kernel/kernel.c').read_text()
        imp = k.index('ledger_resource_import_pci()')
        self.assertLess(k.index('memory_boot_init(mem_kb)'), imp)
        self.assertLess(imp, k.index('pci_bind_all(pci_drivers'))


# 変異 (§4-4)。当て先は load() の写し (実物は書き換えない)。
MUTATIONS = [
    ('raw-grants-authority', 'kernel/pgalloc.c',
     'rr->width_basis > LEDGER_WB_GLUE_CONST', 'rr->width_basis > LEDGER_WB_PROBE_UNVERIFIED'),
    ('decode-end-unchecked', 'kernel/pgalloc.c',
     '            t.end > rr->decode_end ||\n', ''),
    ('decode-first-unchecked', 'kernel/pgalloc.c',
     't.first >= t.end || t.first < rr->decode_first ||', 't.first >= t.end ||'),
    ('res-mask-not-merged', 'kernel/pgalloc.c',
     '            s[k - 1].res |= s[i].res;\n', ''),
    ('mixed-kind-overlap-merged', 'kernel/pgalloc.c',
     '            if (k && s[i].first < s[k - 1].end) goto done;\n', ''),
    ('background-refused', 'kernel/pgalloc.c',
     '        if (g->type == LEDGER_R_BACKGROUND) continue;\n', ''),
    ('fixed-allowed', 'kernel/pgalloc.c',
     '        if (g->type == LEDGER_R_BACKGROUND) continue;',
     '        if (g->type == LEDGER_R_BACKGROUND || g->type == LEDGER_R_FIXED) continue;'),
    ('other-owner-device-allowed', 'kernel/pgalloc.c',
     '        if (g->type == LEDGER_R_BACKGROUND) continue;',
     '        if (g->type == LEDGER_R_BACKGROUND || g->type == LEDGER_R_DEVICE) continue;'),
    ('partial-match-accepted', 'kernel/pgalloc.c',
     '        ok = hit == n && mine == n;', '        ok = hit == n;'),
    ('res-mask-not-compared', 'kernel/pgalloc.c',
     '                    g->res_mask == s[i].res && g->cache == s[i].kind) hit++;',
     '                    g->cache == s[i].kind) hit++;'),
    ('live-or-permanent-unchecked', 'kernel/pgalloc.c',
     '            if (owner_map[p] ||\n', '            if (0 ||\n'),
    ('ram-eligibility-unchecked', 'kernel/pgalloc.c',
     '(s[i].kind == LEDGER_CACHE_WB && !bit(eligible, p))) goto done;', '0) goto done;'),
    ('eligibility-not-dropped', 'kernel/pgalloc.c',
     '                eligible[p / 32] &= ~(1UL << (p % 32));\n                total_pages--;\n',
     ''),
    ('non-device-owner', 'kernel/pgalloc.c',
     '        ledger_owners[owner].kind != LEDGER_KIND_DEVICE || !spans || !n ||',
     '        !spans || !n ||'),
    ('selfcheck-rejects-device-on-background', 'kernel/pgalloc.c',
     '                !(r->type == LEDGER_R_DEVICE && g->type == LEDGER_R_BACKGROUND))\n',
     '                1)\n'),
    ('overflow-not-counted', 'kernel/pgalloc.c',
     '        ledger_res_overflow++;\n', ''),
    ('record-map-unchecked', 'kernel/pgalloc.c',
     '        rec->map_first > rec->map_end ||\n', ''),
    ('import-width-verified', 'kernel/ledger_pci.c',
     '    r.width_basis = LEDGER_WB_RAW;', '    r.width_basis = LEDGER_WB_SIZING;'),
    ('import-io-bars', 'kernel/ledger_pci.c',
     '            if (k < PCI_BAR_MEM32 || k > PCI_BAR_MEM64 ||',
     '            if (k < PCI_BAR_IO || k > PCI_BAR_MEM64 ||'),
    ('import-mem64-high-kept', 'kernel/ledger_pci.c',
     '            if (k == PCI_BAR_MEM64 && (b + 1 >= nb || d.bar[++b])) continue;',
     '            if (k == PCI_BAR_MEM64 && (b + 1 >= nb || !d.bar[++b])) continue;'),
    ('import-bridge-all-six', 'kernel/ledger_pci.c',
     '             layout == PCI_HDR_LAYOUT_BRIDGE ? 2 : 1;',
     '             layout == PCI_HDR_LAYOUT_BRIDGE ? PCI_CFG_BAR_COUNT : 1;'),
    ('import-overflow-counted-as-added', 'kernel/ledger_pci.c',
     '            if (ledger_resource_add(&r, &rid)) n++;',
     '            ledger_resource_add(&r, &rid); n++;'),
]


def _mutate_one(m):
    results = all_checks(load(m))
    reds = [name for name, ok, log in results
            if not ok and not log.startswith('compile')]
    builds = [name for name, ok, log in results
              if not ok and log.startswith('compile')]
    return m[0], reds, builds


def mutate():
    import mutpar
    red = 0
    for name, reds, builds in mutpar.run_ordered(_mutate_one, MUTATIONS, processes=True):
        ok = bool(reds) and not builds
        print('  変異 %-38s %s' % (name, ('RED (%s)' % ', '.join(reds)) if ok
                                   else ('**組み立てで落ちた (%s) — 実行時の検出の証拠にならない**'
                                         % ', '.join(builds)) if builds
                                   else '**GREEN — 試験が穴を見逃した**'))
        red += ok
    print('%d/%d の変異が RED' % (red, len(MUTATIONS)))
    return 0 if red == len(MUTATIONS) else 1


if __name__ == '__main__':
    if '--mutate' in sys.argv:
        sys.argv.remove('--mutate')
        rc = mutate()
        if rc:
            sys.exit(rc)
    unittest.main()
