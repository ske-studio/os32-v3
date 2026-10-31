#include "serial_diag.h"
#include "memmap.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "os32_kapi_shared.h"
#include "kstring.h"
#include "con_sink.h"     /* con_sink_is_enabled — GUI 中かどうか (票 T8) */
#include "console.h"      /* shell_print — 拒否を端末へ (票 T8-2) */
#include "pgalloc.h"      /* 台帳: 予約・BB・SURFACE (TASK_T1_LEDGER §3-8) */
#include "v86_bios.h"
#include "paging.h"       /* paging_map_phys: ⑥ の窓の写像 */
#include "pegc.h"         /* PEGC の窓と BIOS ワークの番地 (⑥ の資源レコード) */
#include "wab_xe10.h"     /* Xe10 の窓の定数 (同上) */
#include "kprintf.h"
#include "kselftest.h"
#include "kcg_module.h"
#include "geforce_module.h"
#include "gui.h"
#include "../drivers/video/pc98/pc98_route.h"
#include "../exec/surface_query.h"
#include "../exec/appslot.h"
#include "../exec/exec.h"

/* 画面の所有者の表は exec/appslot.c にある (判定材料が AppSlot にあり、
 * ホストで試験できるため)。gfx/ は -Iexec を持たないので、kernel/console/con_sink.c
 * が res_owner_get を引くのと同じ流儀で extern 宣言する。 */
extern int ring3_call_from_user(void);
extern int appslot_gfx_claim(int gui_mode);
extern int appslot_gfx_owner(void);

/* ======================================================================== */
/*  バックバッファ (boot 時の連続 pool, 128KB)                          */
/* ======================================================================== */
u8 *bb_b, *bb_r, *bb_g, *bb_i;

int gfx_current_height = GFX_HEIGHT;  /* current display: 200/400/480 */

u8 *bb[SURFACE_QUERY_MAX];

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

static int gfx_started;
/* Boot binds PC98 before DISPLAY registration; selection validates thereafter. */
static int gfx_mode_ready = 1;
u32 gfx_init_gen;
static u32 present_logged_gen;

static void __attribute__((cold)) gfx_diag_init(void)
{
    if (serial_gate_active()) return;
    serial_puts_polled("[gfx] init gen="); serial_diag_u32(gfx_init_gen);
    serial_puts_polled(" be="); serial_puts_polled(g_backend ? g_backend->name : "none");
    serial_puts_polled(" started="); serial_diag_u32(gfx_started);
    serial_puts_polled(" bb="); serial_diag_hex(bb[0] ? V2P(bb[0]) : 0, 8);
    serial_puts_polled(" h="); serial_diag_u32(gfx_current_height);
    serial_puts_polled("\r\n");
}

void __attribute__((cold)) gfx_diag_first_present(u32 before)
{
    if (!gfx_init_gen || present_logged_gen == gfx_init_gen) return;
    if (serial_gate_active()) return;
    present_logged_gen = gfx_init_gen;
    serial_puts_polled("[gfx] first present gen="); serial_diag_u32(gfx_init_gen);
    serial_puts_polled(" bytes="); serial_diag_u32(gfx_counters.present_bytes - before);
    serial_puts_polled("\r\n");
}

/* probe 順のバックエンド一覧。速い/高機能な順に前から並べ、最初に probe() が
 * 1 を返したものを使う (Cirrus → PEGC → 9801)。
 * 9801 の probe は常に 1 なので、末尾が必ず受け皿になる。 */
static GfxBackend *const g_backend_list[] = {
    &gfx_backend_geforce, /* Explicit selection only. */
    &gfx_backend_cirrus, /* Cirrus GD54xx アクセラレータ (H3)。ID 5Bh のみ */
    &gfx_backend_pegc,   /* 9821 PEGC 256 色 (H2)。9801 では probe が 0 */
    &gfx_backend_pc98    /* 9801 標準グラフィック (H1)。最後の受け皿 */
};

/* 起動設定 (/etc/system.cfg GFX=) から渡される希望バックエンド (票 H2b)。
 * kernel.c が gfx_init より前に gfx_set_backend_pref() で設定する。 */
static int g_backend_pref = GFX_PREF_AUTO;

STATIC_ASSERT(LEDGER_MAX_SURFACES <= 32, gfx_surface_bitmap_width);
/* The not-ready bitmap lives with the ledger (pgalloc.c); ledger_surface_create clears it on slot reuse (e8a). */
static u32 gfx_reinit_pending;
volatile u32 gfx_reinit_fail_count;
static void gfx_reinit_surfaces(int finish);
static void gfx_bind_client(void);

