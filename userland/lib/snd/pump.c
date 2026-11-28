#include "live.h"

static u32 stream, refs, rate, written, sent, buffered;
static int explicit_open;
static enum { NORMAL_PUMP, DOS_READY, DOS_REFILL } feed_mode;
static u32 pcm_period, last_submit_ms, half_period_ms;
static int16_t scratch[2048];
static int32_t stereo[2048];
static SndlMixSpan records[512];
static uint32_t generations[512];
static unsigned count;
static struct { u32 voice, buffer, frame, generation; } cursor[SNDL_VOICE_MAX];

static int pcm_acquire(const SND2_Fmt *fmt, u32 period, u32 lead)
{
    if (!sound_api || !fmt || fmt->channels != 2 || fmt->bits != 16 ||
        fmt->enc != SND2_ENC_S16LE || !fmt->rate || fmt->flags) return OS32_ERR_INVAL;
    if (refs) {
        if (rate != fmt->rate) return OS32_ERR_INVAL;
        ++refs;
        return 0;
    }
    int failure = OS32_ERR_NOSYS;
    int devices = sound_api->snd2_dev_count();
    for (int i = 0; i < devices; ++i) {
        SND2_DevInfo info;
        if (sound_api->snd2_dev_info(i, &info) || info.kind != SND2_KIND_PCM_OUT) continue;
        SND2_StreamDesc d = {.size=sizeof d, .dev=info.dev, .fmt=*fmt,
                             .period_frames=period, .lead_periods=lead};
        int rc = sound_api->snd2_stream_open(&d, &stream);
        if (rc) {
            failure = rc;
            continue;
        }
        last_submit_ms = 0;
        half_period_ms = (SNDL_SOFT_PERIOD * 500 + fmt->rate - 1) / fmt->rate;
        feed_mode = NORMAL_PUMP;
        pcm_period = period;
        rate = fmt->rate;
        refs = 1;
        written = count = buffered = sent = 0;
        __builtin_memset(cursor, 0, sizeof cursor);
        return 0;
    }
    return failure;
}

int sndl_soft_pcm_acquire(const SND2_Fmt *fmt)
{
    if (!refs) return pcm_acquire(fmt, SNDL_SOFT_PERIOD, 3);
    if (rate != 44100 && rate != 22050) return OS32_ERR_NOSYS;
    ++refs;
    return 0;
}

int sndl_open(const SND2_Fmt *fmt, u32 period, u32 lead)
{
    if (explicit_open) return OS32_ERR_BUSY;
    int rc = pcm_acquire(fmt, period, lead);
    if (!rc) explicit_open = 1;
    return rc;
}

static int record(void *ctx, const SndlMixSpan *s)
{
    (void)ctx;
    if (count == 512) return 0;
    generations[count] = sndl_mix_generation(s->voice);
    records[count] = *s;
    records[count++].offset += written + buffered - sent;
    return 1;
}

static u32 frame_at(const SndlMixSpan *s, u32 distance)
{
    uint64_t frame = s->first + (((uint64_t)distance*s->step16 + s->phase16) >> 16);
    return s->loop_len ? frame % s->loop_len : frame;
}

static void retire(u32 played)
{
    unsigned keep = 0;
    for (unsigned i = 0; i < count; ++i) {
        SndlMixSpan *s = &records[i];
        if ((int32_t)(played - s->offset - s->count) < 0) {
            generations[keep] = generations[i];
            records[keep++] = *s;
            continue;
        }
        unsigned k = (s->voice-1) % SNDL_VOICE_MAX;
        cursor[k].voice = s->voice;
        cursor[k].generation = generations[i];
        cursor[k].buffer = s->buf_id;
        cursor[k].frame = frame_at(s, s->count);
        if (s->last) sndl_mix_played(s->voice, s->buf_id);
    }
    count = keep;
}

int sndl_voice_pos(u32 voice, u32 *buffer, u32 *play, u32 *gen)
{
    if (!refs || !play || sndl_mix_position(voice, buffer, gen)) return OS32_ERR_INVAL;
    SND2_Pos p;
    int rc = sound_api->snd2_stream_pos(stream, &p);
    if (rc) return rc;
    retire(p.played);
    unsigned k = (voice-1) % SNDL_VOICE_MAX;
    *play = 0;
    if (cursor[k].voice == voice && cursor[k].generation == sndl_mix_generation(voice)) {
        *play = cursor[k].frame;
        *buffer = cursor[k].buffer;
    }
    for (unsigned i = 0; i < count; ++i) {
        SndlMixSpan *s = &records[i];
        u32 distance = p.played - s->offset;
        if (s->voice != voice) continue;
        if (distance < s->count) {
            *buffer = s->buf_id;
            *play = frame_at(s, distance);
        } else if (cursor[k].voice == voice && generations[i] != sndl_mix_generation(voice)) {
            /* A pre-reset span still lies beyond this silent gap. */
            *buffer = cursor[k].buffer;
            *play = cursor[k].frame;
        }
        break;
    }
    return 0;
}

