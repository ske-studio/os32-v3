#include "in2_int.h"
#include "io.h"
#include "kbd.h"
#include "legacy_guest.h"

static u8 cnt[128], mechanical[IN2_MAX_DEV], usb_lock;
/* Compatibility snapshot for existing kbd/IME consumers; merge owns writes. */
volatile u8 kbd_shift_state;

void input_native_locks(u8 src, u8 mods)
{
    mechanical[src - 1] = mods & (SHIFT_CAPS | SHIFT_KANA);
    kbd_shift_state = input_key_mods();
}


int input_key_pressed(u8 sc)
{
    return sc < 128 && cnt[sc] != 0;
}

u8 input_key_mods(void)
{
    unsigned int flags = irq_save();
    u8 mods = usb_lock;
    for (unsigned int i = 0; i < IN2_MAX_DEV; ++i) mods |= mechanical[i];
    if (cnt[KEY_SHIFT]) mods |= SHIFT_SHIFT;
    if (cnt[KEY_CTRL]) mods |= SHIFT_CTRL;
    if (cnt[KEY_GRPH]) mods |= SHIFT_GRPH;
    irq_restore(flags);
    return mods;
}

void input_merge_key(InputEvent *e, u8 old, u8 origin)
{
    u8 sc = e->sc, down = e->down;
    u8 bit = sc == KEY_CAPS ? SHIFT_CAPS : sc == KEY_KANA ? SHIFT_KANA : 0;
    if (origin == INPUT_ORIGIN_NATIVE) {
        if (down) mechanical[e->src - 1] |= bit;
        else mechanical[e->src - 1] &= ~bit;
    }
    if (old != down) {
        cnt[sc] += down ? 1 : -1;
        if (origin == INPUT_ORIGIN_USB) {
            if (down) usb_lock ^= bit;
        }
        e->buttons = kbd_shift_state = input_key_mods();
        if (legacy_active()) return;
        input_emit_in2(e);
        if (down || cnt[sc] == 0) {
            input_emit_legacy_key(sc, down, e->buttons);
        }
    } else {
        kbd_shift_state = input_key_mods();
        if (down && !legacy_active()) input_emit_legacy_repeat(sc, kbd_shift_state);
    }
}

/* ======================================================================== */
/*  スキャンコード → ASCII 変換テーブル                                    */
/*  PC9800Bible §2-5 表2-14 の「通常」列から抽出                           */
/*  インデックス = キーコード (0x00〜0x6B)                                  */
/* ======================================================================== */

/* 通常状態 (Shift なし) */
static const u8 scancode_to_ascii[128] = {
    /* 0x00-0x0F */
    0x1B, '1', '2', '3', '4', '5', '6', '7',   /* ESC, 1-7 */
    '8',  '9', '0', '-', '^', '\\', 0x08, 0x09, /* 8-0,-,^,\,BS,TAB */
    /* 0x10-0x1F */
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',   /* Q-I */
    'o',  'p', '@', '[', 0x0D, 'a', 's', 'd',   /* O-P,@,[,ENTER,A-D */
    /* 0x20-0x2F */
    'f',  'g', 'h', 'j', 'k', 'l', ';', ':',   /* F-L,;,: */
    ']',  'z', 'x', 'c', 'v', 'b', 'n', 'm',   /* ],Z-M */
    /* 0x30-0x3F */
    ',',  '.', '/', 0,   ' ', 0,   0x12,0x03,   /* ,./ _,SPACE,XFER,RLUP(12),RLDN(03) */
    0x16, 0x7F, 0x1E, 0x1D, 0x1C, 0x1F, 0x01, 0x05,  /* INS(16),DEL,↑,←,→,↓,HOME(01),HELP(05) */
    /* 0x40-0x4F (テンキー) */
    '-',  '/', '7', '8', '9', '*', '4', '5',
    '6',  '+', '1', '2', '3', '=', '0', ',',
    /* 0x50-0x5F */
    '.',  0,   0,   0,   0,   0,   0,   0,      /* NFER,vf1-vf5 */
    0,    0,   0,   0,   0,   0,   0,   0,
    /* 0x60-0x6F (STOP,COPY,F1-F10) */
    0,    0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,
    /* 0x70-0x7F (SHIFT,CAPS,KANA,GRPH,CTRL) */
    0,    0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,
};

/* Shift状態 */
static const u8 scancode_to_ascii_shift[128] = {
    /* 0x00-0x0F */
    0x1B, '!', '"', '#', '$', '%', '&', '\'',
    '(',  ')', 0,   '=', '`', '|', 0x08, 0x09,
    /* 0x10-0x1F */
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O',  'P', '~', '{', 0x0D, 'A', 'S', 'D',
    /* 0x20-0x2F */
    'F',  'G', 'H', 'J', 'K', 'L', '+', '*',
    '}',  'Z', 'X', 'C', 'V', 'B', 'N', 'M',
    /* 0x30-0x3F */
    '<',  '>', '?', '_', ' ', 0,   0x12,0x03,
    0x16, 0x7F, 0x1E, 0x1D, 0x1C, 0x1F, 0x01, 0x05,
    /* 0x40-0x7F: テンキー以降はShiftでも同じ */
    '-',  '/', '7', '8', '9', '*', '4', '5',
    '6',  '+', '1', '2', '3', '=', '0', ',',
    '.',  0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,
};

void input_emit_legacy_key(u8 sc, u8 down, u8 mods)
{
    u8 ascii;
    if (kbd_gui_mode) {
        if (legacy_stop_filter(sc, !down, mods)) return;
        legacy_raw_push(sc, !down, mods);
        return;
    }
    if (!down || (sc >= KEY_SHIFT && sc <= KEY_CTRL)) return;
    if (sc == KEY_STOP && (mods & SHIFT_CTRL)) return;
    /* スキャンコード → ASCII変換 */
    if (mods & SHIFT_SHIFT) {
        ascii = scancode_to_ascii_shift[sc];
    } else {
        ascii = scancode_to_ascii[sc];
    }

    /* CAPS時の大文字小文字切替 */
    if (mods & SHIFT_CAPS) {
        if (ascii >= 'a' && ascii <= 'z') ascii -= ('a' - 'A');
        else if (ascii >= 'A' && ascii <= 'Z') ascii += ('a' - 'A');
    }

    /* CTRL+文字 → コントロールコード */
    if ((mods & SHIFT_CTRL) && ascii >= 'a' && ascii <= 'z') {
        ascii = ascii - 'a' + 1;
    }

    /* バッファに格納: 全メイクキーイベントを格納（修飾キーは上で既にreturn済み）*/
    legacy_cooked_push(((u16)sc << 8) | ascii);
}

void input_emit_legacy_repeat(u8 sc, u8 mods)
{
    input_emit_legacy_key(sc, 1, mods);
}
