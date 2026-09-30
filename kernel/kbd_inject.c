/* ======================================================================== */
/*  KBD_INJECT.C — 打鍵の注入リング (票 TASK_K7_input.md §1 D2 / §5)        */
/*                                                                          */
/*  設計は include/kbd_inject.h の冒頭。ここに置く判断は 3 つ:               */
/*                                                                          */
/*  1. 容量 256B、カーネル帯の静的配列 (kmalloc しない)。あふれは            */
/*     **新しい方を捨てる** — 打鍵は順序が意味を持つので、古い方を捨てると   */
/*     打った文字列の頭が欠ける。捨てた分は戻り値 (< len) で呼び手に返る。   */
/*  2. 権限は con_sink の読み手 1 本 (票 §5 R2)。ここに 2 つ目の所有者表を   */
/*     作らない — 端末アプリは con_sink_read で読み手になってから注入する。  */
/*  3. push は CPL=3 の syscall 文脈から、take は syscall 文脈と             */
/*     exec_resume から。IRQ 文脈からは触らないが、take 側は hlt を挟む      */
/*     ループの中で回るので、con_sink と同じく IF 退避 (irq_save/restore)    */
/*     で守る。単一プロデューサを仮定しない。                                */
/*                                                                          */
/*  ホスト試験 (tools/tests/kbd_inject_host.c) はこの .c をそのまま          */
/*  #include して回す。                                                      */
/* ======================================================================== */

#include "os32_kapi_shared.h"
#include "kbd_inject.h"
#include "con_sink.h"          /* CON_SINK_NO_READER / con_sink_reader_get */

/* 割込み禁止区間。ホスト試験は CPL=3 で走り cli/popfl を実行できないので、
 * そこだけ空の錠に差し替える (con_sink.c と同じ流儀)。カーネル本体は必ず
 * include/io.h の irq_save/irq_restore を使う。 */
#ifdef KBD_INJECT_NO_IRQ_LOCK
static unsigned int kbd_inject_lock(void)      { return 0; }
static void kbd_inject_unlock(unsigned int f)  { (void)f; }
#else
#include "io.h"
static unsigned int kbd_inject_lock(void)      { return irq_save(); }
static void kbd_inject_unlock(unsigned int f)  { irq_restore(f); }
#endif

/* res_owner_get() は fs/fd_redirect.c。kernel/ は -Ifs を持つが、con_sink.c
 * と同じく宣言だけを引く (fd_redirect.h は FD 表の型まで引き込むため)。 */
extern int res_owner_get(void);

/* ------------------------------------------------------------------------ */
/*  状態                                                                     */
/*                                                                          */
/*  名前に inj_ を付けてあるのは、ホスト試験 (tools/tests/kbd_inject_host.c) */
/*  が kernel/con_sink.c と**同じ翻訳単位**へ両方を #include して、権限の    */
/*  経路 (「注げるのは con_sink の読み手だけ」) を模型で置き換えずに通す     */
/*  ため。con_sink.c 側の g_ring / g_head / ring_reset とぶつからない。      */
/* ------------------------------------------------------------------------ */

static u8  g_inj_ring[KBD_INJECT_RING_SIZE];
static u32 g_inj_head;                  /* 次に書く位置 */
static u32 g_inj_tail;                  /* 次に読む位置 */
static u32 g_inj_count;                 /* 使用バイト数 */

/* あふれで捨てたバイト数の累計。con_sink_drop_count と同じ理由で static に
 * しない — 実機では emu_read_mem で kernel.map の番地から読む。 */
volatile u32 kbd_inject_drop_count = 0;

/* ------------------------------------------------------------------------ */
/*  リングの素操作 (すべて錠の中で呼ぶこと)                                  */
/* ------------------------------------------------------------------------ */

/* 入る分だけ積んで、積んだバイト数を返す。あふれた分は捨てて数える。 */
static u32 inj_push(const u8 *p, u32 len)
{
    u32 n = 0;
    while (n < len && g_inj_count < (u32)KBD_INJECT_RING_SIZE) {
        g_inj_ring[g_inj_head] = p[n];
        g_inj_head++;
        if (g_inj_head >= (u32)KBD_INJECT_RING_SIZE) g_inj_head = 0;
        g_inj_count++;
        n++;
    }
    if (n < len) kbd_inject_drop_count += (len - n);
    return n;
}

static int inj_take(u8 *out)
{
    if (g_inj_count == 0) return 0;
    *out = g_inj_ring[g_inj_tail];
    g_inj_tail++;
    if (g_inj_tail >= (u32)KBD_INJECT_RING_SIZE) g_inj_tail = 0;
    g_inj_count--;
    return 1;
}

/* 取り出さずに先頭だけ写す。空なら 0 (*out 不変)。 */
static int inj_peek(u8 *out)
{
    if (g_inj_count == 0) return 0;
    *out = g_inj_ring[g_inj_tail];
    return 1;
}

static void inj_reset(void)
{
    g_inj_head = 0;
    g_inj_tail = 0;
    g_inj_count = 0;
}

/* ------------------------------------------------------------------------ */
/*  KAPI v47                                                                 */
/* ------------------------------------------------------------------------ */

