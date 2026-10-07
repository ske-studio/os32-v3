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
#include "appslot.h"
#include "launch.h"

extern volatile int ring3_wm_depth;
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
/* 0 preserves the terminal-reader stream; WM bytes retain their owner until
 * that owner exits, even if the active fullscreen owner changes. */
static u8  g_inj_dest[KBD_INJECT_RING_SIZE];
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
static u32 inj_push(const u8 *p, u32 len, int dest)
{
    u32 n = 0;
    while (n < len && g_inj_count < (u32)KBD_INJECT_RING_SIZE) {
        g_inj_ring[g_inj_head] = p[n];
        g_inj_dest[g_inj_head] = (u8)dest;
        g_inj_head++;
        if (g_inj_head >= (u32)KBD_INJECT_RING_SIZE) g_inj_head = 0;
        g_inj_count++;
        n++;
    }
    if (n < len) kbd_inject_drop_count += (len - n);
    return n;
}

/* Compute all ancestors once per read, including mixed synchronous/launch
 * chains. Bounds also make malformed cycles finite; no IRQ-time allocation. */
static void inj_readable(int id, u8 allowed[APP_SLOT_COUNT])
{
    int n, parent;
    for (n = 0; n < APP_SLOT_COUNT; n++) allowed[n] = 0;
    allowed[0] = 1;                 /* legacy terminal stream */
    if (id < APP_ID_MIN || id > APP_ID_MAX) return;
    allowed[id] = 1;
    for (n = 0; n < APP_MAX_APPS; n++) {
        for (parent = APP_ID_MIN; parent <= APP_ID_MAX; parent++) {
            AppSlot *slot = appslot_get(parent);
            int child = (int)launch_child(parent);
            if (child >= APP_ID_MIN && child <= APP_ID_MAX && allowed[child])
                allowed[parent] = 1;
            if (allowed[parent] && slot && slot->parent >= APP_ID_MIN &&
                slot->parent <= APP_ID_MAX) allowed[slot->parent] = 1;
        }
    }
}

/* Remove a single entry, preserving FIFO order of every other destination.
 * At most 255 entries move; a normal head removal is constant time. */
static void inj_remove(u32 pos)
{
    if (pos == g_inj_tail) {
        g_inj_tail = (g_inj_tail + 1) % KBD_INJECT_RING_SIZE;
    } else {
        u32 next = (pos + 1) % KBD_INJECT_RING_SIZE;
        while (next != g_inj_head) {
            g_inj_ring[pos] = g_inj_ring[next];
            g_inj_dest[pos] = g_inj_dest[next];
            pos = next;
            next = (next + 1) % KBD_INJECT_RING_SIZE;
        }
        g_inj_head = pos;
    }
    g_inj_count--;
}