void gfx_set_backend_pref(int pref)
{
    if (pref != GFX_PREF_PC98 && pref != GFX_PREF_PEGC &&
        pref != GFX_PREF_CIRRUS && pref != GFX_PREF_GEFORCE)
        pref = GFX_PREF_AUTO;
    g_backend_pref = pref;
}

int gfx_get_backend_pref(void)
{
    return g_backend_pref;
}

/* Missing module stubs do not count as a Cirrus hardware probe. */
static int cirrus_probed;

int gfx_cirrus_probed(void)
{
    return cirrus_probed;
}

static int gfx_probe_backend(const GfxBackend *backend)
{
    if (!backend->modes || !backend->probe) return 0;
    if (backend == &gfx_backend_cirrus && backend->init) cirrus_probed = 1;
    return backend->probe();
}

/* Compare display usage with existing reservation capacity, never resize it.
 * PC98 CLIENT planes retain their 400-line offsets even in 200-line mode. */
static int gfx_mode_fits(const GfxBackend *be, const GfxMode *mode)
{
    u32 plane, total, displays = 0;
    u32 backend = be == &gfx_backend_pc98 ? LEDGER_SF_PC98 :
                  be == &gfx_backend_pegc ? LEDGER_SF_PEGC :
                  be == &gfx_backend_cirrus ? LEDGER_SF_CIRRUS :
                  be == &gfx_backend_geforce ? LEDGER_SF_GEFORCE : 0;
    const struct ledger_surface *client = ledger_surface_find(backend, LEDGER_ROLE_CLIENT);
    GfxFormatDesc format;
    if (!gfx_mode_layout(mode, &plane, &total) ||
        !gfx_format_desc(mode->format, &format) || !client || client->closing ||
        client->format != mode->format || client->planes != format.planes ||
        client->npages > U32_MAX / PAGE_SIZE || total > client->npages * PAGE_SIZE)
        return 0;
    for (u32 i = 0; i < client->planes; i++) {
        u32 end = i + 1 < client->planes ? client->plane_offset[i + 1] :
                                         client->npages * PAGE_SIZE;
        if (client->plane_offset[i] > end || plane > end - client->plane_offset[i])
            return 0;
    }
    for (u32 i = 0; i < LEDGER_MAX_SURFACES; i++) {
        const struct ledger_surface *sf = &ledger_surfaces[i];
        if (!sf->npages || sf->backend != backend || sf->role != LEDGER_ROLE_DISPLAY)
            continue;
        if (sf->closing || sf->npages > U32_MAX / PAGE_SIZE ||
            (backend == LEDGER_SF_PC98 ? plane : total) > sf->npages * PAGE_SIZE)
            return 0;
        displays++;
    }
    /* PC98 can draw from CLIENT alone when boot DISPLAY registration fails.
     * Existing DISPLAY records still have to fit; packed requires its record. */
    return backend == LEDGER_SF_PC98 || displays == 1;
}

/* Select only registered data after probe, before any hardware setup. */
static int gfx_select_mode(GfxBackend *be, int legacy200)
{
    const GfxMode *mode = gfx_mode_pick(be, legacy200);
    gfx_mode_ready = 0;
    if (!mode) return 0;
    if (!gfx_mode_fits(be, mode)) return 0;
    gfx_mode_ready = 1;
    be->selected_mode = mode;
    return 1;
}

