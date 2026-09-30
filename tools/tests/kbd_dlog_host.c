/* ======================================================================== */
/*  KBD_DLOG_HOST.C — キーボードの受信記録 (KAPI v67 kbd_diag_log) と        */
/*  `kbdstat -w` の行をそのままホストで回す                                 */
/*                                                                          */
/*  実物を 1 行も写さずに #include する (-I で解決。変異は写しの上で):       */
/*    drivers/kbd_dlog.c      リングの純粋部                                */
/*    drivers/kbd.c           IRQ1 ハンドラ (0043h / 0041h は模型へ回す —   */
/*                            tools/tests/kbd_hostshim/io.h) と kbd_diag_log */
/*    userland/shell/kbd_watch.c  kbdstat -w の行の組み立て                 */
/*  見るのは (票 docs/tasks/gui/TASK_KBD_NAV.md §3、記録 kbd_dlog_tdd.md):    */
/*    (a) 積み方: seq は 1 から通し、32 件の循環で古い方から上書き           */
/*    (b) 写し方: after_seq より新しい分を古い順に、失われた分は飛ばす       */
/*        (先頭の seq の飛びで分かる)、max を超えて書かない                */
/*    (c) IRQ1: EMPTY / ERROR のバイトは積まない、OVERRUN は積んで印を付ける */
/*    (d) IRQ1: 修飾は配った**後**の値 (カナの make で KANA が立った行)      */
/*    (d') ロックキー (カナ・CAPS) は方式 B: make で ON、break で OFF、make の */
/*        繰り返しでも ON のまま。V86 中も追う。起動時は 053Ah から引き継ぐ  */
/*    (e) V86 / GUI の印、kbd_diag_log の引数検査と禁止区間の釣り合い        */
/*    (f) kbdstat -w の行と LOST 行の書式                                   */
/* ======================================================================== */
#include <stdio.h>
#include <string.h>

/* 型のサイズの STATIC_ASSERT は i386 前提 (u32 = unsigned long)。ホスト
 * (x86-64) では外し、--target の i386-elf ビルドで見る。 */
#include "types.h"
#undef STATIC_ASSERT
#define STATIC_ASSERT(cond, name) typedef char host_skip_static_assert_##name

/* kbd.c の 0000:053Ah の読みを模型 (kbd_host_bios_shift) へ回す */
#define KBD_HOST_TEST 1
u8 kbd_host_bios_shift;

#include "drivers/kbd_status.c"
#include "drivers/kbd_dlog.c"
#include "drivers/kbd.c"
#include "userland/shell/kbd_watch.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)
#define CHECK_STR(got, want) do { if (strcmp((got), (want)) != 0) { \
    fprintf(stderr, "FAIL %s:%d: got \"%s\" want \"%s\"\n", \
            __func__, __LINE__, (got), (want)); failed++; \
} } while (0)

static int failed;

/* ---- 8251 の模型と kbd.c の外部 ---------------------------------------- */
int kbd_shim_irq_depth;
static unsigned int shim_st;     /* 0043h が返す値 */
static unsigned int shim_data;   /* 0041h が返す値 */
static int shim_data_reads;

unsigned int kbd_shim_inp(unsigned int port)
{
    if (port == KBD_CMD) return shim_st;
    if (port == KBD_DATA) { shim_data_reads++; return shim_data; }
    return 0xFF;
}
void kbd_shim_outp(unsigned int port, unsigned int value) { (void)port; (void)value; }

volatile int exec_nest_level;
volatile u32 tick_count;
int rshell_active;
static int stub_v86;
static int stub_v86_pushed;
void irq_enable(unsigned int irq) { (void)irq; }
int  v86_is_active(void) { return stub_v86; }
void v86_request_exit(void) { }
int  v86_kbd_push(u8 scancode) { (void)scancode; stub_v86_pushed++; return 0; }
void ring3_abort_request(void) { }
int  exec_park_kbd(void) { return 0; }
int  exec_park_poll(u32 now_tick) { (void)now_tick; return 0; }
void appslot_poll_yield_reset(void) { }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int  serial_trygetchar(void) { return -1; }
int  serial_peekchar(void) { return -1; }
u32  kbd_inject_pending(void) { return 0; }
int  kbd_inject_take(u8 *out) { (void)out; return 0; }
int  kbd_inject_peek(u8 *out) { (void)out; return 0; }
void kbd_inject_discard(void) { }

