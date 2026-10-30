/* ======================================================================== */
/*  CON_SINK.C — console シンク (GUI モード中のカーネル出力のリング)        */
/*                                                                          */
/*  票: docs/archive/gui_v13/TASK_K6C_console.md §2 (PM 案、3 点は決定事項)    */
/*                                                                          */
/*  1. リング容量 8KB、カーネル帯の静的配列 (kmalloc しない)。あふれは       */
/*     **古い方をレコード単位で捨てる**。捨てた回数は con_sink_drop_count。  */
/*  2. 割込み文脈からの書き込みも通す。単一プロデューサを仮定しないので      */
/*     push/pop は IF 退避 (irq_save/irq_restore) で守る。v86 セッション中も  */
/*     溜める (シリアルと同じ「出力は失わない」)。                           */
/*  3. CUI へ戻るとき (console_text_gdc_start) は捨てる。GUI へ入るとき      */
/*     (console_text_gdc_stop) に空で始める。有効化の KAPI は足さない。      */
/*                                                                          */
/*  ワイヤ形式は os32_kapi_shared.h の CON_SINK_* が正典 (端末アプリと共有)。 */
/*  リングは「バイトの環」で、レコードの切れ目は先頭バイトの型から導く。      */
/*  固定長スロットにしないのは、PRINT が 1 バイトから 200 バイトまで幅広く、  */
/*  8KB を最大長で割ると 40 本しか入らないため。                             */
/* ======================================================================== */

/* PRINT wire header: type, color, payload length. */
#define CON_SINK_PRINT_LEN_OFF 2
#include "utf8.h"
#include "os32_kapi_shared.h"
#include "con_sink.h"

/* 割込み禁止区間。ホスト試験 (tools/tests/con_sink_host.c) は CPL=3 で走り
 * cli/popfl を実行できないので、そこだけ空の錠に差し替える。カーネル本体は
 * 必ず include/io.h の irq_save/irq_restore を使う (盲目的な cli/sti では
 * ネストした禁止区間を壊す)。 */
#ifdef CON_SINK_NO_IRQ_LOCK
static unsigned int con_sink_lock(void)        { return 0; }
static void con_sink_unlock(unsigned int f)    { (void)f; }
#else
#include "io.h"
static unsigned int con_sink_lock(void)        { return irq_save(); }
static void con_sink_unlock(unsigned int f)    { irq_restore(f); }
#endif

/* res_owner_get() は fs/fd_redirect.c。kernel/ は -Ifs を持つが、shm.c と
 * 同じく宣言だけを引く (fd_redirect.h は FD 表の型まで引き込むため)。 */
extern int  res_owner_get(void);

/* ------------------------------------------------------------------------ */
/*  状態                                                                     */
/* ------------------------------------------------------------------------ */

static u8  g_ring[CON_SINK_RING_SIZE];
static u32 g_head;                  /* 次に書く位置 */
static u32 g_tail;                  /* 次に読む位置 */
static u32 g_count;                 /* 使用バイト数 */
static int g_enabled;               /* GUI モード中だけ 1 */
static int g_reader = CON_SINK_NO_READER;

/* あふれで捨てたレコード数。ブート後に emu_read_mem で読めるよう、
 * kselftest_pass と同じ理由で static にしない (票 §2-1)。 */
volatile u32 con_sink_drop_count = 0;

/* ------------------------------------------------------------------------ */
/*  リングの素操作 (すべて錠の中で呼ぶこと)                                  */
/* ------------------------------------------------------------------------ */

static u8 ring_peek(u32 off)
{
    u32 i = g_tail + off;
    if (i >= (u32)CON_SINK_RING_SIZE) i -= (u32)CON_SINK_RING_SIZE;
    return g_ring[i];
}

