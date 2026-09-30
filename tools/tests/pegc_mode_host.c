/* =========================================================================
 *  PEGC_MODE_HOST.C — PEGC 640x480 へ入る / 戻る OUT 列と GDC の FIFO・VSYNC 待ち
 *
 *  実行: python3 -B tools/tests/test_pegc_mode.py [--mutate]
 *  記録: tools/tests/pegc_mode_tdd.md
 *  票:   docs/tasks/realhw/TASK_PEGC480_REALHW.md (§3-3 比較表、§4、§5)
 *        docs/archive/realhw_v21/TASK_PEGC_RA266_TIMING.md
 *
 *  実物の gfx/backend_pegc.c を 1 行も写さずに #include し、ポート I/O だけを
 *  偽物 (tools/tests/pegc_hostshim/io.h → pegc_shim_inp / pegc_shim_outp) に
 *  する。偽の I/O は IN / OUT / 待ち (cpu_delay_us) を順に記録し、GDC の
 *  ステータス (60h / A0h) はケースごとに「FIFO が詰まっている回数」と VSYNC の
 *  振る舞いを決めて返す。
 *
 *  **受け入れ (A)**: 実機 PC-9821Ra266 の ROM (INT 18h AH=30h) の OUT 列を
 *  `v86 -g` で記録したもの (2026-09-29 20:51、feat/gui d5cd3a5) を**この試験の
 *  中に数値で直に書き** (ROM_S480 / ROM_BACK、pegc.h を見ない)、起動時の状態が
 *  同じ (09A8h 81h・GDC 2.5MHz) ときに実物の backend_pegc.c が出す OUT 列と
 *  1 行ずつ突き合わせる。一致しない行は下の「意図的に違える」表 (EDIT_*・ADD_*)
 *  に理由付きで列挙したものだけで、それ以外は完全一致を要求する。
 *  比べる区間: 入る = pegc_enter_480_ports() の全部、戻る = pegc_text_sync_400()
 *  の全部 (CUI なら末尾の console_hw_cursor_enable の実物の OUT を含む)。
 *  比べないもの: 起動時の記録 (pegc_boot_sync_record) と診断 (pegc_diag_take) —
 *  どちらも 09A0h の選択と読み・09A8h の読みだけ。5Fh (io_wait) は比べない。
 * ========================================================================= */
#include "types.h"

#include "../../gfx/backend_pegc.c"
/* 戻りの後のカーソル復帰 (console_hw_cursor_enable) も実物の OUT を記録する */
/* カーネルは -Wall だけで組む (build/config.mk)。この試験の -Wextra -Werror で
 * console.c 側の既存の警告 2 種を止める (console.c そのものは写さず変えない)。 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "../../kernel/console.c"
#pragma GCC diagnostic pop

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
static const char *s_case = "";
static void fail(const char *message, int line)
{
    char num[12];
    int i = 11;
    num[i] = 0;
    do { num[--i] = (char)('0' + line % 10); line /= 10; } while (line && i);
    output("ASSERT FAIL ["); output(s_case); output("] line ");
    output(num + i); output(": "); output(message); output("\n");
    finish(1);
}
#define CHECK(c) do { if (!(c)) fail(#c, __LINE__); } while (0)

/* ---- 偽の I/O ---- */
#define EV_IN     0
#define EV_OUT    1
#define EV_DELAY  2     /* cpu_delay_us (val = us、port = 0) */
#define EV_MAX 1200000  /* 詰まったまま (fifo_stuck) は 1 バイト 5000 回読んで待つ */
static u16 ev_port[EV_MAX];
static u8  ev_val[EV_MAX];
static u8  ev_kind[EV_MAX];
static int ev_n;

static int fake_full_reads;    /* FIFO FULL を返す残り回数 (-1 = ずっと) */
static int fake_busy_reads;    /* FIFO EMPTY を返さない残り回数 (-1 = ずっと) */
static u8  fake_09a0_sel;
static u8  fake_09a0_clk;      /* 09A0h sel 09h の読み: bit0 = CLK1, bit1 = CLK2 */
static u8  fake_09a8_raw;
static u32 fifo_delays;        /* cpu_delay_us(PEGC_GDC_FIFO_POLL_US) の回数 */
static u32 vsync_delays;       /* cpu_delay_us(PEGC_VSYNC_POLL_US) の回数 */
static int fake_full_after_sync; /* テキスト GDC に SYNC を書いたら FULL をこの回数 */
static int fake_vsync_mode;    /* 0 = 2 回ごとに反転、1 = ずっと 0、2 = ずっと 1 */
static u32 text_stat_reads;
static int fake_con_sink;      /* con_sink_is_enabled の返り値 */
static int fake_v86_active;    /* v86_is_active の返り値 */
static unsigned int fake_busy_port; /* 0 = 両 GDC、それ以外 = そのステータスだけ詰まる */

static void ev_add(int kind, unsigned int port, unsigned int val)
{
    if (ev_n >= EV_MAX) fail("event log overflow", __LINE__);
    ev_kind[ev_n] = (u8)kind;
    ev_port[ev_n] = (u16)port;
    ev_val[ev_n] = (u8)val;
    ev_n++;
}

static u8 gdc_status(unsigned int port)
{
    u8 st = 0;
    int stuck_here = (fake_busy_port == 0U || fake_busy_port == port);
    if (!stuck_here) {
        st |= GDC_STAT_FEMP;
    } else if (fake_full_reads != 0) {
        st |= GDC_STAT_FFUL;
        if (fake_full_reads > 0) fake_full_reads--;
    }
    if (!stuck_here) {
        /* 詰まらない側 */
    } else if (fake_busy_reads != 0) {
        if (fake_busy_reads > 0) fake_busy_reads--;
    } else if (!(st & GDC_STAT_FFUL)) {
        st |= GDC_STAT_FEMP;
    }
    if (port == GDC_TEXT_STAT) {
        int vs = 0;
        if (fake_vsync_mode == 0) vs = (int)((text_stat_reads / 2U) & 1U);
        if (fake_vsync_mode == 2) vs = 1;
        if (vs) st |= GDC_STAT_VSYNC;
        text_stat_reads++;
    }
    return st;
}

unsigned int pegc_shim_inp(unsigned int port)
{
    u8 v = 0;
    if (port == GDC_TEXT_STAT || port == GDC_GFX_STAT) {
        v = gdc_status(port);
    } else if (port == PEGC_STAT_PORT) {
        /* sel 09h: CLK1 は bit0、CLK2 は選択に関係なく bit1。sel 0Ah (拡張
         * モードか) は 0 = 標準 — リニア窓 (MMIO) へは書かせない。 */
        v = (u8)(fake_09a0_clk & PEGC_STAT_RD_GDCCLK2);
        if (fake_09a0_sel == PEGC_STAT_SEL_GDCCLK1) {
            v |= (u8)(fake_09a0_clk & PEGC_STAT_BIT);
        }
    } else if (port == PEGC_HSYNC_PORT) {
        v = fake_09a8_raw;
    }
    ev_add(EV_IN, port, v);
    return v;
}

void pegc_shim_outp(unsigned int port, unsigned int value)
{
    if (port == PEGC_STAT_PORT) fake_09a0_sel = (u8)value;
    if (port == PEGC_HSYNC_PORT) {
        fake_09a8_raw = (u8)((fake_09a8_raw & 0xFCU) | (value & 0x03U));
    }
    if (port == GDC_TEXT_CMD && value == GDC_CMD_SYNC && fake_full_after_sync) {
        fake_full_reads = fake_full_after_sync;
        fake_full_after_sync = 0;
    }
    ev_add(EV_OUT, port, value);
}

void cpu_delay_us(u32 us)
{
    if (us == PEGC_GDC_FIFO_POLL_US) fifo_delays++;
    if (us == PEGC_VSYNC_POLL_US) vsync_delays++;
    ev_add(EV_DELAY, 0, us);
}

