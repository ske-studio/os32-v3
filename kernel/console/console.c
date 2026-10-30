/* ======================================================================== */
/*  CONSOLE.C — テキストコンソールドライバ                                   */
/*                                                                          */
/*  テキストVRAM操作、カーソル管理、shell_print等のコンソール出力を提供。    */
/*  shell.cから分離。外部プログラム(shell.bin含む)はKernelAPI経由で利用。    */
/* ======================================================================== */

#define CONSOLE_TAB_COLS 4
#include "types.h"
#include "console_output.h"
#include "utf8.h"
#include "tvram.h"
#include "kprintf.h"
#include "kstring.h"
#include "con_sink.h"
#include "bootlog.h"
#include "console.h"
#include "console_backend.h"
#include "kbd_inject.h"
#include "pc98_route.h"

/* V86 セッション中の画面描画抑止 (kernel/v86/v86.c)。
 * セッション中は低位 640KB がゲスト用バッキング RAM に差し替わる。
 * Unicode-JIS 表は独立した kernel 所有頁にあるが、TVRAM/KCG への書き込みは
 * ゲストの画面と CG 状態を汚す。
 * 描画だけを抑止し、シリアル出力 (rshell ログ) は通す。 */
extern int v86_is_active(void);

/* デフォルトの通常console属性。 */
#ifndef ATTR_WHITE
#define ATTR_WHITE TATTR_WHITE
#endif

/* rshellフラグ (外部から設定可) */
int rshell_active = 0;

/* カーソル位置 */
static int cursor_x = 0;
static int cursor_y = 0;

/* テキスト面へ描いてよいか。GF の decode 未確認時も同じ描画抑止を使う。
 * 描かないのは次の場合:
 *   - V86 セッション中 (上の extern のとおり、画面はゲストのもの)。
 *   - GUI モード中 = con_sink 有効 (console_text_gdc_stop 以降)。gshell が
 *     全画面 GFX を握っている間、テキスト面は gfx_init が一度消したまま
 *     誰も描き直さない。そこへ CUI プログラムの出力を書くと、表示されたままの
 *     テキスト面 (text_disp=1) の画素がグラフィックの上に残る (票 K6C-2、
 *     PM 実測 2026-09-12)。シンクへ積むのは端末アプリの仕事なので従来どおり、
 *     描くほうだけを止める。
 * 抑止中は論理カーソル (cursor_x/y) も進めない — 画面が凍っているのに
 * 論理位置だけ進むと、CUI 復帰時に食い違う。 */
static int console_render_allowed(void)
{
    return (!pc98_console_safe || pc98_console_safe()) &&
           !v86_is_active() && !con_sink_is_enabled();
}

/* GDC (テキスト) のハードウェアカーソルを論理カーソルへ追従させる。
 * 以前は console_set_cursor() (シェルの行編集) だけが CSRW を出していたので、
 * 通常出力やスクロールで論理位置が進んでもハードウェアカーソルは最後に
 * 置かれた場所で点滅し続けた (「左下でちかちか」)。1 文字ごとではなく
 * 文字列単位の出力の末尾で呼ぶ。V86 セッション中は GDC をゲストが
 * 握っているので触らない。 */
static void console_hw_cursor_sync(void)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    if (v86_is_active()) return;
    con_backend_cursor_at(cursor_x, cursor_y);
}

/* スクロール保護行数 (下から数えた固定行)。0=全行 */
static int g_scroll_reserve = 0;

#define CONSOLE_LAST_ROW  (TVRAM_ROWS - 1 - g_scroll_reserve)

void tvram_set_scroll_reserve(int rows)
{
    if (rows < 0) {
        rows = 0;
    }
    if (rows >= TVRAM_ROWS) {
        rows = TVRAM_ROWS - 1;
    }
    /* When the IME first reserves the last row, move an existing input
     * line out of it before the preedit renderer overwrites that row. */
    if (g_scroll_reserve == 0 && rows == 1 &&
        cursor_y == TVRAM_ROWS - 1 && console_render_allowed()) {
        con_backend_scroll(TVRAM_ROWS);
        cursor_y--;
        console_hw_cursor_sync();
    }
    g_scroll_reserve = rows;
}

/* ======================================================================== */
/*  TVRAM低レベル操作                                                       */
/* ======================================================================== */

void tvram_clear(void)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    con_sink_push_clear();      /* 票 K6C: 画面クリアの経路 (CLEAR レコード) */
    /* 票 K6C-2: GUI 中はテキスト面を消さない (消す相手が見えていない上に、
     * 論理カーソルだけ 0,0 に戻ると凍っている画面と食い違う)。 */
    if (con_sink_is_enabled()) return;
    con_backend_clear();
    cursor_x = 0;
    cursor_y = 0;
    console_hw_cursor_sync();
}

