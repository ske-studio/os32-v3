#include "mmtime.h"

static MmtEvent events[MMT_EVENT_MAX];
static u32 pass, hold;
static int delivering;

static unsigned long long now_us(void)
{
    u32 lo, hi;
    mmt_now_us(&lo, &hi);
    return ((unsigned long long)hi << 32) | lo;
}

static unsigned long long deadline(const MmtEvent *e)
{
    return ((unsigned long long)e->next_hi << 32) | e->next_lo;
}

static void set_deadline(MmtEvent *e, unsigned long long next)
{
    e->next_lo = (u32)next;
    e->next_hi = (u32)(next >> 32);
}

static void free_event(MmtEvent *e, u32 slot)
{
    e->live = e->dying = 0;
    e->id += MMT_EVENT_MAX;
    if (!e->id) e->id = slot + 1;
}

/* delay_us <= 10^9, so the shifted remainder fits in u32. */
static unsigned long long periods(unsigned long long delta, u32 delay_us)
{
    unsigned long long count = 0;
    u32 remainder = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | (u32)((delta >> bit) & 1u);
        if (remainder >= delay_us) {
            remainder -= delay_us;
            count |= 1ULL << bit;
        }
    }
    return count;
}

u32 mmt_set_event(u32 delay_ms, u32 res_ms, MMT_CB cb, u32 user, u32 flags)
{
    if (delay_ms < MMT_PERIOD_MIN || delay_ms > MMT_PERIOD_MAX ||
        res_ms > MMT_PERIOD_MAX || !cb || flags > MMT_PERIODIC) return 0;
    for (u32 i = 0; i < MMT_EVENT_MAX; ++i) {
        MmtEvent *e = &events[i];
        if (e->live) continue;
        if (!e->id) e->id = i + 1;
        e->delay_us = delay_ms * 1000u;
        set_deadline(e, now_us() + e->delay_us);
        e->cb = cb;
        e->user = user;
        e->missed = 0;
        e->born_pass = pass;
        e->flags = (u8)flags;
        e->dying = 0;
        e->live = 1;
        return e->id;
    }
    return 0;
}

int mmt_kill_event(u32 id)
{
    if (id) {
        MmtEvent *e = &events[(id - 1) % MMT_EVENT_MAX];
        if (e->id == id && e->live && !e->dying) {
            e->dying = 1;
            return 0;
        }
    }
    return OS32_ERR_INVAL;
}

void mmt_deliver(void)
{
    if (delivering || hold) return;
    delivering = 1;
    ++pass;
    for (u32 i = 0; i < MMT_EVENT_MAX; ++i) {
        MmtEvent *e = &events[i];
        u32 id = e->id, calls = 0;
        if (!e->live || e->dying || e->born_pass == pass) continue;
        while (deadline(e) <= now_us()) {
            MMT_CB cb = e->cb;
            u32 user = e->user;
            if (e->flags == MMT_ONESHOT) {
                free_event(e, i);
                cb(id, 0, user, 0, 0);
                break;
            }
            cb(id, 0, user, 0, 0);
            if (e->id != id || !e->live || e->dying) break;
            set_deadline(e, deadline(e) + e->delay_us);
            if (++calls == MMT_CATCHUP_MAX) {
                unsigned long long now = now_us(), next = deadline(e);
                if (next <= now) {
                    unsigned long long n = periods(now - next, e->delay_us) + 1;
                    e->missed += (u32)n;
                    set_deadline(e, next + n * e->delay_us);
                }
                break;
            }
        }
    }
    for (u32 i = 0; i < MMT_EVENT_MAX; ++i)
        if (events[i].dying) free_event(&events[i], i);
    delivering = 0;
}

int mmt_next_due_ms(u32 *ms)
{
    unsigned long long now, earliest = 0;
    int found = 0;
    if (!ms) return OS32_ERR_INVAL;
    now = now_us();
    for (u32 i = 0; i < MMT_EVENT_MAX; ++i) {
        MmtEvent *e = &events[i];
        if (!e->live || e->dying) continue;
        if (!found || deadline(e) < earliest) earliest = deadline(e);
        found = 1;
    }
    if (found) {
        /* Every timer is at most MMT_PERIOD_MAX ms ahead of now. */
        *ms = earliest <= now ? 0 : ((u32)(earliest - now) + 999u) / 1000u;
    }
    return found;
}

void mmt_hold(int on)
{
    if (on) ++hold;
    else if (hold) --hold;
}
