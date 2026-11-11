/* G5 contract tests: fake F2 and instmem, real G5 translation unit. */
#include "drivers/video/geforce/gf_v2.h"
/* Compiler support for large aggregate zeroing; no libc is linked. */
void *memset(void *ptr, int value, unsigned int n)
{
    unsigned char *p = ptr;
    for (unsigned int i = 0; i < n; i++) p[i] = (unsigned char)value;
    return ptr;
}
static struct gf_foundation foundation;
static struct gf2_chan ch;
static struct gf_v2 g;
static u32 table[GF_V2_GART_PT_BYTES / 4];
static u32 pfns[GF_V2_GART_PAGES];
static const u32 a[] = {0x111, 0x222, 0x333, 0x444};
static const u32 b[] = {0x555, 0x666};
static u32 writes, pauses, resumes, allocs, frees, registrations, step;
static u32 fail_off, ram_handle, ram_off;
static int alloc_rc, ram_rc, emit_rc, wait_rc, pause_rc, resume_rc;
static int active;
static void fail(int line)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 250 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
int gf2_fence_emit(struct gf2_chan *c, struct gf2_fence *f, u32 dl)
{
    CHECK(c == &ch && dl == 1234 && step == 0); step = 1;
    *f = (struct gf2_fence){ch.epoch, 42}; return emit_rc;
}
int gf2_fence_wait(struct gf2_chan *c, struct gf2_fence f, u32 dl)
{
    CHECK(c == &ch && f.epoch == ch.epoch && f.seq == 42 && dl == 1234);
    CHECK(step == 1); step = 2; return wait_rc;
}
int gf2_fifo_pause(struct gf2_chan *c)
{
    CHECK(c == &ch && step == 2); step = 3; pauses++; return pause_rc;
}
void gf2_fifo_resume(struct gf2_chan *c)
{
    CHECK(c == &ch && step >= 3 && c->last_rc == 0);
    resumes++; c->last_rc = resume_rc; step = 0;
}
int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero,
                  struct gf_inst *out)
{
    CHECK(f == &foundation && bytes == 0x20008 && align == 0x1000 && zero);
    CHECK(step == 3 && !active && out == &g.gart.obj.inst);
    allocs++; if (alloc_rc) return alloc_rc;
    for (u32 i = 0; i < sizeof(table) / sizeof(table[0]); i++) table[i] = 0;
    out->off = 0x50000; out->size = bytes; active = 1; return 0;
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *obj)
{
    CHECK(f == &foundation && obj == &g.gart.obj.inst && active);
    active = 0; frees++;
}
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *obj, u32 off, u32 v)
{
    CHECK(f == &foundation && obj == &g.gart.obj.inst && active);
    CHECK(off < sizeof(table) && !(off & 3));
    writes++;
    if (off == fail_off) { fail_off = ~0u; return GF1_EIO; }
    table[off / 4] = v; return 0;
}
int gf2_ramht_add(struct gf2_chan *c, u32 handle, u8 engine, u32 off, u32 *cookie)
{
    CHECK(c == &ch && engine == GF2_RAMHT_ENGINE_DMA && step == 3);
    CHECK(active && table[0] == 0x0002103d && table[1] == 0x07ffffff);
    registrations++; ram_handle = handle; ram_off = off;
    if (ram_rc) return ram_rc;
    *cookie = 7; return 0;
}
static void fresh(void)
{
    g = (struct gf_v2){0}; ch = (struct gf2_chan){0};
    g.ch = &ch; ch.f = &foundation; ch.epoch = 1; ch.state = GF2_RUN;
    writes = pauses = resumes = allocs = frees = registrations = step = 0;
    fail_off = ~0u; active = 0;
    alloc_rc = ram_rc = emit_rc = wait_rc = pause_rc = resume_rc = 0;
}
static void start(void) { CHECK(gf_v2_gart_start(&g, 1234) == 0); }
static void scenario(void)
{
    u32 off = ~0u, before;
    fresh(); start();
    CHECK(table[0] == 0x0002103d && table[1] == 0x07ffffff);
    CHECK(pauses == 1 && resumes == 1 && allocs == 1 && registrations == 1);
    CHECK(ram_handle == GF3_HANDLE_GART_DMA && ram_off == 0x50000);
    CHECK(g.gart.obj.handle == ram_handle && g.gart.obj.cookie == 7);
    CHECK(g.gart.obj.class_id == GF2_DMA_IN_MEMORY && g.gart.epoch == 1);
    start(); CHECK(pauses == 1 && allocs == 1);
    CHECK(gf_v2_gart_bind(&g, a, 4, &off) == 0 && off == 0);
    for (u32 i = 0; i < 4; i++) CHECK(table[2 + i] == (a[i] << 12 | 3));
    CHECK(gf_v2_gart_bind(&g, b, 1, &off) == 0 && off == 0x4000);
    CHECK(gf_v2_gart_bind(&g, b, 2, &off) == 0 && off == 0x5000);
    CHECK(gf_v2_gart_bind(&g, b, 1, &off) == 0 && off == 0x7000);
    CHECK(gf_v2_gart_unbind(&g, 0, 4) == 0);
    for (u32 i = 0; i < 4; i++) CHECK(table[2 + i] == 0);
    CHECK(gf_v2_gart_unbind(&g, 0x5000, 2) == 0);
    CHECK(gf_v2_gart_bind(&g, b, 2, &off) == 0 && off == 0x5000);
    CHECK(g.gart.count == 3 && g.gart.bind[1].first == 5);
    fail_off = 8 + 2 * 4; off = ~0u;
    CHECK(gf_v2_gart_bind(&g, a, 4, &off) == OS32_ERR_IO);
    CHECK(off == ~0u && g.gart.count == 3 && !table[2] && !table[3]);
    before = writes;
    CHECK(gf_v2_gart_unbind(&g, 0x5001, 2) == OS32_ERR_INVAL);
    CHECK(gf_v2_gart_unbind(&g, 0x5000, 1) == OS32_ERR_INVAL);
    CHECK(writes == before && g.gart.count == 3 && table[7] == (b[0] << 12 | 3));
    CHECK(gf_v2_gart_range(&g, GF3_HANDLE_GART_DMA, 0x5000, 0x2000) == 0);
    CHECK(gf_v2_gart_range(&g, GF3_HANDLE_GART_DMA, 0x6000, 0x2000) == GF3_ERANGE);
    CHECK(gf_v2_gart_range(&g, GF3_HANDLE_GART_DMA, 0x6000, 0xfffff000) == GF3_ERANGE);
    CHECK(gf_v2_gart_range(&g, GF2_HANDLE_VRAM_DMA, 0x5000, 1) == GF3_EINVAL);
    ch.epoch = 2;
    CHECK(gf_v2_gart_range(&g, GF3_HANDLE_GART_DMA, 0x5000, 1) == GF3_ERANGE);
    CHECK(gf_v2_gart_bind(&g, b, 1, &off) == OS32_ERR_IO);
    CHECK(gf_v2_gart_unbind(&g, 0x7000, 1) == 0 && writes == before);
    active = 0; /* Fake F2 stop has freed the previous epoch's inst. */
    start(); CHECK(g.gart.epoch == 2 && g.gart.count == 2);
    CHECK(table[6] == (b[0] << 12 | 3) && table[7] == (b[0] << 12 | 3));
    CHECK(table[8] == (b[1] << 12 | 3) && table[9] == 0);
    ch.state = GF2_OFF; active = 0; gf_v2_gart_reset(&g);
    CHECK(!g.gart.count && !g.gart.epoch && !g.gart.obj.inst.size);
    for (u32 i = 0; i < GF_V2_GART_BINDS; i++) CHECK(!g.gart.bind[i].pfn);
}
static void failures(void)
{
    u32 off = ~0u;
    fresh(); ch.state = GF2_OFF;
    CHECK(gf_v2_gart_start(&g, 1234) == GF3_ESTATE && !allocs);
    fresh(); emit_rc = GF2_ETIME;
    CHECK(gf_v2_gart_start(&g, 1234) == GF2_ETIME && !pauses);
    fresh(); wait_rc = GF2_EIO;
    CHECK(gf_v2_gart_start(&g, 1234) == GF2_EIO && !pauses);
    fresh(); pause_rc = GF2_EIO;
    CHECK(gf_v2_gart_start(&g, 1234) == GF2_EIO && !allocs && !resumes);
    fresh(); alloc_rc = GF1_ENOSPC;
    CHECK(gf_v2_gart_start(&g, 1234) == GF2_ENOSPC && resumes == 1 && !frees);
    for (u32 i = 0; i < 2; i++) {
        fresh(); fail_off = 4 * i;
        CHECK(gf_v2_gart_start(&g, 1234) == GF1_EIO);
        CHECK(frees == 1 && resumes == 1 && !active && !g.gart.epoch);
        CHECK(!g.gart.obj.inst.size && !registrations);
    }
    fresh(); ram_rc = GF2_ENOSPC;
    CHECK(gf_v2_gart_start(&g, 1234) == GF2_ENOSPC);
    CHECK(frees == 1 && resumes == 1 && !active && !g.gart.obj.inst.size);
    fresh(); alloc_rc = GF1_ENOSPC; resume_rc = GF2_EIO;
    CHECK(gf_v2_gart_start(&g, 1234) == GF2_EIO);
    fresh(); resume_rc = GF2_EIO;
    CHECK(gf_v2_gart_start(&g, 1234) == GF2_EIO && !g.gart.epoch && active);
    /* On resume failure only F2 stop may release the published object. */
    fresh(); start(); CHECK(gf_v2_gart_bind(&g, a, 4, &off) == 0);
    ch.epoch++; active = 0; fail_off = 12;
    CHECK(gf_v2_gart_start(&g, 1234) == GF1_EIO);
    CHECK(g.gart.count == 1 && !g.gart.epoch && !active && frees == 1);
    start(); CHECK(table[3] == (a[1] << 12 | 3));
    CHECK(gf_v2_gart_unbind(&g, 0, 4) == 0);
    CHECK(gf_v2_gart_bind(&g, 0, 1, &off) == OS32_ERR_INVAL);
    CHECK(gf_v2_gart_bind(&g, b, 0, &off) == OS32_ERR_INVAL);
    CHECK(gf_v2_gart_bind(&g, b, 1, 0) == OS32_ERR_INVAL);
    CHECK(gf_v2_gart_bind(&g, b, GF_V2_GART_PAGES + 1, &off) == OS32_ERR_NOSPC);
    for (u32 i = 0; i < GF_V2_GART_PAGES; i++) pfns[i] = i + 1;
    CHECK(gf_v2_gart_bind(&g, pfns, GF_V2_GART_PAGES, &off) == 0 && off == 0);
    CHECK(table[GF_V2_GART_PAGES + 1] == (GF_V2_GART_PAGES << 12 | 3));
    CHECK(gf_v2_gart_bind(&g, b, 1, &off) == OS32_ERR_NOSPC);
    CHECK(gf_v2_gart_unbind(&g, 0, GF_V2_GART_PAGES) == 0);
    for (u32 i = 0; i < GF_V2_GART_BINDS; i++)
        CHECK(gf_v2_gart_bind(&g, b, 1, &off) == 0 && off == i * 4096);
    off = ~0u;
    CHECK(gf_v2_gart_bind(&g, b, 1, &off) == OS32_ERR_NOSPC && off == ~0u);
    /* Equal holes choose the lower address. */
    CHECK(gf_v2_gart_unbind(&g, 0, 1) == 0);
    CHECK(gf_v2_gart_unbind(&g, 8192, 1) == 0);
    CHECK(gf_v2_gart_bind(&g, b, 1, &off) == 0 && off == 0);
    fail_off = 8; /* Unmap deliberately ignores the writer's rc. */
    CHECK(gf_v2_gart_unbind(&g, 0, 1) == 0);
    CHECK(gf_v2_gart_unbind(&g, 0, 1) == OS32_ERR_INVAL);
    ch.state = GF2_FAULT;
    CHECK(gf_v2_gart_range(&g, GF3_HANDLE_GART_DMA, 4096, 1) == GF3_ERANGE);
}
void _start(void)
{
    scenario(); failures();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
