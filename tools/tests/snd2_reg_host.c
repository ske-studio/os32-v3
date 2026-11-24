#include "snd2.h"
int snd2_refill_caps(Snd2Device *d, SND2_Caps *out) { (void)d; (void)out; return -1; }

static int fail_stop, stops, owner = 7, caps_result, units;
static u32 ring_frames = 4096, period_min = 257, second_rate = 48000;
static SND2_Fmt fmt = {44100, 16, 2, SND2_ENC_S16LE, 0};
static int caps(void *ctx, SND2_Caps *c)
{
    c->fmt_count = 2;
    c->fmt[1] = fmt;
    c->fmt[1].rate = second_rate;
    c->fmt[0] = fmt;
    c->period_min = period_min;
    c->period_max = 2048;
    c->ring_frames = ring_frames;
    c->dsp_units = units;
    return caps_result;
}

void snd2_teardown_device(Snd2Device *d)
{
    ++stops;
    d->stream.owner = 0;
    if (fail_stop)
        snd2_fault(d);
}

int snd2_owned_device(u32 stream, Snd2Device **out)
{
    *out = snd2_device(0);
    if (stream != 123)
        return OS32_ERR_STALE;
    return (*out)->stream.owner == owner ? 0 : OS32_ERR_INVAL;
}

void *kmemcpy(void *dst, const void *src, u32 n)
{
    for (u32 i = 0; i < n; ++i)
        ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}

static int open_hw(void *c, const SND2_Fmt *f, u32 p, u8 **r) { return 0; }
static int pos(void *c, u32 *p) { return 0; }
static int ok(void *c) { return 0; }
static void release(void *c, int leak) {}
static int volume(void *c, u32 l, u32 r) { return 0; }
static u32 max_frames = 4, readable, writable;
static int processes;
static void *fault_env[5];
void ring3_fault_kill(void) { __builtin_longjmp(fault_env, 1); }
int ring3_user_range_ok(u32 addr, u32 bytes) { return addr == 100 && bytes <= readable; }
int ring3_user_ranges_writable(u32 addr, u32 bytes, u32 other, u32 len)
{
    return addr == 200 && bytes <= writable && !other && !len;
}
int snd2_chip_dev_info(u32 index, SND2_DevInfo *out)
{
    if (index || !out) return OS32_ERR_INVAL;
    out->dev = 19;
    return 0;
}
static int audio(u32 frames)
{
    return kapi_snd2_dsp_process(0, 1, 0, 0, (void *)100, (void *)200, frames, 0);
}
static int unit(void *c, u32 u, SND2_DspUnit *o)
{
    *o = (SND2_DspUnit){u, 1, 3, 1, 1, 1, max_frames, 0};
    return 0;
}
static int param(void *c, u32 u, u32 i, SND2_DspParam *o)
{
    o->id = 42;
    return 0;
}
static int insert(void *c, u32 u, const i32 *p, u32 n) { return 10 + u; }
static int process(void *c, u32 u, const i32 *p, u32 n, const void *in,
                   void *out, u32 frames, u32 fi) { ++processes; return frames; }