i32 kbd_inject(const u8 *utf8, u32 len)
{
    unsigned int f;
    u32 n;
    int reader;

    if (!utf8) return (i32)OS32_ERR_INVAL;

    /* 票 §5 R2: 読み手が確立していない段階の注入は拒否する。端末アプリは
     * イベントループに入る前に con_sink_read を 1 回呼ぶ規約。 */
    reader = con_sink_reader_get();
    if (reader == CON_SINK_NO_READER) return (i32)OS32_ERR_EXIST;
    if (res_owner_get() != reader)    return (i32)OS32_ERR_EXIST;

    if (len == 0) return 0;

    f = kbd_inject_lock();
    n = inj_push(utf8, len);
    kbd_inject_unlock(f);
    return (i32)n;
}

u32 kbd_inject_pending(void)
{
    unsigned int f = kbd_inject_lock();
    u32 n = g_inj_count;
    kbd_inject_unlock(f);
    return n;
}

/* ------------------------------------------------------------------------ */
/*  カーネル内から                                                           */
/* ------------------------------------------------------------------------ */

int kbd_inject_take(u8 *out)
{
    unsigned int f;
    int got;

    if (!out) return 0;
    f = kbd_inject_lock();
    got = inj_take(out);
    kbd_inject_unlock(f);
    return got;
}

int kbd_inject_peek(u8 *out)
{
    unsigned int f;
    int got;

    if (!out) return 0;
    f = kbd_inject_lock();
    got = inj_peek(out);
    kbd_inject_unlock(f);
    return got;
}

void kbd_inject_discard(void)
{
    unsigned int f = kbd_inject_lock();
    inj_reset();
    kbd_inject_unlock(f);
}

void kbd_inject_owner_exit(int id)
{
    if (id == CON_SINK_NO_READER) return;
    /* con_sink_owner_exit より前に呼ばれる前提なので、g_reader はまだこの
     * ID を指している。注ぎ手が退場したら残りは古い打鍵にしかならない。 */
    if (con_sink_reader_get() != id) return;
    kbd_inject_discard();
}

/* ------------------------------------------------------------------------ */
/*  自己診断 (kernel/kselftest.c から、ブート時に 1 回)                      */
/*                                                                          */
/*  票 K7 の受入 I5 の半分 (もう半分 = 印なし resume の拒否は appslot.c)。   */
/*  実機で毎回踏むのは「積んだ順に 1 バイトずつ出る」「UTF-8 の並びが        */
/*  変わらない」「あふれは新しい方を捨てる」「破棄で空になる」「読み手が     */
/*  居ない間の注入は拒否」の 5 つ。権限が**通る**側はホスト試験              */
/*  (tools/tests/kbd_inject_host.c) が con_sink の読み手を作って見る —       */
/*  ここで作ると CON_SINK_REC_MAX の受け皿をカーネル帯に常駐させることに     */
/*  なり、256B で収めた意味がなくなる。                                      */
/*                                                                          */
/*  inj_push を直に叩くのは同じ翻訳単位に居るからで、権限を迂回する口を     */
/*  外へ出しているわけではない。                                             */
/* ------------------------------------------------------------------------ */
u32 kbd_inject_selftest(void)
{
    u32 bad = 0;
    u32 saved_drop = kbd_inject_drop_count;
    u8  b;
    u32 i;

    /* 日本語 1 文字 "あ" = E3 81 82 (UTF-8)。バイト順がそのまま出ること。 */
    static const u8 utf8_a[3] = { 0xE3, 0x81, 0x82 };

    kbd_inject_discard();

    /* (0) 読み手が未確立なら拒否され、リングは空のまま */
    if (kbd_inject((const u8 *)"A", 1) != (i32)OS32_ERR_EXIST) bad |= 1u << 0;
    if (kbd_inject((const u8 *)0, 1) != (i32)OS32_ERR_INVAL)   bad |= 1u << 0;
    if (kbd_inject_pending() != 0)                             bad |= 1u << 0;

    /* (1) 積んだ順に 1 バイトずつ出る (UTF-8 の並びは変えない) */
    if (inj_push(utf8_a, 3) != 3)    bad |= 1u << 1;
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
            pushed += inj_push(&one, 1);
        }
        if (pushed != (u32)KBD_INJECT_RING_SIZE) bad |= 1u << 3;
        one = 'Z';
        if (inj_push(&one, 1) != 0) bad |= 1u << 3;
        if (kbd_inject_pending() != (u32)KBD_INJECT_RING_SIZE) bad |= 1u << 3;
        b = 0xFF;
        if (!kbd_inject_take(&b) || b != 0) bad |= 1u << 3;   /* 最初の 1 本 */
    }

    /* (4) 破棄で空になり、環の巻き戻しも起きない */
    kbd_inject_discard();
    if (kbd_inject_pending() != 0) bad |= 1u << 4;
    if (inj_push((const u8 *)"xy", 2) != 2) bad |= 1u << 4;
    b = 0;
    if (!kbd_inject_take(&b) || b != 'x') bad |= 1u << 4;
    b = 0;
    if (!kbd_inject_take(&b) || b != 'y') bad |= 1u << 4;

    /* (5) 覗きは**取り出さない** (KAPI v54 の kbd_peekkey が立つ土台) */
    kbd_inject_discard();
    b = 0x5A;
    if (kbd_inject_peek(&b) != 0 || b != 0x5A) bad |= 1u << 5;   /* 空は 0 */
    if (inj_push((const u8 *)"pq", 2) != 2) bad |= 1u << 5;
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
    kbd_inject_drop_count = saved_drop;
    return bad;
}
