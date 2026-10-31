/* Boot SURFACE reservation, publication, regeneration and ownership transfer. */
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
/*       しない、B3)。PEGC / Xe10-bank / Xe10-linear / PEGC-alt の 4 本で、   */
/*       各 span が自分の資源レコードを指す (B10)。                         */
/*    3. 写像: レコードの写像範囲だけを supervisor + PCD (PEGC は窓全体     */
/*       512KB、Xe10 のリニア窓は decode 4MB のうち 2MB)。戻り値を全部見て、 */
/*       落ちた候補を外す。予約と写像は probe が落ちても永久に保持する。     */
/*    4. BB: PEGC が残れば 300KB を池の CPL=0 子のアリーナ内の上端から連続    */
/*       (owner = boot、X14 — 旧 sys_reserve_top と同じ区間)、直後に 0 で    */
/*       埋める (CPL=3 に USER で写るので前の中身を見せない)。               */
/*    5. SURFACE: planar の pool 面 (常に)、PEGC の CLIENT (RAM)、Cirrus の     */
/*       CLIENT + DISPLAY (MMIO、B9)。位置は probe を待たずに定数から決まる。 */
/*    6. アリーナの上端を凍結 (sys_usable_mem_end が BB の下になる、§3-6)、  */
/*       ledger_selfcheck("gfx")。                                          */
/*  **予約は装置の存在の証明ではない** (DEVICE_RESERVATION §6)。             */
/* ======================================================================== */
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
static u32 gfx_identify_candidates(void)
{
    u32 m = 0;
    if (g_backend_pref == GFX_PREF_PC98 || g_backend_pref == GFX_PREF_GEFORCE ||
        !(v86_bios_real_lowmem()[PEGC_BIOS_ARCH_FLAG] & PEGC_BIOS_ARCH_EXTGFX)) return 0;
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
    struct ledger_span sp[GFX_RES_COUNT];
    struct ledger_surface sf;
    const struct ledger_resource *r;
    u32 i, n, m, rid, pfn, cand, display_fail = 0;
    u32 resource_ids[GFX_RES_COUNT] = {LEDGER_MAX_RESOURCES, LEDGER_MAX_RESOURCES,
                           LEDGER_MAX_RESOURCES, LEDGER_MAX_RESOURCES};
    rid = 0;

    cand = m = g_backend_pref == GFX_PREF_GEFORCE ? 0 : gfx_identify_candidates();
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
    gfx_bind_client();
    for (i = 0; i < GVRAM_PLANES; i++) {
        sf = gfx_planar_display;
        sf.first = GFX_PFN(gfx_display_planes[i]);
        if (!ledger_surface_create(&sf, 0)) display_fail++;
    }
    (void)ledger_surface_create(&system_tvram, 0);
    /* GF and PEGC are mutually exclusive boot candidates. */
    if (g_backend_pref == GFX_PREF_GEFORCE && geforce_boot_reserve)
        (void)geforce_boot_reserve();
    /* 6. */
    ledger_arena_freeze();
    kprintf(KLOG_ATTR, "[gfx] ledger cand=%u ok=%u display_fail=%u bb=%x top=%x\n", cand, m, display_fail,
            pfn * PAGE_SIZE, ledger_arena_top() * PAGE_SIZE);
    (void)ledger_selfcheck("gfx");
}

/* 選択中の backend の SURFACE 上の名前 (LEDGER_SF_*)。 */
u32 gfx_sf_backend(void)
{
    if (g_backend == &gfx_backend_pegc) return LEDGER_SF_PEGC;
    if (g_backend == &gfx_backend_cirrus) return LEDGER_SF_CIRRUS;
    if (g_backend == &gfx_backend_geforce) return LEDGER_SF_GEFORCE;
    return LEDGER_SF_PC98;
}

/* Normal context: source construction through snapshot/acquire completion
 * must not cross a callback or scheduling point (T2e e1/e3 contract).
 * Dormant until e5/e11; TVRAM/Unicode are supplied in e8. */
