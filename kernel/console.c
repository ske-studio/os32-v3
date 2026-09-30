/* ======================================================================== */
/*  CONSOLE.C — テキストコンソールドライバ                                   */
/*                                                                          */
/*  テキストVRAM操作、カーソル管理、shell_print等のコンソール出力を提供。    */
/*  shell.cから分離。外部プログラム(shell.bin含む)はKernelAPI経由で利用。    */
/* ======================================================================== */

#include "types.h"
#include "serial.h"
#include "utf8.h"
#include "tvram.h"
#include "io.h"
#include "pc98.h"
#include "kprintf.h"
#include "kstring.h"
#include "con_sink.h"
#include "bootlog.h"
#include "kbd_inject.h"

/* V86 セッション中の画面描画抑止 (kernel/v86.c)。
 * セッション中は低位 640KB がゲスト用バッキング RAM に差し替わっており、
 * Unicode-JIS 表 (0x4A000) を読むとゲストのメモリをテーブルとして解釈して
 * 文字化けする。TVRAM/KCG への書き込みもゲストの画面と CG 状態を汚す。
 * 描画だけを抑止し、シリアル出力 (rshell ログ) は通す。 */
extern int v86_is_active(void);

/* テキストVRAM定義 (tvram.hとpc98.hの定義を使用) */
#define TVRAM_TEXT  TVRAM_BASE

/* デフォルト属性 (os32_kapi_shared.h で定義済みの場合はスキップ) */
#ifndef ATTR_WHITE
#define ATTR_WHITE   TATTR_WHITE
#endif

/* rshellフラグ (外部から設定可) */
int rshell_active = 0;

/* カーソル位置 */
static int cursor_x = 0;
static int cursor_y = 0;

/* テキスト面へ描いてよいか。描かないのは 2 つの場合:
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
    return !v86_is_active() && !con_sink_is_enabled();
}

/* GDC (テキスト) のハードウェアカーソルを論理カーソルへ追従させる。
 * 以前は console_set_cursor() (シェルの行編集) だけが CSRW を出していたので、
 * 通常出力やスクロールで論理位置が進んでもハードウェアカーソルは最後に
 * 置かれた場所で点滅し続けた (「左下でちかちか」)。1 文字ごとではなく
 * 文字列単位の出力の末尾で呼ぶ。V86 セッション中は GDC をゲストが
 * 握っているので触らない。 */
static void console_hw_cursor_sync(void)
{
    u16 offset;
    if (v86_is_active()) return;
    offset = (u16)(cursor_y * TVRAM_COLS + cursor_x);
    outp(GDC_TEXT_CMD, GDC_CMD_CSRW);
    outp(GDC_TEXT_PARAM, (u8)(offset & 0xFF));
    outp(GDC_TEXT_PARAM, (u8)((offset >> 8) & 0xFF));
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
    g_scroll_reserve = rows;
}

/* ======================================================================== */
/*  TVRAM低レベル操作                                                       */
/* ======================================================================== */

void tvram_clear(void)
{
    volatile u16 *text = (volatile u16 *)TVRAM_TEXT;
    volatile u8  *attr;
    int i;
    con_sink_push_clear();      /* 票 K6C: 画面クリアの経路 (CLEAR レコード) */
    /* 票 K6C-2: GUI 中はテキスト面を消さない (消す相手が見えていない上に、
     * 論理カーソルだけ 0,0 に戻ると凍っている画面と食い違う)。 */
    if (con_sink_is_enabled()) return;
    for (i = 0; i < TVRAM_COLS * TVRAM_ROWS; i++) {
        text[i] = 0x0020;
        attr = (volatile u8 *)(TVRAM_ATTR + (u32)i * 2);
        *attr = ATTR_WHITE;
    }
    cursor_x = 0;
    cursor_y = 0;
    console_hw_cursor_sync();
}

void tvram_putchar_at(int x, int y, char ch, u8 color)
{
    u32 offset = (u32)y * TVRAM_BPR + (u32)x * 2;
    *(volatile u16 *)(TVRAM_TEXT + offset) = (u16)(u8)ch;
    *(volatile u8 *)(TVRAM_ATTR + offset) = color;
}

/* TVRAM 1セル読み取り (文字コード + 属性) */
void tvram_readchar_at(int x, int y, u16 *code, u8 *attr)
{
    u32 offset = (u32)y * TVRAM_BPR + (u32)x * 2;
    if (code) *code = *(volatile u16 *)(TVRAM_TEXT + offset);
    if (attr) *attr = *(volatile u8 *)(TVRAM_ATTR + offset);
}

/* TVRAM 反転トグル (漢字対応)
 * 指定位置の属性反転ビット(0x04)をXORでトグルする。
 * 漢字右半分の場合は左半分に自動調整し、2セル同時反転。
 * 戻り値: 反転したセル数 (1=ANK, 2=漢字) */
