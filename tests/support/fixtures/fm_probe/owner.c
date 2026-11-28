#include "snd2_int.h"
#include "kstring.h"

static unsigned opens, quiets, closes;
static int owner = 1;
int res_owner_get(void) { return owner; }
static int caps(void *c, SND2_ChipCaps *out) { out->banks = 1; return 0; }
static int open_chip(void *c) { ++opens; return 0; }
static int quiet_chip(void *c) { ++quiets; return 0; }
static int close_chip(void *c) { ++closes; return 0; }
static int read_chip(void *c, u32 b, u32 w, u32 r) { return 42; }
static int write_chip(void *c, const SND2_RegW *w, u32 n) { return n; }
static const SndChipBackend be = {
    .abi = SND_HAL2_ABI, .caps = caps, .open = open_chip, .quiet = quiet_chip,
    .close = close_chip, .read = read_chip, .write = write_chip
};

void _start(void)
{
    SND2_DevInfo info = {.kind = SND2_KIND_FM};
    int dev = snd2_chip_register(&be, 0, &info);
    u32 h, diag = ~0u;
    int ok = dev >= 0 && !snd2_chip_open(dev, &h);
    for (owner = 1; owner <= 2; ++owner)
        ok = ok && snd2_chip_diag_open(dev, &diag) == OS32_ERR_BUSY && diag == ~0u;
    owner = 1;
    ok = ok && opens == 1 && !quiets && !closes &&
         snd2_chip_read(h, 0, 1, 0) == 42 && !snd2_chip_close(h) &&
         !snd2_chip_diag_open(dev, &diag) && !snd2_chip_close(diag) &&
         opens == 2 && quiets == 2 && closes == 2;
    if (!ok) {
        static const char reason[] = "diagnostic FM ownership";
        __asm__ volatile("int $0x80" :: "a"(4), "b"(2), "c"(reason), "d"(sizeof(reason)-1) : "memory");
    }
    __asm__ volatile("int $0x80" :: "a"(1), "b"(!ok));
    __builtin_unreachable();
}
