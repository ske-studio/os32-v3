#include "memmap.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "os32_kapi_shared.h"
#include "kstring.h"
#include "con_sink.h"     /* con_sink_is_enabled — GUI 中かどうか (票 T8) */
#include "console.h"      /* shell_print — 拒否を端末へ (票 T8-2) */
#include "pgalloc.h"      /* 台帳: 予約・BB・SURFACE (TASK_T1_LEDGER §3-8) */
#include "paging.h"       /* paging_map_phys: ⑥ の窓の写像 */
#include "pegc.h"         /* PEGC の窓と BIOS ワークの番地 (⑥ の資源レコード) */
#include "wab_xe10.h"     /* Xe10 の窓の定数 (同上) */
#include "kprintf.h"
#include "../exec/surface_query.h"

/* 画面の所有者の表は exec/appslot.c にある (判定材料が AppSlot にあり、
 * ホストで試験できるため)。gfx/ は -Iexec を持たないので、kernel/con_sink.c
 * が res_owner_get を引くのと同じ流儀で extern 宣言する。 */
extern int appslot_gfx_claim(int gui_mode);
extern int appslot_gfx_owner(void);

/* ======================================================================== */
/*  バックバッファ (拡張メモリ固定アドレス, 128KB)                          */
/* ======================================================================== */
u8 *bb_b, *bb_r, *bb_g, *bb_i;

int gfx_current_height = GFX_HEIGHT;  /* 200 or 400 */

u8 *bb[4];

DirtyRectQueue dirty_queue = {0};
DirtyRectQueue prev_dirty = {0};   /* 前フレームdirty (ステイルページ対策) */

int gfx_flip_enabled = 0;
int gfx_display_page = 0;

/* ======================================================================== */
/*  HAL バックエンド選択とカウンタ (GUI v1.1, レーン H1)                     */
/*  g_backend は probe 順で選ばれる。静的初期化子で 9801 を指すので、        */
/*  gfx_init 前でも NULL にならない (boot_splash 等が present を呼べる)。     */
/* ======================================================================== */
GfxCounters gfx_counters = { 0, 0, 0, 0 };

const GfxBackend *g_backend = &gfx_backend_pc98;

/* probe 順のバックエンド一覧。速い/高機能な順に前から並べ、最初に probe() が
 * 1 を返したものを使う (Cirrus → PEGC → 9801)。
 * 9801 の probe は常に 1 なので、末尾が必ず受け皿になる。 */
static const GfxBackend *const g_backend_list[] = {
    &gfx_backend_cirrus, /* Cirrus GD54xx アクセラレータ (H3)。ID 5Bh のみ */
    &gfx_backend_pegc,   /* 9821 PEGC 256 色 (H2)。9801 では probe が 0 */
    &gfx_backend_pc98    /* 9801 標準グラフィック (H1)。最後の受け皿 */
};

/* 起動設定 (/etc/system.cfg GFX=) から渡される希望バックエンド (票 H2b)。
 * kernel.c が gfx_init より前に gfx_set_backend_pref() で設定する。 */
static int g_backend_pref = GFX_PREF_AUTO;
static int gfx_started;
static void gfx_bind_client(void);

void gfx_set_backend_pref(int pref)
{
    if (pref != GFX_PREF_PC98 && pref != GFX_PREF_PEGC &&
        pref != GFX_PREF_CIRRUS)
        pref = GFX_PREF_AUTO;
    g_backend_pref = pref;
}

int gfx_get_backend_pref(void)
{
    return g_backend_pref;
}

static void gfx_select_backend(void)
{
    int i;
    int n = (int)(sizeof(g_backend_list) / sizeof(g_backend_list[0]));

    /* 強制指定 (GFX=) は probe より先に効く。
     * pc98 は probe をまったく行わない (9801 は最後の受け皿なので常に成功)。
     * pegc は probe だけは通す — 実機に PEGC が無ければ描けないので、
     * 失敗したら従来どおり 9801 へ落とす。 */
    if (g_backend_pref == GFX_PREF_PC98) {
        g_backend = &gfx_backend_pc98;
        return;
    }
    if (g_backend_pref == GFX_PREF_PEGC ||
        g_backend_pref == GFX_PREF_CIRRUS) {
        const GfxBackend *want = (g_backend_pref == GFX_PREF_CIRRUS)
                                     ? &gfx_backend_cirrus
                                     : &gfx_backend_pegc;
        for (i = 0; i < n; i++) {
            /* weak 宣言なので未リンクなら要素が 0 (→ 9801 へ落ちる) */
            if (!g_backend_list[i]) continue;
            if (g_backend_list[i] != want) continue;
            if (g_backend_list[i]->probe && g_backend_list[i]->probe()) {
                g_backend = g_backend_list[i];
                return;
            }
            break;
        }
        g_backend = &gfx_backend_pc98;
        return;
    }

    for (i = 0; i < n; i++) {
        /* weak 宣言のバックエンド (未リンクなら 0) を飛ばす。
         * → include/gfx_hal.h の gfx_backend_pegc の注記 */
        if (!g_backend_list[i]) continue;
        if (g_backend_list[i]->probe && g_backend_list[i]->probe()) {
            g_backend = g_backend_list[i];
            return;
        }
    }
    g_backend = &gfx_backend_pc98;   /* フォールバック */
}