int con_sink_is_enabled(void) { return fake_con_sink; }
/* console.c が参照するもの (カーソル復帰の経路では呼ばれないか無害) */
void bootlog_push(const char *buf, u32 len) { (void)buf; (void)len; }
void con_sink_enable(void) { }
void con_sink_disable(void) { }
void con_sink_push_print(const char *buf, u32 len, u8 color) { (void)buf; (void)len; (void)color; }
void con_sink_push_clear(void) { }
void con_sink_push_cursor(int x, int y) { (void)x; (void)y; }
void kbd_inject_discard(void) { }
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
int kutoa_dec(u32 val, char *buf, int bufsz) { (void)val; if (bufsz > 0) buf[0] = 0; return 0; }
int serial_putchar(char c) { (void)c; return 0; }
u16 unicode_to_jis(u32 cp) { (void)cp; return 0; }
u8 unicode_to_ank(u32 cp) { (void)cp; return 0; }
utf8_decode_t utf8_decode(const u8 *src) { utf8_decode_t d; (void)src; kmemset(&d, 0, sizeof(d)); return d; }
int v86_is_active(void) { return fake_v86_active; }

/* ---- 残りの依存 (この試験の経路では呼ばれない、または無害) ---- */
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
/* master AS ではない扱い = bios_flag は 0 を返す (ワークエリアを読まない) */
u32 paging_current_cr3(void) { return 1; }
u32 paging_kernel_pd_phys(void) { return 2; }
int paging_map_phys(u32 v, u32 p, u32 n, u32 f)
{ (void)v; (void)p; (void)n; (void)f; fail("paging_map_phys", __LINE__); return -1; }
int pgalloc_range_has_ram(u32 f, u32 e) { (void)f; (void)e; return 1; }
u32 sys_usable_mem_end(void) { return 0; }
u32 sys_reserve_top(u32 o, u32 b) { (void)o; (void)b; return 0; }
void palette_init(void) { }
const PaletteEntry *palette_get_all(void) { return (const PaletteEntry *)0; }
void palette_shadow_set(int i, u8 r, u8 g, u8 b) { (void)i; (void)r; (void)g; (void)b; }

/* ========================================================================
 *  実機 PC-9821Ra266 の ROM の OUT 列 (受け入れ A の期待値)
 *
 *  出典: `v86 -g` の記録 v86g_ra266_2026-09-29_d5cd3a5.txt の「O」行のうち
 *  pass = hw (実機へ通した) のものを、s480 / back の順のまま全部 (各 94 行)。
 *  pass = emu の行 (063Ch・A46Eh・A660h) は実機へ通していないので載せない
 *  (記録器が偽の読み FFh を返した上での ROM の書き込み — 票 §3-3)。
 *  **include/pegc.h を見ずに数値を直に書く** — ヘッダの値を差し替え損ねても
 *  ここで落ちる。
 * ======================================================================== */
typedef struct { u16 port; u8 val; } PV;

static const PV ROM_S480[] = {
    /*   0 */ {0x6A,0x07}, {0x6A,0x21}, {0x6A,0x06}, {0x6A,0x41}, {0x9A0,0x03}, {0x68,0x0E}, {0x6A,0x83}, {0x6A,0x85},
    /*   8 */ {0x6A,0x07}, {0xA0,0xDF}, {0xA2,0x28}, {0x6A,0x06}, {0xA2,0x00}, {0xA2,0x6E}, {0x62,0x00}, {0x62,0x6F},
    /*  16 */ {0x62,0x0E}, {0x60,0x10}, {0x60,0x4E}, {0x60,0x4B}, {0x60,0x0C}, {0x60,0x03}, {0x60,0x06}, {0x60,0xE0},
    /*  24 */ {0x60,0x95}, {0x62,0x4B}, {0x60,0x0F}, {0x60,0x00}, {0x60,0x7B}, {0xA2,0x0E}, {0xA0,0x02}, {0xA0,0x4E},
    /*  32 */ {0xA0,0x4B}, {0xA0,0x0C}, {0xA0,0x83}, {0xA0,0x06}, {0xA0,0xE0}, {0xA0,0x95}, {0xA2,0x4B}, {0xA0,0x00},
    /*  40 */ {0xA0,0x00}, {0xA0,0x01}, {0xA2,0x00}, {0x62,0x00}, {0xA2,0x05}, {0x62,0x05}, {0xA2,0x47}, {0xA0,0x50},
    /*  48 */ {0xA2,0x46}, {0xA0,0x00}, {0xA2,0x70}, {0xA0,0x00}, {0xA0,0x00}, {0xA0,0xF0}, {0xA0,0x7F}, {0x62,0x47},
    /*  56 */ {0x60,0x50}, {0x62,0x46}, {0x60,0x00}, {0x62,0x70}, {0x60,0x00}, {0x60,0x00}, {0x60,0xF0}, {0x60,0x3F},
    /*  64 */ {0xA2,0x20}, {0xA2,0x78}, {0xA0,0x00}, {0xA0,0x00}, {0xA2,0x6B}, {0x62,0x6B}, {0xA2,0x05}, {0x62,0x05},
    /*  72 */ {0xA2,0x6B}, {0x62,0x6B}, {0xA2,0x05}, {0x62,0x05}, {0x68,0x08}, {0x6A,0x07}, {0x6A,0x69}, {0x6A,0x06},
    /*  80 */ {0x6E,0x03}, {0x6E,0x21}, {0x6E,0x02}, {0x70,0x00}, {0x72,0x0F}, {0x74,0x10}, {0x76,0x00}, {0x78,0x01},
    /*  88 */ {0x7A,0x00}, {0x68,0x0F}, {0x6E,0x03}, {0x68,0x07}, {0x68,0x00}, {0x6E,0x02}
};
static const PV ROM_BACK[] = {
    /*   0 */ {0x6A,0x07}, {0x6A,0x20}, {0x6A,0x06}, {0x6A,0x41}, {0x9A0,0x03}, {0x68,0x0E}, {0x6A,0x82}, {0x6A,0x84},
    /*   8 */ {0x6A,0x07}, {0xA0,0xDF}, {0xA2,0x28}, {0x6A,0x06}, {0xA2,0x00}, {0xA2,0x6E}, {0x62,0x00}, {0x62,0x6F},
    /*  16 */ {0x62,0x0E}, {0x60,0x10}, {0x60,0x4E}, {0x60,0x47}, {0x60,0x0C}, {0x60,0x07}, {0x60,0x0D}, {0x60,0x90},
    /*  24 */ {0x60,0x89}, {0x62,0x4B}, {0x60,0x0F}, {0x60,0x00}, {0x60,0x7B}, {0xA2,0x0E}, {0xA0,0x02}, {0xA0,0x26},
    /*  32 */ {0xA0,0x41}, {0xA0,0x0C}, {0xA0,0x83}, {0xA0,0x0D}, {0xA0,0x90}, {0xA0,0x89}, {0xA2,0x4B}, {0xA0,0x00},
    /*  40 */ {0xA0,0x00}, {0xA0,0x01}, {0xA2,0x00}, {0x62,0x00}, {0xA2,0x05}, {0x62,0x05}, {0xA2,0x47}, {0xA0,0x28},
    /*  48 */ {0xA2,0x46}, {0xA0,0x00}, {0xA2,0x70}, {0xA0,0x00}, {0xA0,0x00}, {0xA0,0xF0}, {0xA0,0x3F}, {0x62,0x47},
    /*  56 */ {0x60,0x50}, {0x62,0x46}, {0x60,0x00}, {0x62,0x70}, {0x60,0x00}, {0x60,0x00}, {0x60,0xF0}, {0x60,0x3F},
    /*  64 */ {0xA2,0x20}, {0xA2,0x78}, {0xA0,0x00}, {0xA0,0x00}, {0xA2,0x6B}, {0x62,0x6B}, {0xA2,0x05}, {0x62,0x05},
    /*  72 */ {0xA2,0x6B}, {0x62,0x6B}, {0xA2,0x05}, {0x62,0x05}, {0x68,0x09}, {0x6A,0x07}, {0x6A,0x68}, {0x6A,0x06},
    /*  80 */ {0x6E,0x03}, {0x6E,0x21}, {0x6E,0x02}, {0x70,0x00}, {0x72,0x0F}, {0x74,0x10}, {0x76,0x00}, {0x78,0x01},
    /*  88 */ {0x7A,0x00}, {0x68,0x0F}, {0x6E,0x03}, {0x68,0x07}, {0x68,0x00}, {0x6E,0x02}
};
#define ROM_N ((int)(sizeof(ROM_S480) / sizeof(ROM_S480[0])))

