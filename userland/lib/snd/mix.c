#include "mix.h"

#define BUF_MAX 512
#define ONE 65536u

enum State { FREE, QUEUED, GENERATED, DONE };
typedef struct Buffer Buffer;
struct Buffer {
    const uint8_t *pcm;
    uint32_t owner, id, frames, flags, how;
    enum State state;
    Buffer *next;
};

typedef struct {
    uint32_t id, hz, left, right, frame, phase;
    SND2_Fmt fmt;
    int playing;
    Buffer *head, *tail;
} Voice;

static Voice voices[SNDL_VOICE_MAX];
static Buffer buffers[BUF_MAX];
static Buffer *done_head, *done_tail;
static uint32_t next_voice, next_buffer;

/* Q16 amplitudes at whole dB and hundredths of a dB respectively.
 * round(65536 * 10^(-attenuation_cb/2000)). */
static const uint32_t whole_db[] = {
    65536, 58409, 52057, 46396, 41350, 36854, 32846, 29274, 26090, 23253,
    20724, 18471, 16462, 14672, 13076, 11654, 10387, 9257, 8250, 7353,
    6554, 5841, 5206, 4640, 4135, 3685, 3285, 2927, 2609, 2325,
    2072, 1847, 1646, 1467, 1308, 1165, 1039, 926, 825, 735,
    655, 584, 521, 464, 414, 369, 328, 293, 261, 233,
    207, 185, 165, 147, 131, 117, 104, 93, 83, 74,
    66, 58, 52, 46, 41, 37, 33, 29, 26, 23,
    21, 18, 16, 15, 13, 12, 10, 9, 8, 7,
    7, 6, 5, 5, 4, 4, 3, 3, 3, 2,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
};
static const uint32_t fraction_db[] = {
    65536, 65461, 65385, 65310, 65235, 65160, 65085, 65010, 64935, 64860,
    64786, 64711, 64637, 64562, 64488, 64414, 64340, 64266, 64192, 64118,
    64044, 63971, 63897, 63823, 63750, 63677, 63603, 63530, 63457, 63384,
    63311, 63238, 63165, 63093, 63020, 62948, 62875, 62803, 62731, 62658,
    62586, 62514, 62442, 62371, 62299, 62227, 62156, 62084, 62013, 61941,
    61870, 61799, 61728, 61657, 61586, 61515, 61444, 61373, 61303, 61232,
    61162, 61091, 61021, 60951, 60881, 60811, 60741, 60671, 60601, 60531,
    60462, 60392, 60323, 60253, 60184, 60115, 60045, 59976, 59907, 59838,
    59770, 59701, 59632, 59563, 59495, 59426, 59358, 59290, 59222, 59153,
    59085, 59017, 58949, 58882, 58814, 58746, 58679, 58611, 58544, 58476,
};

static uint32_t gain(int attenuation)
{
    if (attenuation >= 10000)
        return 0;
    return ((uint64_t)whole_db[attenuation / 100] *
            fraction_db[attenuation % 100] + ONE / 2) >> 16;
}

static Voice *lookup(uint32_t id)
{
    if (!id)
        return 0;
    Voice *v = &voices[(id - 1) % SNDL_VOICE_MAX];
    return v->id == id ? v : 0;
}

int sndl_voice_new(const SND2_Fmt *src, unsigned long *voice)
{
    if (!voice || !src || !src->rate || src->rate > UINT32_MAX ||
        (src->channels != 1 && src->channels != 2) || src->flags ||
        !((src->bits == 8 && src->enc == SND2_ENC_U8) ||
          (src->bits == 16 && src->enc == SND2_ENC_S16LE)))
        return -1;
    for (unsigned i = 0; i < SNDL_VOICE_MAX; ++i) {
        if (!++next_voice)
            ++next_voice;
        Voice *v = &voices[(next_voice - 1) % SNDL_VOICE_MAX];
        if (!v->id) {
            *v = (Voice){.id = next_voice, .fmt = *src, .hz = src->rate,
                         .left = ONE, .right = ONE};
            *voice = v->id;
            return 0;
        }
    }
    return SNDL_AGAIN;
}

int sndl_voice_submit(unsigned long id, const void *pcm, unsigned long bytes,
                      unsigned long flags, unsigned long *buf_id)
{
    Voice *v = lookup(id);
    if (!v || !pcm || !buf_id || !bytes || (flags & ~SNDL_LOOP))
        return -1;
    unsigned stride = v->fmt.channels * (v->fmt.bits / 8);
    if (bytes % stride)
        return -1;
    for (unsigned i = 0; i < BUF_MAX; ++i) {
        Buffer *b = &buffers[i];
        if (b->state != FREE)
            continue;
        if (!++next_buffer)
            ++next_buffer;
        *b = (Buffer){.pcm = pcm, .id = next_buffer, .owner = id,
                      .frames = bytes / stride, .flags = flags, .state = QUEUED};
        if (v->tail)
            v->tail->next = b;
        else
            v->head = b;
        if (!(flags & SNDL_LOOP))
            for (Buffer *p = v->head; p != b && p; p = p->next)
                p->flags = 0;
        v->tail = b;
        *buf_id = b->id;
        return 0;
    }
    return SNDL_AGAIN;
}

int sndl_voice_set(unsigned long id, unsigned long hz, signed long vol, signed long pan)
{
    Voice *v = lookup(id);
    if (!v || !hz || hz > UINT32_MAX || vol < -10000 || vol > 0 || pan < -10000 || pan > 10000)
        return -1;
    v->hz = hz;
    v->left = gain(-vol + (pan > 0 ? pan : 0));
    v->right = gain(-vol + (pan < 0 ? -pan : 0));
    return 0;
}