/* 先頭レコードのバイト数。型から導くので、長さを別に持たなくてよい。 */
static u32 ring_rec_size(void)
{
    u8 type = ring_peek(0);
    if (type == (u8)CON_SINK_REC_PRINT) {
        return (u32)CON_SINK_HDR_PRINT + (u32)ring_peek(CON_SINK_PRINT_LEN_OFF);
    }
    if (type == (u8)CON_SINK_REC_CURSOR) {
        return (u32)CON_SINK_HDR_CURSOR;
    }
    if (type == (u8)CON_SINK_REC_EXIT) {
        return (u32)CON_SINK_HDR_EXIT;
    }
    return (u32)CON_SINK_HDR_CLEAR;
}

static void ring_drop_oldest(void)
{
    u32 size = ring_rec_size();
    if (size > g_count) size = g_count;     /* 保険 (起こらない) */
    g_tail += size;
    if (g_tail >= (u32)CON_SINK_RING_SIZE) g_tail -= (u32)CON_SINK_RING_SIZE;
    g_count -= size;
    con_sink_drop_count++;
}

static void ring_put(u8 b)
{
    g_ring[g_head] = b;
    g_head++;
    if (g_head >= (u32)CON_SINK_RING_SIZE) g_head = 0;
    g_count++;
}

/* size バイトが入るまで古いレコードを捨てる。入らない (= レコード 1 本が
 * リングより大きい) ときだけ 0 を返す。 */
static int ring_reserve(u32 size)
{
    if (size > (u32)CON_SINK_RING_SIZE) return 0;
    while (g_count + size > (u32)CON_SINK_RING_SIZE) {
        ring_drop_oldest();
    }
    return 1;
}

static void ring_reset(void)
{
    g_head = 0;
    g_tail = 0;
    g_count = 0;
}

/* ------------------------------------------------------------------------ */
/*  入退場                                                                   */
/* ------------------------------------------------------------------------ */

void con_sink_enable(void)
{
    unsigned int f = con_sink_lock();
    ring_reset();
    g_enabled = 1;
    con_sink_unlock(f);
}

void con_sink_disable(void)
{
    unsigned int f = con_sink_lock();
    ring_reset();
    g_enabled = 0;
    con_sink_unlock(f);
}

int con_sink_is_enabled(void)
{
    return g_enabled;
}

/* ------------------------------------------------------------------------ */
/*  積む側                                                                   */
/*                                                                          */
/*  console.c の入口から、v86_is_active() / rshell_active の分岐とは無関係に */
/*  呼ばれる (票 §2-2: 描画を抑止していても出力は失わない)。無効なときの     */
/*  費用は g_enabled の 1 回の読みだけ。                                     */
/* ------------------------------------------------------------------------ */

/* UTF-8 の切れ目まで戻した長さ。max を超えないところで、継続バイト
 * (10xxxxxx) の直前まで下げる。全部が継続バイト (壊れた入力) のときは
 * max をそのまま返して前進を保証する (CLAUDE.md 既知の罠 §4-27)。 */
static u32 utf8_chunk_len(const u8 *p, u32 avail, u32 max)
{
    u32 n;
    if (avail <= max) return avail;
    n = max;
    while (n > 0 && UTF8_IS_CONT(p[n])) n--;
    if (n == 0) return max;
    return n;
}

void con_sink_push_print(const char *buf, u32 len, u8 color)
{
    const u8 *p = (const u8 *)buf;
    unsigned int f;

    if (!g_enabled || !buf || len == 0) return;

    f = con_sink_lock();
    if (!g_enabled) { con_sink_unlock(f); return; }   /* 錠の中で再確認 */
    while (len > 0) {
        u32 n = utf8_chunk_len(p, len, (u32)CON_SINK_PRINT_MAX);
        u32 i;
        if (!ring_reserve((u32)CON_SINK_HDR_PRINT + n)) break;
        ring_put((u8)CON_SINK_REC_PRINT);
        ring_put(color);
        ring_put((u8)n);
        for (i = 0; i < n; i++) ring_put(p[i]);
        p += n;
        len -= n;
    }
    con_sink_unlock(f);
}

void con_sink_push_clear(void)
{
    unsigned int f;
    if (!g_enabled) return;
    f = con_sink_lock();
    if (g_enabled && ring_reserve((u32)CON_SINK_HDR_CLEAR)) {
        ring_put((u8)CON_SINK_REC_CLEAR);
    }
    con_sink_unlock(f);
}

