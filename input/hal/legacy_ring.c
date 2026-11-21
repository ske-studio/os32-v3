#include "in2_int.h"
#include "io.h"
#include "kbd_contract.h"
/* The producer is IRQ1; flush/reset callers hold the same exclusion. */
static volatile u16 kbd_buf[KBD_BUF_SIZE];
static volatile int kbd_head, kbd_tail, kbd_count;
static volatile u32 kbd_dropped;
static u32 pushed, popped;
u32 legacy_cooked_pushed(void) { return pushed; }
u32 legacy_cooked_popped(void) { return popped; }
void legacy_cooked_push(u16 entry)
{
    if (kbd_count < KBD_BUF_SIZE) {
        kbd_buf[kbd_tail] = entry;
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        kbd_count++;
        ++pushed;
    } else {
        kbd_dropped++;
    }
}
int legacy_cooked_take(void)
{
    u16 entry;
    if (kbd_count == 0) return -1;
    RING_DEQUEUE(entry, kbd_buf, kbd_head, kbd_count, KBD_BUF_SIZE);
    ++popped;
    return (int)entry;
}
int legacy_cooked_peek(void)
{
    u16 entry;
    unsigned int flags;
    if (kbd_count == 0) return -1;
    flags = irq_save();
    entry = kbd_buf[kbd_head];
    irq_restore(flags);
    return (int)entry;
}
int legacy_cooked_pending(void) { return kbd_count; }
void legacy_cooked_flush(void) {
    const struct InputLegacyProvider *p = input_ime_provider();
    kbd_head = kbd_tail = kbd_count = 0;
    popped = pushed;
    if (p && p->flush_actions) p->flush_actions();
}
void legacy_cooked_reset_dropped(void) { kbd_dropped = 0; }
u32 legacy_cooked_dropped(void) { return kbd_dropped; }

/* 生 make/break イベントリング (レビュー ⑥、GUI の Key down/up 用)。
 * cooked なリング (make のみ、ASCII 化) と別に、全キーの押下/離しを
 * 順序どおり積む。WM (gshell) が kbd_trygetrawkey() で読み、down=1/0 の
 * Key イベントを作る。満杯時は WM の操作を優先し、
 * 捨てた数は kbd_raw_dropped に数える (kbd_dropped_count は cooked と合算)。 */
static volatile u16 kbd_raw_buf[KBD_BUF_SIZE];
static volatile int kbd_raw_head = 0;
static volatile int kbd_raw_tail = 0;
static volatile int kbd_raw_count = 0;
/* raw リング満杯で捨てた生イベント数 (レビュー ①)。GUI の取りこぼし検出用に
 * kbd_dropped_count() へ合算する。break を落とすと WM の修飾状態がずれるため必須。 */
static volatile u32 kbd_raw_dropped = 0;

static int keep_raw(int sc)
{
    return sc == KEY_STOP || sc == KEY_CTRL || sc == KEY_GRPH || sc == KEY_TAB;
}

/* A protected pair survives foreground purges even when an ordinary raw
 * with GRPH clear was the completion. Tags move with their entries. */
enum raw_priority { RAW_NORMAL, RAW_REPEAT, RAW_WM, RAW_PROTECTED };
static u8 priority[KBD_BUF_SIZE];
static int switch_pending, tab_down;

static int raw_slot(int offset)
{
    return (kbd_raw_head + offset) % KBD_BUF_SIZE;
}

void kbd_raw_purge_keep(void)
{
    int kept = 0;
    for (int i = 0; i < kbd_raw_count; ++i) {
        int from = raw_slot(i);
        if (!keep_raw(kbd_raw_buf[from] & 0xff) && priority[from] != RAW_PROTECTED)
            continue;
        int to = raw_slot(kept++);
        kbd_raw_buf[to] = kbd_raw_buf[from];
        priority[to] = priority[from];
    }
    kbd_raw_tail = raw_slot(kept);
    kbd_raw_count = kept;
}

/* Select all victims before modifying the queue. A failed admission
 * must leave previously accepted starts and their reservations intact. */
static int make_room(int slots, int limit, int rank)
{
    int needed = kbd_raw_count + slots - limit;
    if (needed <= 0) return 1;
    int victim[3], n = 0;
    for (int p = RAW_NORMAL; p < rank && n < needed; ++p)
        for (int i = kbd_raw_count - 1; i >= 0 && n < needed; --i)
            if (priority[raw_slot(i)] == p) victim[n++] = i;
    if (n != needed) return 0;
    int kept = 0;
    for (int i = 0; i < kbd_raw_count; ++i) {
        if (i == victim[0] || (n >= 2 && i == victim[1]) || (n == 3 && i == victim[2])) continue;
        int to = raw_slot(kept++), from = raw_slot(i);
        kbd_raw_buf[to] = kbd_raw_buf[from];
        priority[to] = priority[from];
    }
    kbd_raw_count = kept;
    kbd_raw_tail = raw_slot(kept);
    kbd_raw_dropped += n;
    return 1;
}

