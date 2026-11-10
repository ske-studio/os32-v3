/* PC-98 display provider: boot-time SURFACE reservation (⑥) and its tables.
 * (第 5 段 B5: gfx/gfx_core_surface.inc から移した。表の値・予約の順・BB の確保・
 * 識別と probe の区別は不変。世代・lease・公開の判定は gfx/ に残る。) */
#include "display_provider.h"
#include "gfx_internal.h"
#include "memmap.h"
#include "pgalloc.h"      /* 台帳: 予約・BB・SURFACE (TASK_T1_LEDGER §3-8) */
#include "paging.h"       /* paging_map_phys: ⑥ の窓の写像 */
#include "v86_bios.h"
#include "pegc.h"         /* PEGC の窓と BIOS ワークの番地 (⑥ の資源レコード) */
#include "wab_xe10.h"     /* Xe10 の窓の定数 (同上) */
#include "kstring.h"
#include "kprintf.h"
#include "geforce_module.h"   /* geforce_boot_reserve (weak) */

enum { GFX_RES_PEGC, GFX_RES_XE10_BANK, GFX_RES_XE10_LINEAR,
       GFX_RES_PEGC_ALT, GFX_RES_COUNT };
enum { GFX_SF_PC98, GFX_SF_PEGC, GFX_SF_CIRRUS_CLIENT,
       GFX_SF_CIRRUS_DISPLAY, GFX_SF_COUNT };
#define GFX_CAND_PEGC   1u
#define GFX_CAND_CIRRUS 2u
#define GFX_PFN(a)      ((u32)(a) / PAGE_SIZE)

/* 候補の資源レコード (1 レコード = decode 区間 1 組、B10)。並びは
 * raw_bar, decode_first/end, map_first/end, bdf, vendor, device, bus,
 * revision, bar, width_basis, boot_gen。Xe10 の 2 本は glue の定数 =
 * **NP21/W の窓の値で、実機 Xe10 の実測ではない** (GLUE_CONST)。 */
static const struct ledger_resource gfx_res[GFX_RES_COUNT] = {
    /* PEGC: 拡張グラフィックスのリニア窓 512KB (資料の値) */
    [GFX_RES_PEGC] = { 0, GFX_PFN(PEGC_LINEAR_BASE), GFX_PFN(PEGC_LINEAR_BASE + PEGC_LINEAR_SIZE),
      GFX_PFN(PEGC_LINEAR_BASE), GFX_PFN(PEGC_LINEAR_BASE + PEGC_LINEAR_SIZE),
      0, 0, 0, LEDGER_BUS_FIXED, 0, 0, LEDGER_WB_DATASHEET, 0, 0 },
    /* Xe10-bank: 銀行窓 32KB (写像しない) */
    [GFX_RES_XE10_BANK] = { 0, GFX_PFN(WAB_XE10_WIN_BASE), GFX_PFN(WAB_XE10_WIN_BASE + WAB_XE10_WIN_SIZE),
      0, 0, 0, 0, 0, LEDGER_BUS_CBUS, 0, 0, LEDGER_WB_GLUE_CONST, 0, 0 },
    /* Xe10-linear: decode 4MB、写像は描画に使う先頭 2MB */
    [GFX_RES_XE10_LINEAR] = { 0, GFX_PFN(WAB_XE10_LINEARWIN_BASE),
      GFX_PFN(WAB_XE10_LINEARWIN_BASE + WAB_XE10_LINEARWIN_DECODE),
      GFX_PFN(WAB_XE10_LINEARWIN_BASE),
      GFX_PFN(WAB_XE10_LINEARWIN_BASE + WAB_XE10_LINEARWIN_SIZE),
      0, 0, 0, LEDGER_BUS_CBUS, 0, 0, LEDGER_WB_GLUE_CONST, 0, 0 },
    /* PEGC-alt: 16MB システム空間を RAM にした機械の別名 (512KB)。 */
    [GFX_RES_PEGC_ALT] = { 0, GFX_PFN(PEGC_LINEAR_ALT_BASE), GFX_PFN(PEGC_LINEAR_ALT_BASE + PEGC_LINEAR_SIZE),
      GFX_PFN(PEGC_LINEAR_ALT_BASE), GFX_PFN(PEGC_LINEAR_ALT_BASE + PEGC_LINEAR_SIZE),
      0, 0, 0, LEDGER_BUS_FIXED, 0, 0, LEDGER_WB_DATASHEET, 0, 0 }
};