/* ---- 意図的に違える行 (理由は票 TASK_PEGC480_REALHW §3-3 と backend_pegc.c
 *      pegc_apply_timing の注記) ----
 *  DROP  ROM の行を OS32 は出さない
 *  SET   ROM の行の値を OS32 は別の値で出す
 *  ADD_* ROM の列の末尾の後に OS32 が足す
 *  OS32 側の OUT のうち 5Fh (io_wait) は比べない — v86 -g の記録器は 5Fh を
 *  捕まえない (票 §3 段 1「素通しのまま捕まえないポート」) ので ROM が挟んで
 *  いたかは記録から分からない。6Eh の後に 5Fh があることは別に見る。 */
#define E_DROP 1
#define E_SET  2
typedef struct { int kind; int idx; u8 val; const char *why; } Edit;

static const Edit EDIT_COMMON[] = {
    { E_DROP,  4, 0, "09A0h 03h: ROM reads DISP ENABLE; OS32 always ends with 68h 0Fh" },
    /* ---- 未解明の差分 (Codex P2、2026-09-29): ROM がこの 4 行で何をしているかは
     * 資料から決まらない。[U] io_disp.md は A0h/A2h をグラフィック GDC の
     * パラメータ/コマンドとしか書かず、A2h 28h は WRITE (20h〜3Fh) — [B] 2-6
     * 表2-26 の「GDC 描画制御コマンド」= [HW1] が禁じる類。6Ah 07h/06h は
     * [U] 006Ah 0000011nb「(*1) の F/F 変更 許可/禁止」・[B] 表3-2「拡張モード
     * 変更可/不可」で、A0h/A2h の意味を変えるとはどこにも無い。パラメータ DFh が
     * コマンドより先に来るのは [B] 2-6「コマンド → パラメータ」の手順の外で、
     * そのときの GDC の状態 (直前のコマンド) 次第 = OS32 からは再現できない。
     * よって送らない。**RESET1 で打ち消されるとは言えない** (RESET の前に処理
     * された設定が残るかは資料に無い)。実機で表示が直らなければ最初に疑う差分。 */
    { E_DROP,  8, 0, "UNRESOLVED: 6Ah 07h before A0h DFh / A2h 28h" },
    { E_DROP,  9, 0, "UNRESOLVED: A0h DFh, a parameter before any command (meaning not in [U]/[B])" },
    { E_DROP, 10, 0, "UNRESOLVED: A2h 28h = GDC WRITE ([B] table 2-26 drawing control, [HW1])" },
    { E_DROP, 11, 0, "UNRESOLVED: 6Ah 06h after the pair" },
    { E_DROP, 64, 0, "A2h 20h: GDC WRITE, [HW1]" },
    { E_DROP, 65, 0, "A2h 78h: GDC TEXTW (pattern), [HW1]" },
    { E_DROP, 66, 0, "TEXTW parameter" },
    { E_DROP, 67, 0, "TEXTW parameter" }
};
#define N_EDIT_COMMON ((int)(sizeof(EDIT_COMMON) / sizeof(EDIT_COMMON[0])))
static const Edit EDIT_BACK[] = {
    { E_SET, 76, 0x08, "68h 09h (BIOS 200-line graphics) -> 08h: OS32 400-line (gfx_core.c)" }
};
/* ROM は両 GDC を STOP2 のまま返す (表示開始は呼び手の AH=40h / 0Ch)。
 * OS32 は呼び手を兼ねる: 480 は両方、戻りはテキストだけ START (6Bh)。 */
static const PV ADD_S480[] = { {0xA2,0x6B}, {0x62,0x6B} };
static const PV ADD_BACK[] = { {0x62,0x6B} };
/* CUI へ戻るとき (GUI のシンクが無効で V86 中でない) は、続けて実物の
 * console_hw_cursor_enable() (kernel/console.c) がカーソルを戻す OUT を出す:
 * CSRFORM (DC=1 | L/R 0Fh、上端 14、下端 15<<3 | 3) と CSRW (論理カーソルの
 * 番地)。ROM の CSRFORM がカーソル非表示 (CS=0) なので要る — 意図的な追加。
 * 試験は論理カーソルを (5, 3) に置く = 番地 3*80+5 = 00F5h。 */
#define XCUR_X 5
#define XCUR_Y 3
static const PV ADD_CURSOR[] = {
    {0x62,0x4B}, {0x60,0x8F}, {0x60,0x0E}, {0x60,0x7B},
    {0x62,0x49}, {0x60,0xF5}, {0x60,0x00}
};

static u16 xp[512];
static u8  xv[512];
static int xn;

static void x_reset(void) { xn = 0; }
static void x_out(unsigned int port, unsigned int val)
{
    if (xn >= 512) fail("expected list overflow", __LINE__);
    xp[xn] = (u16)port;
    xv[xn] = (u8)val;
    xn++;
}

static void x_cursor(void)
{
    int k;
    for (k = 0; k < (int)(sizeof(ADD_CURSOR) / sizeof(ADD_CURSOR[0])); k++)
        x_out(ADD_CURSOR[k].port, ADD_CURSOR[k].val);
}

/* ROM の列に EDIT を当てて期待列を組む。insert_after >= 0 なら ROM の
 * その行の後に (port, val) を 1 行差し込む (起動時 24kHz の 09A8h)。 */
static void x_from_rom(const PV *rom, const Edit *extra, int n_extra,
                       const PV *add, int n_add,
                       int insert_after, u16 ins_port, u8 ins_val)
{
    int i, k;
    x_reset();
    for (i = 0; i < ROM_N; i++) {
        int drop = 0;
        u8 v = rom[i].val;
        for (k = 0; k < N_EDIT_COMMON; k++) {
            if (EDIT_COMMON[k].idx == i && EDIT_COMMON[k].kind == E_DROP) drop = 1;
        }
        for (k = 0; k < n_extra; k++) {
            if (extra[k].idx != i) continue;
            if (extra[k].kind == E_DROP) drop = 1;
            if (extra[k].kind == E_SET) v = extra[k].val;
        }
        if (!drop) x_out(rom[i].port, v);
        if (i == insert_after) x_out(ins_port, ins_val);
    }
    for (k = 0; k < n_add; k++) x_out(add[k].port, add[k].val);
}

static void put_hex(char *m, int *j, unsigned int v, int digits)
{
    static const char hx[] = "0123456789ABCDEF";
    while (digits-- > 0) m[(*j)++] = hx[(v >> (digits * 4)) & 15U];
}

/* 記録の OUT (5Fh を除く) を期待列と突き合わせる */
static void check_outs(void)
{
    int i, k = 0;
    for (i = 0; i < ev_n; i++) {
        if (ev_kind[i] != EV_OUT || ev_port[i] == PC98_IO_WAIT_PORT) continue;
        if (k >= xn) fail("more OUTs than expected", __LINE__);
        if (ev_port[i] != xp[k] || ev_val[i] != xv[k]) {
            char m[64];
            int j = 0;
            const char *t = "OUT differs at #";
            while (*t) m[j++] = *t++;
            put_hex(m, &j, (unsigned int)k, 3);
            m[j++] = ' '; m[j++] = 'g'; m[j++] = 'o'; m[j++] = 't'; m[j++] = ' ';
            put_hex(m, &j, ev_port[i], 3); m[j++] = ':'; put_hex(m, &j, ev_val[i], 2);
            m[j++] = ' '; m[j++] = 'w'; m[j++] = 'a'; m[j++] = 'n'; m[j++] = 't'; m[j++] = ' ';
            put_hex(m, &j, xp[k], 3); m[j++] = ':'; put_hex(m, &j, xv[k], 2);
            m[j] = 0;
            fail(m, __LINE__);
        }
        k++;
    }
    if (k != xn) fail("fewer OUTs than expected", __LINE__);
}

