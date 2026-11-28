#include "live.h"

static u32 stream, refs, rate, written, sent, buffered;
static int explicit_open;
static u32 pcm_period;
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
    records[count++].offset += written;
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
    /* Stage one period ahead for the implicit OPNA stream. A full HAL
     * retains the suffix in scratch until a segment retires. */
    int soft_run = !explicit_open && !draining && pcm_period == SNDL_SOFT_PERIOD &&
                   pos.state == SND2_ST_RUN;
    if (soft_run) {
        if (!space && sent < buffered) {
            if (observed) return 0;
        } else {
            space = (space / pcm_period + 1) * pcm_period;
        }
    }
    struct SndlWindow window = {0};
    while (space) {
        if (sent == buffered) {
            if (draining && pos.state != SND2_ST_IDLE && !sndl_mix_queued()) break;
            if (!window.frames) window.frames = space;
            rc = sndl_chip_prepare(&window);
            if (rc) return rc;
            u32 n = space > 1024 ? 1024 : space;
            /* Allow a period of cached position age and the half-period DMA guard. */
            if (window.render_limit && pos.state == SND2_ST_RUN &&
                n > window.render_limit && written - pos.played < written % window.render_limit +
                    (2 * n + 2) / 3 + window.render_limit + window.render_limit / 2)
                n = window.render_limit;
            __builtin_memset(stereo, 0, n * 2 * sizeof(*stereo));
            rc = sndl_mix_render(stereo, n, rate, record, 0);
            if (rc) return rc;
            rc = sndl_chip_mix(stereo, n, rate, &window);
            if (rc) return rc;
            sndl_mix_finish(stereo, scratch, n);
            window.offset += n;
            buffered = n;
            sent = 0;
        }
        if (soft_run && !pos.underruns &&
            written - (pos.played - pos.played % pcm_period) == pos.cap) {
            /* Staging is full until retirement. Keep the prepared suffix. */
            if (observed) return 0;
            break;
        }
        u32 n = buffered - sent;
        if (n > space) n = space;
        int bytes = sound_api->snd2_stream_write(stream, scratch + 2*sent, n*4);
        if (bytes <= 0) return bytes;
        if ((bytes & 3) || (u32)bytes > n*4) return -1;
        sent += bytes/4;
        written += bytes/4;
        space -= bytes/4;
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
    return pump(0, 0);
}

/* Keep 17.5 ms plus the DMA guard queued. The prepared period can be
 * published directly on return even when the cached DMA position is old. */
int sndl_prime_slice(void)
{
    if (!refs || pcm_period != SNDL_SOFT_PERIOD) return OS32_ERR_INVAL;
    u32 reserve = (rate * 35 + 1999) / 2000 + pcm_period / 2;
    u32 started = 0, initial_queue = 0;
    int waiting = 0;
    for (;;) {
        SND2_Pos pos;
        int rc = sound_api->snd2_stream_pos(stream, &pos);
        if (rc) return rc;
        u32 queued = pos.written - pos.played;
        if (pos.state != SND2_ST_RUN || queued > pos.cap) return OS32_ERR_IO;
        if (queued >= reserve && (explicit_open || buffered - sent >= pcm_period)) return 0;
        /* Staging space is released on period retirement. A short or zero
         * write can therefore be followed by progress at the next boundary. */
        if (!waiting) {
            started = pos.time_ms;
            initial_queue = queued;
            waiting = 1;
        }
        if (reserve > pos.cap) return OS32_ERR_INVAL;
        if (pos.time_ms - started >= 17)
            return sent < buffered || queued >= reserve || queued > initial_queue
                ? OS32_ERR_AGAIN : OS32_ERR_TIMEOUT;
        rc = pump(0, &pos);
        if (rc) return rc;
    }
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