#define REQUIRE(x) do { if (!(x)) return __LINE__ - PROBE_LINE; } while (0)
enum { PROBE_LINE = __LINE__ };
int probe(void)
{
    SndBackend be = {.abi=SND_HAL2_ABI, .caps=caps, .hw_open=open_hw,
        .hw_pos=pos, .hw_start=ok, .hw_stop=ok, .hw_ack=ok,
        .hw_volume=volume, .hw_release=release, .dsp_unit=unit,
        .dsp_param=param, .dsp_insert=insert, .dsp_process=process};
    SND2_DevInfo info = {.kind=SND2_KIND_PCM_OUT};
    caps_result = OS32_ERR_IO;
    REQUIRE(snd2_register(&be, 0, &info) == OS32_ERR_IO && !snd2_dev_count());
    caps_result = 0;
    second_rate = 0;
    REQUIRE(snd2_register(&be, 0, &info) == OS32_ERR_INVAL && !snd2_dev_count());
    second_rate = 48000;
    ring_frames = 4097;
    REQUIRE(snd2_register(&be, 0, &info) == OS32_ERR_INVAL && !snd2_dev_count());
    ring_frames = 4096;
    int dev = snd2_register(&be, 0, &info);
    REQUIRE(dev == 0 && snd2_dev_count() == 1);
    SND2_Caps c = {.dev=99};
    REQUIRE(!snd2_caps(dev, &c, 4) && c.size == 256 && c.dev == 99);
    REQUIRE(snd2_caps(dev, &c, 3) == OS32_ERR_INVAL);
    REQUIRE(!snd2_caps(dev, &c, 256) && c.fmt[0].rate == 44100);
    SND2_StreamDesc desc = {.size=sizeof(desc), .dev=dev, .fmt=fmt};
    u32 p, l, cap;
    REQUIRE(!snd2_validate_open(&desc, 0, &p,&l,&cap) && p==2048 && l==1 && cap==4096);
    REQUIRE(!snd2_validate_open(&desc, 1, &p,&l,&cap) && cap==8192);
    desc.period_frames = 256;
    REQUIRE(snd2_validate_open(&desc, 0, &p,&l,&cap) == OS32_ERR_INVAL);
    REQUIRE(!snd2_unregister(dev));
    period_min = 256;
    REQUIRE(snd2_register(&be, 0, &info) == dev);
    desc.lead_periods = 15;
    REQUIRE(!snd2_validate_open(&desc, 0, &p,&l,&cap) && cap==4096);
    desc.lead_periods = 16;
    snd2_device(dev)->stream.owner = owner;
    REQUIRE(snd2_validate_open(&desc, 0, &p,&l,&cap) == OS32_ERR_INVAL);
    desc.lead_periods = 1;
    REQUIRE(snd2_validate_open(&desc, 0, &p,&l,&cap) == OS32_ERR_BUSY);
    desc.fmt.rate++;
    REQUIRE(snd2_validate_open(&desc, 0, &p,&l,&cap) == OS32_ERR_INVAL);
    desc.fmt = fmt;
    desc.period_frames = 513;
    REQUIRE(snd2_validate_open(&desc, 0, &p,&l,&cap) == OS32_ERR_INVAL);
    REQUIRE(snd2_dsp_unit(dev, 0, 0) == OS32_ERR_NOSYS);
    REQUIRE(snd2_dsp_param(dev, 0, 0, 0) == OS32_ERR_NOSYS);
    REQUIRE(snd2_dsp_insert(123, 0, 0, 0) == OS32_ERR_NOSYS);
    REQUIRE(snd2_dsp_process(dev, 0, 0, 0, 0, 0, 0, 0) == OS32_ERR_NOSYS);
    int second = snd2_register(&be, 0, &info);
    REQUIRE(!snd2_unregister(dev) && stops==1 && snd2_dev_count()==1);
    REQUIRE(!snd2_dev_info(0,&info) && info.dev==(u32)second);
    units = 1;
    REQUIRE(snd2_register(&be, 0, &info)==dev);
    snd2_device(dev)->stream.owner = owner;
    SND2_DspUnit u;
    SND2_DspParam a;
    REQUIRE(!snd2_dsp_unit(dev,1,&u) && u.max_frames==4);
    REQUIRE(snd2_dsp_unit(dev,2,&u)==OS32_ERR_INVAL);
    REQUIRE(!snd2_dsp_param(dev,1,0,&a) && a.id==42);
    REQUIRE(snd2_dsp_param(dev,1,1,&a)==OS32_ERR_INVAL);
    REQUIRE(snd2_dsp_insert(123,1,0,0)==11 && snd2_dsp_insert(123,0,0,0)==10);
    ++owner;
    REQUIRE(snd2_dsp_insert(123,0,0,0)==OS32_ERR_INVAL);
    REQUIRE(snd2_dsp_insert(0,1,0,0)==OS32_ERR_STALE);
    REQUIRE(snd2_dsp_process(dev,1,0,0,&a,&c,4,0)==4);
    REQUIRE(snd2_dsp_process(dev,1,0,0,&a,&c,5,0)==OS32_ERR_INVAL);
    REQUIRE(snd2_dsp_process(dev,1,0,0,&a,&c,4,32)==OS32_ERR_INVAL);
    REQUIRE(snd2_dsp_process(dev,1,&a.min,2,&a,&c,4,0)==OS32_ERR_INVAL);
    REQUIRE(kapi_snd2_dev_count() == 3);
    REQUIRE(!kapi_snd2_dev_info(1, &info) && info.dev == (u32)second);
    REQUIRE(!kapi_snd2_dev_info(2, &info) && info.dev == 19);
    info.kind = SND2_KIND_PCM_OUT;
    readable = writable = 16;
    REQUIRE(audio(4) == 4);
    int before = processes;
    max_frames = ~0U;
    REQUIRE(audio(0x40000000U) == OS32_ERR_INVAL && processes == before);
    readable = 15;
    if (!__builtin_setjmp(fault_env)) { audio(4); return 98; }
    readable = 16;
    writable = 15;
    if (!__builtin_setjmp(fault_env)) { audio(4); return 99; }
    REQUIRE(processes == before);
    for (int slot = 2; slot < 16; ++slot)
        REQUIRE(snd2_register(&be, 0, &info) == slot);
    REQUIRE(snd2_register(&be, 0, &info) == OS32_ERR_FULL && snd2_dev_count() == 16);
    fail_stop = 1;
    REQUIRE(snd2_unregister(dev)==OS32_ERR_BUSY && stops==2);
    REQUIRE(snd2_unregister(dev)==OS32_ERR_BUSY && stops==2);
    REQUIRE(!snd2_dev_info(0,&info) && (info.flags & SND2_DEV_FAULTED) &&
            !(info.flags & SND2_DEV_READY));
    REQUIRE(snd2_validate_open(&desc,0,&p,&l,&cap)==OS32_ERR_IO);
    return 0;
}
