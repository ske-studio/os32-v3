#include "sndl.h"

static KernelAPI api;
static SND2_Fmt fmt = {44100,16,2,SND2_ENC_S16LE,0};
static SND2_Pos pos;
static u32 mode, chip_type, ram, now, limit, opens, closes, starts, writes, hw_opens;
static short received[128];
static unsigned used;
static int faulted;
static unsigned char heap[800000] __attribute__((aligned(16)));
static u32 top;

static void *alloc(u32 n)
{
    if (n > sizeof heap - top) return 0;
    void *p = heap + top;
    top += (n+15) & ~15UL;
    return p;
}
static void release(void *p) { (void)p; }
static int time_now(u32 *lo, u32 *hi) { *lo = ++now; *hi = 0; return 0; }
static int devices(void) { return chip_type ? 2 : 1; }
static int info(u32 i, SND2_DevInfo *o)
{
    *o = (SND2_DevInfo){.kind=i ? SND2_KIND_FM : SND2_KIND_PCM_OUT, .dev=i, .flags=mode};
    return 0;
}
static int caps(u32 id, SND2_Caps *c, u32 size)
{
    (void)id; (void)size;
    c->fmt_count = 1; c->fmt[0] = fmt;
    return 0;
}
static int stream_open(const SND2_StreamDesc *d, u32 *s)
{
    if (d->dev || d->fmt.rate != 44100) return -1;
    *s = 7;
    ++opens;
    pos = (SND2_Pos){.cap=4};
    used = 0;
    return 0;
}
static int stream_pos(u32 s, SND2_Pos *p)
{
    if (faulted) return OS32_ERR_IO;
    *p = pos;
    return s == 7 ? 0 : -1;
}
static int stream_write(u32 s, const void *data, u32 bytes)
{
    if (s != 7) return -1;
    u32 n = bytes/4;
    if (n > limit) n = limit;
    if (n > pos.cap - (pos.written-pos.played) || 2*n > 128-used) return -1;
    for (u32 i = 0; i < 2*n; ++i) received[used++] = ((const short *)data)[i];
    limit -= n;
    pos.written += n;
    return n*4;
}
static int start(u32 s)
{
    if (s != 7 || pos.written-pos.played != pos.cap) return -1;
    ++starts; pos.state = SND2_ST_RUN;
    return 0;
}
static int stream_close(u32 s, u32 how)
{
    if (s != 7 || how > 1) return -1;
    ++closes;
    return 0;
}
static i32 advance(void) { pos.played = pos.written; limit = 4; return 0; }
static int chip_caps(u32 dev, SND2_ChipCaps *c, u32 size)
{
    (void)size;
    *c = (SND2_ChipCaps){.chip=chip_type,.fm_ch=chip_type == SND2_CHIP_OPN ? 3 : 6,
                         .ssg_ch=3,.banks=chip_type,.adpcm_bytes=ram,
                         .flags=SND2_CHF_TIMER | (ram ? SND2_CHF_ADPCM : 0)};
    return dev == 1 ? 0 : -1;
}
static int chip_open(u32 dev, u32 *h) { ++hw_opens; *h = dev; return 0; }
static int chip_write(u32 h, const SND2_RegW *w, u32 n)
{
    if (h != 1 || !w) return -1;
    writes += n;
    return n;
}
static int chip_read(u32 h, u32 b, u32 what, u32 reg)
{ (void)h; (void)b; (void)what; (void)reg; return 1; }
static int chip_close(u32 h) { return h == 1 ? 0 : -1; }

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static int pump(void)
{
    short data[] = {101,-101,202,-202,303,-303,404,-404};
    u32 v, b, done, how, play, gen;
    CHECK(!sndl_open(&fmt,256,3) && !sndl_voice_new(&fmt,&v));
    CHECK(!sndl_voice_submit(v,data,sizeof data,0,&b) && !sndl_voice_play(v));
    limit = 1;
    CHECK(!sndl_pump() && used == 2 && !starts);
    data[2] = 999;
    limit = 3;
    CHECK(!sndl_pump() && used == 8 && starts == 1 && received[2] == 202);
    pos.played = 3; limit = 1;
    CHECK(!sndl_pump() && sndl_voice_done(v,&done,&how) == -14);
    CHECK(!sndl_voice_pos(v,&done,&play,&gen) && done == b && play == 3);
    pos.played = 4;
    CHECK(!sndl_pump() && !sndl_voice_done(v,&done,&how) && done == b && how == SNDL_PLAYED);
    CHECK(!sndl_voice_submit(v,data,sizeof data,0,&b));
    CHECK(!sndl_close(SND2_CLOSE_ABORT) && !sndl_voice_done(v,&done,&how) && how == SNDL_RESET);
    CHECK(!sndl_open(&fmt,256,3) && !sndl_voice_submit(v,data,sizeof data,0,&b));
    CHECK(!sndl_voice_play(v) && !sndl_close(SND2_CLOSE_DRAIN));
    CHECK(!sndl_voice_done(v,&done,&how) && how == SNDL_PLAYED && !sndl_voice_delete(v));
    CHECK(!sndl_open(&fmt,256,3) && !sndl_voice_new(&fmt,&v));
    CHECK(!sndl_voice_submit(v,data,sizeof data,0,&b));
    CHECK(sndl_close(SND2_CLOSE_DRAIN) == OS32_ERR_BUSY && !sndl_voice_play(v));
    faulted = 1;
    CHECK(sndl_close(SND2_CLOSE_DRAIN) == OS32_ERR_IO);
    CHECK(!sndl_voice_done(v,&done,&how) && how == SNDL_RESET);
    faulted = 0;
    CHECK(!sndl_open(&fmt,256,3) && !sndl_close(SND2_CLOSE_ABORT) && !sndl_voice_delete(v));
    return 0;
}