/* The console uses the standard TVRAM_ROWS grid (also clear/scroll/cursor).
 * Reject before unsigned address arithmetic; a kanji must fit both cells. */
void tvram_putchar_at(int x, int y, char ch, u8 color)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    con_backend_putchar(x, y, ch, color);
}

/* TVRAM 1セル読み取り (文字コード + 属性) */
void tvram_readchar_at(int x, int y, u16 *code, u8 *attr)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    con_backend_readchar(x, y, code, attr);
}

/* TVRAM 反転トグル (漢字対応)
 * 指定位置の属性反転ビット(0x04)をXORでトグルする。
 * 漢字右半分の場合は左半分に自動調整し、2セル同時反転。
 * 戻り値: 反転したセル数 (1=ANK, 2=漢字) */
int tvram_reverse_cell(int x, int y)
{
    if (pc98_console_safe && !pc98_console_safe()) return 0;
    return con_backend_reverse(x, y);
}

void tvram_scroll(void)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    con_backend_scroll(TVRAM_ROWS - g_scroll_reserve);
}

/* 全角漢字1文字をTVRAMに書き込み
 * PC9800Bible §2-6-2 */
void tvram_putkanji_at(int x, int y, u16 jis, u8 color)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    con_backend_putkanji(x, y, jis, color);
}

/* ======================================================================== */
/*  コンソール出力 (カーソル追従)                                            */
/* ======================================================================== */

/* 1文字出力 (ハードウェアカーソルは同期しない内部版) */
static void putchar_raw(char ch, u8 color)
{
    if (ch == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (ch == '\r') {
        cursor_x = 0;
    } else if (ch == ASCII_BS) {
        if (cursor_x > 0) {
            cursor_x--;
            tvram_putchar_at(cursor_x, cursor_y, ' ', ATTR_WHITE);
        }
        return;
    } else {
        tvram_putchar_at(cursor_x, cursor_y, ch, color);
        cursor_x++;
    }
    if (cursor_x >= TVRAM_COLS) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y > CONSOLE_LAST_ROW) {
        tvram_scroll();
        cursor_y = CONSOLE_LAST_ROW;
    }
}

/* 1文字出力 */
void shell_putchar(char ch, u8 color)
{
    con_sink_push_print(&ch, 1, color);
    bootlog_push(&ch, 1);             /* 起動ログ (色は捨てる) */
    if (!console_render_allowed()) return;   /* 票 K6C-2 / V86: 描画抑止 */
    putchar_raw(ch, color);
    console_hw_cursor_sync();
}

/* 文字列表示 */
void shell_print(const char *str, u8 color)
{
    int render = console_render_allowed();
    /* 票 K6C: 描画抑止 (v86) や rshell 複写とは無関係にシンクへ積む。
     * shell_print_dec / shell_print_hex32 はここへ落ちるので、あちらに
     * 差し込みは要らない (二重に積むことになる)。 */
    con_sink_push_print(str, kstrlen(str), color);
    bootlog_push(str, kstrlen(str));  /* 起動ログ (色は捨てる) */
    while (*str) {
        if (render) putchar_raw(*str, color);
        if (rshell_active) con_output_putchar(*str);
        str++;
    }
    if (render) console_hw_cursor_sync();
}

/* 10進表示 (変換は kprintf.h の kutoa_dec に統一) */
void shell_print_dec(u32 val, u8 color)
{
    char buf[U32_DEC_BUFSIZE + 1];
    kutoa_dec(val, buf, (int)sizeof(buf));
    shell_print(buf, color);
}

/* 32ビット16進表示 */
void shell_print_hex32(u32 val, u8 color)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[HEX32_DIGITS + sizeof("0x")];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = HEX32_DIGITS + 1; i >= 2; i--) {
        buf[i] = hex[val & ((1u << HEX_NIBBLE_BITS) - 1)];
        val >>= HEX_NIBBLE_BITS;
    }
    buf[HEX32_DIGITS + 2] = '\0';
    shell_print(buf, color);
}

