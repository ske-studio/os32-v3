#include "legacy_source.inc"
/* Oracle: 2c03d5ed:drivers/input/kbd.c cooked ring, renamed only. */
static volatile u16 old_buf[KBD_BUF_SIZE];
static volatile int old_head, old_tail, old_count;
static volatile u32 old_dropped;
static void old_push(u16 entry) {
    if (old_count < KBD_BUF_SIZE) {
        old_buf[old_tail] = entry;
        old_tail = (old_tail + 1) % KBD_BUF_SIZE;
        old_count++;
    } else {
        old_dropped++;
    }
}
static int old_take(void) {
    u16 entry;
    if (old_count == 0) return -1;
    RING_DEQUEUE(entry, old_buf, old_head, old_count, KBD_BUF_SIZE);
    return (int)entry;
}
static int old_peek(void) {
    u16 entry;
    unsigned int flags;
    if (old_count == 0) return -1;
    flags = irq_save();
    entry = old_buf[old_head];
    irq_restore(flags);
    return (int)entry;
}
/* Oracle: 7eb7efac:drivers/input/kbd.c raw enqueue/dequeue. */
static volatile u16 old_raw_buf[KBD_BUF_SIZE];
static volatile int old_raw_head, old_raw_tail, old_raw_count;
static volatile u32 old_raw_dropped;
static void old_raw_push(int keycode, int is_break, int kbd_shift_state) {
    if (old_raw_count < KBD_BUF_SIZE) {
        old_raw_buf[old_raw_tail] = (u16)keycode | (is_break ? 0 : KBD_RAW_MAKE)
            | (u16)((kbd_shift_state & KBD_RAW_MOD_MASK) << KBD_RAW_MOD_SHIFT);
        old_raw_tail = (old_raw_tail + 1) % KBD_BUF_SIZE;
        old_raw_count++;
    } else {
        if (keycode == KEY_STOP && !is_break && (kbd_shift_state & SHIFT_CTRL)) {
            int last = (old_raw_tail + KBD_BUF_SIZE - 1) % KBD_BUF_SIZE;
            old_raw_buf[last] = KEY_STOP | KBD_RAW_MAKE | (u16)((kbd_shift_state & KBD_RAW_MOD_MASK) << KBD_RAW_MOD_SHIFT);
        }
        old_raw_dropped++;
    }
}
static int old_raw_take(void) {
    u16 entry;
    if (old_raw_count == 0) return -1;
    RING_DEQUEUE(entry, old_raw_buf, old_raw_head, old_raw_count, KBD_BUF_SIZE);
    return (int)entry;
}
#define REQUIRE(c) do { if (!(c)) return 1; } while (0)
static int probe(void) {
    REQUIRE(KBD_BUF_SIZE == 128);
    for (int enabled=0; enabled<2; enabled++) {
        if (enabled) _enable(); else _disable();
        old_head=old_tail=old_count=0; old_dropped=0;
        legacy_cooked_flush(); legacy_cooked_reset_dropped();
        /* Empty, single, exact-full, overflow, partial drain and repeated wrap. */
        for (int round=0; round<12; round++) {
            int n=round==0 ? 0 : round==1 ? 1 : round==2 ? 128 : 133;
            for (int i=0; i<n; i++) {
                u16 key=(u16)(round*257+i);
                old_push(key); legacy_cooked_push(key);
                REQUIRE(legacy_cooked_dropped()==old_dropped);
            }
            for (int i=0; i<140; i++) {
                REQUIRE(legacy_cooked_pending()==old_count);
                REQUIRE(legacy_cooked_peek()==old_peek());
                REQUIRE(legacy_cooked_peek()==old_peek());
                REQUIRE(legacy_cooked_take()==old_take());
                REQUIRE(_irq_enabled()==enabled);
                if (round>2 && i==16) break;
            }
        }
        old_raw_head=old_raw_tail=old_raw_count=old_raw_dropped=0;
        kbd_raw_head=kbd_raw_tail=kbd_raw_count=kbd_raw_dropped=0;
        for (int round=0; round<12; round++) {
            int n=round==0 ? 0 : round==1 ? 1 : round==2 ? 128 : 133;
            for (int i=0; i<n; i++) {
                int key=i==131 ? KEY_STOP : (i & 31);
                int mods=i==131 ? SHIFT_CTRL : i;
                old_raw_push(key, 0, mods); legacy_raw_push(key, 0, mods);
                REQUIRE(kbd_raw_dropped==old_raw_dropped);
            }
            for (int i=0; i<140; i++) {
                REQUIRE(kbd_raw_count==old_raw_count);
                REQUIRE(legacy_raw_take()==old_raw_take());
                REQUIRE(_irq_enabled()==enabled);
                if (round>2 && i==16) break;
            }
        }
        legacy_cooked_flush(); old_head=old_tail=old_count=0;
        REQUIRE(legacy_cooked_take()==old_take() && legacy_cooked_peek()==old_peek());
        REQUIRE(legacy_cooked_dropped()==old_dropped && old_dropped>0);
        legacy_cooked_reset_dropped(); REQUIRE(!legacy_cooked_dropped());
    }
    return 0;
}
void _start(void) {
    int rc=probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}

int in2_excl_keyboard(void) { return 0; }