/* ======================================================================== */
/*  KAPI: フレームバッファ取得                                              */
/* ======================================================================== */
/* Internal backing is separate from the future public value/lease ABI.
 * Before init (including prepare), the framebuffer is the boot PC98 CLIENT. */
STATIC_ASSERT(GFX_FMT_PLANAR4 == GFX_BB_PLANAR4, gfx_planar_format_matches);
STATIC_ASSERT(GFX_FMT_PACKED8 == GFX_BB_PACKED8, gfx_packed_format_matches);

static int gfx_client_framebuffer(struct gfx_kernel_fb *out, int selected)
{
    struct gfx_kernel_fb fb = {0};
    const struct ledger_surface *sf;
    GFX_ScreenInfo si;
    u8 *base;
    u32 i;
    if (!out) return OS32_ERR_INVAL;
    sf = ledger_surface_find(selected ? gfx_sf_backend() : LEDGER_SF_PC98,
                             LEDGER_ROLE_CLIENT);
    fb.width = GFX_WIDTH;
    fb.height = GFX_HEIGHT;
    fb.pitch = GFX_BPL;
    fb.format = GFX_BB_PLANAR4;
    if (sf && !sf->closing) {
        fb.width = sf->width;
        fb.height = sf->height;
        fb.pitch = sf->pitch;
        fb.format = sf->format;
        if (selected && g_backend->query && !g_backend->query(&si)) {
            fb.width = si.width;
            fb.height = si.height;
            fb.pitch = g_backend->bb_pitch; /* query has no pitch field */
            fb.format = si.format;
        }
        base = (sf->backing == LEDGER_SB_MMIO || sf->backing == LEDGER_SB_VRAM)
             ? (u8 *)P2V_IO(sf->first * PAGE_SIZE) : (u8 *)P2V(sf->first * PAGE_SIZE);
        for (i = 0; i < sf->planes; i++) fb.planes[i] = base + sf->plane_offset[i];
    }
    *out = fb;
    return fb.planes[0] ? 0 : OS32_ERR_INVAL;
}

int gfx_kernel_framebuffer(struct gfx_kernel_fb *out)
{
    return gfx_client_framebuffer(out, gfx_started);
}

/* e11 installs the USER lease bridge. Keep both current callers on aliases. */
void __cdecl gfx_get_framebuffer(GFX_Framebuffer *fb)
{
    struct gfx_kernel_fb kernel;
    int i;
    if (!fb) return;
    (void)gfx_client_framebuffer(&kernel, 1);
    fb->width = kernel.width;
    fb->height = kernel.height;
    fb->pitch = kernel.pitch;
    for (i = 0; i < 4; i++) fb->planes[i] = kernel.planes[i];
}

/* All legacy planar pointers and the selected backend descriptor share one
 * binding point. Missing CLIENT must erase pointers from the previous mode. */
static void gfx_bind_client(void)
{
    const struct ledger_surface *sf = ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);
    GfxBackend *backend = gfx_sf_backend() == LEDGER_SF_PEGC ? &gfx_backend_pegc :
                          gfx_sf_backend() == LEDGER_SF_CIRRUS ? &gfx_backend_cirrus : &gfx_backend_pc98;
    u8 *base = 0;
    u32 i;
    if (sf && !sf->closing)
        base = (sf->backing == LEDGER_SB_MMIO || sf->backing == LEDGER_SB_VRAM)
             ? (u8 *)P2V_IO(sf->first * PAGE_SIZE) : (u8 *)P2V(sf->first * PAGE_SIZE);
    for (i = 0; i < 4; i++) bb[i] = base && i < sf->planes ? base + sf->plane_offset[i] : 0;
    bb_b = bb[0]; bb_r = bb[1]; bb_g = bb[2]; bb_i = bb[3];
    backend->bb_base = bb[0];
    backend->bb_size = base ? sf->npages * PAGE_SIZE : 0;
    if (base) {
        backend->bb_pitch = sf->pitch;
        backend->bb_format = sf->format;
    }
}