static int inj_read(u8 *out, int id, int consume)
{
    u8 allowed[APP_SLOT_COUNT];
    u32 n, pos = g_inj_tail;
    if (g_inj_count == 0) return 0;
    inj_readable(id, allowed);
    for (n = 0; n < g_inj_count; n++) {
        if (allowed[g_inj_dest[pos]]) {
            *out = g_inj_ring[pos];
            if (consume) inj_remove(pos);
            return 1;
        }
        pos = (pos + 1) % KBD_INJECT_RING_SIZE;
    }
    return 0;
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

/* The WM pumps on a suspended application's stack, or runs as slot 1.
 * res_owner_get() alone identifies neither case. Async GUI children belong to
 * launch rows (slot.parent is WM); synchronous children use slot.parent.
 * Both kernel-owned chains are bounded by APP_MAX_APPS. */
static int inj_fullscreen_wm(int reader)
{
    int id = appslot_gfx_owner();
    int n;
    if (id < APP_ID_MIN || id > APP_ID_MAX) return 0;
    if (ring3_wm_depth <= 0 && appslot_cur() != APP_ID_SHELL) return 0;
    {
        int child = reader;
        for (n = 0; n < APP_MAX_APPS && child >= APP_ID_MIN; n++) {
            if (child == id) return 0;
            child = (int)launch_child(child);
        }
    }
    for (n = 0; n < APP_MAX_APPS; n++) {
        AppSlot *slot;
        if (id == reader) return 0;
        if (id == APP_ID_SHELL) return 1;
        slot = appslot_get(id);
        if (!slot) return 0;
        id = slot->parent;
    }
    return id == APP_ID_SHELL;
}

i32 kbd_inject(const u8 *utf8, u32 len)
{
    unsigned int f;
    u32 n;
    int reader, dest = 0;

    if (!utf8) return (i32)OS32_ERR_INVAL;

    /* Reader permission is unchanged; only the fullscreen WM exception may
     * operate without it. Terminals establish the reader before their loop. */
    reader = con_sink_reader_get();
    if (inj_fullscreen_wm(reader)) {
        dest = appslot_gfx_owner();
    } else {
        /* A WM pump runs with the suspended terminal's resource owner. It
         * must not borrow that USER's reader permission after WM rejection. */
        if (ring3_wm_depth > 0) return (i32)OS32_ERR_EXIST;
        if (reader == CON_SINK_NO_READER) return (i32)OS32_ERR_EXIST;
        if (res_owner_get() != reader)    return (i32)OS32_ERR_EXIST;
    }

    if (len == 0) return 0;

    f = kbd_inject_lock();
    n = inj_push(utf8, len, dest);
    kbd_inject_unlock(f);
    return (i32)n;
}

/* During a WM pump appslot_cur still names the suspended application. WM
 * queries must see the WM's stream, not borrow that application's identity. */
static int inj_current_reader(void)
{
    return ring3_wm_depth > 0 ? APP_ID_SHELL : appslot_cur();
}

u32 kbd_inject_pending(void)
{
    unsigned int f = kbd_inject_lock();
    u8 allowed[APP_SLOT_COUNT];
    u32 n = 0, i, pos = g_inj_tail;
    if (g_inj_count == 0) {
        kbd_inject_unlock(f);
        return 0;
    }
    inj_readable(inj_current_reader(), allowed);
    for (i = 0; i < g_inj_count; i++) {
        if (allowed[g_inj_dest[pos]]) n++;
        pos = (pos + 1) % KBD_INJECT_RING_SIZE;
    }
    kbd_inject_unlock(f);
    return n;
}

/* ------------------------------------------------------------------------ */
/*  カーネル内から                                                           */
/* ------------------------------------------------------------------------ */

int kbd_inject_take_for(int id, u8 *out)
{
    unsigned int f;
    int got;

    if (!out) return 0;
    f = kbd_inject_lock();
    got = inj_read(out, id, 1);
    kbd_inject_unlock(f);
    return got;
}

int kbd_inject_take(u8 *out)
{
    return kbd_inject_take_for(inj_current_reader(), out);
}

int kbd_inject_peek(u8 *out)
{
    unsigned int f;
    int got;

    if (!out) return 0;
    f = kbd_inject_lock();
    got = inj_read(out, inj_current_reader(), 0);
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
    unsigned int f = kbd_inject_lock();
    u32 read = g_inj_tail, write = g_inj_tail, kept = 0, n;
    int reader_exit = con_sink_reader_get() == id;
    /* Filter once, preserving terminal typeahead when only a fullscreen
     * descendant exits, and preserving WM input when the terminal exits. */
    for (n = 0; n < g_inj_count; n++) {
        int dest = g_inj_dest[read];
        if (dest != id && !(dest == 0 && reader_exit)) {
            g_inj_ring[write] = g_inj_ring[read];
            g_inj_dest[write] = g_inj_dest[read];
            write = (write + 1) % KBD_INJECT_RING_SIZE;
            kept++;
        }
        read = (read + 1) % KBD_INJECT_RING_SIZE;
    }
    g_inj_head = write;
    g_inj_count = kept;
    kbd_inject_unlock(f);
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

    /* (0) Boot has no fullscreen application or console reader. Keep the
     * expected rejection independent of the authorization implementation. */
    if (appslot_gfx_owner() >= APP_ID_MIN) bad |= 1u << 0;
    if (con_sink_reader_get() != CON_SINK_NO_READER) bad |= 1u << 0;
    if (kbd_inject((const u8 *)"A", 1) != (i32)OS32_ERR_EXIST) bad |= 1u << 0;
    if (kbd_inject((const u8 *)0, 1) != (i32)OS32_ERR_INVAL)   bad |= 1u << 0;
    if (kbd_inject_pending() != 0)                             bad |= 1u << 0;

    /* (1) 積んだ順に 1 バイトずつ出る (UTF-8 の並びは変えない) */
    if (inj_push(utf8_a, 3, 0) != 3)    bad |= 1u << 1;
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
            pushed += inj_push(&one, 1, 0);
        }
        if (pushed != (u32)KBD_INJECT_RING_SIZE) bad |= 1u << 3;
        one = 'Z';
        if (inj_push(&one, 1, 0) != 0) bad |= 1u << 3;
        if (kbd_inject_pending() != (u32)KBD_INJECT_RING_SIZE) bad |= 1u << 3;
        b = 0xFF;
        if (!kbd_inject_take(&b) || b != 0) bad |= 1u << 3;   /* 最初の 1 本 */
    }

    /* (4) 破棄で空になり、環の巻き戻しも起きない */
    kbd_inject_discard();
    if (kbd_inject_pending() != 0) bad |= 1u << 4;
    if (inj_push((const u8 *)"xy", 2, 0) != 2) bad |= 1u << 4;
    b = 0;
    if (!kbd_inject_take(&b) || b != 'x') bad |= 1u << 4;
    b = 0;
    if (!kbd_inject_take(&b) || b != 'y') bad |= 1u << 4;

    /* (5) 覗きは**取り出さない** (KAPI v54 の kbd_peekkey が立つ土台) */
    kbd_inject_discard();
    b = 0x5A;
    if (kbd_inject_peek(&b) != 0 || b != 0x5A) bad |= 1u << 5;   /* 空は 0 */
    if (inj_push((const u8 *)"pq", 2, 0) != 2) bad |= 1u << 5;
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
