/* Cooperative multimedia timers. Public declarations remain C89 compatible. */
#ifndef OS32_MMTIME_H
#define OS32_MMTIME_H

#include "os32_kapi_shared.h"

#define MMT_QPF 1000000UL
#define MMT_EVENT_MAX 16
#define MMT_CATCHUP_MAX 8
#define MMT_PERIOD_MIN 1u
#define MMT_PERIOD_MAX 1000000u
#define MMT_ONESHOT 0u
#define MMT_PERIODIC 1u

typedef void (*MMT_CB)(u32 id, u32 msg, u32 user, u32 dw1, u32 dw2);
typedef struct {
    u32 id, delay_us, next_lo, next_hi;
    MMT_CB cb;
    u32 user, missed, born_pass;
    u8 flags, live, dying;
} MmtEvent;

u32 mmt_now_ms(void);
/* Valid output pointers are required; always returns 0, including fallback. */
int mmt_now_us(u32 *lo, u32 *hi);
/* Validate 1..1000000 ms; the physical timer period stays unchanged. */
int mmt_begin_period(u32 ms);
int mmt_end_period(u32 ms);
/* Returns nonzero ID, or 0 for invalid arguments/full table. res_ms is a
 * resolution hint (0..MMT_PERIOD_MAX); delay_ms is 1..MMT_PERIOD_MAX.
 * Callbacks receive (id, 0, user, 0, 0). */
u32 mmt_set_event(u32 delay_ms, u32 res_ms, MMT_CB cb, u32 user, u32 flags);
int mmt_kill_event(u32 id);
void mmt_deliver(void);
void mmt_sleep_ms(u32 ms);
/* 1 and rounded-up remaining ms when pending, 0 when none, INVAL for NULL. */
int mmt_next_due_ms(u32 *ms);
/* Nestable delivery suppression: nonzero acquires, zero releases one hold. */
void mmt_hold(int on);

#endif