/* SURFACE の型板。plane offset は pitch × height (ページ丸めなし)。
 * RAM の first は確保した PFN で埋める。Cirrus の面の割り付け (表示面 = 窓の
 * 先頭、クライアント面 = その直後、同じ 300KB) は backend_cirrus.c の
 * STATIC_ASSERT が突き合わせる。 */
static const struct ledger_surface gfx_sf[GFX_SF_COUNT] = {
    [GFX_SF_PC98] = { .npages = GFX_PFN(MEM_GFX_BB_SIZE),
      .width = GFX_WIDTH, .height = GFX_HEIGHT, .pitch = GFX_BPL,
      .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_RAM,
      .backend = LEDGER_SF_PC98, .role = LEDGER_ROLE_CLIENT,
      .format = GFX_BB_PLANAR4, .planes = GVRAM_PLANES, .cache = LEDGER_CACHE_WB,
      .perm_max = LEDGER_PERM_RW,
      .plane_offset = {0, GFX_BPL * GFX_HEIGHT, 2 * GFX_BPL * GFX_HEIGHT,
                       3 * GFX_BPL * GFX_HEIGHT} },
    [GFX_SF_PEGC] = { .npages = GFX_PFN(MEM_GFX_BB8_SIZE), .width = MEM_GFX_BB8_WIDTH,
      .height = MEM_GFX_BB8_HEIGHT, .pitch = MEM_GFX_BB8_PITCH,
      .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_RAM,
      .backend = LEDGER_SF_PEGC, .role = LEDGER_ROLE_CLIENT,
      .format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_WB,
      .perm_max = LEDGER_PERM_RW },
    [GFX_SF_CIRRUS_CLIENT] = { .first = GFX_PFN(WAB_XE10_LINEARWIN_BASE + MEM_GFX_BB8_SIZE),
      .npages = GFX_PFN(MEM_GFX_BB8_SIZE), .width = MEM_GFX_BB8_WIDTH,
      .height = MEM_GFX_BB8_HEIGHT, .pitch = MEM_GFX_BB8_PITCH,
      .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_MMIO,
      .backend = LEDGER_SF_CIRRUS, .role = LEDGER_ROLE_CLIENT,
      .format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_UC,
      .perm_max = LEDGER_PERM_RW },
    [GFX_SF_CIRRUS_DISPLAY] = { .first = GFX_PFN(WAB_XE10_LINEARWIN_BASE), .npages = GFX_PFN(MEM_GFX_BB8_SIZE),
      .width = MEM_GFX_BB8_WIDTH, .height = MEM_GFX_BB8_HEIGHT,
      .pitch = MEM_GFX_BB8_PITCH, .owner = LEDGER_OWNER_KERNEL,
      .backing = LEDGER_SB_MMIO, .backend = LEDGER_SF_CIRRUS,
      .role = LEDGER_ROLE_DISPLAY, .format = GFX_BB_PACKED8,
      .planes = 1, .cache = LEDGER_CACHE_UC, .perm_max = LEDGER_PERM_RW }
};

/* PC98 is always a candidate: even explicit PEGC/Cirrus falls back to it.
 * One record per discontiguous plane. PLANAR4 describes the pixel layout;
 * planes=1 describes this record. Do not clear device padding (32000..32767).
 * DISPLAY permissions are granted only by the authorized lease entry. */
static const u32 gfx_display_planes[GVRAM_PLANES] = {
    VRAM_PLANE_B, VRAM_PLANE_R, VRAM_PLANE_G, VRAM_PLANE_I
};
static const struct ledger_surface gfx_planar_display = {
    .npages = GFX_PFN(GVRAM_PLANE_SIZE),
    .width = GFX_WIDTH, .height = GFX_HEIGHT, .pitch = GFX_BPL,
    .owner = LEDGER_OWNER_KERNEL, .backing = LEDGER_SB_VRAM,
    .backend = LEDGER_SF_PC98, .role = LEDGER_ROLE_DISPLAY,
    .format = GFX_BB_PLANAR4, .planes = 1, .cache = LEDGER_CACHE_UC,
    .perm_max = LEDGER_PERM_RW
};