static int is_gdc_cmd(u16 p) { return p == GDC_TEXT_CMD || p == GDC_GFX_CMD; }
static int is_gdc_prm(u16 p) { return p == GDC_TEXT_PARAM || p == GDC_GFX_PARAM; }
static u16 stat_of(u16 p)
{
    return (p == GDC_TEXT_CMD || p == GDC_TEXT_PARAM) ? GDC_TEXT_STAT : GDC_GFX_STAT;
}

/* GDC への書き込みの直前の事象は、その GDC のステータスの読みで、条件を
 * 満たしている (fifo_ok = 1) か、「読み → FIFO の待ち」を上限回繰り返した
 * (fifo_ok = 0) こと。 */
static void check_fifo_gate(int fifo_ok)
{
    int i, gdc_writes = 0, end = -1;
    /* 見るのは backend_pegc.c が出す分 (最後の 62h 6Bh まで)。その後の
     * console_hw_cursor_enable は従来から FIFO を見ずに書く (範囲外)。 */
    for (i = 0; i < ev_n; i++) {
        if (ev_kind[i] == EV_OUT && ev_port[i] == GDC_TEXT_CMD &&
            ev_val[i] == PEGC_GDC_CMD_START2) end = i;
    }
    for (i = 0; i <= end; i++) {
        u16 p = ev_port[i];
        if (ev_kind[i] != EV_OUT || !(is_gdc_cmd(p) || is_gdc_prm(p))) continue;
        gdc_writes++;
        CHECK(i > 0);
        if (fifo_ok) {
            CHECK(ev_kind[i - 1] == EV_IN && ev_port[i - 1] == stat_of(p));
            if (is_gdc_cmd(p)) CHECK(ev_val[i - 1] & GDC_STAT_FEMP);
            else               CHECK(!(ev_val[i - 1] & GDC_STAT_FFUL));
        } else {
            int j = i - 1, reads = 0;
            while (j >= 1 && ev_kind[j] == EV_DELAY &&
                   ev_val[j] == PEGC_GDC_FIFO_POLL_US &&
                   ev_kind[j - 1] == EV_IN && ev_port[j - 1] == stat_of(p)) {
                reads++;
                j -= 2;
            }
            CHECK(reads == PEGC_GDC_FIFO_POLLS);
        }
    }
    CHECK(gdc_writes > 0);
}

static int count_gdc_writes(void)
{
    int i, n = 0;
    for (i = 0; i < ev_n; i++) {
        if (ev_kind[i] == EV_OUT && (is_gdc_cmd(ev_port[i]) || is_gdc_prm(ev_port[i]))) n++;
    }
    return n;
}

/* 6Eh の書き込みの直後は必ず 5Fh ([U] io_disp.md I/O 006Eh「OUT 5Fh,AL による
 * ウェイトが必要」)。 */
static void check_xattr_wait(void)
{
    int i, n = 0;
    for (i = 0; i < ev_n; i++) {
        if (ev_kind[i] != EV_OUT || ev_port[i] != PEGC_XATTR_PORT) continue;
        CHECK(i + 1 < ev_n && ev_kind[i + 1] == EV_OUT &&
              ev_port[i + 1] == PC98_IO_WAIT_PORT);
        n++;
    }
    CHECK(n > 0);
}

/* VSYNC 待ちの位置: 期待列の k 番目と k+1 番目の OUT の間で、テキスト GDC の
 * ステータスの VSYNC が 1 → 0 に落ちた回数を数える (fake_vsync_mode = 0 の
 * 2 回ごと反転)。FIFO の待ちは詰まっていなければ 1 回しか読まないので、
 * 落ちを作れない。 */
static void vsync_falls_by_gap(int *falls, int n_outs)
{
    int i, k = -1, prev = -1;
    for (i = 0; i < n_outs; i++) falls[i] = 0;
    for (i = 0; i < ev_n; i++) {
        if (ev_kind[i] == EV_OUT && ev_port[i] != PC98_IO_WAIT_PORT) {
            k++;
            prev = -1;
            continue;
        }
        if (ev_kind[i] == EV_IN && ev_port[i] == GDC_TEXT_STAT) {
            int vs = (ev_val[i] & GDC_STAT_VSYNC) ? 1 : 0;
            if (prev == 1 && vs == 0 && k >= 0 && k < n_outs) falls[k]++;
            prev = vs;
        }
    }
}

static void world_reset(void)
{
    ev_n = 0;
    fake_full_reads = 0;
    fake_busy_reads = 0;
    fake_09a0_sel = 0;
    fake_09a0_clk = 0;
    fake_09a8_raw = 0;
    fifo_delays = 0;
    vsync_delays = 0;
    fake_full_after_sync = 0;
    fake_vsync_mode = 0;
    text_stat_reads = 0;
    fake_con_sink = 0;
    fake_v86_active = 0;
    cursor_x = XCUR_X;
    cursor_y = XCUR_Y;
    fake_busy_port = 0;
    pegc_gdc_fifo_timeouts = 0;
    pegc_vsync_timeouts = 0;
    s_boot_recorded = 0;
    s_boot_hsync = -1;
    s_boot_clk1 = -1;
    s_boot_clk2 = -1;
    s_probed = 1;
    s_probe_ok = 1;
    s_active = 0;
}

/* 起動時の記録を偽の読みで走らせる (pegc_prepare の一部) */
static void boot_with(u8 hs_raw, u8 clk)
{
    fake_09a8_raw = hs_raw;
    fake_09a0_clk = clk;
    pegc_boot_sync_record();
    ev_n = 0;
    fifo_delays = vsync_delays = 0;
}

/* 起動時が実機 Ra266 と同じ (boot.log: 09a8=81、gdcclk=2.5M clk1=0 clk2=0) */
#define RA266_09A8 0x81
#define RA266_CLK  0x00

/* ========================================================================
 *  独立の書き下し: 表示モードの順序 (pegc_apply_timing の注記の 1〜13) を
 *  試験側でもう一度書く。値は引数で。Ra266 の値を入れたものが ROM の記録
 *  (EDIT 適用後) と一致することを rom_* で確かめ、その上で記録の無い組
 *  (24kHz・5MHz・probe なし) の期待列に使う。
 * ======================================================================== */
typedef struct {
    int pegc, set_clock, write_hs;
    u8 ext, vram, xattr, hs, clk1, clk2, pitch;
    const u8 *ms, *ss, *sc;
} XMode;