/* ======================================================================== */
/*  ⑥ gfx の識別 → 予約 → 写像 → BB (TASK_T1_LEDGER §3-3 ⑥・§3-8、T1e)     */
/*                                                                          */
/*  起動時 (exec_init より前、live AS 0) に 1 回だけ。**probe を呼ばない** — */
/*  予約・写像・BB は副作用のない識別だけで決め (D33)、probe / enable は     */
/*  今の位置 (gfx_prepare_backend) のまま、ここで用意したものを使う。        */
/*    1. 識別: GFX= と、9821 系 (BIOS ワーク 045Ch bit6) かつ各 backend の    */
/*       identify (BIOS ワークと物理地図を読むだけ)。                        */
/*    2. 予約: 候補の窓を owner = DEVICE gfx の **1 回の** ledger_reserve_set */
/*       に入れる (PEGC と Xe10 の銀行窓は重なるので候補ごとの owner に      */
/*       しない、B3)。資源レコードは PEGC / Xe10-bank / Xe10-linear の 3 本で */
/*       span も 3 本、各 span が自分のレコードを指す (B10)。                */
/*    3. 写像: レコードの写像範囲だけを supervisor + PCD (PEGC は窓全体     */
/*       512KB、Xe10 のリニア窓は decode 4MB のうち 2MB)。戻り値を全部見て、 */
/*       落ちた候補を外す。予約と写像は probe が落ちても永久に保持する。     */
/*    4. BB: PEGC が残れば 300KB を池の CPL=0 子のアリーナ内の上端から連続    */
/*       (owner = boot、X14 — 旧 sys_reserve_top と同じ区間)、直後に 0 で    */
/*       埋める (CPL=3 に USER で写るので前の中身を見せない)。               */
/*    5. SURFACE: planar の固定面 (常に)、PEGC の CLIENT (RAM)、Cirrus の     */
/*       CLIENT + DISPLAY (MMIO、B9)。位置は probe を待たずに定数から決まる。 */
/*    6. アリーナの上端を凍結 (sys_usable_mem_end が BB の下になる、§3-6)、  */
/*       ledger_selfcheck("gfx")。                                          */
/*  **予約は装置の存在の証明ではない** (DEVICE_RESERVATION §6)。             */
/* ======================================================================== */
#define GFX_CAND_PEGC   1u
#define GFX_CAND_CIRRUS 2u
#define GFX_PFN(a)      ((u32)(a) / PAGE_SIZE)

/* 候補の資源レコード (1 レコード = decode 区間 1 組、B10)。並びは
 * raw_bar, decode_first/end, map_first/end, bdf, vendor, device, bus,
 * revision, bar, width_basis, boot_gen。Xe10 の 2 本は glue の定数 =
 * **NP21/W の窓の値で、実機 Xe10 の実測ではない** (GLUE_CONST)。 */
static const struct ledger_resource gfx_res[3] = {
    /* PEGC: 拡張グラフィックスのリニア窓 512KB (資料の値) */
    { 0, GFX_PFN(PEGC_LINEAR_BASE), GFX_PFN(PEGC_LINEAR_BASE + PEGC_LINEAR_SIZE),
      GFX_PFN(PEGC_LINEAR_BASE), GFX_PFN(PEGC_LINEAR_BASE + PEGC_LINEAR_SIZE),
      0, 0, 0, LEDGER_BUS_FIXED, 0, 0, LEDGER_WB_DATASHEET, 0, 0 },
    /* Xe10-bank: 銀行窓 32KB (写像しない) */
    { 0, GFX_PFN(WAB_XE10_WIN_BASE), GFX_PFN(WAB_XE10_WIN_BASE + WAB_XE10_WIN_SIZE),
      0, 0, 0, 0, 0, LEDGER_BUS_CBUS, 0, 0, LEDGER_WB_GLUE_CONST, 0, 0 },
    /* Xe10-linear: decode 4MB、写像は描画に使う先頭 2MB */
    { 0, GFX_PFN(WAB_XE10_LINEARWIN_BASE),
      GFX_PFN(WAB_XE10_LINEARWIN_BASE + WAB_XE10_LINEARWIN_DECODE),
      GFX_PFN(WAB_XE10_LINEARWIN_BASE),
      GFX_PFN(WAB_XE10_LINEARWIN_BASE + WAB_XE10_LINEARWIN_SIZE),
      0, 0, 0, LEDGER_BUS_CBUS, 0, 0, LEDGER_WB_GLUE_CONST, 0, 0 }
};

/* SURFACE の型板。plane offset は pitch × height (ページ丸めなし)。
 * PEGC の first は確保した PFN で埋める。Cirrus の面の割り付け (表示面 = 窓の
 * 先頭、クライアント面 = その直後、同じ 300KB) は backend_cirrus.c の
 * STATIC_ASSERT が突き合わせる。 */
static const struct ledger_surface gfx_sf[4] = {
    { .first = GFX_PFN(MEM_GFX_BB_BASE), .npages = GFX_PFN(MEM_GFX_BB_SIZE),
      .width = GFX_WIDTH, .height = GFX_HEIGHT, .pitch = GFX_BPL,
      .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_FIXED_RAM,
      .backend = LEDGER_SF_PC98, .role = LEDGER_ROLE_CLIENT,
      .format = GFX_BB_PLANAR4, .planes = 4, .cache = LEDGER_CACHE_WB,
      .perm_max = LEDGER_PERM_RW,
      .plane_offset = {0, GFX_BPL * GFX_HEIGHT, 2 * GFX_BPL * GFX_HEIGHT,
                       3 * GFX_BPL * GFX_HEIGHT} },
    { .npages = GFX_PFN(MEM_GFX_BB8_SIZE), .width = MEM_GFX_BB8_WIDTH,
      .height = MEM_GFX_BB8_HEIGHT, .pitch = MEM_GFX_BB8_PITCH,
      .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_RAM,
      .backend = LEDGER_SF_PEGC, .role = LEDGER_ROLE_CLIENT,
      .format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_WB,
      .perm_max = LEDGER_PERM_RW },
    { .first = GFX_PFN(WAB_XE10_LINEARWIN_BASE + MEM_GFX_BB8_SIZE),
      .npages = GFX_PFN(MEM_GFX_BB8_SIZE), .width = MEM_GFX_BB8_WIDTH,
      .height = MEM_GFX_BB8_HEIGHT, .pitch = MEM_GFX_BB8_PITCH,
      .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_MMIO,
      .backend = LEDGER_SF_CIRRUS, .role = LEDGER_ROLE_CLIENT,
      .format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_UC,
      .perm_max = LEDGER_PERM_RW },
    { .first = GFX_PFN(WAB_XE10_LINEARWIN_BASE), .npages = GFX_PFN(MEM_GFX_BB8_SIZE),
      .width = MEM_GFX_BB8_WIDTH, .height = MEM_GFX_BB8_HEIGHT,
      .pitch = MEM_GFX_BB8_PITCH, .owner = LEDGER_OWNER_KERNEL,
      .backing = LEDGER_SB_MMIO, .backend = LEDGER_SF_CIRRUS,
      .role = LEDGER_ROLE_DISPLAY, .format = GFX_BB_PACKED8,
      .planes = 1, .cache = LEDGER_CACHE_UC, .perm_max = LEDGER_PERM_NONE }
};

