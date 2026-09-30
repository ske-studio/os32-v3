#include "gfx_internal.h"
#include "gfx_hal.h"
#include "os32_kapi_shared.h"
#include "kstring.h"
#include "con_sink.h"     /* con_sink_is_enabled — GUI 中かどうか (票 T8) */
#include "console.h"      /* shell_print — 拒否を端末へ (票 T8-2) */

/* 画面の所有者の表は exec/appslot.c にある (判定材料が AppSlot にあり、
 * ホストで試験できるため)。gfx/ は -Iexec を持たないので、kernel/con_sink.c
 * が res_owner_get を引くのと同じ流儀で extern 宣言する。 */
extern int appslot_gfx_claim(int gui_mode);
extern int appslot_gfx_owner(void);

/* ======================================================================== */
/*  バックバッファ (拡張メモリ固定アドレス, 128KB)                          */
/* ======================================================================== */
u8 *bb_b = (u8 *)MEM_GFX_BB_BASE;
u8 *bb_r = (u8 *)(MEM_GFX_BB_BASE + GFX_PLANE_SZ);
u8 *bb_g = (u8 *)(MEM_GFX_BB_BASE + GFX_PLANE_SZ * 2);
u8 *bb_i = (u8 *)(MEM_GFX_BB_BASE + GFX_PLANE_SZ * 3);

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
void __cdecl gfx_get_framebuffer(GFX_Framebuffer *fb)
{
    if (!fb) return;
    fb->width = GFX_WIDTH;
    fb->height = gfx_current_height;
    /* ピッチと先頭はバックエンドの記述子から取る (9801 は 80B/ライン ×
     * 4 プレーンで従来と同じ値、PEGC は 640B/ライン のパックド 1 面)。
     * 決め打ちにするとパックド系で嘘のピッチを返す。 */
    fb->pitch = (int)(g_backend ? g_backend->bb_pitch : (u32)GFX_BPL);
    if (g_backend && g_backend->bb_format == GFX_BB_PACKED8) {
        fb->planes[0] = g_backend->bb_base;
        fb->planes[1] = (u8 *)0;
        fb->planes[2] = (u8 *)0;
        fb->planes[3] = (u8 *)0;
    } else {
        fb->planes[0] = bb[0];
        fb->planes[1] = bb[1];
        fb->planes[2] = bb[2];
        fb->planes[3] = bb[3];
    }
}