static void x_gdc(unsigned int cmd_port, unsigned int prm_port, u8 cmd,
                  const u8 *para, int n)
{
    int i;
    x_out(cmd_port, cmd);
    for (i = 0; i < n; i++) x_out(prm_port, para[i]);
}
static void x_ff2(u8 v)
{
    x_out(0x6A, 0x07);
    x_out(0x6A, v);
    x_out(0x6A, 0x06);
}
static void x_mode(const XMode *m)
{
    static const u8 tcsr[3] = { 0x0F, 0x00, 0x7B };
    static const u8 gcsr[3] = { 0x00, 0x00, 0x01 };
    static const u8 tsc[4]  = { 0x00, 0x00, 0xF0, 0x3F };
    static const u8 zero[1] = { 0x00 };
    static const u8 tp[1]   = { 80 };
    static const u8 crtc[6] = { 0x00, 0x0F, 0x10, 0x00, 0x01, 0x00 };
    int i;
    if (m->pegc) { x_ff2(m->ext); x_out(0x6A, 0x41); }
    x_out(0x68, 0x0E);
    if (m->write_hs) x_out(0x9A8, m->hs);
    if (m->set_clock) { x_out(0x6A, m->clk1); x_out(0x6A, m->clk2); }
    x_out(0xA2, 0x00); x_out(0xA2, 0x6E);
    x_out(0x62, 0x00); x_out(0x62, 0x6F);
    x_gdc(0x62, 0x60, 0x0E, m->ms, 8);
    x_gdc(0x62, 0x60, 0x4B, tcsr, 3);
    x_gdc(0xA2, 0xA0, 0x0E, m->ss, 8);
    x_gdc(0xA2, 0xA0, 0x4B, gcsr, 3);
    x_out(0xA2, 0x00); x_out(0x62, 0x00);
    x_out(0xA2, 0x05); x_out(0x62, 0x05);
    if (m->set_clock) x_gdc(0xA2, 0xA0, 0x47, &m->pitch, 1);
    x_gdc(0xA2, 0xA0, 0x46, zero, 1);
    x_gdc(0xA2, 0xA0, 0x70, m->sc, 4);
    x_gdc(0x62, 0x60, 0x47, tp, 1);
    x_gdc(0x62, 0x60, 0x46, zero, 1);
    x_gdc(0x62, 0x60, 0x70, tsc, 4);
    x_out(0xA2, 0x6B); x_out(0x62, 0x6B);
    x_out(0xA2, 0x05); x_out(0x62, 0x05);
    x_out(0xA2, 0x6B); x_out(0x62, 0x6B);
    x_out(0xA2, 0x05); x_out(0x62, 0x05);
    x_out(0x68, 0x08);
    if (m->pegc) {
        x_ff2(m->vram);
        x_out(0x6E, 0x03); x_out(0x6E, m->xattr); x_out(0x6E, 0x02);
    }
    for (i = 0; i < 6; i++) x_out((unsigned int)(0x70 + i * 2), crtc[i]);
    x_out(0x68, 0x0F);
    if (m->pegc) x_out(0x6E, 0x03);
    x_out(0x68, 0x07);
    x_out(0x68, 0x00);
    if (m->pegc) x_out(0x6E, 0x02);
}

/* 実機の記録にある SYNC・SCROLL の数値 (試験側に直に書く) */
static const u8 R_MS480[8]   = { 0x10, 0x4E, 0x4B, 0x0C, 0x03, 0x06, 0xE0, 0x95 };
static const u8 R_SS480[8]   = { 0x02, 0x4E, 0x4B, 0x0C, 0x83, 0x06, 0xE0, 0x95 };
static const u8 R_MS31K[8]   = { 0x10, 0x4E, 0x47, 0x0C, 0x07, 0x0D, 0x90, 0x89 };
static const u8 R_SS31K_L[8] = { 0x02, 0x26, 0x41, 0x0C, 0x83, 0x0D, 0x90, 0x89 };
static const u8 R_SC480[4]   = { 0x00, 0x00, 0xF0, 0x7F };
static const u8 R_SC400L[4]  = { 0x00, 0x00, 0xF0, 0x3F };
/* 記録の無い組: [B] 2-6 表2-27 (24kHz) と NP21/W 31-M、IM は [B] 2-7 */
static const u8 B_MS24[8]    = { 0x10, 0x4E, 0x07, 0x25, 0x07, 0x07, 0x90, 0x65 };
static const u8 B_SS24_L[8]  = { 0x02, 0x26, 0x03, 0x11, 0x83, 0x07, 0x90, 0x65 };
static const u8 B_SS24_M[8]  = { 0x02, 0x4E, 0x07, 0x25, 0x87, 0x07, 0x90, 0x65 };
static const u8 N_SS31K_M[8] = { 0x02, 0x4E, 0x47, 0x0C, 0x87, 0x0D, 0x90, 0x89 };
static const u8 B_SC400M[4]  = { 0x00, 0x00, 0xF0, 0x7F };

/* ---- ケース ---- */

/* 1. 受け入れ A: 480 へ入る列 = 実機の ROM の s480 (EDIT 適用) */
static void case_rom_s480(void)
{
    int falls[128], i;
    XMode m;
    s_case = "rom_s480";
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    pegc_enter_480_ports();

    x_from_rom(ROM_S480, (const Edit *)0, 0, ADD_S480, 2, -1, 0, 0);
    check_outs();
    check_fifo_gate(1);
    check_xattr_wait();
    CHECK(pegc_gdc_fifo_timeouts == 0 && pegc_vsync_timeouts == 0);
    for (i = 0; i < ev_n; i++) {
        /* 09A8h は書かない (実機の ROM も起動時 31kHz なので書いていない) */
        CHECK(!(ev_kind[i] == EV_OUT && ev_port[i] == PEGC_HSYNC_PORT));
        /* emu 行のポート (063Ch・A46Eh・A660h) にも書かない */
        CHECK(!(ev_kind[i] == EV_OUT && (ev_port[i] == 0x063C ||
               ev_port[i] == 0xA46E || ev_port[i] == 0xA660)));
    }
    /* VSYNC 待ちの位置 = 実機の ROM が 60h を 1 フレーム前後読み続けた所:
     * グラフィック RESET の前 1 回、SLAVE の後 1 回、RESET ×2 の後 2 回、テキスト SCROLL の後 1 回、
     * START の後 1 回、CRTC の後 1 回。期待列の添字 = ROM の添字 − それより
     * 前の DROP の数 (4, 8〜11 で 5 行、64〜67 も含めると 9 行)。 */
    vsync_falls_by_gap(falls, xn);
    {
        int n_total = 0;
        for (i = 0; i < xn; i++) n_total += falls[i];
        CHECK(n_total == 7);
        CHECK(falls[7 - 1] == 1);           /* 6Ah 85h の後 (ROM は 6Ah 06h の後) */
        CHECK(falls[13 - 5] == 1);          /* A2h 6Eh の後 */
        CHECK(falls[43 - 5] == 2);          /* 62h 00h (2 回目の RESET) の後 */
        CHECK(falls[63 - 5] == 1);          /* テキスト SCROLL の最後の後 */
        CHECK(falls[69 - 9] == 1);          /* 62h 6Bh の後 */
        CHECK(falls[88 - 9] == 1);          /* 7Ah の後 */
    }
    /* 独立の書き下しも同じ列になる */
    m.pegc = 1; m.set_clock = 1; m.write_hs = 0;
    m.ext = 0x21; m.vram = 0x69; m.xattr = 0x21; m.hs = 0x01;
    m.clk1 = 0x83; m.clk2 = 0x85; m.pitch = 80;
    m.ms = R_MS480; m.ss = R_SS480; m.sc = R_SC480;
    x_reset();
    x_mode(&m);
    x_out(0xA2, 0x6B); x_out(0x62, 0x6B);
    check_outs();
}

/* 2. 受け入れ A: 戻る列 = 実機の ROM の back (EDIT 適用)。pegc_shutdown の口
 *    (pegc_text_sync_400) と v86 -g の FALLBACK の口 (pegc_restore_text_sync) */