static void append_raw(int keycode, int is_break, int mods, int rank)
{
    priority[kbd_raw_tail] = rank;
    kbd_raw_buf[kbd_raw_tail] = (u16)keycode | (is_break ? 0 : KBD_RAW_MAKE)
        | (u16)((mods & KBD_RAW_MOD_MASK) << KBD_RAW_MOD_SHIFT);
    kbd_raw_tail = (kbd_raw_tail + 1) % KBD_BUF_SIZE;
    ++kbd_raw_count;
}

void legacy_raw_push(int keycode, int is_break, int mods)
{
    int grph = mods & SHIFT_GRPH;
    int finish = switch_pending && (!grph || (keycode == KEY_GRPH && is_break));
    int start = !switch_pending && grph && keycode == KEY_TAB && !is_break;
    int stop = keycode == KEY_STOP && !is_break && (mods & SHIFT_CTRL);
    int repeat = switch_pending && tab_down && keycode == KEY_TAB && !is_break;
    /* Once this physical press ends, its queued steps determine an
     * earlier selection. Later input must not coalesce those repeats. */
    if (tab_down && (finish || (keycode == KEY_TAB && is_break))) {
        for (int i = 0; i < kbd_raw_count; ++i) {
            int slot = raw_slot(i);
            if (priority[slot] == RAW_REPEAT) priority[slot] = RAW_PROTECTED;
        }
    }
    if (keycode == KEY_TAB) tab_down = !is_break;
    if (!grph || finish) tab_down = 0;
    if (finish && stop) {
        /* The reserved finish and the emergency request are independent.
         * ABORTED cleanup can remove STOP while preserving the switch. */
        append_raw(KEY_GRPH, 1, mods & ~SHIFT_GRPH, RAW_PROTECTED);
        switch_pending = finish = 0;
    }
    if (in2_excl_drop_raw(keycode, is_break)) {
        if (!finish) return;
        /* Signal the observed end of GRPH without exposing the suppressed
         * key to the WM. The in2 event retains its original scan code. */
        keycode = KEY_GRPH;
        is_break = 1;
        mods &= ~SHIFT_GRPH;
    }
    int rank = keep_raw(keycode) ? RAW_WM : RAW_NORMAL;
    if (repeat) rank = RAW_REPEAT;
    if (start || finish || stop) rank = RAW_PROTECTED;
    int limit = KBD_BUF_SIZE - (switch_pending && !finish);
    /* The last seat can contain ordinary expendable input or STOP, never
     * a protected switch. Count STOP as already occupying its seat. */
    if (start && kbd_raw_count + 2 >= KBD_BUF_SIZE) {
        int has_stop = 0;
        for (int i = 0; i < kbd_raw_count; ++i) {
            u16 raw = kbd_raw_buf[raw_slot(i)];
            if ((raw & KBD_RAW_KEY_MAKE_MASK) == (KEY_STOP | KBD_RAW_MAKE) &&
                (raw & (SHIFT_CTRL << KBD_RAW_MOD_SHIFT))) {
                has_stop = 1;
                break;
            }
        }
        limit -= !has_stop;
    }
    if (!make_room(1 + start, limit, rank)) {
        kbd_raw_dropped++;
        return;
    }
    append_raw(keycode, is_break, mods, rank);
    if (start) switch_pending = 1;
    if (finish) switch_pending = 0;
}
int legacy_raw_take(void)
{
    u16 entry;
    if (kbd_raw_count == 0) return -1;
    RING_DEQUEUE(entry, kbd_raw_buf, kbd_raw_head, kbd_raw_count, KBD_BUF_SIZE);
    return (int)entry;
}

void legacy_raw_discard_stop(void)
{
    int n = kbd_raw_count;
    int read = kbd_raw_head;
    int write = read;
    int kept = 0;
    while (n-- > 0) {
        int from = read;
        u16 raw = kbd_raw_buf[read];
        read = (read + 1) % KBD_BUF_SIZE;
        if ((raw & KBD_RAW_KEY_MAKE_MASK) == (KEY_STOP | KBD_RAW_MAKE) &&
            (raw & (SHIFT_CTRL << KBD_RAW_MOD_SHIFT))) continue;
        kbd_raw_buf[write] = raw;
        priority[write] = priority[from];
        write = (write + 1) % KBD_BUF_SIZE;
        kept++;
    }
    kbd_raw_tail = write;
    kbd_raw_count = kept;
}
void legacy_raw_flush(void)
{
    switch_pending = tab_down = 0;
    kbd_raw_head = kbd_raw_tail = kbd_raw_count = 0;
}
void legacy_raw_reset_dropped(void) { kbd_raw_dropped = 0; }
u32 legacy_raw_dropped(void) { return kbd_raw_dropped; }
