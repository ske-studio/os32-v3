/* Diagnostic CPU transfers only. Production present_rect is unchanged.
 * Address/format: include/pegc.h [B] §2-7, §3-2, [BM]. Spacing follows
 * trident/write_gap.c and regs.c: integer LPT * ns / 10ms, nop loop.
 * No yield or CLIENT writes; interrupts remain enabled for sys_time_now. */
#include "pegc_command_wire.h"
#include "drivers/video/pc98/pegc_diagnostic.h"
#include "pegc.h"
#include "sys.h"
#include "cpu_calibrate.h"
#include "pgalloc.h"
#include "memmap.h"
#include "kstring.h"
#include "serial_diag.h"
#include "generated/kdebug_import_macros.h"

#define PEGC_RUNS 8u
#define PEGC_FRAME_BYTES (PEGC_PITCH * PEGC_HEIGHT_480)
#define PEGC_SAVE_PAGES (PEGC_FRAME_BYTES / MEM_PAGE_SIZE)
#define PEGC_TICK_NS 10000000u

enum method { REP_MOVSD, DWORD, WORD, BYTE, GAP };
static const char *const names[] = {"rep_movsd", "dword", "word", "byte", "dword_gap"};

static u32 gap_loops(u32 lpt, u32 ns)
{
    unsigned long long value = (unsigned long long)lpt * ns;
    if (value >= ((unsigned long long)PEGC_TICK_NS << 32)) return (u32)-1;
    u32 loops = 0;
    for (int bit = 31; bit >= 0; --bit) {
        unsigned long long part = (unsigned long long)PEGC_TICK_NS << bit;
        if (value >= part) { value -= part; loops |= 1u << bit; }
    }
    return loops;
}