/* PC98 is always a candidate: even explicit PEGC/Cirrus falls back to it.
 * One record per discontiguous plane. PLANAR4 describes the pixel layout;
 * planes=1 describes this record. Do not clear device padding (32000..32767).
 * Cirrus DISPLAY remains NONE until its fullscreen publisher is connected. */
static const u32 gfx_display_planes[4] = {
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

/* 資源レコード i (と SURFACE の型板 i + 1) がどの候補のものか。 */
static u32 gfx_cand_of(u32 i)
{
    return i ? GFX_CAND_CIRRUS : GFX_CAND_PEGC;
}

/* 副作用のない識別。GFX= と機種 (9821 系) と各 backend の identify だけ。 */
static u32 gfx_identify_candidates(void)
{
    volatile u32 arch = PEGC_BIOS_ARCH_FLAG;
    u32 m = 0;
    if (g_backend_pref == GFX_PREF_PC98 ||
        !(*(volatile u8 *)P2V_IO(arch) & PEGC_BIOS_ARCH_EXTGFX)) return 0;
    if (g_backend_pref != GFX_PREF_CIRRUS && pegc_identify && pegc_identify())
        m |= GFX_CAND_PEGC;
    if (g_backend_pref != GFX_PREF_PEGC && cirrus_identify && cirrus_identify())
        m |= GFX_CAND_CIRRUS;
    return m;
}

/* cold: 起動時に 1 回だけ走るので速さより大きさで組ませる (カーネルの予算、
 * TASK_T1_LEDGER §4-5-R)。判定は変わらない。 */
void __attribute__((cold)) gfx_boot_reserve(void)
{
    struct ledger_span sp[3];
    struct ledger_surface sf;
    const struct ledger_resource *r;
    u32 i, n, m, rid, pfn, cand, display_fail = 0;
    rid = 0;

    cand = m = gfx_identify_candidates();
    pfn = 0;
    /* 2. 候補の窓をまとめて 1 回で予約 (失敗なら候補なし = PC98)。 */
    for (i = n = 0; m && i < 3; i++) {
        if (!(m & gfx_cand_of(i))) continue;
        if (!ledger_resource_add(&gfx_res[i], &rid)) m = 0;
        sp[n].first = gfx_res[i].decode_first;
        sp[n].end = gfx_res[i].decode_end;
        sp[n].kind = LEDGER_SPAN_MMIO;
        sp[n++].res = rid;
    }
    if (m && !ledger_reserve_set(LEDGER_OWNER_GFX, sp, n)) m = 0;
    /* 3. 写像範囲だけを supervisor + PCD。落ちた候補は外す (予約は残る)。 */
    for (i = 0; i < 3; i++) {
        r = &gfx_res[i];
        if ((m & gfx_cand_of(i)) && r->map_first < r->map_end &&
            paging_map_phys(r->map_first * PAGE_SIZE, r->map_first * PAGE_SIZE,
                            r->map_end - r->map_first, PAGE_RW | PTE_PCD) != 0)
            m &= ~gfx_cand_of(i);
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
    /* 5. SURFACE。planar は常に、他は残った候補だけ。 */
    for (i = 0; i < 4; i++) {
        if (i && !(m & gfx_cand_of(i - 1))) continue;
        sf = gfx_sf[i];
        if (i == 1) sf.first = pfn;
        if (!ledger_surface_create(&sf, 0) && i) m &= ~gfx_cand_of(i - 1);
        if (!i) gfx_bind_client();
    }
    for (i = 0; i < 4; i++) {
        sf = gfx_planar_display;
        sf.first = GFX_PFN(gfx_display_planes[i]);
        if (!ledger_surface_create(&sf, 0)) display_fail++;
    }
    /* 6. */
    ledger_arena_freeze();
    kprintf(0x07, "[gfx] ledger cand=%u ok=%u display_fail=%u bb=%x top=%x\n", cand, m, display_fail,
            pfn * PAGE_SIZE, ledger_arena_top() * PAGE_SIZE);
    (void)ledger_selfcheck("gfx");
}

/* 選択中の backend の SURFACE 上の名前 (LEDGER_SF_*)。 */
u32 gfx_sf_backend(void)
{
    if (g_backend == &gfx_backend_pegc) return LEDGER_SF_PEGC;
    if (g_backend == &gfx_backend_cirrus) return LEDGER_SF_CIRRUS;
    return LEDGER_SF_PC98;
}

/* Normal context: source construction through snapshot/acquire completion
 * must not cross a callback or scheduling point (T2e e1/e3 contract).
 * Dormant until e5/e11; TVRAM/Unicode are supplied in e8. */
__attribute__((section(".text.gfx_surface_source")))
int gfx_surface_source(u32 role, struct surface_query_source *out)
{
    struct surface_query_source source = {0};
    u32 i, plane, count;
    if (!out) return OS32_ERR_INVAL;
    source.role = role;
    source.backend = gfx_sf_backend();
    *out = source;
    if (!gfx_started || (role != LEDGER_ROLE_CLIENT && role != LEDGER_ROLE_DISPLAY))
        return OS32_ERR_INVAL;
    count = role == LEDGER_ROLE_DISPLAY && source.backend == LEDGER_SF_PC98 ? 4 : 1;
    for (plane = 0; plane < count; plane++) {
        for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
            const struct ledger_surface *sf = &ledger_surfaces[i];
            if (!sf->npages || sf->closing || sf->backend != source.backend ||
                sf->role != role || sf->perm_max == LEDGER_PERM_NONE) continue;
            if (count == 4 && sf->first != GFX_PFN(gfx_display_planes[plane])) continue;
            source.refs[plane] = (struct surface_ref){i, sf->gen};
            break;
        }
        if (i == LEDGER_MAX_SURFACES) return OS32_ERR_INVAL;
    }
    source.count = count;
    source.ready = 1;
    *out = source;
    return 0;
}

/* ======================================================================== */
/*  バックバッファの物理範囲 (exec が CPL=3 へ USER マップする範囲)。        */
/*  = **選択中 backend の CLIENT の SURFACE** (TASK_T1_LEDGER §3-8、B9)。    */
/*  9801 は 0x6A000 の 128KB (固定 RAM)、PEGC は ⑥ が池から確保した 300KB、  */
/*  Cirrus はリニア窓 FE000000h の中の非表示面 300KB (MMIO)。backend の      */
/*  bb_base (CPU 描画の記述子) も同じ SURFACE から引くので情報源は 1 つ。    */
/*  **表示面は決してここに含めない** — CPL=3 に USER で見せてよいのは         */
/*  クライアント面だけ (契約 G4、レビュー #5 ②)。Cirrus の probe に落ちて    */
/*  PC98 になった起動では Cirrus の SURFACE は表に残るが選択中でないので返らない。 */
/* ======================================================================== */
void gfx_bb_phys_range(u32 *base, u32 *size)
{
    const struct ledger_surface *sf =
        ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);
    if (base) *base = sf ? sf->first * PAGE_SIZE : 0;
    if (size) *size = sf ? sf->npages * PAGE_SIZE : 0;
}