/* 1 回の IRQ1: 0043h = st、0041h = data */
static void irq(unsigned int st, unsigned int data)
{
    shim_st = st;
    shim_data = data;
    (void)kbd_irq_handler();
    shim_st = 0;
}

static void fresh(void)
{
    shim_st = 0;         /* kbd_init の読み捨てループが空で抜ける */
    kbd_host_bios_shift = 0;
    stub_v86 = 0;
    stub_v86_pushed = 0;
    kbd_set_gui_mode(0);
    kbd_init();
    shim_data_reads = 0;
}

static void push_n(KbdDlog *l, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        kbd_dlog_push(l, (u8)(i & 0xFF), (u8)(i & 0x1F), 0);
    }
}

/* (a)(b) 積み方と写し方の基本 */
static void case_ring_basic(void)
{
    KbdDlog l;
    KbdDiagLogEnt out[KBD_DLOG_CAP];
    int n;

    kbd_dlog_reset(&l);
    CHECK(kbd_dlog_copy(&l, 0, out, KBD_DLOG_CAP) == 0);
    push_n(&l, 3);
    n = kbd_dlog_copy(&l, 0, out, KBD_DLOG_CAP);
    CHECK(n == 3);
    CHECK(out[0].seq == 1 && out[1].seq == 2 && out[2].seq == 3);
    CHECK(out[0].code == 0 && out[2].code == 2 && out[2].mods == 2);
    n = kbd_dlog_copy(&l, 2, out, KBD_DLOG_CAP);
    CHECK(n == 1 && out[0].seq == 3);
    CHECK(kbd_dlog_copy(&l, 3, out, KBD_DLOG_CAP) == 0);
    CHECK(kbd_dlog_copy(&l, 99, out, KBD_DLOG_CAP) == 0);
    /* max で打ち切る (古い方から) */
    n = kbd_dlog_copy(&l, 0, out, 2);
    CHECK(n == 2 && out[0].seq == 1 && out[1].seq == 2);
    CHECK(kbd_dlog_copy(&l, 0, out, 0) == 0);
    CHECK(kbd_dlog_copy(&l, 0, out, -1) == 0);
}

/* (a)(b) 循環: 溢れたら古い方を上書き、失われた分は飛ばす */
static void case_ring_wrap(void)
{
    KbdDlog l;
    KbdDiagLogEnt out[KBD_DLOG_CAP + 1];
    int n, i, ok;

    kbd_dlog_reset(&l);
    push_n(&l, KBD_DLOG_CAP);           /* ちょうど満杯: 何も失われていない */
    n = kbd_dlog_copy(&l, 0, out, KBD_DLOG_CAP);
    CHECK(n == KBD_DLOG_CAP && out[0].seq == 1 && out[n - 1].seq == KBD_DLOG_CAP);

    push_n(&l, 8);                      /* 40 件: 1..8 は上書きで消えた */
    n = kbd_dlog_copy(&l, 0, out, KBD_DLOG_CAP);
    CHECK(n == KBD_DLOG_CAP);
    CHECK(out[0].seq == 9);             /* 飛び = 取りこぼしの印 */
    CHECK(out[n - 1].seq == 40);
    ok = 1;
    for (i = 1; i < n; i++) ok &= (out[i].seq == out[i - 1].seq + 1);
    CHECK(ok);                          /* 古い順、連続 */
    CHECK(out[0].code == 8);            /* seq 9 = 1 回目の push_n の i = 8 */
    CHECK(kbdw_lost(0, out[0].seq) == 8);

    n = kbd_dlog_copy(&l, 5, out, KBD_DLOG_CAP);   /* 5 は消えている */
    CHECK(n == KBD_DLOG_CAP && out[0].seq == 9);
    n = kbd_dlog_copy(&l, 20, out, KBD_DLOG_CAP);
    CHECK(n == 20 && out[0].seq == 21 && out[19].seq == 40);

    /* max を超えて書かない (番兵) */
    memset(out, 0xA5, sizeof(out));
    n = kbd_dlog_copy(&l, 30, out, 4);
    CHECK(n == 4 && out[0].seq == 31 && out[3].seq == 34);
    CHECK(out[4].code == 0xA5 && out[4].mods == 0xA5);
}