static void transfer(const struct pegc_diagnostic_view *v, u32 method, u32 loops)
{
    if (method == REP_MOVSD) {
        /* Match the production full-screen path: one rep per scanline. */
        for (u32 y = 0; y < PEGC_HEIGHT_480; ++y) {
            const u8 *src = v->client + y * PEGC_PITCH;
            volatile u8 *dst = v->vram + y * PEGC_PITCH;
            u32 words = PEGC_WIDTH / sizeof(u32);
            __asm__ volatile("cld; rep movsl" : "+S"(src), "+D"(dst), "+c"(words)
                             : : "memory", "cc");
        }
    } else if (method == DWORD || method == GAP) {
        volatile u32 *dst = (volatile u32 *)v->vram;
        const u32 *src = (const u32 *)v->client;
        for (u32 i = 0; i < PEGC_FRAME_BYTES / sizeof(u32); ++i) {
            dst[i] = src[i];
            if (method == GAP) {
                u32 remaining = loops;
                while (remaining > 0) {
                    __asm__ volatile("nop" ::: "memory");
                    --remaining;
                }
            }
        }
    } else if (method == WORD) {
        volatile u16 *dst = (volatile u16 *)v->vram;
        const u16 *src = (const u16 *)v->client;
        for (u32 i = 0; i < PEGC_FRAME_BYTES / sizeof(u16); ++i) dst[i] = src[i];
    } else {
        for (u32 i = 0; i < PEGC_FRAME_BYTES; ++i) v->vram[i] = v->client[i];
    }
}
static void number(const char *key, u32 n)
{
    serial_puts_polled(key); serial_diag_u32(n);
}
static int compare(const struct pegc_diagnostic_view *v, u32 method, u32 ns, u32 loops)
{
    u32 minimum = (u32)-1, maximum = 0, count = 0, mismatches = 0;
    unsigned long long total = 0;
    int rc = 0;
    for (u32 run = 0; run < PEGC_RUNS; ++run) {
        /* A successful previous copy must not conceal dropped stores. Both
         * poison and its verification are outside the measured interval. */
        for (u32 i = 0; i < PEGC_FRAME_BYTES; ++i) {
            v->vram[i] = (u8)~v->client[i];
            if (v->vram[i] != (u8)~v->client[i]) { rc = OS32_ERR_IO; break; }
        }
        if (rc) break;
        u32 before, after, hi;
        rc = sys_time_now(&before, &hi);
        if (rc) break;
        transfer(v, method, loops);
        rc = sys_time_now(&after, &hi);
        if (rc) break;
        u32 elapsed = after - before;
        if (elapsed < minimum) minimum = elapsed;
        if (elapsed > maximum) maximum = elapsed;
        total += elapsed;
        ++count;
        for (u32 i = 0; i < PEGC_FRAME_BYTES; ++i)
            if (v->vram[i] != v->client[i]) ++mismatches;
    }
    if (!rc && mismatches) rc = OS32_ERR_IO;
    serial_puts_polled(method == GAP ? "PEGC phase=gap" : "PEGC phase=present");
    serial_puts_polled(" method="); serial_puts_polled(names[method]);
    if (method == GAP) { number(" gap_ns=", ns); number(" loops=", loops); }
    number(" count=", count);
    number(" min_us=", count ? minimum : 0);
    number(" max_us=", maximum);
    number(" avg_us=", count == PEGC_RUNS ? (u32)(total >> 3) : 0);
    number(" mismatches=", mismatches);
    serial_puts_polled(" rc="); serial_diag_i32(rc); serial_puts_polled("\r\n");
    return rc;
}
int pegc_command_call(void *buf, u32 size)
{
    if (!buf || size != sizeof(struct pegc_request)) return OS32_ERR_INVAL;
    struct pegc_request q;
    kmemcpy(&q, buf, sizeof(q));
    int rc = OS32_ERR_INVAL;
    if (q.version != PEGC_COMMAND_VERSION || q.size != sizeof(q) ||
        q.test > PEGC_TEST_GAP_SWEEP || q.rc || q.result_count ||
        (q.test == PEGC_TEST_PRESENT && (q.start_ns || q.end_ns || q.step_ns)) ||
        (q.test == PEGC_TEST_GAP_SWEEP && (!q.step_ns || q.start_ns > q.end_ns)))
        goto reply;
    if (serial_gate_active()) { rc = OS32_ERR_BUSY; goto reply; }
    struct pegc_diagnostic_view v;
    rc = pegc_diagnostic_view(&v);
    if (rc) goto reply;
    /* The 192KiB kernel heap cannot hold a 300KiB frame. Borrow pages
     * synchronously from the general pool, then return the exact span. */
    u32 saved_phys = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, PEGC_SAVE_PAGES);
    u8 *saved = P2V(saved_phys);
    if (!saved_phys) { rc = OS32_ERR_NOSPC; goto reply; }
    for (u32 i = 0; i < PEGC_FRAME_BYTES; ++i) saved[i] = v.vram[i];
    rc = 0;
    u32 lpt = cpu_loops_per_tick();
    for (u32 n = q.test == PEGC_TEST_PRESENT ? REP_MOVSD : q.start_ns;;) {
        int result = compare(&v, q.test == PEGC_TEST_PRESENT ? n : GAP,
                             n, gap_loops(lpt, n));
        ++q.result_count;
        if (!rc && result) rc = result;
        if (q.test == PEGC_TEST_PRESENT) {
            if (n == BYTE) break;
            ++n;
        } else {
            if (q.end_ns - n < q.step_ns) break;
            n += q.step_ns;
        }
    }
    /* Restore the exact entry image, even after a clock/readback failure.
     * Single byte writes with readback avoid another packed burst. */
    u32 restore_bad = 0;
    for (u32 i = 0; i < PEGC_FRAME_BYTES; ++i) {
        v.vram[i] = saved[i];
        if (v.vram[i] != saved[i]) ++restore_bad;
    }
    int freed = pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, saved_phys / MEM_PAGE_SIZE,
                                     PEGC_SAVE_PAGES);
    if (!freed && !rc) rc = OS32_ERR_IO;
    if (restore_bad && !rc) rc = OS32_ERR_IO;
    number("PEGC phase=restore mismatches=", restore_bad);
    serial_puts_polled(" rc="); serial_diag_i32(restore_bad ? OS32_ERR_IO : 0);
    serial_puts_polled("\r\n");
    ++q.result_count;
reply:
    q.rc = rc;
    q.result_count = rc == OS32_ERR_INVAL ? 0 : q.result_count;
    kmemcpy(buf, &q, sizeof(q));
    return rc;
}