/* ⑨ GUI の開始 (exec_run(gshell) の直前): 選択中 backend の CLIENT を
 * boot → gshell へ移す (RAM の面は L2 のページごと、MMIO の面は SURFACE の
 * owner だけ)。2 回目以降 (GUI → CUI → GUI) は同じ owner なので何もしない —
 * BB は返さず保持する (R5 (b))。 */
void __attribute__((cold)) gfx_client_to_gshell(void)
{
    struct ledger_surface *sf =
        ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);
    if (sf) (void)ledger_surface_transfer((u32)(sf - ledger_surfaces),
                                          LEDGER_OWNER_GSHELL);
    (void)ledger_selfcheck("gui");
}

/* ======================================================================== */
/*  KAPI: 画面能力の問い合わせ (GUI HAL, docs/tasks/gui/API_CONTRACTS.md G5)  */
/*  現在の唯一のバックエンドは 9801 プレーン: CPU 直書き、HW 塗り/転送なし。   */
/* ======================================================================== */
void __cdecl gfx_screen_info(void *out)
{
    if (!out) return;
    /* 決め打ちせずバックエンドに問い合わせる (契約 G5)。9801 だけの現状でも
     * 400/200 ラインやフリップ有無はバックエンドが正直に申告する。 */
    if (g_backend && g_backend->query)
        g_backend->query((GFX_ScreenInfo *)out);
}

/* ======================================================================== */
/*  KAPI: gfx_set_palette — 1 色差し替え。HAL 経由 (レビュー ④)。            */
/*  従来は palette_set を直接叩いており、PEGC/Cirrus でも 9801 の 16 色       */
/*  palette_set へ行ってしまう問題があった。バックエンドの set_palette へ    */
/*  通し、機種ごとの実装に届くようにする。                                   */
/* ======================================================================== */
void __cdecl gfx_set_palette_hal(int idx, u8 r, u8 g, u8 b)
{
    u8 rgb[3];
    rgb[0] = r; rgb[1] = g; rgb[2] = b;
    if (g_backend && g_backend->set_palette) {
        g_backend->set_palette(idx, 1, rgb);
    } else {
        palette_set(idx, r, g, b);   /* 保険 */
    }
}