static void case_rom_back(void)
{
    int i;
    XMode m;
    s_case = "rom_back";
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    CHECK(s_boot_hsync == PEGC_HSYNC_31KHZ);
    CHECK(s_boot_clk1 == 0 && s_boot_clk2 == 0);
    pegc_text_sync_400(pegc_restore_hsync());
    /* CUI: ROM の back + テキスト START + カーソル復帰 (実物の console.c の OUT) */
    x_from_rom(ROM_BACK, EDIT_BACK, 1, ADD_BACK, 1, -1, 0, 0);
    x_cursor();
    check_outs();
    check_fifo_gate(1);
    check_xattr_wait();

    /* GUI から抜ける途中 (シンク有効): カーソルは console_text_gdc_start に任せる */
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    fake_con_sink = 1;
    pegc_text_sync_400(pegc_restore_hsync());
    x_from_rom(ROM_BACK, EDIT_BACK, 1, ADD_BACK, 1, -1, 0, 0);
    check_outs();

    /* v86 -g の FALLBACK の口 (V86 中 = console が GDC に触らない)。先頭に
     * 09A0h の選択と読み (拡張モードか) が入る */
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    fake_v86_active = 1;
    pegc_restore_text_sync(1);
    x_from_rom(ROM_BACK, EDIT_BACK, 1, ADD_BACK, 1, -1, 0, 0);
    {
        int k = 0;
        for (i = 0; i < ev_n; i++) {
            if (ev_kind[i] != EV_OUT || ev_port[i] == PC98_IO_WAIT_PORT) continue;
            if (ev_port[i] == PEGC_STAT_PORT && k == 0) continue;
            CHECK(k < xn);
            CHECK(ev_port[i] == xp[k] && ev_val[i] == xv[k]);
            k++;
        }
        CHECK(k == xn);
    }

    m.pegc = 1; m.set_clock = 1; m.write_hs = 0;
    m.ext = 0x20; m.vram = 0x68; m.xattr = 0x21; m.hs = 0x01;
    m.clk1 = 0x82; m.clk2 = 0x84; m.pitch = 40;
    m.ms = R_MS31K; m.ss = R_SS31K_L; m.sc = R_SC400L;
    x_reset();
    x_mode(&m);
    x_out(0x62, 0x6B);
    x_cursor();
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    pegc_text_sync_400(pegc_restore_hsync());
    check_outs();
}

/* 3. 起動時 24kHz (NP21/W): 実機の s480 に「68h 0Eh の後に 09A8h 01h」を
 *    足した列。戻りは 24kHz の組 (資料) で、09A8h 00h を書く。 */
static void case_enter_24k(void)
{
    XMode m;
    s_case = "enter_24k";
    world_reset();
    boot_with(0x00, 0x00);
    pegc_enter_480_ports();
    x_from_rom(ROM_S480, (const Edit *)0, 0, ADD_S480, 2, 5, 0x9A8, 0x01);
    check_outs();
    check_fifo_gate(1);

    /* 戻り (09A8h は今 31kHz を読むので 00h を書く) */
    ev_n = 0;
    pegc_text_sync_400(pegc_restore_hsync());
    m.pegc = 1; m.set_clock = 1; m.write_hs = 1;
    m.ext = 0x20; m.vram = 0x68; m.xattr = 0x20; m.hs = 0x00;
    m.clk1 = 0x82; m.clk2 = 0x84; m.pitch = 40;
    m.ms = B_MS24; m.ss = B_SS24_L; m.sc = R_SC400L;
    x_reset();
    x_mode(&m);
    x_out(0x62, 0x6B);
    x_cursor();
    check_outs();
}

/* 4. 戻り: 起動時 24kHz・両方 5MHz → 83h, 85h・24-M・PITCH 80・IM=1 */
static void case_restore_24k_5m(void)
{
    XMode m;
    s_case = "restore_24k_5m";
    world_reset();
    boot_with(0x00, 0x03);
    CHECK(s_boot_clk1 == 1 && s_boot_clk2 == 1);
    CHECK(pegc_restore_pitch() == 80);
    pegc_text_sync_400(pegc_restore_hsync());
    m.pegc = 1; m.set_clock = 1; m.write_hs = 0;
    m.ext = 0x20; m.vram = 0x68; m.xattr = 0x20; m.hs = 0x00;
    m.clk1 = 0x83; m.clk2 = 0x85; m.pitch = 80;
    m.ms = B_MS24; m.ss = B_SS24_M; m.sc = B_SC400M;
    x_reset();
    x_mode(&m);
    x_out(0x62, 0x6B);
    x_cursor();
    check_outs();
    check_fifo_gate(1);
}

/* 5. 起動時のクロックの読み分け: 片方だけ 5MHz は 2.5MHz ([U] 006Ah 82h の注) */
static void case_boot_clock(void)
{
    XMode m;
    s_case = "boot_clock";
    world_reset();
    boot_with(0x00, 0x01);
    CHECK(s_boot_clk1 == 1 && s_boot_clk2 == 0);
    CHECK(pegc_restore_pitch() == 40);
    pegc_text_sync_400(PEGC_HSYNC_24KHZ);
    m.pegc = 1; m.set_clock = 1; m.write_hs = 0;
    m.ext = 0x20; m.vram = 0x68; m.xattr = 0x20; m.hs = 0x00;
    m.clk1 = 0x83; m.clk2 = 0x84; m.pitch = 40;
    m.ms = B_MS24; m.ss = B_SS24_L; m.sc = R_SC400L;
    x_reset();
    x_mode(&m);
    x_out(0x62, 0x6B);
    x_cursor();
    check_outs();

    world_reset();
    boot_with(0x00, 0x02);
    CHECK(s_boot_clk1 == 0 && s_boot_clk2 == 1);
    CHECK(pegc_restore_pitch() == 40);

    /* 記録の読み: 09A0h へ 09h を書いてから読む */
    world_reset();
    fake_09a0_clk = 0x03;
    pegc_boot_sync_record();
    {
        int i, sel_at = -1, rd_at = -1;
        for (i = 0; i < ev_n; i++) {
            if (ev_kind[i] == EV_OUT && ev_port[i] == PEGC_STAT_PORT &&
                ev_val[i] == 0x09) sel_at = i;
            if (ev_kind[i] == EV_IN && ev_port[i] == PEGC_STAT_PORT && sel_at >= 0 &&
                rd_at < 0) rd_at = i;
        }
        CHECK(sel_at >= 0 && rd_at > sel_at);
    }
    /* 2 回目は何もしない (起動時の値を 480 の後で読み直さない) */
    ev_n = 0;
    fake_09a0_clk = 0x00;
    pegc_boot_sync_record();
    CHECK(ev_n == 0);
    CHECK(s_boot_clk1 == 1 && s_boot_clk2 == 1);
}

/* 6. 記録が無い (PEGC の probe が通っていない) 戻り: 6Ah・6Eh・09A0h に触らず、
 *    クロック・PITCH にも触らない。SYNC は従来の組 (5MHz 用 + IM=0)。 */
static void case_restore_unrecorded(void)
{
    XMode m;
    int i;
    s_case = "restore_unrecorded";
    world_reset();
    s_probed = 1;
    s_probe_ok = 0;
    fake_09a8_raw = 0xFF;               /* ポートが無い機種の読み */
    pegc_restore_text_sync(0);
    m.pegc = 0; m.set_clock = 0; m.write_hs = 1;
    m.ext = 0; m.vram = 0; m.xattr = 0; m.hs = 0x00;
    m.clk1 = 0; m.clk2 = 0; m.pitch = 0;
    m.ms = B_MS24; m.ss = B_SS24_M; m.sc = R_SC400L;
    x_reset();
    x_mode(&m);
    x_out(0x62, 0x6B);
    x_cursor();
    check_outs();
    check_fifo_gate(1);
    for (i = 0; i < ev_n; i++) {
        CHECK(!(ev_kind[i] == EV_OUT && (ev_port[i] == 0x6A || ev_port[i] == 0x6E ||
                                         ev_port[i] == PEGC_STAT_PORT)));
    }
}

/* 7. 09A8h へは bit1,0 だけ ([U] io_disp.md 09A8h「bit 7〜2 は常に 0」)、
 *    今の読みと同じなら書かない */
static void case_hsync_bits(void)
{
    int i, seen = 0;
    u8 vals[4];
    s_case = "hsync_bits";
    world_reset();
    boot_with(0xFE, 0x00);               /* D0 = 0 (24kHz)、上位は全部 1 */
    CHECK(s_boot_hsync == PEGC_HSYNC_24KHZ);
    pegc_enter_480_ports();
    pegc_text_sync_400(pegc_restore_hsync());
    for (i = 0; i < ev_n; i++) {
        if (ev_kind[i] == EV_OUT && ev_port[i] == PEGC_HSYNC_PORT) {
            CHECK((ev_val[i] & ~PEGC_HSYNC_MASK) == 0);
            CHECK(seen < 4);
            vals[seen++] = ev_val[i];
        }
    }
    CHECK(seen == 2 && vals[0] == 0x01 && vals[1] == 0x00);

    world_reset();
    boot_with(0xFF, 0x00);               /* 31kHz のまま入って戻る = 書かない */
    pegc_enter_480_ports();
    pegc_text_sync_400(pegc_restore_hsync());
    for (i = 0; i < ev_n; i++) {
        CHECK(!(ev_kind[i] == EV_OUT && ev_port[i] == PEGC_HSYNC_PORT));
    }
}

