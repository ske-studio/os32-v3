#include "../../hwdev/gf/command.h"

/* Temporary A2 substitutes: same signatures as the foundation and B3 APIs. */
static u32 words[4], writes, adds, dels, frees, add_engine, add_off;
static int alloc_rc, write_fail, add_rc, del_rc;
int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero,
                  struct gf_inst *out)
{
    (void)f;
    if (alloc_rc) return alloc_rc;
    if (bytes != 16 || align != 16 || zero) return GF1_EINVAL;
    out->off = 0x40000; out->size = bytes; out->next = 0;
    return GF1_OK;
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *obj)
{ (void)f; (void)obj; ++frees; }
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *obj, u32 off, u32 v)
{
    (void)f; (void)obj;
    if (off != writes * 4 || writes >= 4) return GF1_EINVAL;
    if (write_fail && writes == (u32)(write_fail - 1)) return GF1_EIO;
    words[writes++] = v;
    return GF1_OK;
}
int gf2_ramht_add(struct gf2_chan *c, u32 handle, u8 engine, u32 off, u32 *cookie)
{
    (void)c; (void)handle; ++adds; add_engine = engine; add_off = off;
    if (add_rc) return add_rc;
    *cookie = 7; return GF2_OK;
}
int gf2_ramht_del(struct gf2_chan *c, u32 cookie)
{ (void)c; if (cookie != 7) return GF2_EINVAL; ++dels; return del_rc; }
static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
void _start(void)
{
    struct gf_foundation f = {0};
    struct gf2_chan c = {0};
    struct gf2_object obj = {0};
    c.f = &f;
    CHECK(gf2_dma_new(&c, GF2_HANDLE_PCI_DMA, GF2_DMA_IN_MEMORY,
                      GF2_DMA_TARGET_PCI, GF2_DMA_ACCESS_RW,
                      0x12345abc, 0x12345fff, &obj) == GF2_OK);
    CHECK(writes == 4 && words[0] == 0xabc2303d && words[1] == 0x543 &&
          words[2] == 0x12345002 && words[3] == 0x12345002);
    CHECK(adds == 1 && add_engine == GF2_RAMHT_ENGINE_DMA &&
          add_off == obj.inst.off && obj.cookie == 7);
    del_rc = GF2_EIO;
    CHECK(gf2_object_del(&c, &obj) == GF2_EIO && frees == 0 && obj.cookie == 7);
    del_rc = 0;
    CHECK(gf2_object_del(&c, &obj) == GF2_OK && dels == 2 && frees == 1);
    writes = 0; adds = 0;
    CHECK(gf2_dma_new(&c, GF2_HANDLE_PUSH_DMA, GF2_DMA_FROM_MEMORY,
                      GF2_DMA_TARGET_VRAM, GF2_DMA_ACCESS_RO, 0, 0xffffffffU,
                      &obj) == GF2_OK);
    CHECK(words[0] == 0x7002 && words[1] == 0xffffffffU &&
          words[2] == 0 && words[3] == 0 && adds == 0);
    CHECK(gf2_object_del(&c, &obj) == GF2_OK && frees == 2);
    writes = 0;
    CHECK(gf2_dma_new(&c, 9, GF2_DMA_TO_MEMORY, GF2_DMA_TARGET_VRAM,
                      GF2_DMA_ACCESS_WO, 0x1000, 0x1000, &obj) == GF2_OK);
    CHECK(words[0] == 0xb003 && words[1] == 0 && words[2] == 0x1002);
    CHECK(gf2_object_del(&c, &obj) == GF2_OK);
    CHECK(gf2_dma_new(&c, 0, GF2_DMA_IN_MEMORY, 0, 0, 0, 0, &obj) == GF2_EINVAL);
    CHECK(gf2_dma_new(&c, 9, 4, 0, 0, 0, 0, &obj) == GF2_EINVAL);
    CHECK(gf2_dma_new(&c, 9, GF2_DMA_IN_MEMORY, 2, 0, 0, 0, &obj) == GF2_EINVAL);
    CHECK(gf2_dma_new(&c, 9, GF2_DMA_IN_MEMORY, 0, 3, 0, 0, &obj) == GF2_EINVAL);
    CHECK(gf2_dma_new(&c, 9, GF2_DMA_IN_MEMORY, 0, 0, 2, 1, &obj) == GF2_EINVAL);
    alloc_rc = GF1_ENOSPC;
    CHECK(gf2_dma_new(&c, 9, GF2_DMA_IN_MEMORY, 0, 0, 0, 0, &obj) == GF2_ENOSPC);
    alloc_rc = 0; writes = 0; write_fail = 2;
    CHECK(gf2_dma_new(&c, 9, GF2_DMA_IN_MEMORY, 0, 0, 0, 0, &obj) == GF2_EIO && frees == 4);
    write_fail = 0; writes = 0; add_rc = GF2_EEXIST;
    CHECK(gf2_dma_new(&c, 9, GF2_DMA_IN_MEMORY, 0, 0, 0, 0, &obj) == GF2_EEXIST && frees == 5);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