/* ======================================================================== */
/*  KAPI: ハードウェア塗り / 転送 (アクセラレータ系バックエンド用の枠)        */
/*  バックエンドが fill_rect / blit を持てばそれへ、無ければ OS32_ERR_NOSYS。 */
/*  呼ぶ側は GFX_CAP_HW_* を見て CPU 実装へフォールバックする。               */
/* ======================================================================== */
int __cdecl gfx_hw_fill_rect(int x, int y, int w, int h, u8 color)
{
    if (g_backend && g_backend->fill_rect)
        return g_backend->fill_rect(x, y, w, h, color);
    return OS32_ERR_NOSYS;
}

int __cdecl gfx_hw_blit(int dx, int dy, int sx, int sy, int w, int h)
{
    if (g_backend && g_backend->blit)
        return g_backend->blit(dx, dy, sx, sy, w, h);
    return OS32_ERR_NOSYS;
}

/* ======================================================================== */
/*  KAPI (v41): 描画カウンタの取得 (契約 G7 / DESIGN §8)。                   */
/*  GFX_Stats を埋める。K1 が kapi.json に載せる弱既定 (NOSYS 返し) を、この   */
/*  強シンボルがリンク時に上書きする。                                       */
/* ======================================================================== */
int __cdecl gfx_stats(void *out)
{
    GFX_Stats *s = (GFX_Stats *)out;
    if (!s) return OS32_ERR_INVAL;
    s->present_bytes = gfx_counters.present_bytes;
    s->hw_ops        = gfx_counters.hw_ops;
    s->io_accesses   = gfx_counters.io_accesses;
    s->commits       = gfx_counters.commits;
    return 0;
}

/* ======================================================================== */
/*  KAPI (v41): パレットのリース (契約 G8)。                                 */
/*  フォーカスのあるアプリ / WM が、バックエンドが許す範囲だけパレットを      */
/*  差し替える。範囲は gfx_screen_info() の lease_mask (16 色) /              */
/*  lease_first, lease_count (256 色) が正典。範囲外は OS32_ERR_INVAL で弾く。 */
/*  H1 は状態を持たない — システム色へ戻すのは WM が同じ関数で行う。          */
/* ======================================================================== */
int __cdecl gfx_lease_palette(int first, int count, const u8 *rgb)
{
    GFX_ScreenInfo si;
    int i;

    if (count <= 0 || first < 0 || !rgb) return OS32_ERR_INVAL;
    if (!g_backend || !g_backend->query || !g_backend->set_palette)
        return OS32_ERR_NOSYS;

    g_backend->query(&si);

    if (si.lease_count > 0) {
        /* 256 色機: 連続範囲 [lease_first, lease_first + lease_count) */
        if (first < (int)si.lease_first ||
            first + count > (int)si.lease_first + (int)si.lease_count)
            return OS32_ERR_INVAL;
    } else {
        /* 16 色機: lease_mask のビットが立つ index だけ貸せる */
        if (first + count > 16) return OS32_ERR_INVAL;
        for (i = first; i < first + count; i++) {
            if (!(si.lease_mask & (u16)(1u << i))) return OS32_ERR_INVAL;
        }
    }

    g_backend->set_palette(first, count, rgb);
    return 0;
}

int gfx_get_height(void)
{
    return gfx_current_height;
}

/* ======================================================================== */
/*  初期化・終了                                                            */
/* ======================================================================== */
/* 共通初期化処理 (テキストVRAMクリア + バックバッファゼロクリア) */
static void _gfx_common_init(int plane_sz)
{
    int i;
    volatile u16 *tvram_char = (volatile u16 *)P2V_IO(TVRAM_CHAR_BASE);
    volatile u8  *tvram_attr = (volatile u8  *)P2V_IO(TVRAM_ATTR_BASE);

    if (!bb_b || !bb_r || !bb_g || !bb_i) return;
    dirty_queue.count = 0;

    /* テキストVRAMクリア */
    for (i = 0; i < TVRAM_COLS * TVRAM_ROWS; i++) {
        tvram_char[i] = 0x0000;
        tvram_attr[i * 2] = 0x00;
    }

    /* ゼロクリア (バックバッファ) — kmemset (rep stosd) で高速化 */
    kmemset(bb_b, 0, plane_sz);
    kmemset(bb_r, 0, plane_sz);
    kmemset(bb_g, 0, plane_sz);
    kmemset(bb_i, 0, plane_sz);

    _out(MODE_FF2_PORT, MFF2_16COLOR);
}

