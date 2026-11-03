#include <stdio.h>
#include "../../hwdev/gf/command.h"

/* A2 can supply these F1 stubs; storage models the fixed RAMHT. */
static u32 table[GF2_RAMHT_ENTRIES * 2U];
int gf_inst_read32(struct gf_foundation *f, const struct gf_inst *obj,
                   u32 off, u32 *value)
{
    (void)f; (void)obj;
    *value = table[off / 4U];
    return GF1_OK;
}
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *obj,
                    u32 off, u32 value)
{
    (void)f; (void)obj;
    table[off / 4U] = value;
    return GF1_OK;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    struct gf_foundation f = {0};
    struct gf2_chan c = {0};
    u32 a, b, reused;
    c.f = &f;
    /* 0x1001 and 1 hash to zero; deletion must not hide a duplicate. */
    CHECK(gf2_ramht_add(&c, 0x1001, GF2_RAMHT_ENGINE_GR, 0x40000, &a) == GF2_OK);
    CHECK(a == 1 && table[0] == 0x1001 && table[1] == 0x104000);
    CHECK(gf2_ramht_add(&c, 1, GF2_RAMHT_ENGINE_DMA, 0x40010, &b) == GF2_OK);
    CHECK(b == 2 && table[3] == 0x4001);
    CHECK(gf2_ramht_del(&c, a) == GF2_OK && table[0] == 0 && table[1] == 0);
    CHECK(gf2_ramht_add(&c, 1, GF2_RAMHT_ENGINE_DMA, 0x40010, &reused) == GF2_EEXIST);
    CHECK(gf2_ramht_add(&c, 0x1001, GF2_RAMHT_ENGINE_GR, 0x40000, &reused) == GF2_OK && reused == a);
    CHECK(gf2_ramht_add(&c, 0xfff, GF2_RAMHT_ENGINE_DMA, 0x40020, &a) == GF2_OK && a == GF2_RAMHT_ENTRIES);
    CHECK(gf2_ramht_add(&c, 0x1ffe, GF2_RAMHT_ENGINE_DMA, 0x40030, &b) == GF2_OK && b == 3);
    for (u32 i = 0; i < GF2_RAMHT_BITMAP_BYTES; i++) c.bitmap[i] = 0xff;
    CHECK(gf2_ramht_add(&c, 0xdeadbeef, GF2_RAMHT_ENGINE_DMA, 0, &reused) == GF2_ENOSPC);
    return 0;
}
