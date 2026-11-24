#include "snd2_int.h"

#define EXPECT(x) do { if (!(x)) return __LINE__; } while (0)
static int owner = 7, quiet_rc, open_rc, close_rc, short_write;
static int opens, closes, quiets, writes, last_quiet, seq;
int res_owner_get(void) { return owner; }
void *kmemcpy(void *dst, const void *src, u32 n)
{
    for (u32 i = 0; i < n; ++i)
        ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}
static int caps(void *c, SND2_ChipCaps *out) { out->banks = 2; return 0; }
static int open_hw(void *c) { ++opens; return open_rc; }
static int quiet(void *c) { ++quiets; last_quiet = ++seq; return quiet_rc; }
static int close_hw(void *c) { ++closes; return ++seq == last_quiet + 1 ? close_rc : -1; }
static int write_hw(void *c, const SND2_RegW *w, u32 n) { ++writes; return n - short_write; }
static int read_hw(void *c, u32 b, u32 what, u32 reg) { return b + what + reg; }
static SndChipBackend be = { SND_HAL2_ABI, caps, open_hw, close_hw, write_hw, read_hw, quiet };
static SND2_DevInfo info = {.kind = SND2_KIND_FM};

int probe(void)
{
    SND2_RegW w[256] = {{0}};
    SND2_ChipCaps out = {.dev = 99};
    SND2_DevInfo di;
    u32 h = 999, old;
    EXPECT(snd2_chip_implicit(owner) == OS32_ERR_NOTFOUND);
    EXPECT(snd2_chip_register(0, 0, &info) == OS32_ERR_INVAL);
    int dev = snd2_chip_register(&be, 0, &info);
    EXPECT(dev >= 0 && snd2_chip_dev_info(0, &di) == 0 && di.dev == dev);
    EXPECT(snd2_chip_caps(dev, &out, 4) == 0 && out.dev == 99 && out.size == sizeof(out));
    EXPECT(snd2_chip_caps(dev, &out, 5) == OS32_ERR_INVAL);
    EXPECT(snd2_chip_implicit(owner) == 0 && opens == 1);
    EXPECT(snd2_chip_open(dev, &h) == 0 && opens == 1);
    owner++;
    EXPECT(snd2_chip_implicit(owner) == OS32_ERR_BUSY);
    EXPECT(snd2_chip_close(h) == OS32_ERR_BUSY);
    snd2_chip_reclaim(owner);
    owner--;
    EXPECT(snd2_chip_reset(h) == 0 && quiets == 1 && closes == 0);
    EXPECT(snd2_chip_write(h, w, 256) == 256 && writes == 1);
    w[255].bank = 2;
    EXPECT(snd2_chip_write(h, w, 256) == OS32_ERR_INVAL && writes == 1);
    EXPECT(snd2_chip_write(h, w, 257) == OS32_ERR_INVAL);
    EXPECT(snd2_chip_write(h, 0, 0) == 0 && writes == 1);
    EXPECT(snd2_chip_read(h, 1, 2, 4) == 7);
    EXPECT(snd2_chip_read(h, 2, 2, 4) == OS32_ERR_INVAL);
    EXPECT(snd2_chip_read(h, 0, 3, 4) == OS32_ERR_INVAL);
    old = h;
    snd2_chip_reclaim(owner);
    snd2_chip_reclaim(owner);
    EXPECT(closes == 1 && quiets == 2 && snd2_chip_close(old) == OS32_ERR_STALE);
    EXPECT(snd2_chip_open(dev, &h) == 0 && snd2_chip_reset(old) == OS32_ERR_STALE);
    EXPECT(snd2_chip_unregister(dev) == 0 && closes == 2);
    EXPECT(snd2_chip_dev_info(0, &di) == OS32_ERR_INVAL);
    for (int fail = 0; fail < 7; ++fail) {
        dev = snd2_chip_register(&be, 0, &info);
        EXPECT(dev >= 0);
        open_rc = fail == 0 ? -1 : 0;
        quiet_rc = fail >= 1 && fail <= 3 ? -1 : 0;
        close_rc = fail == 5 ? -1 : 0;
        if (!fail) {
            h = 999;
            EXPECT(snd2_chip_open(dev, &h) == OS32_ERR_IO && h == 999);
        } else {
            EXPECT(snd2_chip_open(dev, &h) == 0);
            if (fail == 1 || fail == 5)
                EXPECT(snd2_chip_close(h) == OS32_ERR_IO);
            if (fail == 2)
                EXPECT(snd2_chip_reset(h) == OS32_ERR_IO);
            if (fail == 3)
                snd2_chip_reclaim(owner);
            if (fail == 4) {
                short_write = 1;
                EXPECT(snd2_chip_write(h, w, 2) == 1);
            }
            if (fail == 6)
                snd2_chip_fault(dev);
        }
        EXPECT(snd2_chip_dev_info(fail, &di) == 0 &&
               (di.flags & SND2_DEV_FAULTED) && !(di.flags & SND2_DEV_READY));
        EXPECT(snd2_chip_open(dev, &h) == OS32_ERR_IO);
        EXPECT(snd2_chip_unregister(dev) == OS32_ERR_BUSY);
    }
    return 0;
}
