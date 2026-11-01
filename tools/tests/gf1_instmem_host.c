#include "../../hwdev/gf/foundation.h"
static u32 aperture[0x618000 / 4];
static u32 vs_mask, vram = 0x4000000, fail_read, calls, map_fail;
static int rd(void *p, u32 bar, u32 off, u32 width, u32 *v)
{
    (void)p;
    ++calls;
    if (bar != 0 || width != 4 || calls > 2 ||
        off != (calls == 1 ? 0x10020cU : 0x1540U)) return -1;
    if (calls == fail_read) return -1;
    *v = calls == 1 ? vram : vs_mask << 8;
    return 0;
}
int gf_instmem_map(struct gf_foundation *f, const struct recover_bar_extent *bar, u32 bytes)
{
    (void)bar;
    if (map_fail || bytes > sizeof(aperture)) return GF1_EMAP;
    f->bar2 = (volatile u8 *)aperture; f->mapped = true; f->imem.aperture_bytes = bytes;
    return GF1_OK;
}
static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
void _start(void)
{
    struct gf_foundation f = {0};
    struct gf_inst a, b, c;
    u32 value;
    f.io.read = rd;
    for (unsigned vs = 0; vs <= 8; vs++) {
        vs_mask = (1U << vs) - 1; calls = 0;
        for (u32 i = 0; i <= 0x40000 / 4; i++) aperture[i] = 0x1234;
        CHECK(gf_instmem_init(&f) == GF1_OK && calls == 2);
        CHECK(f.imem.reserved == ((0x4980U * vs + 0x4000U) * 32 + 0x100fffU) / 4096 * 4096);
        CHECK(f.imem.vbios.off == 0 && f.imem.ramht.off == 0x10000 &&
              f.imem.ramro.off == 0x18000 && f.imem.ramfc.off == 0x20000);
        for (u32 i = 0; i <= 0x40000 / 4; i++)
            CHECK(aperture[i] == ((i >= 0x10000 / 4 && i < 0x18000 / 4) ||
                                  (i >= 0x20000 / 4 && i < 0x40000 / 4) ? 0U : 0x1234U));
    }
    aperture[0x40000/4 - 1] = 0x1234;
    aperture[0x40000/4] = aperture[0x40000/4 + 1] = aperture[0x40000/4 + 2] = 0x1234;
    CHECK(gf_inst_alloc(&f, 5, 16, true, &a) == 0 && a.off == 0x40000 && a.size == 8);
    CHECK(aperture[0x40000/4 - 1] == 0x1234 && aperture[0x40000/4] == 0 &&
          aperture[0x40000/4 + 1] == 0 && aperture[0x40000/4 + 2] == 0x1234);
    CHECK(gf_inst_alloc(&f, 8, 16, false, &b) == 0 && b.off == 0x40010);
    gf_inst_free(&f, &a);
    CHECK(gf_inst_alloc(&f, 4, 4, false, &c) == 0 && c.off == 0x40000);
    CHECK(gf_inst_write32(&f, &c, 0, 0x98765432) == 0);
    CHECK(aperture[0x40000/4] == 0x98765432);
    CHECK(gf_inst_read32(&f, &c, 0, &value) == 0 && value == 0x98765432);
    CHECK(gf_inst_read32(&f, &c, 1, &value) == GF1_EINVAL);
    CHECK(gf_inst_alloc(&f, 0, 1, false, &a) == GF1_EINVAL);
    CHECK(gf_inst_alloc(&f, 4, 3, false, &a) == GF1_EINVAL);
    CHECK(gf_inst_alloc(&f, f.imem.aperture_bytes, 4, false, &a) == GF1_ENOSPC);
    gf_inst_free(&f, &b); gf_inst_free(&f, &c);
    CHECK(gf_inst_alloc(&f, f.imem.aperture_bytes - 0x40000, 4, false, &a) == 0);
    CHECK(a.off + a.size == f.imem.aperture_bytes);
    CHECK(gf_inst_alloc(&f, 4, 4, false, &b) == GF1_ENOSPC);
    CHECK(gf_inst_write32(&f, &a, a.size - 4, 0xabcdef12) == 0);
    CHECK(gf_inst_read32(&f, &a, a.size - 4, &value) == 0 && value == 0xabcdef12);
    CHECK(gf_inst_read32(&f, &a, a.size, &value) == GF1_EINVAL);
    CHECK(gf_inst_write32(&f, &a, a.size, 1) == GF1_EINVAL);
    f.mapped = false; CHECK(gf_inst_read32(&f, &c, 0, &value) == GF1_EMAP);
    calls = 0; fail_read = 1; CHECK(gf_instmem_init(&f) == GF1_EIO);
    calls = 0; fail_read = 2; CHECK(gf_instmem_init(&f) == GF1_EIO);
    fail_read = 0; calls = 0; vram = 0; CHECK(gf_instmem_init(&f) == GF1_ENOSPC);
    vram = 0x4000000; calls = 0; map_fail = 1; aperture[0x10000/4] = 77;
    CHECK(gf_instmem_init(&f) == GF1_EMAP && aperture[0x10000/4] == 77);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