/* Append to the unsent suffix. Voice positions include that suffix in their
 * stream offset even when the HAL accepted only part of the last write. */
static int render(struct SndlWindow *window, u32 n)
{
    __builtin_memset(stereo, 0, n * 2 * sizeof(*stereo));
    int rc = sndl_mix_render(stereo, n, rate, record, 0);
    if (!rc) rc = sndl_chip_mix(stereo, n, rate, window);
    if (rc) return rc;
    if (sent == buffered || buffered + n > sizeof scratch / (2 * sizeof *scratch)) {
        buffered -= sent;
        if (buffered) __builtin_memmove(scratch, scratch + 2 * sent, buffered * 4);
        sent = 0;
    }
    sndl_mix_finish(stereo, scratch + 2 * buffered, n);
    buffered += n;
    window->offset += n;
    return 0;
}

static int publish_frames(u32 n)
{
    int bytes = sound_api->snd2_stream_write(stream, scratch + 2 * sent, n * 4);
    if (bytes <= 0) return bytes;
    if ((bytes & 3) || (u32)bytes > n * 4) return OS32_ERR_IO;
    sent += bytes / 4;
    written += bytes / 4;
    return bytes / 4;
}

/* Fill retired segments first. While the HAL owns all four, prepare the
 * next two periods so a retirement can be supplied without synthesizing. */
static int soft_feed(SND2_Pos pos, int priming)
{
    const u32 period = SNDL_SOFT_PERIOD;
    u32 free_frames = pos.cap - (written - pos.played);
    struct SndlWindow window = {.frames = (free_frames / period + 2) * period};
    int synced = 0;
    for (;;) {
        if (pos.state != SND2_ST_RUN || written - pos.played > pos.cap) return OS32_ERR_IO;
        u32 pending = buffered - sent;
        u32 room = pos.cap - (written - pos.played);
        u32 offset = pos.played % period;
        if (!pos.underruns && room >= offset) room -= offset;
        /* A coarse DMA count can retire two closely spaced periods followed
         * by a long gap. Pace the last writable period on ordinary turns. */
        if (feed_mode == NORMAL_PUMP && room == period && pos.time_ms - last_submit_ms < half_period_ms)
            room = 0;
        if (pending && room) {
            u32 nframes = pending;
            if (feed_mode == NORMAL_PUMP && nframes > period) nframes = period;
            int n = publish_frames(nframes);
            if (n <= 0) return n;
            last_submit_ms = pos.time_ms;
            if (priming) return 0;
        } else {
            int full = pending >= 2 * period || window.offset == window.frames;
            if (full && (priming || synced)) return 0;
            struct SndlWindow empty = {0};
            int rc = sndl_chip_prepare(full ? &empty : &window);
            if (rc) return rc;
            synced = 1;
            if (!full) {
                u32 n = room ? period : 2 * period - pending;
                if (n > window.frames - window.offset) n = window.frames - window.offset;
                rc = render(&window, n);
                if (rc) return rc;
            }
        }
        int rc = sound_api->snd2_stream_pos(stream, &pos);
        if (rc) return rc;
    }
}

static int pump(int draining, const SND2_Pos *observed)
{
    if (!refs) return OS32_ERR_INVAL;
    SND2_Pos pos;
    int rc = 0;
    if (observed) pos = *observed;
    else rc = sound_api->snd2_stream_pos(stream, &pos);
    if (rc) return rc;
    retire(pos.played);
    if (pos.state == SND2_ST_FAULTED) return OS32_ERR_IO;
    u32 queued = written - pos.played;
    if (queued > pos.cap) return -1;
    u32 space = pos.cap - queued;
    if (!explicit_open && !draining && pcm_period == SNDL_SOFT_PERIOD && pos.state == SND2_ST_RUN)
        return soft_feed(pos, observed != 0);
    struct SndlWindow window = {0};
    while (space) {
        if (sent == buffered) {
            if (draining && pos.state != SND2_ST_IDLE && !sndl_mix_queued()) break;
            if (!window.frames) window.frames = space;
            rc = sndl_chip_prepare(&window);
            if (rc) return rc;
            u32 n = space > 1024 ? 1024 : space;
            if (window.render_limit && pos.state == SND2_ST_RUN &&
                n > window.render_limit && written - pos.played < written % window.render_limit +
                    (2 * n + 2) / 3 + window.render_limit + window.render_limit / 2)
                n = window.render_limit;
            rc = render(&window, n);
            if (rc) return rc;
        }
        u32 n = buffered - sent;
        if (n > space) n = space;
        int accepted = publish_frames(n);
        if (accepted <= 0) return accepted;
        space -= accepted;
        if (space && sent == buffered && window.render_limit && pos.state == SND2_ST_RUN) {
            rc = sound_api->snd2_stream_pos(stream, &pos);
            if (rc) return rc;
            if (pos.state == SND2_ST_FAULTED) return OS32_ERR_IO;
        }
    }
    if (!window.frames) {
        rc = sndl_chip_prepare(&window);
        if (rc) return rc;
    }
    if (pos.state == SND2_ST_IDLE && pos.cap)
        return sound_api->snd2_stream_start(stream);
    return 0;
}

