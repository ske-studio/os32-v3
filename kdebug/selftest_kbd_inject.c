#include "os32_kapi_shared.h"
#include "kbd_inject.h"
#include "appslot.h"
#include "con_sink.h"
#include "kselftest_probe.h"
#include "selftest_internal.h"

/* ホスト fixture はデータの SETTER だけを自前でつなぐ。 */
#ifdef KDEBUG_SELFTEST_HOST
extern void kdi_set_kbd_inject_drop_count(u32 value);
#else
#include "generated/kdebug_import_macros.h"
#endif

/* ------------------------------------------------------------------------ */
/*  自己診断 (kdebug/kselftest.c から、ブート時に 1 回)                      */
/*                                                                          */
/*  票 K7 の受入 I5 の半分 (もう半分 = 印なし resume の拒否は appslot.c)。   */
/*  実機で毎回踏むのは「積んだ順に 1 バイトずつ出る」「UTF-8 の並びが        */
/*  変わらない」「あふれは新しい方を捨てる」「破棄で空になる」「読み手が     */
/*  居ない間の注入は拒否」の 5 つ。権限が**通る**側はホスト試験              */
/*  (tools/tests/kbd_inject_host.c) が con_sink の読み手を作って見る —       */
/*  ここで作ると CON_SINK_REC_MAX の受け皿をカーネル帯に常駐させることに     */
/*  なり、256B で収めた意味がなくなる。                                      */
/*                                                                          */
/*  素操作は起動自己診断専用の型付き口を通す。呼出し条件は移設前と同じ。   */
/* ------------------------------------------------------------------------ */
u32 kst_kbd_inject(void)
{
    u32 bad = 0;
    u32 saved_drop = kbd_inject_drop_count;
    u8  b;
    u32 i;

    /* 日本語 1 文字 "あ" = E3 81 82 (UTF-8)。バイト順がそのまま出ること。 */
    static const u8 utf8_a[3] = { 0xE3, 0x81, 0x82 };

    kbd_inject_discard();

    /* (0) Boot has no fullscreen application or console reader. Keep the
     * expected rejection independent of the authorization implementation. */
    if (appslot_gfx_owner() >= APP_ID_MIN) bad |= 1u << 0;
    if (con_sink_reader_get() != CON_SINK_NO_READER) bad |= 1u << 0;
    if (kbd_inject((const u8 *)"A", 1) != (i32)OS32_ERR_EXIST) bad |= 1u << 0;
    if (kbd_inject((const u8 *)0, 1) != (i32)OS32_ERR_INVAL)   bad |= 1u << 0;
    if (kbd_inject_pending() != 0)                             bad |= 1u << 0;

    /* (1) 積んだ順に 1 バイトずつ出る (UTF-8 の並びは変えない) */
    if (kdi_selftest_inj_push(utf8_a, 3, appslot_cur()) != 3)    bad |= 1u << 1;
    if (kbd_inject_pending() != 3)    bad |= 1u << 1;
    for (i = 0; i < 3; i++) {
        b = 0;
        if (!kbd_inject_take(&b) || b != utf8_a[i]) bad |= 1u << 1;
    }
    if (kbd_inject_pending() != 0)    bad |= 1u << 1;

    /* (2) 空の take は 0 を返し、出力を触らない */
    b = 0x5A;
    if (kbd_inject_take(&b) != 0 || b != 0x5A) bad |= 1u << 2;

    /* (3) あふれは**新しい方**を捨てる: 容量ちょうど積んだ後の 1 バイトは
     * 入らず、先頭は最初に積んだ 1 バイトのまま */
    {
        u32 pushed = 0;
        u8  one;
        /* 受け皿の静的配列は置かない (256B に収めた意味が消える) */
        for (i = 0; i < (u32)KBD_INJECT_RING_SIZE; i++) {
            one = (u8)(i & 0xFF);
            pushed += kdi_selftest_inj_push(&one, 1, appslot_cur());
        }
        if (pushed != (u32)KBD_INJECT_RING_SIZE) bad |= 1u << 3;
        one = 'Z';
        if (kdi_selftest_inj_push(&one, 1, appslot_cur()) != 0) bad |= 1u << 3;
        if (kbd_inject_pending() != (u32)KBD_INJECT_RING_SIZE) bad |= 1u << 3;
        b = 0xFF;
        if (!kbd_inject_take(&b) || b != 0) bad |= 1u << 3;   /* 最初の 1 本 */
    }

    /* (4) 破棄で空になり、環の巻き戻しも起きない */
    kbd_inject_discard();
    if (kbd_inject_pending() != 0) bad |= 1u << 4;
    if (kdi_selftest_inj_push((const u8 *)"xy", 2, appslot_cur()) != 2) bad |= 1u << 4;
    b = 0;
    if (!kbd_inject_take(&b) || b != 'x') bad |= 1u << 4;
    b = 0;
    if (!kbd_inject_take(&b) || b != 'y') bad |= 1u << 4;

    /* (5) 覗きは**取り出さない** (KAPI v54 の kbd_peekkey が立つ土台) */
    kbd_inject_discard();
    b = 0x5A;
    if (kbd_inject_peek(&b) != 0 || b != 0x5A) bad |= 1u << 5;   /* 空は 0 */
    if (kdi_selftest_inj_push((const u8 *)"pq", 2, appslot_cur()) != 2) bad |= 1u << 5;
    b = 0;
    if (!kbd_inject_peek(&b) || b != 'p') bad |= 1u << 5;
    b = 0;
    if (!kbd_inject_peek(&b) || b != 'p') bad |= 1u << 5;   /* 何度でも同じ */
    if (kbd_inject_pending() != 2) bad |= 1u << 5;          /* 減っていない */
    b = 0;
    if (!kbd_inject_take(&b) || b != 'p') bad |= 1u << 5;   /* 取り出せば進む */
    b = 0;
    if (!kbd_inject_peek(&b) || b != 'q') bad |= 1u << 5;

    /* 後始末: ブートの続きに持ち越さない */
    kbd_inject_discard();
    kdi_set_kbd_inject_drop_count(saved_drop);
    return bad;
}