static void gfx_select_backend(int legacy200)
{
    int i;
    int n = (int)(sizeof(g_backend_list) / sizeof(g_backend_list[0]));

    /* 強制指定 (GFX=) は probe より先に効く。
     * pc98 は probe をまったく行わない (9801 は最後の受け皿なので常に成功)。
     * pegc は probe だけは通す — 実機に PEGC が無ければ描けないので、
     * 失敗したら従来どおり 9801 へ落とす。 */
    if (g_backend_pref == GFX_PREF_PC98) {
        gfx_select_mode(&gfx_backend_pc98, legacy200);
        g_backend = &gfx_backend_pc98;
        return;
    }
    if (g_backend_pref == GFX_PREF_PEGC ||
        g_backend_pref == GFX_PREF_CIRRUS || g_backend_pref == GFX_PREF_GEFORCE) {
        const GfxBackend *want = g_backend_pref == GFX_PREF_GEFORCE ? &gfx_backend_geforce : (g_backend_pref == GFX_PREF_CIRRUS)
                                     ? &gfx_backend_cirrus
                                     : &gfx_backend_pegc;
        for (i = 0; i < n; i++) {
            /* weak 宣言なので未リンクなら要素が 0 (→ 9801 へ落ちる) */
            if (!g_backend_list[i]) continue;
            if (g_backend_list[i] != want) continue;
            if (gfx_probe_backend(g_backend_list[i]) &&
                gfx_select_mode(g_backend_list[i], legacy200)) {
                g_backend = g_backend_list[i];
                return;
            }
            break;
        }
        gfx_select_mode(&gfx_backend_pc98, legacy200);
        g_backend = &gfx_backend_pc98;
        return;
    }

    for (i = 0; i < n; i++) {
        /* weak 宣言のバックエンド (未リンクなら 0) を飛ばす。
         * → include/gfx_hal.h の gfx_backend_pegc の注記 */
        if (!g_backend_list[i] || g_backend_list[i] == &gfx_backend_geforce) continue;
        if (gfx_probe_backend(g_backend_list[i])) {
            if (!gfx_select_mode(g_backend_list[i], legacy200)) break;
            g_backend = g_backend_list[i];
            return;
        }
    }
    gfx_select_mode(&gfx_backend_pc98, legacy200);
    g_backend = &gfx_backend_pc98;   /* フォールバック */
}

#include "gfx_core_framebuffer.inc"

#include "gfx_core_surface.inc"

#include "gfx_core_api.inc"

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
static int gfx_select_and_init_backend(int legacy200)
{
    gfx_started = 0;
    gfx_reinit_surfaces(0);
    gfx_select_backend(legacy200);
    gfx_reinit_surfaces(0);
    if (g_backend == &gfx_backend_geforce) gfx_bind_client();
    if (g_backend && g_backend->init) {
        g_backend->init();
        /* init が失敗して probe が取り下げられた場合は 9801 へ落とす
         * (バックバッファが取れない等)。 */
        if (g_backend->probe && !g_backend->probe()) {
            if (g_backend == &gfx_backend_geforce && !pc98_route_safe()) return -1;
            gfx_select_mode(&gfx_backend_pc98, legacy200);
            g_backend = &gfx_backend_pc98;
            gfx_reinit_surfaces(0);
            gfx_bind_client(); /* bind init failure fallback */
            return 0;
        }
        return 1;
    }
    return g_backend_pref != GFX_PREF_GEFORCE || !pc98_route_safe || pc98_route_safe() ? 0 : -1;
}

/* ======================================================================== */
/*  gfx_prepare_backend — 起動時の PEGC 準備と、初回 GUI 境界の準備          */
/*                                                                          */
/*  splash は PC98 を強制する。起動時は Cirrus 登録前にここを呼び、PEGC の   */
/*  BIOS ワークを master PD で probe する。窓・BB は起動時の ⑥ が確保済み。 */
/*  PEGC の prepare は表示のモードも同期も変えず、同期状態だけ記録する。     */
/*  Cirrus は起動時には温めず、最初の GUI 境界で probe / init → shutdown。  */
/*  CUI の描画アプリの gfx_init も graphics 境界で、直接 probe / init する。 */
/*  probe 済みなら GUI 境界からは再度呼ばず、描画側の gfx_init に任せる。    */
/*  予約と窓は shutdown をまたいで保持される (TASK_T1_LEDGER §3-8)。         */
/*  9801 (init == NULL) は何もしない。                                      */
/* ======================================================================== */
void gfx_prepare_backend(void)
{
    gfx_started = 0;
    gfx_reinit_surfaces(0);
    gfx_select_backend(0);
    gfx_reinit_surfaces(0);
    gfx_bind_client();
    if (!g_backend) goto done;
    /* 下ごしらえを別に持つバックエンド (PEGC) は表示に触らずに済ませる。
     * init → shutdown で済ませると CUI しか使わない起動でも同期を送り直す
     * (実機 Ra266 + 液晶の桁ズレの原因という仮説、TASK_FDC_REALHW §9-1)。 */
    if (g_backend->prepare) {
        g_backend->prepare();
        if (g_backend->probe && !g_backend->probe()) {
            gfx_select_mode(&gfx_backend_pc98, 0);
            g_backend = &gfx_backend_pc98;
            gfx_reinit_surfaces(0);
            gfx_bind_client();
        }
        goto done;
    }
    if (!g_backend->init) goto done;
    g_backend->init();
    /* init が失敗して probe が取り下げられたら 9801 へ落とす (gfx_init と同じ)。 */
    if (g_backend->probe && !g_backend->probe()) {
        gfx_select_mode(&gfx_backend_pc98, 0);
        g_backend = &gfx_backend_pc98;
        gfx_reinit_surfaces(0);
        gfx_bind_client();
        goto done;
    }
    if (g_backend->shutdown) g_backend->shutdown();
done:
    gfx_reinit_surfaces(1);
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
    gfx_init_gen++;
    /* ⑤: GDC 初期化より前にバックエンドを決める */
    int initialized = gfx_select_and_init_backend(0);
    if (initialized < 0) return;
    if (initialized) {
        gfx_reinit_surfaces(1);
        gfx_started = bb[0] != 0;
        if (g_backend->enter) g_backend->enter();
        gfx_diag_init();
        return;   /* 9821 等: モード設定もバックバッファも init() が済ませた */
    }

    gfx_current_height = gfx_backend_mode(g_backend)->height;  /* selected planar mode */
    gfx_display_page = 0;
    prev_dirty.count = 0;

    gfx_bind_client();
    if (!bb[0]) { gfx_reinit_surfaces(1); gfx_diag_init(); return; }
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
    gfx_reinit_surfaces(1);
    gfx_started = 1;

    /* 表示出力を有効化 (9801 の enter は空)。バックエンドの選択は先頭で済み。 */
    if (g_backend && g_backend->enter) g_backend->enter();
    gfx_diag_init();
}