int sndl_voice_play(unsigned long id)
{
    Voice *v = lookup(id);
    if (!v)
        return -1;
    v->playing = 1;
    return 0;
}

int sndl_voice_stop(unsigned long id)
{
    Voice *v = lookup(id);
    if (!v)
        return -1;
    v->playing = 0;
    return 0;
}

static void complete(Buffer *b, uint32_t how)
{
    b->pcm = 0;
    b->state = DONE;
    b->how = how;
    b->next = 0;
    if (done_tail)
        done_tail->next = b;
    else
        done_head = b;
    done_tail = b;
}

void sndl_mix_played(uint32_t id, uint32_t buf_id)
{
    for (unsigned i = 0; i < BUF_MAX; ++i) {
        Buffer *b = &buffers[i];
        if (b->state == GENERATED && b->owner == id && b->id == buf_id) {
            complete(b, SNDL_PLAYED);
            return;
        }
    }
}

int sndl_voice_done(unsigned long id, unsigned long *buf_id, unsigned long *how)
{
    if (!buf_id || !how)
        return -1;
    Buffer *prev = 0;
    for (Buffer *b = done_head; b; b = b->next) {
        if (b->owner == id) {
            *buf_id = b->id;
            *how = b->how;
            if (prev)
                prev->next = b->next;
            else
                done_head = b->next;
            if (done_tail == b)
                done_tail = prev;
            b->state = FREE;
            return 0;
        }
        prev = b;
    }
    return lookup(id) ? SNDL_AGAIN : -1;
}

int sndl_voice_reset(unsigned long id)
{
    Voice *v = lookup(id);
    if (!v)
        return -1;
    for (unsigned i = 0; i < BUF_MAX; ++i)
        if (buffers[i].owner == id && buffers[i].state == GENERATED)
            complete(&buffers[i], SNDL_RESET);
    while (v->head) {
        Buffer *b = v->head;
        v->head = b->next;
        complete(b, SNDL_RESET);
    }
    v->tail = 0;
    v->playing = 0;
    v->frame = v->phase = 0;
    return 0;
}

int sndl_voice_delete(unsigned long id)
{
    if (sndl_voice_reset(id))
        return -1;
    lookup(id)->id = 0;
    return 0;
}

int sndl_mix_position(unsigned long id, unsigned long *buf_id, unsigned long *frame)
{
    Voice *v = lookup(id);
    if (!v || !buf_id || !frame)
        return -1;
    *buf_id = v->head ? v->head->id : 0;
    *frame = v->frame;
    return 0;
}

static int sample(const uint8_t *p, unsigned bits)
{
    if (bits == 8)
        return ((int)p[0] - 128) * 256;
    unsigned raw = p[0] | ((unsigned)p[1] << 8);
    return (int)(raw ^ 32768u) - 32768;
}

static void render_voice(Voice *v, int32_t *out, uint32_t frames, uint32_t step,
                         SndlMixRecord record, void *ctx)
{
    unsigned width = v->fmt.bits / 8;
    unsigned stride = width * v->fmt.channels;
    for (uint32_t off = 0; off < frames && v->head;) {
        Buffer *b = v->head;
        uint64_t pos = ((uint64_t)v->frame << 16) + v->phase;
        uint64_t end = (uint64_t)b->frames << 16;
        /* A downsampling step may skip an entire queued buffer. Record its
         * completion at this boundary without reading a sample. */
        uint64_t until_end = pos >= end ? 0 : (end - pos + step - 1) / step;
        uint32_t n = frames - off;
        if (until_end < n)
            n = (uint32_t)until_end;
        int boundary = n == until_end;
        SndlMixSpan span = {v->id, b->id, off, n, v->frame, v->phase, step,
                            b->flags ? b->frames : 0, boundary && !b->flags};
        if (!record(ctx, &span))
            break;
        for (uint32_t i = 0; i < n; ++i, pos += step) {
            const uint8_t *p = b->pcm + (pos >> 16) * stride;
            int left = sample(p, v->fmt.bits);
            int right = v->fmt.channels == 1 ? left : sample(p + width, v->fmt.bits);
            out[2 * (off + i)] += (int64_t)left * v->left / ONE;
            out[2 * (off + i) + 1] += (int64_t)right * v->right / ONE;
        }
        off += n;
        if (boundary && b->flags)
            pos %= end;
        else if (boundary) {
            v->head = b->next;
            if (!v->head)
                v->tail = 0;
            b->state = GENERATED;
            b->pcm = 0;
            pos = v->head ? pos - end : 0;
        }
        v->frame = pos >> 16;
        v->phase = pos & (ONE - 1);
    }
}

int sndl_mix_render(int32_t *out, uint32_t frames, uint32_t rate,
                     SndlMixRecord record, void *ctx)
{
    if (!rate || !out || !record || frames > 1024)
        return -1;
    /* Validate the whole block before recording or advancing any voice. */
    uint32_t steps[SNDL_VOICE_MAX];
    for (unsigned i = 0; i < SNDL_VOICE_MAX; ++i) {
        if (!voices[i].playing)
            continue;
        uint64_t scaled = (uint64_t)voices[i].hz << 16;
        if (scaled < rate || scaled > (uint64_t)UINT32_MAX * rate)
            return -1;
        steps[i] = scaled / rate;
    }
    for (unsigned i = 0; i < SNDL_VOICE_MAX; ++i)
        if (voices[i].playing)
            render_voice(&voices[i], out, frames, steps[i], record, ctx);
    return 0;
}

void sndl_mix_finish(const int32_t *stereo, int16_t *out, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; ++i) {
        for (unsigned ch = 0; ch < 2; ++ch) {
            int32_t s = *stereo++;
            *out++ = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
        }
    }
}