static int chips(void)
{
    SNDL_ChipReq r = {.size=sizeof r, .chip=SND2_CHIP_OPNA};
    SNDL_ChipGot g;
    u32 h;
    mode = SND2_DEV_FM_HW | SND2_DEV_SOFT_CPU;
    CHECK(sndl_chip_open(&r,&h,&g) < 0);
    mode = 0;
    CHECK(sndl_chip_open(&r,&h,&g) < 0);
    chip_type = SND2_CHIP_OPNA;
    mode = SND2_DEV_FM_NONE;
    CHECK(sndl_chip_open(&r,&h,&g) < 0);
    mode = SND2_DEV_SOFT_CPU;
    CHECK(!sndl_chip_open(&r,&h,&g) && g.backend == SNDL_HW && !sndl_chip_close(h));
    r.pol[0] = 4;
    CHECK(sndl_chip_open(&r,&h,&g) == -9);
    r.pol[0] = SNDL_POL_FAIL;
    mode |= SND2_DEV_FM_SOFT;
    CHECK(sndl_chip_open(&r,&h,&g) < 0);
    r.pol[0] = 0;
    r.flags = SNDL_REQ_PREFER_SOFT;
    mode = SND2_DEV_SOFT_CPU;
    u32 before = opens, closed = closes, hw = hw_opens;
    CHECK(!sndl_open(&fmt,256,3));
    CHECK(!sndl_chip_open(&r,&h,&g) && g.backend == SNDL_SOFT && hw == hw_opens);
    SND2_RegW w = {.reg=8,.val=10};
    CHECK(sndl_chip_write(h,&w,1,now) == 1 && sndl_chip_read(h,0,2,8) == 10);
    CHECK(!sndl_chip_close(h) && opens == before+1 && closes == closed);
    CHECK(!sndl_close(SND2_CLOSE_DRAIN) && closes == closed+1);
    r.flags = 0;
    r.pol[0] = SNDL_POL_FALLDOWN;
    chip_type = SND2_CHIP_OPN;
    CHECK(!sndl_chip_open(&r,&h,&g) && g.backend == SNDL_HW_FALL && g.lost_fm_ch == 3);
    SND2_RegW batch[] = {{1,0x30,0,0},{0,0x10,0,0},{0,0x29,0,0},{0,8,0,0}};
    CHECK(sndl_chip_write(h,batch,4,0) == 4 && writes == 1);
    CHECK(!sndl_chip_read(h,1,1,0) && sndl_chip_read(h,1,2,8) == 255 && !sndl_chip_close(h));
    r.need = SND2_CHF_ADPCM;
    for (chip_type=1; chip_type<=2; chip_type++) {
        CHECK(!sndl_chip_open(&r,&h,&g) && g.backend == SNDL_HW_FALL);
        CHECK((sndl_chip_read(h,1,1,0) & 12) == 12 && sndl_chip_read(h,1,2,8) == 0);
        w = (SND2_RegW){1,8,0,0};
        CHECK(sndl_chip_write(h,&w,1,0) == 1 && writes == 1 && !sndl_chip_close(h));
    }
    return 0;
}

void _start(void)
{
    api = (KernelAPI){.snd2_dev_count=devices,.snd2_dev_info=info,.snd2_caps=caps,
        .snd2_stream_open=stream_open,.snd2_stream_pos=stream_pos,.snd2_stream_write=stream_write,
        .snd2_stream_start=start,.snd2_stream_close=stream_close,.sys_yield=advance,
        .sys_time_now=time_now,.mem_alloc=alloc,.mem_free=release,.snd2_chip_caps=chip_caps,
        .snd2_chip_open=chip_open,.snd2_chip_write=chip_write,.snd2_chip_read=chip_read,
        .snd2_chip_close=chip_close};
    sndl_init(&api);
    int rc = pump();
    if (!rc) rc = chips();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
