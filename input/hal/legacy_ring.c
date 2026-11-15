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
