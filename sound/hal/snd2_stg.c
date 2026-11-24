#include "snd2_int.h"
#include "snd2_ring_math.h"
#include "kstring.h"
#include "mm/kmalloc.h"

u32 snd2_frame_bytes(const Snd2Stream *s)
{
    return (s->fmt.bits / 8U) * s->fmt.channels;
}

/* Called once at open, after the stream format has been validated. */
int snd2_stg_alloc(Snd2Stream *s)
{
    s->stg = kmalloc(SND2_STG_FRAMES * snd2_frame_bytes(s));
    s->stg_frames = s->stg ? SND2_STG_FRAMES : 0;
    return s->stg ? 0 : OS32_ERR_NOSPC;
}

void snd2_stg_free(Snd2Stream *s)
{
    if (s->stg) kfree(s->stg);
    s->stg = 0;
    s->stg_frames = 0;
}

/* One foreground producer. Only publish needs the IRQ lock after reservation.
 * The consumer may retire more frames during the copy, never fewer. */
u32 snd2_stg_reserve(const Snd2Stream *s, u32 frames, u32 *off)
{
    u32 limit = s->cap < s->stg_frames ? s->cap : s->stg_frames;
    u32 free = snd2_room(limit, s->written - s->played);
    return snd2_window(s->written, s->stg_frames, free, frames, off);
}

void snd2_stg_write(Snd2Stream *s, u32 off, const void *src, u32 frames)
{
    u32 bytes = snd2_frame_bytes(s);
    u32 head = snd2_prefix(s->stg_frames, off, frames);
    kmemcpy(s->stg + off * bytes, src, head * bytes);
    if (frames > head)
        kmemcpy(s->stg, (const u8 *)src + head * bytes, (frames - head) * bytes);
}

void snd2_stg_publish(Snd2Stream *s, u32 frames)
{
    s->written += frames;
}

void snd2_stg_read(const Snd2Stream *s, u32 first, void *dst, u32 frames)
{
    u32 bytes = snd2_frame_bytes(s);
    u32 off = first % s->stg_frames;
    u32 head = snd2_prefix(s->stg_frames, off, frames);
    kmemcpy(dst, s->stg + off * bytes, head * bytes);
    if (frames > head)
        kmemcpy((u8 *)dst + head * bytes, s->stg, (frames - head) * bytes);
}