/* 8. FIFO が一時的に詰まる: 詰まっている間は書かず、待ちを挟んで読み直す */
static void case_fifo_busy(void)
{
    int n_ok;
    s_case = "fifo_busy";
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    pegc_enter_480_ports();
    n_ok = count_gdc_writes();

    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    fake_busy_port = GDC_GFX_STAT;   /* 最初の GDC コマンドはグラフィックの RESET */
    fake_full_reads = 3;
    fake_busy_reads = 5;
    pegc_enter_480_ports();
    CHECK(count_gdc_writes() == n_ok);
    check_fifo_gate(1);
    CHECK(fifo_delays == 5);            /* 最初のコマンドの前に 5 回待つ */
    CHECK(pegc_gdc_fifo_timeouts == 0);
    x_from_rom(ROM_S480, (const Edit *)0, 0, ADD_S480, 2, -1, 0, 0);
    check_outs();

    /* パラメータの途中で FULL: テキストの SYNC を書いた直後から 4 回 FULL。
     * 最初のパラメータはその 4 回の後に書く。 */
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    fake_full_after_sync = 4;
    pegc_apply_timing(&s_timing_480);
    check_fifo_gate(1);
    CHECK(fifo_delays == 4);
    {
        int i, cmd_at = -1, prm_at = -1, reads = 0;
        for (i = 0; i < ev_n; i++) {
            if (ev_kind[i] == EV_OUT && ev_port[i] == GDC_TEXT_CMD &&
                ev_val[i] == GDC_CMD_SYNC && cmd_at < 0) cmd_at = i;
            if (cmd_at >= 0 && ev_kind[i] == EV_OUT && ev_port[i] == GDC_TEXT_PARAM) {
                prm_at = i;
                break;
            }
        }
        CHECK(cmd_at >= 0 && prm_at > cmd_at);
        for (i = cmd_at + 1; i < prm_at; i++) {
            if (ev_kind[i] == EV_DELAY) continue;
            CHECK(ev_kind[i] == EV_IN && ev_port[i] == GDC_TEXT_STAT);
            reads++;
        }
        CHECK(reads == 5);            /* FULL ×4 + 空き 1 */
    }
}

/* 9. FIFO が詰まったまま: 1 バイトごとに上限まで読んで諦め、数えて先へ */
static void case_fifo_stuck(void)
{
    int n_ok;
    s_case = "fifo_stuck";
    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    pegc_enter_480_ports();
    n_ok = count_gdc_writes();

    world_reset();
    boot_with(RA266_09A8, RA266_CLK);
    fake_full_reads = -1;
    fake_busy_reads = -1;
    pegc_enter_480_ports();
    CHECK(count_gdc_writes() == n_ok);
    CHECK(pegc_gdc_fifo_timeouts == (u32)n_ok);
    CHECK(fifo_delays == (u32)n_ok * PEGC_GDC_FIFO_POLLS);
    CHECK(pegc_vsync_timeouts == 0);
    check_fifo_gate(0);
    x_from_rom(ROM_S480, (const Edit *)0, 0, ADD_S480, 2, -1, 0, 0);
    check_outs();
}

/* 10. VSYNC が来ない / 明けない: 待ちごとに 1 辺だけ上限まで読んで諦め、
 *     数えて先へ。列は変わらない (無限ループにならない)。 */
static void case_vsync_stuck(void)
{
    int mode;
    s_case = "vsync_stuck";
    for (mode = 1; mode <= 2; mode++) {
        world_reset();
        boot_with(RA266_09A8, RA266_CLK);
        fake_vsync_mode = mode;
        pegc_enter_480_ports();
        CHECK(pegc_vsync_timeouts == 7);
        CHECK(vsync_delays == 7U * PEGC_VSYNC_POLLS);
        CHECK(pegc_gdc_fifo_timeouts == 0);
        x_from_rom(ROM_S480, (const Edit *)0, 0, ADD_S480, 2, -1, 0, 0);
        check_outs();
    }
}

/* ---- 資料と記録の規則で、実際に出た OUT を**独立に**検査する (Codex P2) ----
 * 出た OUT から グラフィック GDC の SYNC・SCROLL・PITCH と 6Ah のクロックを
 * 拾い、資料の数値と照らす:
 *   [B] 2-6 表2-27: グラフィック 2.5MHz = C/R 26h・HS 03h・HFP 04h・HBP 03h、
 *                   5MHz = C/R 4Eh・HS 07h・HFP 09h・HBP 07h、
 *                   共通 VS 08h・VFP 07h・VBP 19h・L/F 190h (24kHz 400 ライン)。
 *   [B] 2-7: SCROLL 第 4 バイト bit6 (IM) は 2.5MHz で 0、5MHz で 1。
 *            PITCH は 2.5MHz で 40、5MHz で 80。
 *   実機の記録: SCROLL の SAD は 0、LEN は 3FFh。
 * 数値はここに直に書く (include/pegc.h を見ない)。 */
#define R_CR_2M5   0x26
#define R_CR_5M    0x4E
#define R_IM       0x40
typedef struct {
    int n_sync, n_scroll, n_pitch;
    u8 sync[8], scroll[4], pitch;
    int clk1, clk2;          /* 最後に書いた 6Ah 82h〜85h (-1 = 無し) */
} Seen;

static void collect_gfx(Seen *z)
{
    int i, want = 0, which = 0;
    kmemset(z, 0, sizeof(*z));
    z->clk1 = z->clk2 = -1;
    for (i = 0; i < ev_n; i++) {
        if (ev_kind[i] != EV_OUT) continue;
        if (ev_port[i] == MODE_FF2_PORT) {
            if (ev_val[i] == 0x82 || ev_val[i] == 0x83) z->clk1 = ev_val[i] & 1;
            if (ev_val[i] == 0x84 || ev_val[i] == 0x85) z->clk2 = ev_val[i] & 1;
        } else if (ev_port[i] == GDC_GFX_CMD) {
            want = 0;
            if (ev_val[i] == 0x0E) { which = 1; want = 8; z->n_sync = 0; }
            if (ev_val[i] == 0x70) { which = 2; want = 4; z->n_scroll = 0; }
            if (ev_val[i] == 0x47) { which = 3; want = 1; z->n_pitch = 0; }
        } else if (ev_port[i] == GDC_GFX_PARAM && want > 0) {
            if (which == 1) z->sync[z->n_sync++] = ev_val[i];
            if (which == 2) z->scroll[z->n_scroll++] = ev_val[i];
            if (which == 3) { z->pitch = ev_val[i]; z->n_pitch++; }
            want--;
        }
    }
}

/* 出たグラフィック GDC の設定がクロックと矛盾しない。is24k なら表2-27 全項目 */
static void check_gfx_by_rule(int is5m, int is24k)
{
    Seen z;
    collect_gfx(&z);
    CHECK(z.n_sync == 8 && z.n_scroll == 4);
    CHECK(z.sync[1] == (is5m ? R_CR_5M : R_CR_2M5));
    CHECK((z.scroll[3] & R_IM) == (is5m ? R_IM : 0));
    CHECK(z.scroll[0] == 0 && z.scroll[1] == 0 && (z.scroll[2] & 3) == 0);
    CHECK((((z.scroll[3] & 0x3F) << 4) | (z.scroll[2] >> 4)) == 0x3FF);  /* LEN */
    if (z.n_pitch) CHECK(z.pitch == (is5m ? 80 : 40));
    if (z.clk1 >= 0) CHECK((z.clk1 && z.clk2) == is5m);
    if (is24k) {
        u8 *p = z.sync;
        CHECK((p[2] & 0x1F) == (is5m ? 0x07 : 0x03));          /* HS */
        CHECK((p[3] >> 2) == (is5m ? 0x09 : 0x04));            /* HFP */
        CHECK((p[4] & 0x3F) == (is5m ? 0x07 : 0x03));          /* HBP */
        CHECK((((p[3] & 3) << 3) | (p[2] >> 5)) == 0x08);      /* VS */
        CHECK((p[5] & 0x3F) == 0x07);                          /* VFP */
        CHECK((p[7] >> 2) == 0x19);                            /* VBP */
        CHECK((p[6] | ((p[7] & 3) << 8)) == 0x190);            /* L/F */
    }
}

