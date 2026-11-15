#include "in2_int.h"
#include "io.h"
#include "../../drivers/input/kbd.h"
/* The producer is IRQ1; flush/reset callers hold the same exclusion. */
static volatile u16 kbd_buf[KBD_BUF_SIZE];
static volatile int kbd_head, kbd_tail, kbd_count;
static volatile u32 kbd_dropped;
void legacy_cooked_push(u16 entry)
{
    if (kbd_count < KBD_BUF_SIZE) {
        kbd_buf[kbd_tail] = entry;
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        kbd_count++;
    } else {
        kbd_dropped++;
    }
}
int legacy_cooked_take(void)
{
    u16 entry;
    if (kbd_count == 0) return -1;
    RING_DEQUEUE(entry, kbd_buf, kbd_head, kbd_count, KBD_BUF_SIZE);
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
void legacy_cooked_flush(void) { kbd_head = kbd_tail = kbd_count = 0; }
void legacy_cooked_reset_dropped(void) { kbd_dropped = 0; }
u32 legacy_cooked_dropped(void) { return kbd_dropped; }

volatile u16 kbd_raw_buf[KBD_BUF_SIZE];
volatile int kbd_raw_head = 0;
volatile int kbd_raw_tail = 0;
volatile int kbd_raw_count = 0;
/* raw リング満杯で捨てた生イベント数 (レビュー ①)。GUI の取りこぼし検出用に
 * kbd_dropped_count() へ合算する。break を落とすと WM の修飾状態がずれるため必須。 */
volatile u32 kbd_raw_dropped = 0;

void legacy_raw_push(int keycode, int is_break, int kbd_shift_state)
{
    if (kbd_raw_count < KBD_BUF_SIZE) {
        kbd_raw_buf[kbd_raw_tail] = (u16)keycode
                                  | (is_break ? 0 : KBD_RAW_MAKE)
                                  | (u16)((kbd_shift_state & KBD_RAW_MOD_MASK) << KBD_RAW_MOD_SHIFT);
        kbd_raw_tail = (kbd_raw_tail + 1) % KBD_BUF_SIZE;
        kbd_raw_count++;
    } else {
        /* Keep emergency make even when ordinary input filled the ring.
         * Replace the newest entry: earlier input order and head stay intact. */
        if (keycode == KEY_STOP && !is_break && (kbd_shift_state & SHIFT_CTRL)) {
            int last = (kbd_raw_tail + KBD_BUF_SIZE - 1) % KBD_BUF_SIZE;
            kbd_raw_buf[last] = KEY_STOP | KBD_RAW_MAKE | (u16)((kbd_shift_state & KBD_RAW_MOD_MASK) << KBD_RAW_MOD_SHIFT);
        }
        kbd_raw_dropped++;   /* GUI が resync できるよう必ず数える (レビュー ①) */
    }
}
int legacy_raw_take(void)
{
    u16 entry;
    if (kbd_raw_count == 0) return -1;
    RING_DEQUEUE(entry, kbd_raw_buf, kbd_raw_head, kbd_raw_count, KBD_BUF_SIZE);
    return (int)entry;
}
