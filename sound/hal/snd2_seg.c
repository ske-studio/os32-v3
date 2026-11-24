#include "snd2_int.h"
#include "snd2_ring_math.h"
#include "kstring.h"

static u8 *segment(const Snd2Stream *s, u32 seg)
{
    return s->ring + seg * s->period * snd2_frame_bytes(s);
}

static void clear_segment(Snd2Stream *s, u32 seg)
{
    kmemset(segment(s, seg), 0, s->period * snd2_frame_bytes(s));
    s->seg_n[seg] = 0;
    s->refill_tried &= ~(1U << seg);
}

static void append(Snd2Stream *s, u32 seg)
{
    u32 used = s->seg_n[seg];
    u32 n = s->written - s->copied;
    if (s->state == SND2_ST_RUN && used < s->period) {
        s->refill_tried |= 1U << seg;
        if (n < s->min_stg) s->min_stg = n;
    }
    if (n > s->period - used) n = s->period - used;
    if (!n) return;
    if (!used) s->seg_first[seg] = s->copied;
    snd2_stg_read(s, s->copied, segment(s, seg) + used * snd2_frame_bytes(s), n);
    s->seg_n[seg] += n;
    s->copied += n;
}

void snd2_seg_reset(Snd2Stream *s, u32 now)
{
    for (u32 i = 0; i < s->nseg; ++i)
        clear_segment(s, i);
    s->copied = s->played;
    s->last_hw = 0;
    s->last_progress_ms = s->last_obs_ms = now;
}

void snd2_seg_prime(Snd2Stream *s)
{
    for (u32 i = 0; i <= s->lead; ++i)
        append(s, i);
}

void snd2_seg_resync(Snd2Stream *s, u32 now)
{
    ++s->epoch;
    snd2_seg_reset(s, now);
    if (!s->paused) snd2_seg_prime(s);
}

static int inside_margin(const Snd2Stream *s, u32 hw)
{
    return hw % s->period >= s->period - s->margin;
}

void snd2_seg_pause(Snd2Stream *s)
{
    u32 p = s->last_hw / s->period;
    u32 keep = inside_margin(s, s->last_hw) ? 2 : 1;
    s->paused = 1;
    s->copied = s->played;
    for (u32 i = 0; i < s->nseg; ++i) {
        u32 seg = (p + i) % s->nseg;
        if (i >= keep) clear_segment(s, seg);
        else if (s->seg_n[seg])
            s->copied = s->seg_first[seg] + s->seg_n[seg];
    }
}

u32 snd2_seg_played_now(const Snd2Stream *s)
{
    u32 n = s->seg_n[s->last_hw / s->period];
    u32 offset = s->last_hw % s->period;
    return s->played + (offset < n ? offset : n);
}

int snd2_seg_observe(Snd2Stream *s, int have_pos, u32 hw, u32 now)
{
    u32 ring = s->period * s->nseg;
    if (!s->fmt.rate) return -1;
    /* Integer timestamps can hide a full turn at its rounded-down duration. */
    u32 revolution_ms = ring * 1000U / s->fmt.rate;
    u32 prev = s->last_hw / s->period;
    if (now - s->last_obs_ms >= revolution_ms) return -1;
    if (!have_pos || hw == s->last_hw) {
        if (now - s->last_progress_ms >= revolution_ms) return -1;
        if (!have_pos) return 0;
    }
    if (hw >= ring) return -1;
    u32 p = hw / s->period;
    /* Retiring the old segment would clear the protected next segment. */
    if (snd2_backtracked(prev, s->last_hw, p, hw) ||
        (prev == (p + 1) % s->nseg && inside_margin(s, hw))) {
        ++s->repeats;
        return -1;
    }
    if (hw != s->last_hw) s->last_progress_ms = now;
    s->last_obs_ms = now;
    u32 step = (hw + ring - s->last_hw) % ring;
    if (step > s->max_dma_step) s->max_dma_step = step;
    s->last_hw = hw;
    for (; prev != p; prev = (prev + 1) % s->nseg) {
        u32 n = s->seg_n[prev];
        s->played += n;
        if (n) s->had_data = 1;
        if (s->state == SND2_ST_RUN && !s->paused && s->had_data &&
            n < s->period) {
            if (!s->underruns) {
                u32 next = (prev + 1) % s->nseg;
                s->gap_tried = (s->refill_tried >> prev) & 1U;
                s->gap_frame = s->played;
                s->gap_n = n;
                s->gap_next = s->seg_n[next] ? s->seg_first[next] : ~0U;
                s->gap_stg = s->written - s->copied;
            }
            ++s->underruns;
        }
        clear_segment(s, prev);
    }
    if (!s->paused) {
        for (u32 i = 1; i <= s->lead; ++i) {
            u32 seg = (p + i) % s->nseg;
            if (i == 1 && inside_margin(s, hw) && s->seg_n[seg] < s->period)
                break;
            append(s, seg);
        }
    }
    return 0;
}