/* UTF-8文字列表示 (漢字対応) */
void shell_print_utf8(const char *utf8_str, u8 color)
{
    const u8 *p = (const u8 *)utf8_str;

    con_sink_push_print(utf8_str, kstrlen(utf8_str), color);
    bootlog_push(utf8_str, kstrlen(utf8_str));
    if (rshell_active) {
        const char *s = utf8_str;
        while (*s) con_output_putchar(*s++);
    }
    /* 描画抑止 (シリアルとシンクには出した)。票 K6C-2 で GUI 中も含む。 */
    if (!console_render_allowed()) return;
    while (*p) {
        utf8_decode_t dec;
        u32 cp;
        u8 ank;
        u16 jis;

        if (*p == '\n') {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y > CONSOLE_LAST_ROW) {
                tvram_scroll();
                cursor_y = CONSOLE_LAST_ROW;
            }
            p++;
            continue;
        }
        if (*p == '\r') { cursor_x = 0; p++; continue; }
        if (*p == '\t') { cursor_x = (cursor_x + CONSOLE_TAB_COLS) & ~(CONSOLE_TAB_COLS - 1); p++; continue; }

        dec = utf8_decode(p);
        cp = dec.codepoint;
        p += dec.bytes_used;

        if (cp == UCS_BOM || cp < UCS_C0_END) continue;

        ank = unicode_to_ank(cp);
        if (ank) {
            tvram_putchar_at(cursor_x, cursor_y, (char)ank, color);
            cursor_x++;
            if (cursor_x >= TVRAM_COLS) { cursor_x = 0; cursor_y++; }
            if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
            continue;
        }

        jis = unicode_to_jis(cp);
        if (jis) {
            if (cursor_x >= TVRAM_COLS - 1) {
                cursor_x = 0;
                cursor_y++;
                if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
            }
            tvram_putkanji_at(cursor_x, cursor_y, jis, color);
            cursor_x += TVRAM_WIDE_COLS;
            if (cursor_x >= TVRAM_COLS) { cursor_x = 0; cursor_y++; }
            if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
            continue;
        }

        /* 変換不可: □を表示 */
        if (cursor_x >= TVRAM_COLS - 1) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
        }
        tvram_putkanji_at(cursor_x, cursor_y, TVRAM_JIS_REPLACEMENT, color);  /* □ (JIS 0x2222) */
        cursor_x += TVRAM_WIDE_COLS;
        if (cursor_x >= TVRAM_COLS) { cursor_x = 0; cursor_y++; }
        if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
    }
    console_hw_cursor_sync();
}

/* UTF-8ストリーム出力 (サイズ指定) */
void console_write(const char *buf, u32 size, u8 color)
{
    const u8 *p = (const u8 *)buf;
    u32 remaining = size;

    con_sink_push_print(buf, size, color);
    bootlog_push(buf, size);
    if (rshell_active) {
        u32 i;
        for (i = 0; i < size; i++) con_output_putchar(buf[i]);
    }

    /* 描画抑止 (シリアルとシンクには出した)。票 K6C-2 で GUI 中も含む。 */
    if (!console_render_allowed()) return;

    while (remaining > 0) {
        utf8_decode_t dec;
        u32 cp;
        u8 ank;
        u16 jis;

        if (*p == '\n') {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y > CONSOLE_LAST_ROW) {
                tvram_scroll();
                cursor_y = CONSOLE_LAST_ROW;
            }
            p++; remaining--;
            continue;
        }
        if (*p == '\r') { cursor_x = 0; p++; remaining--; continue; }
        if (*p == '\t') { cursor_x = (cursor_x + CONSOLE_TAB_COLS) & ~(CONSOLE_TAB_COLS - 1); p++; remaining--; continue; }

        if (remaining < UTF8_MAX_BYTES) {
            u8 tmp[UTF8_MAX_BYTES] = {0, 0, 0, 0};
            u32 k;
            for (k = 0; k < remaining; k++) tmp[k] = p[k];
            dec = utf8_decode(tmp);
            if (dec.bytes_used > remaining) {
                /* 分断された文字: ここではスキップして進める */
                p += remaining;
                remaining = 0;
                continue;
            }
        } else {
            dec = utf8_decode(p);
        }
        
        cp = dec.codepoint;
        p += dec.bytes_used;
        remaining -= dec.bytes_used;

        if (cp == UCS_BOM || cp < UCS_C0_END) continue;

        ank = unicode_to_ank(cp);
        if (ank) {
            tvram_putchar_at(cursor_x, cursor_y, (char)ank, color);
            cursor_x++;
            if (cursor_x >= TVRAM_COLS) { cursor_x = 0; cursor_y++; }
            if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
            continue;
        }

        jis = unicode_to_jis(cp);
        if (jis) {
            if (cursor_x >= TVRAM_COLS - 1) {
                cursor_x = 0;
                cursor_y++;
                if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
            }
            tvram_putkanji_at(cursor_x, cursor_y, jis, color);
            cursor_x += TVRAM_WIDE_COLS;
            if (cursor_x >= TVRAM_COLS) { cursor_x = 0; cursor_y++; }
            if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
            continue;
        }

        /* 変換不可: □を表示 */
        if (cursor_x >= TVRAM_COLS - 1) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
        }
        tvram_putkanji_at(cursor_x, cursor_y, TVRAM_JIS_REPLACEMENT, color);  /* □ (JIS 0x2222) */
        cursor_x += TVRAM_WIDE_COLS;
        if (cursor_x >= TVRAM_COLS) { cursor_x = 0; cursor_y++; }
        if (cursor_y > CONSOLE_LAST_ROW) { tvram_scroll(); cursor_y = CONSOLE_LAST_ROW; }
    }
    console_hw_cursor_sync();
}