int sndl_pump(void)
{
    int rc = pump(0, 0);
    /* A normal turn follows the guest run and its event drain. Until then,
     * terminating a retry must abort even if its last prime just succeeded. */
    if (feed_mode != NORMAL_PUMP) feed_mode = rc ? DOS_REFILL : DOS_READY;
    return rc;
}

/* The implicit stream enters DOS just after a write, with three periods
 * queued and another ready to publish on return. */
static int prime_slice(void)
{
    if (!refs || pcm_period != SNDL_SOFT_PERIOD) return OS32_ERR_INVAL;
    u32 reserve = (rate * 35 + 1999) / 2000 + pcm_period / 2;
    u32 started = 0, initial_queue = 0, previous = written;
    int waiting = 0;
    for (;;) {
        SND2_Pos pos;
        int rc = sound_api->snd2_stream_pos(stream, &pos);
        if (rc) return rc;
        u32 queued = pos.written - pos.played;
        if (pos.state != SND2_ST_RUN || queued > pos.cap) return OS32_ERR_IO;
        if (reserve > pos.cap) return OS32_ERR_INVAL;
        if (explicit_open && queued >= reserve) return 0;
        if (!explicit_open && written != previous && queued >= 3 * pcm_period &&
            buffered - sent >= pcm_period) return 0;
        previous = written;
        /* Staging space is released on period retirement. A short or zero
         * write can therefore be followed by progress at the next boundary. */
        if (!waiting) {
            started = pos.time_ms;
            initial_queue = queued;
            waiting = 1;
        }
        if (pos.time_ms - started >= 17)
            return sent < buffered || queued >= reserve || queued > initial_queue
                ? OS32_ERR_AGAIN : OS32_ERR_TIMEOUT;
        rc = pump(0, &pos);
        if (rc) return rc;
    }
}

int sndl_prime_slice(void)
{
    if (feed_mode == NORMAL_PUMP) feed_mode = DOS_READY;
    int rc = prime_slice();
    if (rc) feed_mode = DOS_REFILL;
    return rc;
}

int sndl_slice_pending(void)
{
    return feed_mode == DOS_REFILL;
}

int sndl_pcm_release(u32 how)
{
    if (!refs || how > SND2_CLOSE_ABORT) return OS32_ERR_INVAL;
    if (refs > 1) {
        --refs;
        if (how == SND2_CLOSE_ABORT) sndl_mix_abort();
        return 0;
    }
    int drain_error = 0;
    if (how == SND2_CLOSE_DRAIN) {
        /* A stopped queue or an unbounded loop needs an application decision. */
        if (!sndl_mix_can_drain()) return OS32_ERR_BUSY;
        while (sndl_mix_pending() || sent < buffered) {
            int rc = pump(1, 0);
            if (rc == OS32_ERR_IO || rc == OS32_ERR_STALE) {
                drain_error = rc;
                how = SND2_CLOSE_ABORT;
                break;
            }
            if (rc && rc != OS32_ERR_AGAIN) return rc;
            sound_api->sys_yield();
        }
    }
    int rc = sound_api->snd2_stream_close(stream, how);
    SND2_Pos pos;
    if (!rc || sound_api->snd2_stream_pos(stream, &pos) == OS32_ERR_STALE) {
        refs = 0;
        if (how == SND2_CLOSE_ABORT || rc) sndl_mix_abort();
    }
    return drain_error ? drain_error : rc;
}

int sndl_close(u32 how)
{
    if (!explicit_open) return OS32_ERR_INVAL;
    int rc = sndl_pcm_release(how);
    if (!rc || !refs) explicit_open = 0;
    return rc;
}

int sndl_pcm_active(void)
{
    return refs != 0;
}
