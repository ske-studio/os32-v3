#include "snd2.h"
#include "io.h"
#include "kernel/time/tick_clock.h"
#include "kstring.h"

extern int res_owner_get(void);
extern i32 exec_sys_yield(void);

/* Four slot bits match the sixteen PCM registry entries. */
static u32 handle(const Snd2Device *d)
{
    return (d->stream.gen << 4) | d->info.dev;
}

static int lookup(u32 id, Snd2Device **out)
{
    Snd2Device *d = snd2_device(id & 15U);
    if (!d || !d->stream.owner || d->stream.closing || handle(d) != id)
        return OS32_ERR_STALE;
    if (d->stream.owner != res_owner_get())
        return OS32_ERR_INVAL;
    *out = d;
    return 0;
}

int snd2_owned_device(u32 id, Snd2Device **out)
{
    int rc = lookup(id, out);
    if (rc == 0 && (*out)->stream.state == SND2_ST_FAULTED)
        return OS32_ERR_IO;
    return rc;
}

/* Enter locked. Consume the caller's saved flags before releasing resources.
 * A failed resync stop supplies its proof result directly (never retry it). */
static void teardown(Snd2Device *d, unsigned flags, int stop_failed, int opened)
{
    Snd2Stream *s = &d->stream;
    s->closing = 1;
    int leak = stop_failed || (opened && d->be->hw_stop(d->ctx) < 0);
    if (leak) {
        s->state = SND2_ST_FAULTED;
        snd2_fault(d);
    }
    irq_restore(flags);
    if (opened) d->be->hw_release(d->ctx, leak);
    if (!d->writing) snd2_stg_free(s);
    flags = irq_save();
    s->ring = 0;
    ++s->gen;
    s->owner = 0;
    s->closing = d->writing;
    if (!leak) s->state = SND2_ST_IDLE;
    irq_restore(flags);
}

void snd2_teardown_device(Snd2Device *d)
{
    unsigned flags = irq_save();
    if (d->stream.owner && !d->stream.closing)
        teardown(d, flags, 0, 1);
    else
        irq_restore(flags);
}

void snd2_reclaim(int owner)
{
    SND2_DevInfo info;
    for (u32 i = 0; snd2_dev_info(i, &info) == 0; ++i) {
        Snd2Device *d = snd2_device(info.dev);
        if (owner && d->stream.owner == owner) snd2_teardown_device(d);
    }
}

int snd2_open_mode(const SND2_StreamDesc *desc, u32 *id, int v1)
{
    u32 period, lead, cap;
    int rc = snd2_validate_open(desc, v1, &period, &lead, &cap);
    if (rc < 0) return rc;
    Snd2Device *d = snd2_device(desc->dev);
    Snd2Stream *s = &d->stream;
    int owner = res_owner_get();
    if (!id || !owner || !desc->fmt.channels || desc->fmt.bits < 8)
        return OS32_ERR_INVAL;
    if (s->closing) return OS32_ERR_BUSY;
    u32 gen = s->gen;
    kmemset(s, 0, sizeof(*s));
    s->gen = gen;
    s->fmt = desc->fmt;
    s->period = period;
    s->margin = period / 2 < 512 ? period / 2 : 512;
    s->nseg = d->caps.ring_frames / period;
    s->lead = lead;
    s->cap = cap;
    s->v1 = v1;
    s->owner = owner;
    s->closing = 1;
    rc = snd2_stg_alloc(s);
    if (rc < 0) {
        teardown(d, irq_save(), 0, 0);
        return rc;
    }
    rc = d->be->hw_open(d->ctx, &s->fmt, period, &s->ring);
    if (rc < 0 || !s->ring) {
        teardown(d, irq_save(), 0, 1);
        return rc < 0 ? rc : OS32_ERR_IO;
    }
    s->closing = 0;
    *id = handle(d);
    return 0;
}

int snd2_stream_open(const SND2_StreamDesc *desc, u32 *id)
{
    return snd2_open_mode(desc, id, 0);
}