void gfx_init_200(void)
{
    gfx_init_gen++;
    /* ⑤: GDC 初期化より前にバックエンドを決める。
     * 200 ラインは 9801 プレーン専用のモードなので、パックド系が選ばれた
     * ときはそのバックエンドのネイティブ解像度で立ち上げる (200 ラインの
     * 縦 2 倍表示は 16 色プレーンの機能で、PEGC には対応物が無い)。 */
    int initialized = gfx_select_and_init_backend(1);
    if (initialized < 0) return;
    if (initialized) {
        gfx_reinit_surfaces(1);
        gfx_started = bb[0] != 0;
        if (g_backend->enter) g_backend->enter();
        gfx_diag_init();
        return;
    }

    gfx_current_height = gfx_backend_mode(g_backend)->height;  /* selected legacy mode */

    gfx_bind_client(); /* bind before init_200; retain 400-line plane stride */
    if (!bb[0]) { gfx_reinit_surfaces(1); gfx_diag_init(); return; }
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
    gfx_reinit_surfaces(1);
    gfx_started = 1;

    /* 表示出力を有効化 (9801 の enter は空)。バックエンドの選択は先頭で済み。 */
    if (g_backend && g_backend->enter) g_backend->enter();
    gfx_diag_init();
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
    if (!kcg_module_ready()) {
        shell_print("Error: KCG unavailable -> CUI\n", ATTR_RED);
        kapi_sys_exit(1);
        return -1;
    }
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
    /* Internal cleanup/WM remains trusted. In GUI, USER may stop only its own
     * fullscreen display, never acquire it merely by calling shutdown. */
    extern int ring3_call_from_user(void);
    if (ring3_call_from_user() && con_sink_is_enabled()) {
        struct caller_access caller;
        if (!caller_access_get_user(&caller) ||
            caller.app_id != appslot_gfx_owner()) return;
    }
    gfx_started = 0;
    gfx_reinit_surfaces(0);
    /* 表示出力を戻し (leave)、バックエンドのハードウェア終了処理へ。
     * 9801 では leave は空、shutdown がフリップ解除 + GDC 表示停止を行う
     * (旧 gfx_shutdown の本体は backend_pc98.c の pc98_shutdown に移設)。 */
    if (g_backend && g_backend->leave) g_backend->leave();
    if (g_backend && g_backend->shutdown) g_backend->shutdown();
    if (g_backend == &gfx_backend_geforce && pc98_route_safe()) {
        gfx_current_height = GFX_HEIGHT;
        gfx_flip_enabled = gfx_display_page = 0;
    }
    if (g_backend == &gfx_backend_geforce && !g_backend->probe() && pc98_route_safe()) {
        gfx_select_mode(&gfx_backend_pc98, 0);
        g_backend = &gfx_backend_pc98;
        gfx_bind_client();
    }
    gfx_reinit_surfaces(1);
}

void geforce_display_failed(void)
{
    gfx_started = 0;
    gfx_reinit_surfaces(0);
    /* Queue the WM's existing SessionAction; CPL0 never consumes abort_req. */
    if (con_sink_is_enabled()) gui_display_failed();
}

#include "gfx_core_bridge.inc"