int tvram_reverse_cell(int x, int y)
{
    u16 code;
    u8 attr;
    int width, i, rx;

    if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) return 0;

    rx = x;

    /* 漢字判定: 右半分 (bit7セット) なら左半分に揃える */
    code = *(volatile u16 *)(TVRAM_TEXT + (u32)y * TVRAM_BPR + (u32)rx * 2);
    if ((code >> 8) != 0 && (code & 0x80)) {
        if (rx > 0) rx--;
    }

    /* 左半分の文字コードで幅を判定 */
    code = *(volatile u16 *)(TVRAM_TEXT + (u32)y * TVRAM_BPR + (u32)rx * 2);
    if ((code >> 8) != 0 && !(code & 0x80) && rx < TVRAM_COLS - 1) {
        width = 2;  /* 漢字 */
    } else {
        width = 1;  /* ANK */
    }

    /* 反転ビットをトグル */
    for (i = 0; i < width; i++) {
        u32 offset = (u32)y * TVRAM_BPR + (u32)(rx + i) * 2;
        volatile u8 *p = (volatile u8 *)(TVRAM_ATTR + offset);
        *p ^= 0x04;
    }

    return width;
}

void tvram_scroll(void)
{
    volatile u16 *text = (volatile u16 *)TVRAM_TEXT;
    volatile u16 *attr = (volatile u16 *)TVRAM_ATTR;
    int rows = TVRAM_ROWS - g_scroll_reserve;
    int i;

    /* 0 .. rows-2 行目までを1行引き上げ */
    for (i = 0; i < TVRAM_COLS * (rows - 1); i++) {
        text[i] = text[i + TVRAM_COLS];
        attr[i] = attr[i + TVRAM_COLS];
    }
    /* rows-1 行目を空白化 */
    for (i = TVRAM_COLS * (rows - 1); i < TVRAM_COLS * rows; i++) {
        text[i] = 0x0020;
        attr[i] = ATTR_WHITE;
    }
}

/* 全角漢字1文字をTVRAMに書き込み
 * PC9800Bible §2-6-2 */
void tvram_putkanji_at(int x, int y, u16 jis, u8 color)
{
    u32 offset = (u32)y * TVRAM_BPR + (u32)x * 2;
    u8 jh = (u8)((jis >> 8) & 0xFF);
    u8 jl = (u8)(jis & 0xFF);
    *(volatile u16 *)(TVRAM_TEXT + offset) = (u16)(jh - 0x20) | ((u16)jl << 8);
    *(volatile u8 *)(TVRAM_ATTR + offset) = color;
    *(volatile u16 *)(TVRAM_TEXT + offset + 2) = (u16)(jh - 0x20 + 0x80) | ((u16)jl << 8);
    *(volatile u8 *)(TVRAM_ATTR + offset + 2) = color;
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
    } else if (ch == 0x08) {
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
        if (rshell_active) serial_putchar(*str);
        str++;
    }
    if (render) console_hw_cursor_sync();
}

/* 10進表示 (変換は kprintf.h の kutoa_dec に統一) */
void shell_print_dec(u32 val, u8 color)
{
    char buf[12];
    kutoa_dec(val, buf, (int)sizeof(buf));
    shell_print(buf, color);
}

/* 32ビット16進表示 */
void shell_print_hex32(u32 val, u8 color)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 9; i >= 2; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[10] = '\0';
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
        while (*s) serial_putchar(*s++);
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
        if (*p == '\t') { cursor_x = (cursor_x + 4) & ~3; p++; continue; }

        dec = utf8_decode(p);
        cp = dec.codepoint;
        p += dec.bytes_used;

        if (cp == 0xFEFF || cp < 0x20) continue;

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
            cursor_x += 2;
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
        tvram_putkanji_at(cursor_x, cursor_y, 0x2222, color);  /* □ (JIS 0x2222) */
        cursor_x += 2;
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
        for (i = 0; i < size; i++) serial_putchar(buf[i]);
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
        if (*p == '\t') { cursor_x = (cursor_x + 4) & ~3; p++; remaining--; continue; }

        if (remaining < 4) {
            u8 tmp[4] = {0, 0, 0, 0};
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

        if (cp == 0xFEFF || cp < 0x20) continue;

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
            cursor_x += 2;
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
        tvram_putkanji_at(cursor_x, cursor_y, 0x2222, color);  /* □ (JIS 0x2222) */
        cursor_x += 2;
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
    if (v86_is_active()) return;
    outp(GDC_TEXT_CMD, GDC_CMD_CSRFORM);
    outp(GDC_TEXT_PARAM, (u8)(GDC_CSRFORM_DC | (GDC_TEXT_LINES_PER_ROW - 1)));
    outp(GDC_TEXT_PARAM, (u8)GDC_TEXT_CSR_TOP);
    outp(GDC_TEXT_PARAM, (u8)((GDC_TEXT_CSR_BOTTOM << 3) | GDC_TEXT_CSR_BR_HI));
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
    /* 票 K6C §2-3: GUI へ入るときにシンクを空で始める。v86 のガードより
     * **前**に置く — GDC を触らない場合でも「テキスト面が見えない」ことは
     * 変わらないので、溜め始める側は抑止しない。 */
    con_sink_enable();
    if (v86_is_active()) return;
    outp(GDC_TEXT_CMD, GDC_CMD_CSRFORM);
    outp(GDC_TEXT_PARAM, (u8)(GDC_TEXT_LINES_PER_ROW - 1));   /* DC=0 → 非表示 */
    outp(GDC_TEXT_PARAM, (u8)GDC_TEXT_CSR_TOP);
    outp(GDC_TEXT_PARAM, (u8)((GDC_TEXT_CSR_BOTTOM << 3) | GDC_TEXT_CSR_BR_HI));
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