/* ======================================================================== */
/*  バックバッファの物理範囲 (exec が CPL=3 へ USER マップする範囲)。        */
/*  9801 は 0x6A000 の 128KB、PEGC は物理末尾から切り出した 300KB、          */
/*  Cirrus はリニア窓 0x1000000 の中の非表示面 300KB (H3b)。                 */
/*  **表示面は決してここに含めない** — CPL=3 に USER で見せてよいのは         */
/*  クライアント面だけ (契約 G4、レビュー #5 ②)。                            */
/* ======================================================================== */
void gfx_bb_phys_range(u32 *base, u32 *size)
{
    if (base) *base = 0;
    if (size) *size = 0;
    if (!g_backend || !g_backend->bb_base || g_backend->bb_size == 0) return;
    if (base) *base = (u32)g_backend->bb_base;
    if (size) *size = g_backend->bb_size;
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
    if (g_backend && g_backend->query) {
        g_backend->query((GFX_ScreenInfo *)out);
    }
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
    volatile u16 *tvram_char = (volatile u16 *)TVRAM_CHAR_BASE;
    volatile u8  *tvram_attr = (volatile u8  *)TVRAM_ATTR_BASE;

    bb[0] = bb_b; bb[1] = bb_r; bb[2] = bb_g; bb[3] = bb_i;
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
    gfx_select_backend();
    if (g_backend && g_backend->init) {
        g_backend->init();
        /* init が失敗して probe が取り下げられた場合は 9801 へ落とす
         * (バックバッファが取れない等)。 */
        if (g_backend->probe && !g_backend->probe()) {
            g_backend = &gfx_backend_pc98;
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
/*   2. **PEGC の**バックバッファは主記憶の物理末尾側から取る予約             */
/*      (sys_reserve_top) だが、アプリが走っている時点ではそのアプリの        */
/*      スタック/ヒープが上限直下を占有しており pgalloc_reserve_pfn が失敗する。 */
/*      (Cirrus のバックバッファはカード VRAM のリニア窓の中なので            */
/*      sys_reserve_top は使わない。ただし窓の写像は init で 1 回張って        */
/*      shutdown で畳まない設計なので、やはり起動時に済ませたい。)            */
/*                                                                          */
/*  どちらも「アプリが 1 つも走っていないカーネル文脈」でなければ成立しない。  */
/*  バックエンドが prepare() を持てば (PEGC) それだけを呼ぶ — 予約・写像・    */
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
    gfx_select_backend();
    if (!g_backend) return;
    /* 下ごしらえを別に持つバックエンド (PEGC) は表示に触らずに済ませる。
     * init → shutdown で済ませると CUI しか使わない起動でも同期を送り直す
     * (実機 Ra266 + 液晶の桁ズレの原因という仮説、TASK_FDC_REALHW §9-1)。 */
    if (g_backend->prepare) {
        g_backend->prepare();
        if (g_backend->probe && !g_backend->probe())
            g_backend = &gfx_backend_pc98;
        return;
    }
    if (!g_backend->init) return;
    g_backend->init();
    /* init が失敗して probe が取り下げられたら 9801 へ落とす (gfx_init と同じ)。 */
    if (g_backend->probe && !g_backend->probe()) {
        g_backend = &gfx_backend_pc98;
        return;
    }
    if (g_backend->shutdown) g_backend->shutdown();
}

void gfx_init(void)
{
    /* ⑤: GDC 初期化より前にバックエンドを決める */
    if (gfx_select_and_init_backend()) {
        if (g_backend->enter) g_backend->enter();
        return;   /* 9821 等: モード設定もバックバッファも init() が済ませた */
    }

    gfx_current_height = GFX_HEIGHT;  /* 400ラインモード */
    gfx_display_page = 0;
    prev_dirty.count = 0;

    _gfx_common_init(GFX_PLANE_SZ);

    /* GDC CSRFORM: L/R=0 (400ライン) */
    _out(GDC_GFX_CMD, GDC_GFX_400LINE);
    _out(GDC_GFX_PARAM, 0x00);
    _out(MODE_FF1_PORT, MFF1_HIRES);

    _out(GDC_GFX_CMD, GDC_CMD_START);

    /* ページフリッピング有効化: 両ページのVRAMをゼロクリア */
    _out(GDC_ACCESS_PAGE, 0x00);
    kmemset((u8 *)VRAM_PLANE_B, 0, GFX_PLANE_SZ);
    kmemset((u8 *)VRAM_PLANE_R, 0, GFX_PLANE_SZ);
    kmemset((u8 *)VRAM_PLANE_G, 0, GFX_PLANE_SZ);
    kmemset((u8 *)VRAM_PLANE_I, 0, GFX_PLANE_SZ);

    _out(GDC_ACCESS_PAGE, 0x01);
    kmemset((u8 *)VRAM_PLANE_B, 0, GFX_PLANE_SZ);
    kmemset((u8 *)VRAM_PLANE_R, 0, GFX_PLANE_SZ);
    kmemset((u8 *)VRAM_PLANE_G, 0, GFX_PLANE_SZ);
    kmemset((u8 *)VRAM_PLANE_I, 0, GFX_PLANE_SZ);

    /* ページ0を表示、ページ1に描画 */
    _out(GDC_DISP_PAGE, 0x00);
    _out(GDC_ACCESS_PAGE, 0x01);
    gfx_flip_enabled = 1;

    palette_init();
    gfx_scroll_init();

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
        if (g_backend->enter) g_backend->enter();
        return;
    }

    gfx_current_height = GFX_HEIGHT_200;  /* 200ラインモード */

    _gfx_common_init(GFX_PLANE_SZ_200);

    /* GDC CSRFORM: L/R=1 (各ライン2倍表示 → 200ライン) */
    _out(GDC_GFX_CMD, GDC_GFX_400LINE);
    _out(GDC_GFX_PARAM, 0x01);
    _out(MODE_FF1_PORT, MFF1_200LINE);

    _out(GDC_GFX_CMD, GDC_CMD_START);

    /* ページフリッピング有効化: 両ページのVRAMをゼロクリア */
    _out(GDC_ACCESS_PAGE, 0x00);
    kmemset((u8 *)VRAM_PLANE_B, 0, GFX_PLANE_SZ_200);
    kmemset((u8 *)VRAM_PLANE_R, 0, GFX_PLANE_SZ_200);
    kmemset((u8 *)VRAM_PLANE_G, 0, GFX_PLANE_SZ_200);
    kmemset((u8 *)VRAM_PLANE_I, 0, GFX_PLANE_SZ_200);

    _out(GDC_ACCESS_PAGE, 0x01);
    kmemset((u8 *)VRAM_PLANE_B, 0, GFX_PLANE_SZ_200);
    kmemset((u8 *)VRAM_PLANE_R, 0, GFX_PLANE_SZ_200);
    kmemset((u8 *)VRAM_PLANE_G, 0, GFX_PLANE_SZ_200);
    kmemset((u8 *)VRAM_PLANE_I, 0, GFX_PLANE_SZ_200);

    /* ページ0を表示、ページ1に描画 */
    _out(GDC_DISP_PAGE, 0x00);
    _out(GDC_ACCESS_PAGE, 0x01);
    gfx_flip_enabled = 1;
    gfx_display_page = 0;
    prev_dirty.count = 0;

    palette_init();
    gfx_scroll_init();

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
    /* 表示出力を戻し (leave)、バックエンドのハードウェア終了処理へ。
     * 9801 では leave は空、shutdown がフリップ解除 + GDC 表示停止を行う
     * (旧 gfx_shutdown の本体は backend_pc98.c の pc98_shutdown に移設)。 */
    if (g_backend && g_backend->leave) g_backend->leave();
    if (g_backend && g_backend->shutdown) g_backend->shutdown();
}
