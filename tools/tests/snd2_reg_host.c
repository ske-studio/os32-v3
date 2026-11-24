#include "snd2.h"

static int fail_stop, stops, owner = 7, caps_result, units;
static SND2_Fmt fmt = {44100, 16, 2, SND2_ENC_S16LE, 0};
static int caps(void *ctx, SND2_Caps *c)
{
    c->fmt_count = 1;
    c->fmt[0] = fmt;
    c->period_min = 256;
    c->period_max = 2048;
    c->ring_frames = 4096;
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
static int unit(void *c, u32 u, SND2_DspUnit *o)
{
    *o = (SND2_DspUnit){u, 1, 3, 1, 1, 1, 4, 0};
    return 0;
}
static int param(void *c, u32 u, u32 i, SND2_DspParam *o)
{
    o->id = 42;
    return 0;
}
static int insert(void *c, u32 u, const i32 *p, u32 n) { return 10 + u; }
static int process(void *c, u32 u, const i32 *p, u32 n, const void *in,
                   void *out, u32 frames, u32 fi) { return frames; }

#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
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
    fail_stop = 1;
    REQUIRE(snd2_unregister(dev)==OS32_ERR_BUSY && stops==2);
    REQUIRE(snd2_unregister(dev)==OS32_ERR_BUSY && stops==2);
    REQUIRE(!snd2_dev_info(0,&info) && (info.flags & SND2_DEV_FAULTED) &&
            !(info.flags & SND2_DEV_READY));
    REQUIRE(snd2_validate_open(&desc,0,&p,&l,&cap)==OS32_ERR_IO);
    return 0;
}