int snd2_stream_write(u32 id, const void *buf, u32 bytes)
{
    return snd2_write_checked(id, buf, bytes, 0);
}

int snd2_write_checked(u32 id, const void *buf, u32 bytes,
                       void (*check)(const void *, u32))
{
    Snd2Device *d;
    unsigned flags = irq_save();
    int rc = snd2_owned_device(id, &d);
    if (!rc) {
        Snd2Stream *s = &d->stream;
        if (s->state == SND2_ST_DRAIN || !buf) rc = OS32_ERR_INVAL;
        else {
            u32 off, stride = snd2_frame_bytes(s);
            u32 n = snd2_stg_reserve(s, bytes / stride, &off);
            if (n) {
                /* A range fault can exit the caller. Leave teardown free to
                 * reclaim staging until the copy actually begins. */
                if (check) {
                    irq_restore(flags);
                    check(buf, n * stride);
                    flags = irq_save();
                    rc = snd2_owned_device(id, &d);
                    if (rc) {
                        irq_restore(flags);
                        return rc;
                    }
                }
                d->writing = 1;
                irq_restore(flags);
                snd2_stg_write(s, off, buf, n);
                flags = irq_save();
                d->writing = 0;
                if (s->closing) {
                    snd2_stg_free(s);
                    s->closing = 0;
                    rc = OS32_ERR_STALE;
                } else {
                    snd2_stg_publish(s, n);
                    rc = n * stride;
                }
            }
        }
    }
    irq_restore(flags);
    return rc;
}

/* Enter locked and restore flags on every path. */
static int start_hw(Snd2Device *d, unsigned flags, int resync)
{
    Snd2Stream *s = &d->stream;
    s->restart_pending = 0;
    int rc = d->be->hw_start(d->ctx);
    if (rc == SND2_START_STOPPED) {
        snd2_seg_reset(s, tick_clock_ms());
        if (resync) s->restart_pending = 1;
        else s->state = SND2_ST_IDLE;
    } else if (rc) {
        teardown(d, flags, 0, 1);
        return OS32_ERR_IO;
    }
    irq_restore(flags);
    return rc ? OS32_ERR_IO : 0;
}

int snd2_stream_start(u32 id)
{
    Snd2Device *d;
    unsigned flags = irq_save();
    int rc = snd2_owned_device(id, &d);
    if (!rc) {
        Snd2Stream *s = &d->stream;
        if (s->state == SND2_ST_IDLE) {
            snd2_seg_reset(s, tick_clock_ms());
            snd2_seg_prime(s);
            s->state = SND2_ST_RUN;
            return start_hw(d, flags, 0);
        }
        if (s->state == SND2_ST_DRAIN) rc = OS32_ERR_INVAL;
        else {
            s->paused = 0;
            s->state = SND2_ST_RUN;
        }
    }
    irq_restore(flags);
    return rc;
}

void snd2_advance(u32 dev)
{
    Snd2Device *d = snd2_device(dev);
    unsigned flags = irq_save();
    if (d && d->stream.owner && !d->stream.closing) {
        Snd2Stream *s = &d->stream;
        if (s->state == SND2_ST_RUN || s->state == SND2_ST_PAUSE ||
            s->state == SND2_ST_DRAIN) {
            u32 hw = 0, now = tick_clock_ms();
            if (s->restart_pending) {
                snd2_seg_reset(s, now);
                if (!s->paused) snd2_seg_prime(s);
                (void)start_hw(d, flags, 1);
                return;
            }
            int valid = d->be->hw_pos(d->ctx, &hw) >= 0;
            if (snd2_seg_observe(s, valid, hw, now) < 0) {
                if (d->be->hw_stop(d->ctx) < 0) {
                    teardown(d, flags, 1, 1);
                    return;
                }
                snd2_seg_resync(s, now);
                (void)start_hw(d, flags, 1);
                return;
            }
            if (s->state == SND2_ST_DRAIN && s->played == s->written) {
                teardown(d, flags, 0, 1);
                return;
            }
        }
    }
    irq_restore(flags);
}