/* 11. 戻りの組: 起動時の周波数 2 通り × クロック 3 通り (片方だけ 5MHz を含む) */
static void case_restore_matrix(void)
{
    static const u8 hs_raw[2] = { 0x00, 0x81 };
    static const u8 clk[3] = { 0x00, 0x03, 0x01 };
    int h, c;
    s_case = "restore_matrix";
    for (h = 0; h < 2; h++) {
        for (c = 0; c < 3; c++) {
            int is5m = (clk[c] == 0x03);
            world_reset();
            boot_with(hs_raw[h], clk[c]);
            pegc_restore_text_sync(h);      /* v86 -g の FALLBACK の口 */
            check_gfx_by_rule(is5m, h == 0);
            world_reset();
            boot_with(hs_raw[h], clk[c]);
            pegc_text_sync_400(pegc_restore_hsync());   /* pegc_shutdown の口 */
            check_gfx_by_rule(is5m, h == 0);
        }
    }
    /* 480 ラインへ入る側は 5MHz の組 (C/R 4Eh・IM 1・PITCH 80) */
    world_reset();
    boot_with(0x00, 0x00);
    pegc_enter_480_ports();
    check_gfx_by_rule(1, 0);
}

/* 12. ヘッダ §10・§13 の値 = 実機の記録・資料の値 (試験側に直に書いた数値) */
static void case_defaults(void)
{
    static const u8 x_ms480[] = PEGC_GDC_MSYNC_480;
    static const u8 x_ss480[] = PEGC_GDC_SSYNC_480;
    static const u8 x_ms31k[] = PEGC_GDC_MSYNC_400_31K;
    static const u8 x_ss31kl[] = PEGC_GDC_SSYNC_400_31K_2M5;
    static const u8 x_ss31km[] = PEGC_GDC_SSYNC_400_31K_5M;
    static const u8 x_ms24[] = PEGC_GDC_MSYNC_400;
    static const u8 x_ss24l[] = PEGC_GDC_SSYNC_400_2M5;
    static const u8 x_ss24m[] = PEGC_GDC_SSYNC_400_5M;
    static const u8 x_sc480[] = PEGC_GDC_SCROLL_480;
    static const u8 x_sc400l[] = PEGC_GDC_SCROLL_400_2M5;
    static const u8 x_sc400m[] = PEGC_GDC_SCROLL_400_5M;
    static const u8 x_tsc[] = PEGC_GDC_TSCROLL;
    static const u8 x_tcsr[] = PEGC_GDC_TCSRFORM;
    static const u8 x_gcsr[] = PEGC_GDC_GCSRFORM;
    static const u8 x_crtc[] = PEGC_CRTC_VALUES;
    static const u8 r_tcsr[3] = { 0x0F, 0x00, 0x7B };
    static const u8 r_gcsr[3] = { 0x00, 0x00, 0x01 };
    static const u8 r_crtc[6] = { 0x00, 0x0F, 0x10, 0x00, 0x01, 0x00 };
    int i;
    s_case = "defaults";
    /* 未解明の差分は ROM の 8〜11 行目の 4 行だけ (票 §3-3 と同じ) */
    {
        int k, n = 0;
        for (k = 0; k < N_EDIT_COMMON; k++) {
            const char *w = EDIT_COMMON[k].why;
            if (w[0] == 'U' && w[1] == 'N' && w[2] == 'R') {
                CHECK(EDIT_COMMON[k].idx >= 8 && EDIT_COMMON[k].idx <= 11);
                n++;
            }
        }
        CHECK(n == 4);
    }
    for (i = 0; i < 8; i++) {
        CHECK(x_ms480[i] == R_MS480[i] && x_ss480[i] == R_SS480[i]);
        CHECK(x_ms31k[i] == R_MS31K[i] && x_ss31kl[i] == R_SS31K_L[i]);
        CHECK(x_ss31km[i] == N_SS31K_M[i]);
        CHECK(x_ms24[i] == B_MS24[i] && x_ss24l[i] == B_SS24_L[i] &&
              x_ss24m[i] == B_SS24_M[i]);
    }
    for (i = 0; i < 4; i++) {
        CHECK(x_sc480[i] == R_SC480[i] && x_sc400l[i] == R_SC400L[i]);
        CHECK(x_sc400m[i] == B_SC400M[i] && x_tsc[i] == R_SC400L[i]);
    }
    for (i = 0; i < 3; i++) CHECK(x_tcsr[i] == r_tcsr[i] && x_gcsr[i] == r_gcsr[i]);
    for (i = 0; i < 6; i++) CHECK(x_crtc[i] == r_crtc[i]);
    CHECK(PEGC_GDC_PITCH_480 == 80 && PEGC_GDC_TPITCH == 80);   /* A2h/62h 47h + 50h */
    CHECK(PEGC_GDC_PITCH_400_2M5 == 40);          /* back A2h 47h + 28h、[B] 2-7 */
    CHECK(PEGC_GDC_PITCH_400_5M == 80);           /* [B] 2-7 */
    CHECK(PEGC_GDC_ZOOM == 0x00);
    CHECK(PEGC_GDC_CLK1_480 == 0x83 && PEGC_GDC_CLK2_480 == 0x85);
    CHECK(PEGC_FF2_GDC_CLK1_2M5 == 0x82 && PEGC_FF2_GDC_CLK2_2M5 == 0x84);
    CHECK(PEGC_FF2_LCD_MODE == 0x41 && PEGC_XATTR_31KHZ == 0x21 &&
          PEGC_XATTR_24KHZ == 0x20 && PEGC_XATTR_UNLOCK == 0x03 &&
          PEGC_XATTR_LOCK == 0x02);
    CHECK(PEGC_GDC_CMD_STOP2 == 0x05 && PEGC_GDC_CMD_START2 == 0x6B);
    CHECK(PEGC_STAT_SEL_GDCCLK1 == 0x09 && PEGC_STAT_RD_GDCCLK2 == 0x02);
    CHECK(PEGC_GDC_FIFO_POLLS > 0 && PEGC_GDC_FIFO_POLL_US > 0);
    /* 待ちの上限: FIFO は 1 バイトあたり 100ms 未満、VSYNC は 1 辺あたり
     * 1 フレーム (24kHz 400 ラインで約 18ms) より長く 100ms 未満 */
    CHECK((u32)PEGC_GDC_FIFO_POLLS * PEGC_GDC_FIFO_POLL_US < 100000UL);
    CHECK((u32)PEGC_VSYNC_POLLS * PEGC_VSYNC_POLL_US > 20000UL);
    CHECK((u32)PEGC_VSYNC_POLLS * PEGC_VSYNC_POLL_US < 100000UL);
    CHECK(PEGC_VSYNC_POLL_US != PEGC_GDC_FIFO_POLL_US);   /* 試験が数え分ける */
}

void _start(void)
{
    /* 資料の規則・記録の数値での検査を先に回す (変異がヘッダ由来の期待列では
     * なくこちらで落ちることを --mutate の行で見られるように) */
    case_restore_matrix();
    case_defaults();
    case_rom_s480();
    case_rom_back();
    case_enter_24k();
    case_restore_24k_5m();
    case_boot_clock();
    case_restore_unrecorded();
    case_hsync_bits();
    case_fifo_busy();
    case_fifo_stuck();
    case_vsync_stuck();
    output("PASS pegc_mode_host (12 cases)\n");
    finish(0);
}
