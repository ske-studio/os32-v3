#include "legacy_source.inc"
/* Oracle: 2c03d5ed:drivers/input/kbd.c cooked ring, renamed only. */
static volatile u16 old_buf[32];
static volatile int old_head, old_tail, old_count;
static volatile u32 old_dropped;
static void old_push(u16 entry) {
    if (old_count < 32) {
        old_buf[old_tail] = entry;
        old_tail = (old_tail + 1) % 32;
        old_count++;
    } else {
        old_dropped++;
    }
}
static int old_take(void) {
    u16 entry;
    if (old_count == 0) return -1;
    RING_DEQUEUE(entry, old_buf, old_head, old_count, 32);
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
#define REQUIRE(c) do { if (!(c)) return 1; } while (0)
static int probe(void) {
    for (int enabled=0; enabled<2; enabled++) {
        if (enabled) _enable(); else _disable();
        old_head=old_tail=old_count=0; old_dropped=0;
        legacy_cooked_flush(); legacy_cooked_reset_dropped();
        /* Empty, single, exact-full, overflow, partial drain and repeated wrap. */
        for (int round=0; round<12; round++) {
            int n=round==0 ? 0 : round==1 ? 1 : round==2 ? 32 : 37;
            for (int i=0; i<n; i++) {
                u16 key=(u16)(round*257+i);
                old_push(key); legacy_cooked_push(key);
                REQUIRE(legacy_cooked_dropped()==old_dropped);
            }
            for (int i=0; i<40; i++) {
                REQUIRE(legacy_cooked_pending()==old_count);
                REQUIRE(legacy_cooked_peek()==old_peek());
                REQUIRE(legacy_cooked_peek()==old_peek());
                REQUIRE(legacy_cooked_take()==old_take());
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
