/* =========================================================================
 *  CIRRUS_WIN_HOST.C — Cirrus の窓の可否を「物理地図」で決めるか
 *
 *  実行: python3 -B tools/tests/test_cirrus_win.py [--mutate]
 *  記録: tools/tests/cirrus_win_tdd.md
 *
 *  gfx/backend_cirrus.c の cirrus_win_usable() は、窓 (バンク窓 F60000h /
 *  リニア窓 01000000h) を張ってよいかを **RAM の上端** (sys_get_mem_kb) で
 *  決めていた。K6-RAM 以後、15MB + 高位 RAM の構成 (NP21/W の RAM
 *  17,408KB) では上端 17MB が 15-16MB の穴より上に出るので、穴の中の
 *  バンク窓まで「RAM が届いている」と誤判定し、probe が ID 判定の前に落ちる。
 *  PEGC で直した不具合 (docs/POLICY_DEBUG.md §4-34) と同じ形。
 *
 *  実物の backend_cirrus.c を 1 行も写さずに #include し、周りの関数だけを
 *  贋物にする。pgalloc_range_has_ram は**贋の物理地図** (RAM の span の表)
 *  から答え、sys_get_mem_kb は構成の上端を返す (旧判定を RED にするため)。
 *  ボードグルー wab_glue_xe10 も贋物にして、窓の番地と probe の到達を見る。
 *
 *  2026-09-29 (A2): リニア窓は v3 のデバイス窓の帯 FE000000h へ移した。
 *  Cirrus は NP21/W 互換のためだけなので、auto では np2_detect() が真の
 *  ときだけボードの ID を読む (GFX=cirrus の明示時は常に読む)。どちらも
 *  贋物 (np2_detect / gfx_get_backend_pref) で切り替えて見る。
 * ========================================================================= */
#include "types.h"
#define NOINST __attribute__((no_instrument_function))

#include "../../gfx/cirrus_identify.c"
#include "../../gfx/backend_cirrus.c"
#include "wab_xe10.h"     /* 実物の窓の番地 (贋グルーに入れる) */

