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
/* 表 (資源レコード・SURFACE の型板・DISPLAY の面) と ⑥ の手順は platform の
 * provider が持つ (第 5 段 B5、PC-98 は platform/pc98/gfx_surfaces.c)。共通側は
 * 世代・lease・公開の判定だけを持ち、CLIENT 面を bb へ結ぶ関数を渡す。 */
void gfx_boot_reserve(void)
{
    display_provider_boot_reserve(g_backend_pref, gfx_bind_client);
}

/* 選択中の backend の SURFACE 上の名前 (LEDGER_SF_*)。 */
u32 gfx_sf_backend(void)
{
    if (g_backend == &gfx_backend_pegc) return LEDGER_SF_PEGC;
    if (g_backend == &gfx_backend_cirrus) return LEDGER_SF_CIRRUS;
    if (g_backend == &gfx_backend_geforce) return LEDGER_SF_GEFORCE;
    if (g_backend == &gfx_backend_trident) return LEDGER_SF_TRIDENT;
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
    count = role == LEDGER_ROLE_DISPLAY ? display_provider_display_records(source.backend) : 1;
    for (plane = 0; plane < count; plane++) {
        for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
            const struct ledger_surface *sf = &ledger_surfaces[i];
            if ((gfx_surface_unready & (1U << i)) || !sf->npages || sf->closing ||
                sf->backend != source.backend ||
                sf->role != role) continue;
            if (count > 1 && sf->first != display_provider_display_first(plane)) continue;
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
void gfx_reinit_surface_roles(int finish, int display_only)
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

