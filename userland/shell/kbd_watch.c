/* ======================================================================== */
/*  KBD_WATCH.C — `kbdstat -w` の行の組み立て (純粋部)                       */
/*                                                                          */
/*  票 docs/tasks/gui/TASK_KBD_NAV.md §3: 実機のカナ / CAPS が「ロックで      */
/*  make、解除で break」か「押すたびに make だけ」かを、受信 1 バイトごとの  */
/*  行で見る。書式はホスト試験 (tools/tests/test_kbd_dlog.py) が固定する。    */
/*  libc の printf 系は使わない (常駐シェルと同じ翻訳単位の制約を持たない   */
/*  ように、自前の小さな追記だけで組む)。                                    */
/* ======================================================================== */

#include "kbd_watch.h"

/* 追記の状態。len は書いた長さ、cap は NUL を含むバッファ長。溢れた分は捨てる。 */
typedef struct {
    char *buf;
    int   cap;
    int   len;
} KbdwOut;

static void kbdw_begin(KbdwOut *o, char *buf, int cap)
{
    o->buf = buf;
    o->cap = cap;
    o->len = 0;
    if (cap > 0) buf[0] = '\0';
}

static void kbdw_ch(KbdwOut *o, char c)
{
    if (o->cap <= 0 || o->len >= o->cap - 1) return;
    o->buf[o->len++] = c;
    o->buf[o->len] = '\0';
}

static void kbdw_str(KbdwOut *o, const char *s)
{
    while (*s) kbdw_ch(o, *s++);
}

static void kbdw_hex2(KbdwOut *o, u32 v)
{
    static const char hex[] = "0123456789ABCDEF";
    kbdw_ch(o, hex[(v >> 4) & 0x0F]);
    kbdw_ch(o, hex[v & 0x0F]);
}

static void kbdw_dec(KbdwOut *o, u32 v)
{
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0 && n < (int)sizeof(tmp));
    while (n > 0) kbdw_ch(o, tmp[--n]);
}

/* 修飾の並び (表示順) */
static const struct { u32 bit; const char *name; } kbdw_mod_names[] = {
    { KBD_DLOG_MOD_SHIFT, "SHIFT" },
    { KBD_DLOG_MOD_CAPS,  "CAPS"  },
    { KBD_DLOG_MOD_KANA,  "KANA"  },
    { KBD_DLOG_MOD_GRPH,  "GRPH"  },
    { KBD_DLOG_MOD_CTRL,  "CTRL"  },
};
#define KBDW_NMODS ((int)(sizeof(kbdw_mod_names) / sizeof(kbdw_mod_names[0])))

static void kbdw_mods(KbdwOut *o, u32 mods)
{
    int i;
    int any = 0;
    for (i = 0; i < KBDW_NMODS; i++) {
        if (mods & kbdw_mod_names[i].bit) {
            if (any) kbdw_ch(o, '|');
            kbdw_str(o, kbdw_mod_names[i].name);
            any = 1;
        }
    }
    if (!any) kbdw_ch(o, '-');
}

/* キーコード (0041h の下位 7 ビット) の名前。修飾キーと ESC だけ。
 * 値は drivers/kbd.h の KEY_SHIFT〜KEY_CTRL (0x70〜0x74) と ESC (0x00)。 */
static const char *kbdw_key_name(u32 key)
{
    switch (key) {
    case 0x00: return "ESC";
    case 0x70: return "SHIFT";
    case 0x71: return "CAPS";
    case 0x72: return "KANA";
    case 0x73: return "GRPH";
    case 0x74: return "CTRL";
    default:   return (const char *)0;
    }
}

int kbdw_fmt_mods(char *buf, int cap, u32 mods)
{
    KbdwOut o;
    kbdw_begin(&o, buf, cap);
    kbdw_mods(&o, mods);
    return o.len;
}

int kbdw_fmt_ent(char *buf, int cap, const KbdDiagLogEnt *e)
{
    KbdwOut o;
    const char *name;
    u32 code = (u32)e->code;
    u32 key = code & KBD_DLOG_KEY_MASK;

    kbdw_begin(&o, buf, cap);
    kbdw_str(&o, "seq=");
    kbdw_dec(&o, e->seq);
    kbdw_str(&o, " code=");
    kbdw_hex2(&o, code);
    kbdw_str(&o, (code & KBD_DLOG_BREAK) ? " break" : " make ");
    kbdw_str(&o, " key=");
    kbdw_hex2(&o, key);
    name = kbdw_key_name(key);
    if (name) {
        kbdw_ch(&o, ' ');
        kbdw_str(&o, name);
    }
    kbdw_str(&o, " mods=");
    kbdw_mods(&o, (u32)e->mods);
    if (e->flags & KBD_DLOG_F_OVERRUN) kbdw_str(&o, " [OE]");
    if (e->flags & KBD_DLOG_F_V86)     kbdw_str(&o, " [V86]");
    if (e->flags & KBD_DLOG_F_GUI)     kbdw_str(&o, " [GUI]");
    return o.len;
}

u32 kbdw_lost(u32 prev, u32 first)
{
    if (first <= prev + 1u) return 0;
    return first - prev - 1u;
}

int kbdw_fmt_lost(char *buf, int cap, u32 prev, u32 first)
{
    KbdwOut o;
    u32 n = kbdw_lost(prev, first);

    kbdw_begin(&o, buf, cap);
    if (n == 0) return 0;
    kbdw_str(&o, "LOST seq=");
    kbdw_dec(&o, prev + 1u);
    kbdw_str(&o, "..");
    kbdw_dec(&o, first - 1u);
    kbdw_str(&o, " (");
    kbdw_dec(&o, n);
    kbdw_str(&o, "): cannot judge this span");
    return o.len;
}