/* 子アプリが畳まれたことを端末へ知らせる (票 T7 E1)。exec_reclaim_owned の
 * 並びから、正常終了 / exec_kill / fault の 3 経路すべてで通る。id は
 * AppSlot の ID で 1..APPSLOT_MAX なので u8 に収まるが、payload の形を
 * 崩さないようここで飽和させる。 */
void con_sink_push_exit(int id)
{
    unsigned int f;
    if (!g_enabled) return;
    if (id < 0) id = 0;
    if (id > (int)U8_MAX) id = (int)U8_MAX;
    f = con_sink_lock();
    if (g_enabled && ring_reserve((u32)CON_SINK_HDR_EXIT)) {
        ring_put((u8)CON_SINK_REC_EXIT);
        ring_put((u8)id);
    }
    con_sink_unlock(f);
}

void con_sink_push_cursor(int x, int y)
{
    unsigned int f;
    if (!g_enabled) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (int)U8_MAX) x = (int)U8_MAX;
    if (y > (int)U8_MAX) y = (int)U8_MAX;
    f = con_sink_lock();
    if (g_enabled && ring_reserve((u32)CON_SINK_HDR_CURSOR)) {
        ring_put((u8)CON_SINK_REC_CURSOR);
        ring_put((u8)x);
        ring_put((u8)y);
    }
    con_sink_unlock(f);
}

/* ------------------------------------------------------------------------ */
/*  読む側 (KAPI v46)                                                        */
/* ------------------------------------------------------------------------ */

i32 con_sink_read(void *buf, u32 cap)
{
    u8 *out = (u8 *)buf;
    u32 written = 0;
    unsigned int f;
    int owner;

    if (!out) return (i32)OS32_ERR_INVAL;
    /* 先頭レコードが cap に入らないと 1 バイトも進めず読み手が止まる。
     * 「短い cap では 0 が返る」を許すと呼び側が空と区別できないので、
     * 最長レコードが必ず入る大きさを要求する。 */
    if (cap < (u32)CON_SINK_REC_MAX) return (i32)OS32_ERR_INVAL;

    owner = res_owner_get();
    f = con_sink_lock();
    if (g_reader == CON_SINK_NO_READER) {
        g_reader = owner;                       /* 最初に読んだ者が所有する */
    } else if (g_reader != owner) {
        con_sink_unlock(f);
        return (i32)OS32_ERR_EXIST;             /* 読み手は 1 本だけ */
    }
    while (g_count > 0) {
        u32 size = ring_rec_size();
        u32 i;
        if (written + size > cap) break;        /* レコードの途中で切らない */
        for (i = 0; i < size; i++) out[written + i] = ring_peek(i);
        g_tail += size;
        if (g_tail >= (u32)CON_SINK_RING_SIZE) g_tail -= (u32)CON_SINK_RING_SIZE;
        g_count -= size;
        written += size;
    }
    con_sink_unlock(f);
    return (i32)written;
}

/* いまの読み手 (票 K7 §5 R2)。kernel/kbd_inject.c が「注入してよいのは
 * 読み手だけ」を照合するのに使う。錠は要らない (int 1 語の読み)。 */
int con_sink_reader_get(void)
{
    return g_reader;
}

i32 con_sink_stat(u32 *pending, u32 *dropped)
{
    unsigned int f = con_sink_lock();
    if (pending) *pending = g_count;
    if (dropped) *dropped = con_sink_drop_count;
    con_sink_unlock(f);
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  回収 (exec/exec.c の exec_reclaim_owned から、FD / SHM / 音と並べて)      */
/* ------------------------------------------------------------------------ */

void con_sink_owner_exit(int id)
{
    unsigned int f;
    if (id == CON_SINK_NO_READER) return;
    f = con_sink_lock();
    if (g_reader == id) g_reader = CON_SINK_NO_READER;
    con_sink_unlock(f);
}
