#include "fmvoice.h"
#include "../../../lib/os32_opna/opna.h"

enum { IO = -1, NOSPC = -4, INVAL = -9, CSM = 512 };
typedef struct Event Event;
struct Event {
    Event *next;
    uint32_t at;
    unsigned reg, value;
};
struct FmVoice {
    _Alignas(OPNA_STATE_ALIGN) unsigned char state[OPNA_STATE_BYTES];
    unsigned char ram[OPNA_ADPCM_BYTES];
    unsigned char *wave[6];
    FmvMemory mem;
    Event *pending, *audio, *tail;
    uint32_t tm_us, previous, window_start;
    int external, started;
};

static uint32_t le32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int tag(const unsigned char *p, const char *s)
{
    return p[0]==s[0] && p[1]==s[1] && p[2]==s[2] && p[3]==s[3];
}

static int load_wave(FmVoice *v, unsigned ch, const FmvSample *s,
                     uint32_t *frames, uint32_t *rate)
{
    unsigned char h[16];
    uint32_t data = 0, size = 0;
    if (s->length < 12) return INVAL;
    if (s->read_at(s->file, 0, h, 12) != 12) return IO;
    uint32_t riff = le32(h + 4);
    if (!tag(h,"RIFF") || !tag(h+8,"WAVE") || riff < 4 || riff > s->length-8)
        return INVAL;
    uint32_t end = 8 + riff;
    for (uint32_t pos = 12; pos < end;) {
        if (end-pos < 8) return INVAL;
        if (s->read_at(s->file, pos, h, 8) != 8) return IO;
        pos += 8;
        uint32_t n = le32(h+4);
        if (n > end-pos || (n & 1) > end-pos-n) return INVAL;
        if (tag(h,"fmt ")) {
            if (*rate || n < 16) return INVAL;
            if (s->read_at(s->file, pos, h, 16) != 16) return IO;
            *rate = le32(h+4);
            if (le32(h) != 0x00010001 || !*rate || le32(h+12) != 0x00100002 ||
                (uint64_t)*rate * 2 != le32(h+8)) return INVAL;
        } else if (tag(h,"data")) {
            if (data || !n || (n & 1) || n/2 > UINT32_MAX/1024) return INVAL;
            data = pos;
            size = n;
        }
        pos += n + (n & 1);
    }
    if (!data || !*rate) return INVAL;
    v->wave[ch] = v->mem.alloc(size);
    if (!v->wave[ch]) return NOSPC;
    *frames = size/2;
    return s->read_at(s->file, data, v->wave[ch], size) == (int)size ? 0 : IO;
}

static void clear_events(FmVoice *v, Event **head)
{
    while (*head) {
        Event *e = *head;
        *head = e->next;
        v->mem.release(e);
    }
}

void fmv_delete(FmVoice *v)
{
    if (!v) return;
    clear_events(v, &v->pending);
    clear_events(v, &v->audio);
    for (unsigned i=0; i<6; ++i)
        if (v->wave[i]) v->mem.release(v->wave[i]);
    v->mem.release(v);
}

int fmv_new(FmVoice **out, FmvMemory mem, uint32_t now, int external,
            const FmvSample samples[6])
{
    if (!out || !mem.alloc || !mem.release) return INVAL;
    FmVoice *v = mem.alloc(sizeof *v);
    if (!v) return NOSPC;
    __builtin_memset(v, 0, sizeof *v);
    v->mem = mem;
    v->external = external;
    v->tm_us = now;
    int rc = opna_init(v->state, sizeof v->state, v->ram, sizeof v->ram);
    uint32_t frames[6], rates[6] = {0};
    unsigned count = 0;
    for (unsigned i=0; samples && i<6 && !rc; ++i) {
        if (!samples[i].read_at) continue;
        rc = load_wave(v, i, &samples[i], &frames[i], &rates[i]);
        ++count;
    }
    for (unsigned i=0; count==6 && i<6 && !rc; ++i)
        rc = opna_rhythm(v->state, i, v->wave[i], frames[i], rates[i]);
    if (rc) { fmv_delete(v); return rc; }
    *out = v;
    return 0;
}

void fmv_reset(FmVoice *v, uint32_t now)
{
    clear_events(v, &v->pending);
    clear_events(v, &v->audio);
    v->tail = 0;
    opna_reset(v->state);
    v->tm_us = now;
    v->started = 0;
}

static void append(FmVoice *v, Event *e)
{
    e->next = 0;
    if (v->tail) v->tail->next = e;
    else v->audio = e;
    v->tail = e;
}

static int before(uint32_t a, uint32_t b)
{
    return (int32_t)(a-b) < 0;
}

static int enqueue(FmVoice *v, uint32_t at, unsigned reg, unsigned value)
{
    Event *e = v->mem.alloc(sizeof *e);
    if (!e) return NOSPC;
    *e = (Event){ .at=at, .reg=reg, .value=value };
    if (v->external) append(v,e);
    else {
        Event **p = &v->pending;
        while (*p && !before(at, (*p)->at)) p = &(*p)->next;
        e->next = *p;
        *p = e;
    }
    return 0;
}