/* ======================================================================== */
/*  バックエンド選択 → 選ばれたバックエンドの hw-init (H1 レビュー ⑤)       */
/*                                                                          */
/*  かつては gfx_init の **末尾** で probe していたため、9801 の GDC/プレーン */
/*  初期化を丸ごと済ませてから機種を判定していた。PEGC (H2) はモード F/F2 と  */
/*  MMIO で表示のしくみ自体を差し替えるので、その順序では 9801 用の設定を     */
/*  上書きするだけの無駄が出るうえ、E0000h の意味が途中で変わる (プレーン 3   */
/*  → 制御レジスタ) 危険な窓ができる。probe を先に回し、選ばれた 1 枚に       */
/*  初期化させる。                                                          */
/*                                                                          */
/*  戻り値: 1 = バックエンドが自前で init() を持っていた (9821 等)。         */
/*          0 = init が NULL = 9801。呼び出し側が従来の GDC 初期化を行う。   */
/*  9801 では従来とまったく同じ経路を通る (回帰ゼロ)。                       */
/* ======================================================================== */
static int gfx_select_and_init_backend(void)
{
    gfx_started = 0;
    gfx_select_backend();
    if (g_backend && g_backend->init) {
        g_backend->init();
        /* init が失敗して probe が取り下げられた場合は 9801 へ落とす
         * (バックバッファが取れない等)。 */
        if (g_backend->probe && !g_backend->probe()) {
            g_backend = &gfx_backend_pc98;
            gfx_bind_client(); /* bind init failure fallback */
            return 0;
        }
        return 1;
    }
    return 0;
}

/* ======================================================================== */
/*  gfx_prepare_backend — 実バックエンドを「起動時に 1 回だけ」用意する      */
/*                                                                          */
/*  boot_splash は PC98 を強制するので、実バックエンドの probe も init も    */
/*  走らない。それをアプリ (CPL=3) の初回 gfx_init まで繰り延べると 2 つ壊れる: */
/*                                                                          */
/*   1. PEGC の probe は BIOS ワークエリア (0x045C / 0x0597) を読むが、そこは */
/*      master PD にしか写像が無く、アプリ PD では #PF でアプリが死ぬ。       */
/*   2. (T1e まで) PEGC の BB の予約もここで行っていた。今は起動時の ⑥     */
/*      (gfx_boot_reserve) が窓の予約・写像と BB の確保を probe より前に      */
/*      済ませ、probe / init はそれを使うだけ (TASK_T1_LEDGER §3-8)。         */
/*                                                                          */
/*  どちらも「アプリが 1 つも走っていないカーネル文脈」でなければ成立しない。  */
/*  バックエンドが prepare() を持てば (PEGC) それだけを呼ぶ — probe と        */
/*  起動時の同期の記録だけで、表示のモードも同期も変えない。実機 Ra266 +      */
/*  液晶の桁ずれは、CUI 起動でも 480 ライン化 → 24kHz 戻しを通していたこと  */
/*  が原因だという **仮説** (未確認。候補は backend_pegc.c の pegc_prepare   */
/*  の注記、TASK_FDC_REALHW §9-1)。持たなければ (Cirrus) 従来どおり           */
/*  init まで済ませて shutdown で表示をテキストへ戻す。予約とリニア窓は        */
/*  shutdown をまたいで保持されるので、以後のアプリの gfx_init は再利用する。  */
/*  9801 (init == NULL) は予約も窓も要らないので何もしない。                 */
/* ======================================================================== */
void gfx_prepare_backend(void)
{
    gfx_started = 0;
    gfx_select_backend();
    gfx_bind_client();
    if (!g_backend) return;
    /* 下ごしらえを別に持つバックエンド (PEGC) は表示に触らずに済ませる。
     * init → shutdown で済ませると CUI しか使わない起動でも同期を送り直す
     * (実機 Ra266 + 液晶の桁ズレの原因という仮説、TASK_FDC_REALHW §9-1)。 */
    if (g_backend->prepare) {
        g_backend->prepare();
        if (g_backend->probe && !g_backend->probe()) {
            g_backend = &gfx_backend_pc98;
            gfx_bind_client();
        }
        return;
    }
    if (!g_backend->init) return;
    g_backend->init();
    /* init が失敗して probe が取り下げられたら 9801 へ落とす (gfx_init と同じ)。 */
    if (g_backend->probe && !g_backend->probe()) {
        g_backend = &gfx_backend_pc98;
        gfx_bind_client();
        return;
    }
    if (g_backend->shutdown) g_backend->shutdown();
}

/* 標準 planar モード専用。CPU 直書きで両ページの表示領域を消す [HW1]。
 * UNDOCUMENTED io_disp.md「VRAMプレーン切り換え」: A6h が CPU の書込先。
 * 戻りは描画ページ1。呼び手は初期化を続けるか直ちに shutdown する。 */
void gfx_clear_planar_pages(u32 plane_size)
{
    _out(GDC_ACCESS_PAGE, GDC_PAGE_0);
    kmemset((u8 *)P2V(VRAM_PLANE_B), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_R), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_G), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_I), 0, plane_size);

    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);
    kmemset((u8 *)P2V(VRAM_PLANE_B), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_R), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_G), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_I), 0, plane_size);
}