__attribute__((section(".text.gfx_surface_source")))
static int gfx_selected_source(u32 role, struct surface_query_source *out)
{
    struct surface_query_source source = {0};
    u32 i, plane, count;
    if (!out) return OS32_ERR_INVAL;
    source.role = role;
    source.backend = gfx_sf_backend();
    *out = source;
    if (role != LEDGER_ROLE_CLIENT && role != LEDGER_ROLE_DISPLAY)
        return OS32_ERR_INVAL;
    count = role == LEDGER_ROLE_DISPLAY && source.backend == LEDGER_SF_PC98 ? GVRAM_PLANES : 1;
    for (plane = 0; plane < count; plane++) {
        for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
            const struct ledger_surface *sf = &ledger_surfaces[i];
            if ((gfx_surface_unready & (1U << i)) || !sf->npages || sf->closing ||
                sf->backend != source.backend ||
                sf->role != role) continue;
            if (count == GVRAM_PLANES && sf->first != GFX_PFN(gfx_display_planes[plane])) continue;
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

int gfx_surface_source(u32 role, struct surface_query_source *out)
{
    int rc = gfx_selected_source(role, out);
    if (rc) return rc;
    if (gfx_started) {
        for (u32 i = 0; i < out->count; i++)
            if (ledger_surfaces[out->refs[i].sid].perm_max == LEDGER_PERM_NONE)
                goto unready;
        return 0;
    }
unready:
    *out = (struct surface_query_source){.role = role, .backend = gfx_sf_backend()};
    return OS32_ERR_INVAL;
}

/* Probe validates selected backing before drawing starts; USER publication
 * additionally requires initialization and the query/lease authorization. */
int gfx_selected_selfcheck(void)
{
    struct surface_query_source client, display;
    if (gfx_selected_source(LEDGER_ROLE_CLIENT, &client) ||
        gfx_selected_source(LEDGER_ROLE_DISPLAY, &display)) return 0;
    for (u32 i = 0; i < display.count; i++)
        if (!ledger_surface_validate(&ledger_surfaces[display.refs[i].sid])) return 0;
    const struct ledger_surface *sf = &ledger_surfaces[client.refs[0].sid];
    if (!ledger_surface_validate(sf) || !g_backend ||
        g_backend->bb_base != bb[0] || g_backend->bb_size != sf->npages * PAGE_SIZE ||
        g_backend->bb_pitch != sf->pitch || g_backend->bb_format != sf->format) return 0;
    u8 *base = sf->backing >= LEDGER_SB_VRAM ?
               (u8 *)P2V_IO(sf->first * PAGE_SIZE) : (u8 *)P2V(sf->first * PAGE_SIZE);
    for (u32 i = 0; i < SURFACE_QUERY_MAX; i++)
        if (bb[i] != (i < sf->planes ? base + sf->plane_offset[i] : 0)) return 0;
    return bb_b == bb[0] && bb_r == bb[1] && bb_g == bb[2] && bb_i == bb[3];
}

/* Separate revoke and regen master phases (PM e5 section 9). The revoke
 * phase visits old/selected/fallback backends as selection resolves. Between
 * phases the publisher is not-ready, so public query/lease cannot acquire it.
 * The normal-context transition performs no scheduling or callbacks.
 * regen rechecks zero references even if this normal-context contract breaks. */
static void gfx_reinit_surface_roles(int finish, int display_only)
{
    unsigned int flags = irq_save();
    u32 cr3 = paging_current_cr3(), i;
    paging_load_cr3(paging_kernel_pd_phys());
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
        struct ledger_surface *sf = &ledger_surfaces[i];
        u32 bit = 1U << i;
        if (!finish) {
            if (!sf->npages || sf->backend != gfx_sf_backend() ||
                (sf->role != LEDGER_ROLE_CLIENT && sf->role != LEDGER_ROLE_DISPLAY) ||
                (display_only && sf->role != LEDGER_ROLE_DISPLAY) ||
                (gfx_reinit_pending & bit)) continue;
            gfx_reinit_pending |= bit;
            gfx_surface_unready |= bit;
            (void)lease_revoke_surface(i);
        } else if (gfx_reinit_pending & bit) {
            /* 200 lines use each 400-line plane's first half. SDK checked
             * attach/geometry migration is deferred to e6/e7 (PM Q5 correction). */
            if (ledger_surface_regen(i, 0)) gfx_surface_unready &= ~bit;
            else gfx_reinit_fail_count++;
        }
    }
    /* Reload the caller even when it owns leases revoked under master. */
    paging_load_cr3(cr3);
    irq_restore(flags);
    if (finish) {
        gfx_reinit_pending = 0;
        gfx_bind_client();
    }
}

static void gfx_reinit_surfaces(int finish)
{
    gfx_reinit_surface_roles(finish, 0);
}

/* TVRAM is independent of the selected graphics backend. Revoke even on
 * repeat GUI entry: a parked CUI parent may have acquired a new RO view. */
void gfx_reinit_tvram(void)
{
    unsigned int flags = irq_save();
    u32 cr3 = paging_current_cr3();
    paging_load_cr3(paging_kernel_pd_phys());
    for (u32 sid = 0; sid < LEDGER_MAX_SURFACES; sid++) {
        struct ledger_surface *sf = &ledger_surfaces[sid];
        if (!sf->npages || sf->role != LEDGER_ROLE_TVRAM) continue;
        gfx_surface_unready |= 1U << sid;
        (void)lease_revoke_surface(sid);
        if (ledger_surface_regen(sid, 0)) gfx_surface_unready &= ~(1U << sid);
        else gfx_reinit_fail_count++;
    }
    paging_load_cr3(cr3);
    irq_restore(flags);
}

void gfx_v86_return(void)
{
    gfx_reinit_surface_roles(0, 1);
    gfx_reinit_surface_roles(1, 1);
    gfx_reinit_tvram();
}

/* ======================================================================== */
/*  バックバッファの物理範囲 (exec が CPL=3 へ USER マップする範囲)。        */
/*  = **選択中 backend の CLIENT の SURFACE** (TASK_T1_LEDGER §3-8、B9)。    */
/*  9801 は ⑥ が池から確保した 128KB、PEGC は同じく 300KB、  */
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
 * BB は返さず保持する (R5 (b))。revoke failure / exhausted gen prevents
 * ownership transfer; the surface stays not-ready and kernel backing survives. */
static void __attribute__((cold)) gfx_diag_client(const struct ledger_surface *sf,
        u32 owner, u32 gen, u32 failures, int revoke, int transfer, int regen)
{
    if (serial_gate_active()) return;
    serial_puts_polled("[gfx] client sid=");
    if (sf) serial_diag_u32((u32)(sf - ledger_surfaces));
    else serial_puts_polled("skip");
    serial_puts_polled(" owner="); serial_diag_u32(owner);
    serial_puts_polled("->"); serial_diag_u32(sf ? sf->owner : 0);
    serial_puts_polled(" gen="); serial_diag_u32(gen);
    serial_puts_polled("->"); serial_diag_u32(sf ? sf->gen : 0);
    serial_puts_polled(" lease="); serial_diag_u32(sf ? sf->lease_count : 0);
    const char *labels[] = {" revoke=", " transfer=", " regen="};
    int results[] = {revoke, transfer, regen};
    for (u32 i = 0; i < 3; i++) {
        serial_puts_polled(labels[i]);
        if (results[i] < 0) serial_puts_polled("skip");
        else serial_diag_i32(results[i]);
    }
    serial_puts_polled(" fail+="); serial_diag_u32(gfx_reinit_fail_count - failures);
    serial_puts_polled(" unready="); serial_diag_hex(gfx_surface_unready, 8);
    serial_puts_polled("\r\n");
}

void __attribute__((cold)) gfx_client_to_gshell(void)
{
    struct ledger_surface *sf =
        ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);
    u32 owner = sf ? sf->owner : 0, gen = sf ? sf->gen : 0;
    u32 failures = gfx_reinit_fail_count;
    int revoke = -1, transfer = -1, regen = -1;
    if (sf && sf->owner != LEDGER_OWNER_GSHELL) {
        unsigned int flags = irq_save();
        u32 cr3 = paging_current_cr3(), sid = (u32)(sf - ledger_surfaces);
        paging_load_cr3(paging_kernel_pd_phys());
        gfx_surface_unready |= 1U << sid;
        revoke = lease_revoke_surface(sid);
        /* Ledger operations return boolean success; diagnostics use rc=0. */
        if (!sf->lease_count && sf->gen != LEDGER_SURFACE_GEN_MAX &&
            (transfer = !ledger_surface_transfer(sid, LEDGER_OWNER_GSHELL)) == 0 &&
            (regen = !ledger_surface_regen(sid, 0)) == 0) gfx_surface_unready &= ~(1U << sid);
        else gfx_reinit_fail_count++;
        paging_load_cr3(cr3);
        irq_restore(flags);
    }
    gfx_diag_client(sf, owner, gen, failures, revoke, transfer, regen);
    gfx_reinit_tvram();
    kselftest_run_audit("gui");
}