/* ---- 出力と終了 (libc なし、ILP32 の int 0x80) ---- */
static void output(const char *s)
{
    unsigned int len = 0;
    while (s[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory");
}
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
static void fail(const char *message)
{
    output("ASSERT FAIL: "); output(message); output("\n"); finish(1);
}
#define CHECK(c) do { if (!(c)) fail(#c); } while (0)

/* ---- 贋の物理地図 ---- */
#define MAX_SPANS 4
static u32 ram_first[MAX_SPANS], ram_end[MAX_SPANS];  /* PFN 半開 */
static int ram_spans;
static u32 top_kb;          /* sys_get_mem_kb が返す「上端」 */
static int map_ready;       /* 0 = pgalloc 未初期化 */
static int has_ram_calls;

static void map_reset(u32 top)
{
    ram_spans = 0;
    top_kb = top;
    map_ready = 1;
    has_ram_calls = 0;
}
static void map_add(u32 base, u32 end)
{
    ram_first[ram_spans] = base / PAGE_SIZE;
    ram_end[ram_spans] = end / PAGE_SIZE;
    ram_spans++;
}

/* 実物 (kernel/pgalloc.c) と同じ約束: 範囲異常・未初期化は 1。 */
int pgalloc_range_has_ram(u32 first, u32 end)
{
    int i;
    has_ram_calls++;
    if (!map_ready || end <= first) return 1;
    for (i = 0; i < ram_spans; i++)
        if (ram_first[i] < end && first < ram_end[i]) return 1;
    return 0;
}
u32 sys_get_mem_kb(void) { return top_kb; }

/* NP21/W 判定と GFX= の希望 (贋物)。np2_calls は「判定を読んだ」回数。 */
static int fake_np2, fake_pref = GFX_PREF_AUTO, np2_calls;
int np2_detect(void) { np2_calls++; return fake_np2; }
int gfx_get_backend_pref(void) { return fake_pref; }

/* 台帳の SURFACE (T1e): ⑥ (gfx_boot_reserve) が窓を予約・写像して Cirrus の
 * CLIENT を登録したか。probe はそれが無ければ I/O に進まない。 */
static int fake_surface = 1;
static struct ledger_surface fake_client;
struct ledger_surface *ledger_surface_find(u32 backend, u32 role)
{
    if (!fake_surface || backend != LEDGER_SF_CIRRUS || role != LEDGER_ROLE_CLIENT)
        return (struct ledger_surface *)0;
    return &fake_client;
}

/* ---- 贋のボードグルー ---- */
static int glue_probe_calls, enable_calls;
static void fake_enable(void) { enable_calls++; }
static int fake_glue_probe(void) { glue_probe_calls++; return 0; }
static int linear_off_calls, relay_value, relay_calls;
static void fake_linear_enable(int on) { if (!on) linear_off_calls++; }
static void fake_relay(int on) { relay_value = on; relay_calls++; }
WabGlue wab_glue_xe10;

static void glue_reset(u32 win_base, u32 win_size, u32 lin_base, u32 lin_size)
{
    kmemset(&wab_glue_xe10, 0, sizeof(wab_glue_xe10));
    wab_glue_xe10.name = "fake-xe10";
    wab_glue_xe10.probe = fake_glue_probe;
    wab_glue_xe10.linear_enable = fake_linear_enable;
    wab_glue_xe10.win_base = win_base;
    wab_glue_xe10.win_size = win_size;
    wab_glue_xe10.lin_base = lin_base;
    wab_glue_xe10.lin_size = lin_size;
    glue_probe_calls = 0;
    enable_calls = 0;
    gfx_backend_cirrus.init = fake_enable;
    s_probed = 0;
    s_probe_ok = 0;
}

/* ---- 残りの依存 (この試験の経路では呼ばれない) ---- */
GfxCounters gfx_counters;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
void *kmemset(void *dst, int val, u32 n)
{
    u8 *p = (u8 *)dst;
    while (n--) *p++ = (u8)val;
    return dst;
}
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int val, u32 n) { return kmemset(dst, val, n); }
void kprintf(unsigned char attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int paging_map_phys(u32 v, u32 p, u32 n, u32 f)
{ (void)v; (void)p; (void)n; (void)f; fail("paging_map_phys"); return -1; }
/* Record real HAL command arguments; hardware itself is the seam. */
static int setup_rc, idle_rc, draw_rc, setup_w, setup_h, fills, copies;
static u32 setup_pitch, draw_dst, draw_src, draw_pitch, source_pitch;
static int draw_w, draw_h;
int wab_cirrus_probe(WabGlue *g) { (void)g; return 0; }
int wab_cirrus_setup_8bpp(WabGlue *g, int w, int h, u32 p, u32 st)
{
    (void)g; CHECK(st == 0);
    setup_w=w; setup_h=h; setup_pitch=p; return setup_rc;
}
void wab_cirrus_shutdown(WabGlue *g) { (void)g; }
void wab_cirrus_dac_set(WabGlue *g, int i, u8 r, u8 gg, u8 b)
{ (void)g; CHECK(i >= 0 && i < 256 && r <= 63 && gg <= 63 && b <= 63); }
void wab_cirrus_set_fill_pattern(WabGlue *g, u32 o)
{ (void)g; CHECK(o == 0x96000); }
int wab_cirrus_fill(WabGlue *g, u32 d, u32 p, int w, int h, u8 c)
{
    (void)g; (void)c; fills++;
    CHECK(d == (fills == 1 ? 0UL : 0x4b000UL) || fills > 2);
    draw_dst=d; draw_pitch=p; draw_w=w; draw_h=h; return draw_rc;
}
int wab_cirrus_copy(WabGlue *g, u32 d, u32 s, u32 dp, u32 sp, int w, int h)
{
    (void)g; copies++;
    draw_dst=d; draw_src=s; draw_pitch=dp; source_pitch=sp;
    draw_w=w; draw_h=h; return draw_rc;
}
int wab_cirrus_wait_idle(WabGlue *g) { (void)g; return idle_rc; }
const PaletteEntry *palette_get_all(void)
{ static const PaletteEntry colors[16] = {{0}}; return colors; }
void palette_shadow_set(int i, u8 r, u8 g, u8 b) { (void)i; (void)r; (void)g; (void)b; }

/* ---- 構成 ---- */
#define KB(x)  ((u32)(x) * 1024UL)
#define MB(x)  ((u32)(x) * 1024UL * 1024UL)
#define BANK   WAB_XE10_WIN_BASE          /* F60000h */
#define BANK_N WAB_XE10_WIN_SIZE
#define LIN    WAB_XE10_LINEARWIN_BASE    /* FE000000h (v3 のデバイス窓の帯) */
#define LIN_N  WAB_XE10_LINEARWIN_SIZE
#define LIN_DEC WAB_XE10_LINEARWIN_DECODE /* NP21/W が窓として出す 4MB */
#define OLD_LIN 0x01000000UL              /* 移す前の番地 (16MB 直上) */

/* 15MB + 高位 1MB (NP21/W ExMemory 16 の K6 以後の地図、上端 17,408KB)。 */
static void cfg_15m_high1(void)
{
    map_reset(17408);
    map_add(0, KB(640));
    map_add(MB(1), MB(15));
    map_add(MB(16), MB(17));
}

/* 1. 本件: 穴の中のバンク窓は張れる。上端 17MB では決めない。 */
static void bank_window_in_hole(void)
{
    cfg_15m_high1();
    CHECK(cirrus_win_usable(BANK, BANK_N));
    CHECK(has_ram_calls > 0);   /* 物理地図に問い合わせている */
    /* PEGC の窓 (F00000h、512KB) も同じ穴。同じ答えになること。 */
    CHECK(cirrus_win_usable(MEM_SYSTEM_SPACE_BASE, MB(1)));
}

/* 2. リニア窓の置き場。16MB 直上は高位 RAM と本当に重なるので使えない。
 *    v3 のデバイス窓の帯 (FE000000h) は RAM の量に関係なく空いている。 */
static void linear_window_placement(void)
{
    /* 番地は帯の先頭、dat = FEh (NP21/W が受け付ける最上位)。 */
    CHECK(LIN == MEM_DEVICE_APERTURE_BASE);
    CHECK(WAB_XE10_LINEARWIN_SEL == 0xFE);
    CHECK(LIN + LIN_DEC <= MEM_DEVICE_APERTURE_END);
    /* NP21/W 17MB: 旧番地は RAM と重なる、新番地は空いている。 */
    cfg_15m_high1();
    CHECK(!cirrus_win_usable(OLD_LIN, LIN_N));
    CHECK(cirrus_win_usable(LIN, LIN_N));
    CHECK(cirrus_win_usable(LIN, LIN_DEC));
    /* 実機 64MB (16MB から RAM が連続): 旧番地は重なる、新番地は空いている。 */
    map_reset(65536);
    map_add(0, KB(640));
    map_add(MB(1), MB(15));
    map_add(MB(16), MB(64));
    CHECK(!cirrus_win_usable(OLD_LIN, LIN_N));
    CHECK(cirrus_win_usable(LIN, LIN_DEC));
    /* 窓の末尾 1 ページだけ RAM でも拒む (部分一致を見落とさない)。 */
    map_reset(8192);
    map_add(MB(1), MB(8));
    map_add(LIN + LIN_N - PAGE_SIZE, LIN + LIN_N);
    CHECK(!cirrus_win_usable(LIN, LIN_N));
    /* 窓のすぐ後ろ・すぐ前の RAM は窓を塞がない (範囲を広げすぎない)。 */
    map_reset(8192);
    map_add(MB(1), MB(8));
    map_add(LIN + LIN_N, LIN + LIN_N + MB(1));
    CHECK(cirrus_win_usable(LIN, LIN_N));
    map_reset(8192);
    map_add(LIN - MB(1), LIN);
    CHECK(cirrus_win_usable(LIN, LIN_N));
}

/* 3. 16MB 丸ごと RAM (043Bh の 15-16MB を RAM にした構成): バンク窓は RAM。 */
static void system_space_is_ram(void)
{
    map_reset(16384);
    map_add(0, KB(640));
    map_add(MB(1), MB(16));
    CHECK(!cirrus_win_usable(BANK, BANK_N));
    /* 窓の最初の 1 ページだけ RAM でも拒む。 */
    map_reset(15744 + 4);
    map_add(MB(1), BANK + PAGE_SIZE);
    CHECK(!cirrus_win_usable(BANK, BANK_N));
}

/* 4. 8MB 機: どちらの窓にも RAM は無い (K6 以前と同じ答え)。 */
static void small_machine(void)
{
    map_reset(8192);
    map_add(0, KB(640));
    map_add(MB(1), MB(8));
    CHECK(cirrus_win_usable(BANK, BANK_N));
    CHECK(cirrus_win_usable(LIN, LIN_N));
}

/* 5. 物理地図が引けない (pgalloc 未初期化) なら張らせない。 */
static void map_not_ready(void)
{
    map_reset(8192);
    map_ready = 0;
    CHECK(!cirrus_win_usable(BANK, BANK_N));
}

/* 6. 大きさ 0・32bit 空間の末尾越え (桁あふれ) は拒む。4GB ちょうどで
 *    終わる窓は可 (末尾番地で持つので u32 に収まる)。 */
static void window_bounds(void)
{
    map_reset(8192);
    map_add(MB(1), MB(8));
    CHECK(!cirrus_win_usable(BANK, 0));
    CHECK(cirrus_win_usable(0xFFFFF000UL, PAGE_SIZE));
    CHECK(!cirrus_win_usable(0xFFFFF000UL, 2 * PAGE_SIZE));
    CHECK(!cirrus_win_usable(0xFFFFFFFFUL, 2));
    CHECK(cirrus_win_usable(0xFFFFFFFFUL, 1));
    /* 末尾が 4GB を越えて先頭側のページへ回り込む窓 (末尾番地を素直に足すと
     * [1, 2) の 1 ページだけを問い合わせてしまう) も拒む。 */
    CHECK(!cirrus_win_usable(0x1800UL, 0xFFFFFFFFUL));
    /* 32MB より上 (旧 PAGING_MAP_SIZE の外) も物理地図で決める。 */
    CHECK(cirrus_win_usable(0x02000000UL, LIN_N));
}

/* 7. probe の段:
 *    - 15MB + 高位 RAM (NP21/W 17MB) でも両方の窓が通り、NP21/W なら ID
 *      判定まで進む (本件の症状が消えること)。
 *    - auto で NP21/W でなければ ID を読みにいかない (実機でポートを叩かない)。
 *    - GFX=cirrus の明示なら NP21/W でなくても ID 判定へ進む。
 *    - バンク窓が RAM に当たれば、どの場合も進まない (NP21/W 判定も読まない)。 */
static void probe_stages(void)
{
    cfg_15m_high1();
    fake_pref = GFX_PREF_AUTO; fake_np2 = 1; np2_calls = 0;
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    CHECK(cirrus_probe() == 0);     /* 贋グルーの ID 判定は 0 を返す */
    CHECK(glue_probe_calls == 1);

    /* 結果はキャッシュ: 二度目は何も読まない。 */
    CHECK(cirrus_probe() == 0);
    CHECK(glue_probe_calls == 1);

    cfg_15m_high1();
    fake_pref = GFX_PREF_AUTO; fake_np2 = 0; np2_calls = 0;
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    CHECK(cirrus_probe() == 0);
    CHECK(np2_calls == 1);
    CHECK(glue_probe_calls == 0);

    cfg_15m_high1();
    fake_pref = GFX_PREF_CIRRUS; fake_np2 = 0; np2_calls = 0;
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    CHECK(cirrus_probe() == 0);
    CHECK(glue_probe_calls == 1);

    /* バンク窓が RAM (16MB 丸ごと RAM) → NP21/W 判定も ID も読まない
     * (auto / 明示の両方)。 */
    map_reset(16384);
    map_add(MB(1), MB(16));
    fake_pref = GFX_PREF_AUTO; fake_np2 = 1; np2_calls = 0;
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    CHECK(cirrus_probe() == 0);
    CHECK(glue_probe_calls == 0);
    CHECK(np2_calls == 0);
    fake_pref = GFX_PREF_CIRRUS; np2_calls = 0;
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    CHECK(cirrus_probe() == 0);
    CHECK(glue_probe_calls == 0);

    /* リニア窓が RAM に当たる番地 (旧番地 + 高位 RAM) → 同じく進まない。 */
    cfg_15m_high1();
    fake_pref = GFX_PREF_AUTO; fake_np2 = 1; np2_calls = 0;
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    map_add(LIN, LIN + LIN_N);
    CHECK(cirrus_probe() == 0);
    CHECK(glue_probe_calls == 0);
    fake_pref = GFX_PREF_AUTO;

    /* T1e: 識別は通るが ⑥ が SURFACE を登録していない (予約か写像に失敗) →
     * probe は NP21/W 判定も ID も読まない (自分で予約・写像しない)。 */
    cfg_15m_high1();
    fake_pref = GFX_PREF_CIRRUS; fake_np2 = 1; np2_calls = 0; fake_surface = 0;
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    CHECK(cirrus_identify());
    CHECK(cirrus_probe() == 0);
    CHECK(glue_probe_calls == 0 && np2_calls == 0);
    CHECK(enable_calls == 0);
    fake_surface = 1;
    /* 識別はポートを叩かない (NP21/W 判定も ID も読まない)。 */
    glue_reset(BANK, BANK_N, LIN, LIN_N);
    np2_calls = 0;
    CHECK(cirrus_identify());
    CHECK(glue_probe_calls == 0 && np2_calls == 0);
    CHECK(enable_calls == 0);
    fake_pref = GFX_PREF_AUTO;
}

/* G5: fixed reservation, mode-driven setup/drawing, and relay rollback. */
static u8 card[0x96000 + PAGE_SIZE] __attribute__((aligned(4096)));
static void mode_drawing(void)
{
    GFX_ScreenInfo info;
    u32 plane, total;
    GfxMode alternate = {632, 472, GFX_FMT_PACKED8, 6, 0, 0, 648};
    CHECK(gfx_backend_cirrus.mode_count == 1 && gfx_backend_cirrus.default_mode == 0);
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_cirrus);
    CHECK(mode->width == 640 && mode->height == 480);
    CHECK(mode->dac_bits == 6 && mode->format == GFX_FMT_PACKED8);
    CHECK(gfx_mode_layout(mode, &plane, &total) && total == MEM_GFX_BB8_SIZE);
    CHECK(gfx_mode_pick(&gfx_backend_cirrus, 1) == mode);
    glue_reset(BANK, BANK_N, V2P(card), sizeof(card));
    wab_glue_xe10.relay = fake_relay;
    fake_surface=1; fake_client.first=V2P(card + 0x4b000)/PAGE_SIZE;
    fake_client.npages=MEM_GFX_BB8_SIZE/PAGE_SIZE;
    s_probed=1; s_probe_ok=1; s_active=0;
    cirrus_init();
    CHECK(s_active && setup_w == 640 && setup_h == 480 && setup_pitch == 640);
    CHECK(fills == 2 && draw_w == 640 && draw_h == 480 && draw_pitch == 640);
    CHECK(gfx_current_height == 480 && gfx_backend_cirrus.bb_pitch == 640);
    CHECK(gfx_backend_cirrus.bb_base == card + 0x4b000);
    CHECK(card[0x96000] == 255 && card[0x96007] == 255 && card[0x96008] == 0);
    CHECK(cirrus_query(&info) == 0 && info.width == 640 && info.height == 480);
    CHECK(info.bpp == 8 && info.format == GFX_FMT_PACKED8);
    CHECK(info.flags == (GFX_CAP_HW_FILL | GFX_CAP_HW_BLT));
    CHECK(info.lease_mask == 0 && info.lease_first == 16 && info.lease_count == 240);
    for (u32 i=0; i<sizeof(info.reserved)/sizeof(info.reserved[0]); i++) CHECK(!info.reserved[i]);
    cirrus_present_rect(639,479,50,50);
    CHECK(draw_dst == 307199 && draw_src == 0x4b000 + 307199);
    CHECK(draw_w == 1 && draw_h == 1 && draw_pitch == 640 && source_pitch == 640);
    CHECK(cirrus_blit(638,478,639,479,30,30) == 0);
    CHECK(draw_dst == 0x4b000 + 478*640 + 638 && draw_src == 0x4b000 + 307199);
    CHECK(draw_w == 1 && draw_h == 1);
    int prior=copies;
    CHECK(cirrus_blit(640,0,0,0,1,1) == 0 && copies == prior);
    CHECK(cirrus_fill_rect(639,479,20,20,37) == 0);
    CHECK(card[0x4b000+307199] == 37 && card[0x96000] == 255);
    idle_rc=-1;
    CHECK(cirrus_fill_rect(0,0,1,1,19) == OS32_ERR_IO && card[0x4b000] == 0);
    idle_rc=0;
    CHECK(cirrus_fill_rect(620,460,30,30,3) == 0);
    CHECK(draw_w == 20 && draw_h == 20 && draw_dst == 0x4b000+460*640+620);
    draw_rc=-1;
    CHECK(cirrus_fill_rect(0,0,20,20,0) == OS32_ERR_IO);
    CHECK(cirrus_blit(0,0,1,1,20,20) == OS32_ERR_IO);
    draw_rc=0;
    /* A different selected row proves reads do not silently use default constants. */
    gfx_backend_cirrus.selected_mode=&alternate;
    cirrus_init();
    CHECK(setup_w == 632 && setup_h == 472 && setup_pitch == 648);
    CHECK(gfx_current_height == 472 && gfx_backend_cirrus.bb_pitch == 648);
    CHECK(cirrus_query(&info) == 0 && info.width == 632 && info.height == 480);
    CHECK(cirrus_fill_rect(630,470,2,2,29) == 0);
    CHECK(card[0x4b000+470*648+630] == 29 && card[0x4b000+470*648+632] == 0);
    cirrus_present_rect(631,471,2,2);
    CHECK(draw_dst == 471*648+631 && draw_pitch == 648 && draw_w == 1 && draw_h == 1);
    CHECK(cirrus_fill_rect(631,471,2,2,29) == 0);
    CHECK(card[0x4b000+471*648+631] == 29 && card[0x4b000+471*648+632] == 0);
    CHECK(cirrus_blit(630,470,631,471,3,3) == 0 && draw_w == 1 && draw_h == 1);
    CHECK(draw_dst == 0x4b000+470*648+630 && source_pitch == 648);
    cirrus_enter(); CHECK(relay_value == 1);
    cirrus_shutdown();
    CHECK(relay_value == 0 && gfx_current_height == GFX_HEIGHT);
    CHECK(gfx_backend_mode(&gfx_backend_cirrus) == mode && s_lin == card);
    CHECK(cirrus_query(&info) == 0 && info.height == 480);
    /* Setup failure is before active: init itself must restore relay and descriptor. */
    setup_rc=-1; prior=relay_calls;
    cirrus_init();
    CHECK(!s_active && !s_probe_ok && !s_lin && !gfx_backend_cirrus.bb_base);
    CHECK(gfx_backend_cirrus.bb_size == 0 && linear_off_calls == 1);
    CHECK(relay_calls == prior+1 && relay_value == 0 && !cirrus_probe());
    output("PASS Cirrus mode setup, BLT/CPU edges, selected pitch, shutdown and failure relay rollback\n");
}

void _start(void)
{
    CHECK(sizeof(u32) == 4);
    bank_window_in_hole();
    output("PASS bank window in the 15-16MB hole (top 17408KB)\n");
    linear_window_placement();
    output("PASS linear window in the v3 aperture band (17MB / 64MB), partial overlap, neighbours\n");
    system_space_is_ram();
    output("PASS system space registered as RAM rejects the bank window\n");
    small_machine();
    output("PASS 8MB machine: both windows free\n");
    map_not_ready();
    output("PASS map not ready: refuse\n");
    window_bounds();
    output("PASS size 0 / 4GB end / overflow\n");
    probe_stages();
    output("PASS probe stages (17MB reaches the ID on NP21/W only; GFX=cirrus forces; RAM blocks; no SURFACE blocks)\n");
    mode_drawing();
    finish(0);
}