/* (c) IRQ1: EMPTY / ERROR は積まない、OVERRUN は積んで印を付ける */
static void case_irq_skip(void)
{
    KbdDiagLogEnt out[KBD_DLOG_CAP];
    KbdDiag d;
    int n;

    fresh();
    irq(0x00, 0x05);                          /* EMPTY (RxRDY = 0) */
    irq(0x85, 0x05);                          /* EMPTY (NP21/W の定常ビットだけ) */
    irq(KBD_STAT_RXRDY | KBD_STAT_FE, 0x06);  /* ERROR: 読み捨て */
    irq(KBD_STAT_RXRDY | KBD_STAT_PE | KBD_STAT_OE, 0x07);  /* ERROR (OE と重なっても) */
    CHECK(kbd_diag_log(0, out, KBD_DLOG_CAP) == 0);
    irq(KBD_STAT_RXRDY, 0x1D);                /* DATA: 'a' の make */
    irq(KBD_STAT_RXRDY | KBD_STAT_OE, 0x9D);  /* OVERRUN: バイトは使う */
    n = kbd_diag_log(0, out, KBD_DLOG_CAP);
    CHECK(n == 2);
    CHECK(out[0].seq == 1 && out[0].code == 0x1D && out[0].flags == 0);
    CHECK(out[1].seq == 2 && out[1].code == 0x9D);
    CHECK(out[1].flags == KBD_DLOG_F_OVERRUN);
    /* 捨てた分は別の数え (KbdDiag) にある */
    CHECK(kbd_diag(&d) == 0);
    CHECK(d.empty_count == 2 && d.err_count == 2 && d.overrun_count == 1);
    CHECK(d.irq_count == 6);
    CHECK(kbd_shim_irq_depth == 0);
}

/* (d) 修飾は配った後の値。カナ・CAPS は方式 B (make で ON、break で OFF) */
static void case_irq_mods(void)
{
    KbdDiagLogEnt out[KBD_DLOG_CAP];
    int n;

    fresh();
    irq(KBD_STAT_RXRDY, 0x72);   /* KANA make  → KANA */
    irq(KBD_STAT_RXRDY, 0xF2);   /* KANA break → 解除 (方式 B) */
    irq(KBD_STAT_RXRDY, 0x72);   /* KANA make  → KANA (もう一度ロック) */
    irq(KBD_STAT_RXRDY, 0x70);   /* SHIFT make → SHIFT */
    irq(KBD_STAT_RXRDY, 0x71);   /* CAPS make  → SHIFT|CAPS */
    irq(KBD_STAT_RXRDY, 0xF0);   /* SHIFT break → CAPS */
    n = kbd_diag_log(0, out, KBD_DLOG_CAP);
    CHECK(n == 6);
    CHECK(out[0].code == 0x72 && out[0].mods == KBD_DLOG_MOD_KANA);
    CHECK(out[1].code == 0xF2 && out[1].mods == 0);
    CHECK(out[2].code == 0x72 && out[2].mods == KBD_DLOG_MOD_KANA);
    CHECK(out[3].mods == (KBD_DLOG_MOD_KANA | KBD_DLOG_MOD_SHIFT));
    CHECK(out[4].mods == (KBD_DLOG_MOD_KANA | KBD_DLOG_MOD_SHIFT | KBD_DLOG_MOD_CAPS));
    CHECK(out[5].mods == (KBD_DLOG_MOD_KANA | KBD_DLOG_MOD_CAPS));
    CHECK((u32)out[5].mods == kbd_get_modifiers());
    /* 読み手の続き: 最後に読んだ seq を渡すと新しい分だけ */
    irq(KBD_STAT_RXRDY, 0x00);   /* ESC make */
    n = kbd_diag_log(6, out, KBD_DLOG_CAP);
    CHECK(n == 1 && out[0].seq == 7 && out[0].code == 0x00);
}