int fmv_write(FmVoice *v, uint32_t at, unsigned reg, unsigned value)
{
    if (!v || reg>511 || value>255) return INVAL;
    return enqueue(v,at,reg,value);
}

int fmv_csm(FmVoice *v, uint32_t at)
{
    if (!v || !v->external) return INVAL;
    return enqueue(v,at,CSM,0);
}

static int advance(FmVoice *v, uint32_t until)
{
    while (before(v->tm_us, until)) {
        unsigned next, step = until-v->tm_us, due;
        opna_next_event(v->state, &next);
        if (next == UINT32_MAX) { v->tm_us = until; break; }
        if (step > 32767) step = 32767;
        if (step > next) step = next;
        /* Reserve before Count so an allocation failure can be retried. */
        Event *e = 0;
        if (step == next && !(e=v->mem.alloc(sizeof *e))) return NOSPC;
        opna_timer_count(v->state+OPNA_TIMER_OFFSET, step, &due);
        v->tm_us += step;
        if (due) {
            *e = (Event){ .at=v->tm_us, .reg=CSM };
            append(v,e);
        } else if (e) v->mem.release(e);
    }
    return 0;
}

static int sync_timer(FmVoice *v, uint32_t now)
{
    if (v->external) return 0;
    Event *e;
    while ((e=v->pending) && !before(now,e->at)) {
        int rc = advance(v,e->at);
        if (rc) return rc;
        if ((e->reg >= 0x24 && e->reg <= 0x27) ||
            (e->reg >= 0x2d && e->reg <= 0x2f) || e->reg == 0x110)
            opna_timer_write(v->state + OPNA_TIMER_OFFSET, e->reg, e->value);
        v->pending = e->next;
        append(v,e);
    }
    return advance(v,now);
}

static void consume(FmVoice *v)
{
    Event *e = v->audio;
    if (e->reg == CSM) opna_apply_csm(v->state);
    else if (e->reg == 0x27 || (e->reg >= 0x2d && e->reg <= 0x2f) || e->reg == 0x110) {
        /* The combined write ABI also writes the embedded timer. Isolate
         * that side effect from the authoritative synchronous timer. */
        unsigned char *timer = v->state+OPNA_TIMER_OFFSET;
        unsigned char saved[OPNA_TIMER_BYTES];
        __builtin_memcpy(saved,timer,sizeof saved);
        __builtin_memset(timer, 0, sizeof saved);
        /* Only mode bits 6 and 7 belong to the audio engine at 27h. */
        unsigned value = e->reg == 0x27 ? e->value & 0xc0 : e->value;
        opna_write(v->state, e->reg, value);
        __builtin_memcpy(timer,saved,sizeof saved);
    } else if (e->reg < 0x24 || e->reg > 0x26) {
        opna_write(v->state, e->reg, e->value);
    }
    v->audio = e->next;
    if (!v->audio) v->tail = 0;
    v->mem.release(e);
}

static void flush(FmVoice *v)
{
    while (v->audio) consume(v);
}

int fmv_status(FmVoice *v, uint32_t now, unsigned bank, unsigned *value)
{
    if (!v || bank>1 || !value) return INVAL;
    int rc = sync_timer(v,now);
    if (rc) return rc;
    if (bank) flush(v);
    return opna_status(v->state,bank,value);
}

int fmv_read(FmVoice *v, uint32_t now, unsigned reg, unsigned *value)
{
    if (!v || reg>511 || !value) return INVAL;
    int rc = sync_timer(v,now);
    if (rc) return rc;
    flush(v);
    return opna_read(v->state,reg,value);
}

static void render_to(FmVoice *v, unsigned char **out, unsigned count)
{
    while (count) {
        unsigned n = count > OPNA_MAX_FRAMES ? OPNA_MAX_FRAMES : count;
        opna_render(v->state,*out,n);
        *out += 4*n;
        count -= n;
    }
}

int fmv_render_part(FmVoice *v, uint32_t now, unsigned char *out, uint32_t frames,
                    uint32_t offset, uint32_t count)
{
    if (!v || (count && !out) || count > UINT32_MAX/4 ||
        offset > frames || count > frames-offset) return INVAL;
    int rc = sync_timer(v,now);
    if (rc) return rc;
    if (!count && frames) return 0; /* Timer preparation for a nonempty window. */
    if (!offset) {
        v->window_start = v->started ? v->previous : now;
        v->previous = now;
        v->started = 1;
    }
    if (!count) return 0; /* Empty pump advances the clock and retains audio events. */
    uint32_t start = v->window_start;
    uint32_t span = now-start, cursor = offset;
    while (v->audio && !before(now,v->audio->at)) {
        uint32_t at = v->audio->at, frame = 0;
        if (before(start,at) && span)
            frame = (uint64_t)(at-start)*frames/span;
        if (frame >= frames) frame = frames-1;
        if (frame >= offset+count) break;
        if (frame > cursor) {
            render_to(v,&out,frame-cursor);
            cursor = frame;
        }
        consume(v);
    }
    render_to(v,&out,offset+count-cursor);
    return 0;
}

int fmv_render(FmVoice *v, uint32_t now, unsigned char *out, uint32_t frames)
{
    return fmv_render_part(v, now, out, frames, 0, frames);
}