void gfx_init(void)
{
    /* ⑤: GDC 初期化より前にバックエンドを決める */
    if (gfx_select_and_init_backend()) {
        gfx_bind_client(); /* bind packed init */
        gfx_started = bb[0] != 0;
        if (g_backend->enter) g_backend->enter();
        return;   /* 9821 等: モード設定もバックバッファも init() が済ませた */
    }

    gfx_current_height = GFX_HEIGHT;  /* 400ラインモード */
    gfx_display_page = 0;
    prev_dirty.count = 0;

    gfx_bind_client();
    if (!bb[0]) return;
    _gfx_common_init(GFX_PLANE_SZ);

    /* GDC CSRFORM: L/R=0 (400ライン) */
    _out(GDC_GFX_CMD, GDC_GFX_400LINE);
    _out(GDC_GFX_PARAM, 0x00);
    _out(MODE_FF1_PORT, MFF1_HIRES);

    _out(GDC_GFX_CMD, GDC_CMD_START);

    gfx_clear_planar_pages(GFX_PLANE_SZ);

    /* ページ0を表示、ページ1に描画 */
    _out(GDC_DISP_PAGE, GDC_PAGE_0);
    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);
    gfx_flip_enabled = 1;

    palette_init();
    gfx_scroll_init();
    gfx_started = 1;

    /* 表示出力を有効化 (9801 の enter は空)。バックエンドの選択は先頭で済み。 */
    if (g_backend && g_backend->enter) g_backend->enter();
}

void gfx_init_200(void)
{
    /* ⑤: GDC 初期化より前にバックエンドを決める。
     * 200 ラインは 9801 プレーン専用のモードなので、パックド系が選ばれた
     * ときはそのバックエンドのネイティブ解像度で立ち上げる (200 ラインの
     * 縦 2 倍表示は 16 色プレーンの機能で、PEGC には対応物が無い)。 */
    if (gfx_select_and_init_backend()) {
        gfx_bind_client();
        gfx_started = bb[0] != 0;
        if (g_backend->enter) g_backend->enter();
        return;
    }

    gfx_current_height = GFX_HEIGHT_200;  /* 200ラインモード */

    gfx_bind_client(); /* rebind for init_200, keeping registered plane offsets */
    if (!bb[0]) return;
    _gfx_common_init(GFX_PLANE_SZ_200);

    /* GDC CSRFORM: L/R=1 (各ライン2倍表示 → 200ライン) */
    _out(GDC_GFX_CMD, GDC_GFX_400LINE);
    _out(GDC_GFX_PARAM, 0x01);
    _out(MODE_FF1_PORT, MFF1_200LINE);

    _out(GDC_GFX_CMD, GDC_CMD_START);

    gfx_clear_planar_pages(GFX_PLANE_SZ_200);

    /* ページ0を表示、ページ1に描画 */
    _out(GDC_DISP_PAGE, GDC_PAGE_0);
    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);
    gfx_flip_enabled = 1;
    gfx_display_page = 0;
    prev_dirty.count = 0;

    palette_init();
    gfx_scroll_init();
    gfx_started = 1;

    /* 表示出力を有効化 (9801 の enter は空)。バックエンドの選択は先頭で済み。 */
    if (g_backend && g_backend->enter) g_backend->enter();
}

/* ======================================================================== */
/*  KAPI の門 — 画面の所有者 (票 T8 D1 / D1a)                                */
/*                                                                          */
/*  sdk/kapi.json のスロット gfx_init / gfx_init_200 はここを指す。外部       */
/*  プログラムが画面を丸ごと取る唯一の入口なので、所有権の移動もここ 1 か所。 */
/*  カーネル内部 (ブート、WM の復帰) は gfx_init() を直接呼ぶので門を通らない。*/
/*                                                                          */
/*  GUI 中 (con_sink 有効) に宣言 (mkos32x --gfx = OS32X_FLAG_GFX) の無い     */
/*  CPL=3 が呼んだら **本体を呼ばず、そのアプリを畳む** (票 T8-2)。gfx_init は */
/*  void なので戻り値では知らせられず、断っただけでは受入 F6 の実測どおり     */
/*  描画 KAPI と VRAM 直書きで描き続けて GUI を壊す。数は                     */
/*  gfx_init_reject_count (カーネルシンボル) で見る。CUI 中は従来どおり素通し。*/
/* ======================================================================== */
static int gfx_kapi_claim(void)
{
    if (appslot_gfx_claim(con_sink_is_enabled()) >= 0) return 0;
    /* 票 T8-2 (受入 F6 の実測): 断って **続行させる**と、プログラムは失敗を
     * 知らないまま描画 KAPI と VRAM 直書きで描き続け GUI を壊す。断ったら
     * そのアプリを畳む — appslot_gfx_claim が abort_req を立てているので、
     * この syscall の出口 (ring3_abort_check) で畳まれる。ここでは con_sink
     * 経由で端末に理由を出すだけ。 */
    shell_print("Error: gfx_init without GFX declaration -> kill app\n",
                ATTR_RED);
    return -1;
}

void gfx_kapi_init(void)
{
    if (gfx_kapi_claim() < 0) return;
    gfx_init();
}

void gfx_kapi_init_200(void)
{
    if (gfx_kapi_claim() < 0) return;
    gfx_init_200();
}

i32 gfx_screen_owner(void)
{
    return (i32)appslot_gfx_owner();
}

void gfx_shutdown(void)
{
    gfx_started = 0;
    /* 表示出力を戻し (leave)、バックエンドのハードウェア終了処理へ。
     * 9801 では leave は空、shutdown がフリップ解除 + GDC 表示停止を行う
     * (旧 gfx_shutdown の本体は backend_pc98.c の pc98_shutdown に移設)。 */
    if (g_backend && g_backend->leave) g_backend->leave();
    if (g_backend && g_backend->shutdown) g_backend->shutdown();
}