static const struct ledger_surface gfx_pegc_display = {
    .first = GFX_PFN(PEGC_LINEAR_BASE), .npages = GFX_PFN(PEGC_FB_SIZE_480),
    .width = MEM_GFX_BB8_WIDTH, .height = MEM_GFX_BB8_HEIGHT, .pitch = MEM_GFX_BB8_PITCH,
    .owner = LEDGER_OWNER_KERNEL, .backing = LEDGER_SB_VRAM,
    .backend = LEDGER_SF_PEGC, .role = LEDGER_ROLE_DISPLAY,
    .format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_UC,
    .perm_max = LEDGER_PERM_RW
};

/* RO until e9/e11: the last attr page also contains memory switches. */
static const struct ledger_surface system_tvram = {
    .first = GFX_PFN(TVRAM_CHAR_BASE),
    .npages = GFX_PFN(TVRAM_CG_WINDOW - TVRAM_CHAR_BASE),
    .width = TVRAM_COLS, .height = TVRAM_ROWS, .pitch = TVRAM_BPR,
    .owner = LEDGER_OWNER_KERNEL, .backing = LEDGER_SB_VRAM,
    .role = LEDGER_ROLE_TVRAM, .format = LEDGER_FMT_TEXT,
    .planes = 2, .cache = LEDGER_CACHE_UC, .perm_max = LEDGER_PERM_RO,
    .plane_offset = {0, TVRAM_ATTR_BASE - TVRAM_CHAR_BASE}
};

/* 資源レコード i がどの候補のものか。先頭3本は SURFACE 型板 i + 1 に対応。 */
static u32 gfx_cand_of(u32 i)
{
    return i == GFX_RES_XE10_BANK || i == GFX_RES_XE10_LINEAR ? GFX_CAND_CIRRUS : GFX_CAND_PEGC;
}

/* 副作用のない識別。GFX= と機種 (9821 系) と各 backend の identify だけ。 */
static u32 gfx_identify_candidates(int pref)
{
    u32 m = 0;
    if (pref == GFX_PREF_PC98 || pref == GFX_PREF_GEFORCE ||
        pref == GFX_PREF_TRIDENT ||
        !(v86_bios_real_lowmem()[PEGC_BIOS_ARCH_FLAG] & PEGC_BIOS_ARCH_EXTGFX)) return 0;
    if (pref != GFX_PREF_CIRRUS && pegc_identify && pegc_identify())
        m |= GFX_CAND_PEGC;
    if (pref != GFX_PREF_PEGC && cirrus_identify && cirrus_identify())
        m |= GFX_CAND_CIRRUS;
    return m;
}

/* cold: 起動時に 1 回だけ走るので速さより大きさで組ませる (カーネルの予算、
 * TASK_T1_LEDGER §4-5-R)。判定は変わらない。 */