/* カーソル位置取得/設定 (外部プログラム用) */
int console_get_cursor_x(void) { return cursor_x; }
int console_get_cursor_y(void) { return cursor_y; }
void console_set_cursor(int x, int y)
{
    /* Keep both the sink record and the next text/GDC operation in range. */
    if (x < 0) x = 0;
    if (x >= TVRAM_COLS) x = TVRAM_COLS - 1;
    if (y < 0) y = 0;
    if (y >= TVRAM_ROWS) y = TVRAM_ROWS - 1;
    con_sink_push_cursor(x, y);     /* 票 K6C: CURSOR レコード */
    /* 票 K6C-2: GUI 中は論理位置も GDC も動かさない。ここで
     * console_hw_cursor_enable() を通すと、console_text_gdc_stop() で消した
     * テキストカーソルが GFX 画面の上でまた点滅しはじめる。 */
    if (con_sink_is_enabled()) return;
    cursor_x = x;
    cursor_y = y;
    console_hw_cursor_enable();
}

/* GDC (テキスト) カーソルを表示にして論理位置へ同期する。
 * CSRFORM: P1 = DC | (LR-1), P2 = CTOP, P3 = CBOT<<3 | BR上位。
 * 以前の実装は存在しない 0x0B を出していたので OS32 は一度も
 * ハードウェアカーソルを表示しておらず、V86 中に DOS が DC=1 にした
 * カーソルだけが終了後も DOS 最後の位置で点滅し続けていた (2026-09-04)。
 * 起動時と V86 セッション終了時にも呼ぶ。 */
void console_hw_cursor_enable(void)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    if (v86_is_active()) return;
    con_backend_cursor_shape(1);
    console_hw_cursor_sync();
}

/* GUI モード: GDC のテキストカーソルを消す (CSRFORM の DC ビットを立てない)。
 * gshell が全画面 GFX を握る間、CUI 由来や DOS 由来のテキストカーソルが
 * 点滅し続けるのを止める (契約 T9 / TASK_K4 作業 2、CLAUDE.md「Text GDC
 * cursor」§4-18)。バックエンドが TVRAM をクローム描画に使うかどうかの判断は
 * gshell 側 (W1) の責任だが、起動ループは GUI シェルを載せる直前に消しておく。
 * V86 セッション中は GDC をゲストが握っているので触らない。 */
void console_text_gdc_stop(void)
{
    if (pc98_console_safe && !pc98_console_safe()) return;
    /* 票 K6C §2-3: GUI へ入るときにシンクを空で始める。v86 のガードより
     * **前**に置く — GDC を触らない場合でも「テキスト面が見えない」ことは
     * 変わらないので、溜め始める側は抑止しない。 */
    con_sink_enable();
    if (v86_is_active()) return;
    con_backend_cursor_shape(0);
}

/* CUI モード復帰: GDC テキストカーソルを再表示して論理位置へ同期する。
 * console_hw_cursor_enable() と同じだが、GUI からの復帰を明示するための別名。 */
void console_text_gdc_start(void)
{
    int was_gui = con_sink_is_enabled();
    /* 票 K6C §2-3: CUI へ戻るときは溜まっているものを捨てる (テキスト VRAM が
     * 再び正になるので、リングの中身はもう誰も描き直さない)。読み手の所有は
     * ここでは返さない — 返すのは exec_exit / exec_kill の owner 回収だけ。 */
    con_sink_disable();
    /* 票 K7 D5: 注入リングも捨てる。CUI では打鍵が IRQ1 の cooked リングから
     * 来るので、GUI 中に注がれた残りをシェルのプロンプトへ流し込まない。 */
    kbd_inject_discard();
    /* 票 K6C-2: GUI 中はテキスト面も cursor_x/y も凍らせていた。テキスト面が
     * 再び正になる今ここで一度消すと、画面と論理位置 (0,0) が必ず一致する。
     * 消すのは GUI から戻ったときだけ — 起動直後の CUI では kernel.c の
     * ブートメッセージが残ってよい (従来どおり)。 */
    if (was_gui) tvram_clear();
    console_hw_cursor_enable();
}

/* コンソール画面サイズ取得 */
void console_get_size(int *w, int *h)
{
    if (w) *w = TVRAM_COLS;
    if (h) *h = TVRAM_ROWS;
}