/* (e) V86 / GUI の印、引数検査、IRQ が 32 件を超えたときの飛び */
static void case_irq_flags_api(void)
{
    KbdDiagLogEnt out[KBD_DLOG_CAP + 8];
    int n, i;

    fresh();
    stub_v86 = 1;
    irq(KBD_STAT_RXRDY, 0x72);   /* V86 へ回す: ロックの状態だけは OS32 側でも追う */
    irq(KBD_STAT_RXRDY, 0x70);   /* SHIFT はゲストのもの: OS32 側で更新しない */
    stub_v86 = 0;
    kbd_set_gui_mode(1);
    irq(KBD_STAT_RXRDY, 0x1D);
    kbd_set_gui_mode(0);
    n = kbd_diag_log(0, out, KBD_DLOG_CAP);
    CHECK(n == 3);
    CHECK(out[0].flags == KBD_DLOG_F_V86 && out[0].mods == KBD_DLOG_MOD_KANA);
    CHECK(out[1].flags == KBD_DLOG_F_V86 && out[1].mods == KBD_DLOG_MOD_KANA);
    CHECK(stub_v86_pushed == 2);
    CHECK(kbd_is_pressed(0x72) == 0 && kbd_is_pressed(0x70) == 0);
    CHECK(out[2].flags == KBD_DLOG_F_GUI && out[2].mods == KBD_DLOG_MOD_KANA);

    CHECK(kbd_diag_log(0, (KbdDiagLogEnt *)0, 4) == OS32_ERR_INVAL);
    CHECK(kbd_diag_log(0, out, 0) == OS32_ERR_INVAL);
    CHECK(kbd_diag_log(0, out, -3) == OS32_ERR_INVAL);

    /* 40 バイト届いて読み手が追いつかなかった: 先頭は seq 9 (1..8 が消えた) */
    fresh();
    for (i = 0; i < 40; i++) irq(KBD_STAT_RXRDY, 0x1D);
    memset(out, 0xA5, sizeof(out));
    n = kbd_diag_log(0, out, KBD_DLOG_CAP + 8);   /* 大きな max は CAP で頭打ち */
    CHECK(n == KBD_DLOG_CAP);
    CHECK(out[0].seq == 9 && out[n - 1].seq == 40);
    CHECK(out[KBD_DLOG_CAP].code == 0xA5);
    CHECK(kbd_shim_irq_depth == 0);
    /* kbd_init でリングも seq も 0 から */
    fresh();
    CHECK(kbd_diag_log(0, out, KBD_DLOG_CAP) == 0);
    irq(KBD_STAT_RXRDY, 0x1D);
    n = kbd_diag_log(0, out, KBD_DLOG_CAP);
    CHECK(n == 1 && out[0].seq == 1);
}

/* (d') ロックキーの方式 B。raw リング (GUI 中) と cooked (CUI 中) の両方と、
 * 起動時の初期値 (0000:053Ah) を見る。 */