void __attribute__((cold)) display_provider_boot_reserve(int pref, void (*bind_client)(void))
{
    struct ledger_span sp[GFX_RES_COUNT];
    struct ledger_surface sf;
    const struct ledger_resource *r;
    u32 i, n, m, rid, pfn, cand, display_fail = 0;
    u32 resource_ids[GFX_RES_COUNT] = {LEDGER_MAX_RESOURCES, LEDGER_MAX_RESOURCES,
                           LEDGER_MAX_RESOURCES, LEDGER_MAX_RESOURCES};
    rid = 0;

    cand = m = pref == GFX_PREF_GEFORCE ? 0 : gfx_identify_candidates(pref);
    pfn = 0;
    /* 2. 登録できた窓をまとめて予約。登録に失敗した候補から先は登録しない
     *    (先に登録した窓は保持する。PEGC は F00 だけ登録済みでも alt が無ければ落ちる)。 */
    for (i = n = 0; m && i < GFX_RES_COUNT; i++) {
        if (!(m & gfx_cand_of(i))) continue;
        if (!ledger_resource_add(&gfx_res[i], &rid)) break;
        resource_ids[i] = rid;
        (void)ledger_resource_set_map(rid, 0, 0);
        sp[n].first = gfx_res[i].decode_first;
        sp[n].end = gfx_res[i].decode_end;
        sp[n].kind = LEDGER_SPAN_MMIO;
        sp[n++].res = rid;
    }
    if (m && !ledger_reserve_set(LEDGER_OWNER_GFX, sp, n)) m = 0;
    /* 3. 写像範囲だけを supervisor + PCD。落ちた候補は外す (予約は残る)。 */
    for (i = 0; i < GFX_RES_COUNT; i++) {
        r = &gfx_res[i];
        if (resource_ids[i] == LEDGER_MAX_RESOURCES)
            m &= ~gfx_cand_of(i);
        else if ((m & gfx_cand_of(i)) && r->map_first < r->map_end &&
            paging_map_phys(r->map_first * PAGE_SIZE, r->map_first * PAGE_SIZE,
                            r->map_end - r->map_first, PAGE_RW | PTE_PCD) != 0)
            m &= ~gfx_cand_of(i);
        else if ((m & gfx_cand_of(i)) && resource_ids[i] < LEDGER_MAX_RESOURCES) {
            if (!ledger_resource_set_map(resource_ids[i], r->map_first, r->map_end))
                m &= ~gfx_cand_of(i);
        }
    }
    /* DISPLAY depends on reservation + mapping, not CLIENT allocation. */
    if ((m & GFX_CAND_PEGC) && !ledger_surface_create(&gfx_pegc_display, 0))
        display_fail++;
    /* 4. BB は候補の最大 (PEGC が残れば 300KB)。アリーナ内の上端から。 */
    if ((m & GFX_CAND_PEGC) &&
        pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, (int)GFX_PFN(MEM_GFX_BB8_SIZE),
                              GFX_PFN(MEM_PHYS_EXEC_FLOOR), pgalloc_arena_end(),
                              LEDGER_TOP_DOWN, &pfn))
        kmemset(P2V(pfn * PAGE_SIZE), 0, (u32)MEM_GFX_BB8_SIZE);
    else
        m &= ~GFX_CAND_PEGC;
    /* 5. planar は fallback 用にも連続 pool を保持する。登録失敗は返却。 */
    for (i = 0; i < GFX_SF_COUNT; i++) {
        if (i && !(m & gfx_cand_of(i - 1))) continue;
        sf = gfx_sf[i];
        if (!i) {
            if (!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, (int)sf.npages,
                    GFX_PFN(MEM_POOL_BASE), pgalloc_arena_end(),
                    LEDGER_TOP_DOWN, &sf.first)) continue;
            kmemset(P2V(sf.first * PAGE_SIZE), 0, MEM_GFX_BB_SIZE);
        }
        if (i == GFX_SF_PEGC) sf.first = pfn;
        if (!ledger_surface_create(&sf, 0)) {
            if (sf.backing == LEDGER_SB_RAM)
                (void)pgalloc_free_n_owner(sf.owner, sf.first, (int)sf.npages);
            if (i) m &= ~gfx_cand_of(i - 1);
        }
    }
    bind_client();
    for (i = 0; i < GVRAM_PLANES; i++) {
        sf = gfx_planar_display;
        sf.first = GFX_PFN(gfx_display_planes[i]);
        if (!ledger_surface_create(&sf, 0)) display_fail++;
    }
    (void)ledger_surface_create(&system_tvram, 0);
    /* GF and PEGC are mutually exclusive boot candidates. */
    if (pref == GFX_PREF_GEFORCE && geforce_boot_reserve) {
        if (!geforce_boot_reserve())
            kprintf(KLOG_ATTR, "[gfx] geforce boot_reserve failed\n");
    }
    if (pref == GFX_PREF_TRIDENT && trident_boot_reserve) {
        int rc = trident_boot_reserve(); /* Trident uses 0-success, unlike GF. */
        if (rc) kprintf(KLOG_ATTR, "[gfx] trident stage=boot_reserve rc=%d\n", rc);
    }
    /* 6. */
    ledger_arena_freeze();
    kprintf(KLOG_ATTR, "[gfx] ledger cand=%u ok=%u display_fail=%u bb=%x top=%x\n", cand, m, display_fail,
            pfn * PAGE_SIZE, ledger_arena_top() * PAGE_SIZE);
    (void)ledger_selfcheck("gfx");
}

/* DISPLAY の SURFACE の記録の数: 標準 planar は不連続な 4 プレーンを 1 枚ずつ。 */
u32 display_provider_display_records(u32 ledger_backend)
{
    return ledger_backend == LEDGER_SF_PC98 ? GVRAM_PLANES : 1;
}

u32 display_provider_display_first(u32 plane)
{
    return GFX_PFN(gfx_display_planes[plane]);
}