int snd2_stream_pause(u32 id)
{
    Snd2Device *d;
    unsigned flags = irq_save();
    int rc = snd2_owned_device(id, &d);
    if (!rc && d->stream.state == SND2_ST_RUN) {
        /* A failed position read cannot identify the DMA-protected segments. */
        u32 hw;
        if (d->stream.restart_pending) {
            d->stream.paused = 1;
            d->stream.state = SND2_ST_PAUSE;
        } else if (d->be->hw_pos(d->ctx, &hw) < 0) rc = OS32_ERR_IO;
        else if (snd2_seg_observe(&d->stream, 1, hw, tick_clock_ms()) < 0) {
            if (d->be->hw_stop(d->ctx) < 0) {
                teardown(d, flags, 1, 1);
                return OS32_ERR_IO;
            }
            d->stream.paused = 1;
            d->stream.state = SND2_ST_PAUSE;
            snd2_seg_resync(&d->stream, tick_clock_ms());
            return start_hw(d, flags, 1);
        } else {
            snd2_seg_pause(&d->stream);
            d->stream.state = SND2_ST_PAUSE;
        }
    } else if (!rc && d->stream.state != SND2_ST_PAUSE) rc = OS32_ERR_INVAL;
    irq_restore(flags);
    return rc;
}

int snd2_stream_volume(u32 id, u32 left, u32 right)
{
    Snd2Device *d;
    unsigned flags = irq_save();
    int rc = snd2_owned_device(id, &d);
    if (!rc) {
        if (left > d->caps.volume_steps || right > d->caps.volume_steps)
            rc = OS32_ERR_INVAL;
        else rc = d->be->hw_volume(d->ctx, left, right);
    }
    irq_restore(flags);
    return rc;
}

int snd2_stream_close(u32 id, u32 how)
{
    Snd2Device *d;
    unsigned flags = irq_save();
    int rc = lookup(id, &d);
    if (!rc && how > SND2_CLOSE_ABORT) rc = OS32_ERR_INVAL;
    if (rc) {
        irq_restore(flags);
        return rc;
    }
    Snd2Stream *s = &d->stream;
    if (how == SND2_CLOSE_ABORT || s->state == SND2_ST_FAULTED ||
        (s->state == SND2_ST_IDLE && s->written == s->played)) {
        teardown(d, flags, 0, 1);
        return 0;
    }
    if (s->state == SND2_ST_DRAIN) {
        irq_restore(flags);
        return 0;
    }
    if (s->state == SND2_ST_IDLE) {
        rc = snd2_stream_start(id);
        if (rc) {
            irq_restore(flags);
            return rc;
        }
    }
    s->paused = 0;
    s->state = SND2_ST_DRAIN;
    /* pcm_close_ticks: queued periods + three periods, rounded up to a
     * logical 10 ms tick, plus three ticks for the stop sequence. */
    u32 periods = (s->written - s->played + s->period - 1) / s->period;
    u32 ms = ((periods + 3) * s->period * 1000U - 1) / s->fmt.rate + 1;
    u32 timeout = ((ms + 9) / 10 + 3) * 10;
    u32 begin = tick_clock_ms(), gen = s->gen;
    irq_restore(flags);
    for (;;) {
        flags = irq_save();
        if (s->gen != gen || s->closing) {
            rc = s->owner || s->played != s->written ? OS32_ERR_IO : 0;
            break;
        }
        if (s->state != SND2_ST_DRAIN || tick_clock_ms() - begin >= timeout) {
            rc = s->played == s->written ? 0 : OS32_ERR_IO;
            teardown(d, flags, 0, 1);
            return rc;
        }
        irq_restore(flags);
        exec_sys_yield();
    }
    irq_restore(flags);
    return rc;
}