static void case_irq_locks(void)
{
    KbdDiagLogEnt out[KBD_DLOG_CAP];
    int n, r;

    /* KANA: make で ON、make の繰り返しでも ON のまま、break で OFF */
    fresh();
    irq(KBD_STAT_RXRDY, 0x72);
    CHECK(kbd_get_modifiers() == SHIFT_KANA);
    irq(KBD_STAT_RXRDY, 0x72);
    CHECK(kbd_get_modifiers() == SHIFT_KANA);
    irq(KBD_STAT_RXRDY, 0xF2);
    CHECK(kbd_get_modifiers() == 0);
    irq(KBD_STAT_RXRDY, 0xF2);   /* break の重複でも OFF のまま */
    CHECK(kbd_get_modifiers() == 0);
    n = kbd_diag_log(0, out, KBD_DLOG_CAP);
    CHECK(n == 4);
    CHECK(out[0].mods == KBD_DLOG_MOD_KANA && out[1].mods == KBD_DLOG_MOD_KANA);
    CHECK(out[2].mods == 0 && out[3].mods == 0);

    /* CAPS: 同じ扱い。KANA とは独立 */
    fresh();
    irq(KBD_STAT_RXRDY, 0x72);
    irq(KBD_STAT_RXRDY, 0x71);
    irq(KBD_STAT_RXRDY, 0x71);
    CHECK(kbd_get_modifiers() == (SHIFT_CAPS | SHIFT_KANA));
    irq(KBD_STAT_RXRDY, 0xF1);
    CHECK(kbd_get_modifiers() == SHIFT_KANA);
    irq(KBD_STAT_RXRDY, 0xF2);
    CHECK(kbd_get_modifiers() == 0);

    /* cooked: CAPS を外したら小文字に戻る (A = key 0x1D) */
    fresh();
    irq(KBD_STAT_RXRDY, 0x71);
    irq(KBD_STAT_RXRDY, 0x1D);
    irq(KBD_STAT_RXRDY, 0x9D);
    irq(KBD_STAT_RXRDY, 0xF1);
    irq(KBD_STAT_RXRDY, 0x1D);
    CHECK(kbd_trygetkey() == ((0x1D << 8) | 'A'));
    CHECK(kbd_trygetkey() == ((0x1D << 8) | 'a'));

    /* raw (GUI 中): 修飾キー自身の行は更新後の状態を載せる */
    fresh();
    kbd_set_gui_mode(1);
    irq(KBD_STAT_RXRDY, 0x72);
    irq(KBD_STAT_RXRDY, 0x72);
    irq(KBD_STAT_RXRDY, 0xF2);
    r = kbd_trygetrawkey();
    CHECK(r == (0x72 | 0x100 | (SHIFT_KANA << 9)));
    r = kbd_trygetrawkey();
    CHECK(r == (0x72 | 0x100 | (SHIFT_KANA << 9)));
    r = kbd_trygetrawkey();
    CHECK(r == 0x72);            /* break、KANA は落ちている */
    CHECK(kbd_trygetrawkey() == -1);
    kbd_set_gui_mode(0);

    /* 起動時: 053Ah のカナ・CAPS だけを引き継ぐ (SHIFT / GRPH / CTRL は捨てる) */
    shim_st = 0;
    kbd_host_bios_shift = 0x1F;
    kbd_init();
    CHECK(kbd_get_modifiers() == (SHIFT_CAPS | SHIFT_KANA));
    irq(KBD_STAT_RXRDY, 0xF2);   /* 起動後に外すと落ちる */
    CHECK(kbd_get_modifiers() == SHIFT_CAPS);
    kbd_host_bios_shift = BIOS_KB_SHIFT_KANA;
    kbd_init();
    CHECK(kbd_get_modifiers() == SHIFT_KANA);
    kbd_host_bios_shift = 0x19;  /* SHIFT | GRPH | CTRL だけ */
    kbd_init();
    CHECK(kbd_get_modifiers() == 0);
    kbd_host_bios_shift = 0;
}

