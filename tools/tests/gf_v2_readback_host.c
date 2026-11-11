/* Real G1 -> layer 3 validation/transfer/M2MF -> fake GF2 DMA engine.
 * Alias descriptors deliberately carry no G9 reservation identity. */
#include "../../drivers/video/geforce/gf_v2_ops.c"

static struct gf_foundation foundation;
static struct gf2_chan channel;
static struct gf3_ctx accel;
static struct gf_vram_reservation_node node;
static u8 vram[16384], staging[4096], output[64];
static u32 packets, waits, input_dma, output_dma;
static int engine_rc;
void *memset(void *dst, int value, unsigned int bytes)
{
    u8 *p = dst;
    for (u32 i = 0; i < bytes; i++) p[i] = (u8)value;
    return dst;
}
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) finish(__LINE__ % 250 + 1); } while (0)
static int clock_us(void *opaque, u32 *now)
{ (void)opaque; *now = 100; return 0; }
int gf2_fault_check(struct gf2_chan *ch) { CHECK(ch == &channel); return 0; }
int gf3_ifc(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r,
            const struct gf3_ram *ram, u32 dl)
{ (void)c; (void)s; (void)r; (void)ram; (void)dl; finish(250); return 0; }
int gf2_emit(struct gf2_chan *ch, u8 subc, u16 method, bool ni,
             const u32 *v, u16 n, u32 dl)
{
    CHECK(ch == &channel && subc == GF3_SUBC_M2MF && !ni && dl == 100 + GF3_TIMEOUT_US);
    packets++;
    if (engine_rc) return engine_rc;
    if (method == 0x184) {
        CHECK(n == 2); input_dma = v[0]; output_dma = v[1];
    } else if (method == 0x30c) {
        CHECK(n == 8 && input_dma == GF3_HANDLE_VRAM_DMA && output_dma == GF3_HANDLE_PCI_DMA);
        CHECK(v[0] == 4096 + 2 * 64 + 3 && v[1] == 0 && v[2] == 64 && v[3] == 7);
        CHECK(v[4] == 7 && v[5] == 3 && v[6] == 0x101);
        for (u32 y = 0; y < v[5]; y++)
            for (u32 x = 0; x < v[4]; x++)
                staging[v[1] + y * v[3] + x] = vram[v[0] + y * v[2] + x];
    }
    return 0;
}
int gf2_fence_emit(struct gf2_chan *ch, struct gf2_fence *f, u32 dl)
{ (void)ch; (void)dl; *f = (struct gf2_fence){1, 1}; return 0; }
int gf2_fence_wait(struct gf2_chan *ch, struct gf2_fence f, u32 dl)
{ (void)ch; (void)f; (void)dl; waits++; return 0; }
void _start(void)
{
    foundation.imem.vram_bytes = sizeof(vram);
    foundation.io.timing.clock_us = clock_us;
    channel = (struct gf2_chan){.f = &foundation, .B = sizeof(vram), .epoch = 1,
        .state = GF2_RUN, .staging_live = true};
    channel.staging.va = staging; channel.staging.size = sizeof(staging);
    accel = (struct gf3_ctx){.ch = &channel, .ready = true, .epoch = 1};
    struct gf_v2 g = {.ch = &channel, .g3 = &accel};
    CHECK(!gf_vram_registry_init(&channel));
    CHECK(!gf_vram_reserve(&channel, &node, 4096, 4096, &node));
    struct gfx2_surf s = {.w = 64, .h = 64, .pitch = 64, .bytes = 4096,
        .gpu_off = 4096, .fmt = GFX2_FMT_INDEX8, .pool = GFX2_POOL_VRAM,
        .flags = GFX2_SF_ALIAS, .role = GFX2_ROLE_DISPLAY};
    GFX2_Op op = {.op = GFX2_OP_READBACK, .sx = 3, .sy = 2, .sw = 7, .sh = 3, .pitch = 11};
    struct gfx2_exec ex = {.op = &op, .src = &s};
    for (u32 i = 0; i < sizeof(vram); i++) vram[i] = (u8)(i * 13 + i / 64);
    for (u32 i = 0; i < sizeof(output); i++) output[i] = 0xa5;
    CHECK(!check(&g, &ex));
    CHECK(!readback(&g, &ex, output) && packets == 4 && waits == 1);
    for (u32 y = 0; y < 3; y++) {
        for (u32 x = 0; x < 7; x++) CHECK(output[y * 11 + x] == vram[4096 + (y + 2) * 64 + x + 3]);
        for (u32 x = 7; x < 11; x++) CHECK(output[y * 11 + x] == 0xa5);
    }
    CHECK(!s.priv[0] && !s.priv[1]); /* borrowed, no cached G9 ownership */
    /* The old descriptor must resolve a newly issued identity on reuse. */
    CHECK(!gf_vram_release(&channel, &node.value));
    packets = 0;
    CHECK(readback(&g, &ex, output) == OS32_ERR_IO && !packets);
    CHECK(!gf_vram_reserve(&channel, &node, 4096, 4096, &node));
    CHECK(!readback(&g, &ex, output));
    /* Normal allocated surfaces must retain identity checking. */
    s.flags = 0; packets = 0;
    CHECK(readback(&g, &ex, output) == OS32_ERR_IO && !packets && accel.last_rc == GF3_ESTATE);
    s.priv[0] = node.value.reservation_id; s.priv[1] = node.value.generation;
    CHECK(!readback(&g, &ex, output));
    s.flags = GFX2_SF_ALIAS; s.bytes++; packets = 0;
    CHECK(readback(&g, &ex, output) == OS32_ERR_IO && !packets);
    s.bytes--; s.gpu_off = 0xfffffff0u;
    CHECK(readback(&g, &ex, output) == OS32_ERR_IO && !packets);
    s.gpu_off = 4096; engine_rc = GF2_ETIME;
    CHECK(readback(&g, &ex, output) == OS32_ERR_TIMEOUT);
    finish(0);
}