/* (f) kbdstat -w の行 */
static void case_watch_fmt(void)
{
    char buf[KBDW_LINE_MAX];
    KbdDiagLogEnt e;
    int len;

    memset(&e, 0, sizeof(e));
    e.seq = 12; e.code = 0xF2; e.mods = KBD_DLOG_MOD_CAPS | KBD_DLOG_MOD_KANA;
    len = kbdw_fmt_ent(buf, (int)sizeof(buf), &e);
    CHECK_STR(buf, "seq=12 code=F2 break key=72 KANA mods=CAPS|KANA");
    CHECK(len == (int)strlen(buf));

    e.seq = 1; e.code = 0x72; e.mods = KBD_DLOG_MOD_KANA;
    (void)kbdw_fmt_ent(buf, (int)sizeof(buf), &e);
    CHECK_STR(buf, "seq=1 code=72 make  key=72 KANA mods=KANA");

    e.seq = 4294967295UL; e.code = 0x1D; e.mods = 0;
    e.flags = KBD_DLOG_F_OVERRUN | KBD_DLOG_F_V86 | KBD_DLOG_F_GUI;
    (void)kbdw_fmt_ent(buf, (int)sizeof(buf), &e);
    CHECK_STR(buf, "seq=4294967295 code=1D make  key=1D mods=- [OE] [V86] [GUI]");

    e.seq = 7; e.code = 0x00; e.mods = 0; e.flags = 0;
    (void)kbdw_fmt_ent(buf, (int)sizeof(buf), &e);
    CHECK_STR(buf, "seq=7 code=00 make  key=00 ESC mods=-");

    (void)kbdw_fmt_mods(buf, (int)sizeof(buf), 0x1F);
    CHECK_STR(buf, "SHIFT|CAPS|KANA|GRPH|CTRL");
    (void)kbdw_fmt_mods(buf, (int)sizeof(buf), KBD_DLOG_MOD_GRPH | KBD_DLOG_MOD_SHIFT);
    CHECK_STR(buf, "SHIFT|GRPH");
    (void)kbdw_fmt_mods(buf, (int)sizeof(buf), KBD_DLOG_MOD_CTRL);
    CHECK_STR(buf, "CTRL");

    /* 切り詰めても NUL 終端、cap を超えない */
    memset(buf, 'X', sizeof(buf));
    len = kbdw_fmt_ent(buf, 8, &e);
    CHECK(len == 7 && buf[7] == '\0' && buf[8] == 'X');
    CHECK_STR(buf, "seq=7 c");
}

/* (f) 取りこぼしの行 */
static void case_watch_lost(void)
{
    char buf[KBDW_LINE_MAX];

    CHECK(kbdw_lost(0, 1) == 0);
    CHECK(kbdw_lost(5, 6) == 0);
    CHECK(kbdw_lost(5, 7) == 1);
    CHECK(kbdw_lost(12, 21) == 8);
    CHECK(kbdw_fmt_lost(buf, (int)sizeof(buf), 5, 6) == 0 && buf[0] == '\0');
    (void)kbdw_fmt_lost(buf, (int)sizeof(buf), 12, 21);
    CHECK_STR(buf, "LOST seq=13..20 (8): cannot judge this span");
    (void)kbdw_fmt_lost(buf, (int)sizeof(buf), 5, 7);
    CHECK_STR(buf, "LOST seq=6..6 (1): cannot judge this span");
}

static const struct { const char *name; void (*fn)(void); } cases[] = {
    { "ring_basic",     case_ring_basic },
    { "ring_wrap",      case_ring_wrap },
    { "irq_skip",       case_irq_skip },
    { "irq_mods",       case_irq_mods },
    { "irq_flags_api",  case_irq_flags_api },
    { "irq_locks",      case_irq_locks },
    { "watch_fmt",      case_watch_fmt },
    { "watch_lost",     case_watch_lost },
};

int main(int argc, char **argv)
{
    unsigned int i;
    int ran = 0;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (argc > 1 && strcmp(argv[1], cases[i].name) != 0) continue;
        cases[i].fn();
        ran++;
    }
    if (!ran) {
        fprintf(stderr, "unknown case %s\n", argc > 1 ? argv[1] : "");
        return 2;
    }
    return failed ? 1 : 0;
}
