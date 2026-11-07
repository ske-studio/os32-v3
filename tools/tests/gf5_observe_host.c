/* Guard therm design §7/§9. Real diagnostic dispatch, borrowed boundary ops.
 * PLL fixture: MIT nouveau c03c1a1 clk/nv40.c:39-114: reference 27000,
 * first-stage N=20 M=2, optional second-stage N=2 M=1, P=1.
 * Recreate expectations by applying those integer divisions in that order.
 * No MMIO model, sensor initialization, program, or poll is supplied. */
#include "../../hwdev/gf/feature5.h"
#include "../../hwdev/gf/foundation.h"
#include "../../hwdev/gf/command.h"
#include "../../kdebug/gf5_test.h"
#include "../../kdebug/gf_command_wire.h"
#include "../../kdebug/gfinit_pll.h"
#include "serial_diag_host.h"
static void __attribute__((noreturn)) quit(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(x) do { __asm__ volatile("" ::: "memory"); if (!(x)) quit(__LINE__ % 254 + 1); } while (0)
static struct gf5 g;
static struct gf2_chan ch;
static struct gf_foundation f;
static struct gf4_ctx d;
static struct gf_display_context legacy;
static struct gf5_test_ops ops;
static u8 rom[8], window;
static u32 reads, writes, polls, progs, mux = 0x23;
static i32 failure;
static u32 disabled;
static int read_reg(void *opaque, u32 bar, u32 reg, u32 width, u32 *value)
{
    REQUIRE(opaque == &f && !bar && width == 4);
    ++reads;
    if (reg == GF_PLL_CORE && failure) return failure;
    switch (reg) {
    case GF_PLL_GATE: *value = mux; break;
    case GF_PLL_CORE - GF_PLL_CTRL_OFFSET:
    case GF_PLL_MEMORY - GF_PLL_CTRL_OFFSET: *value = disabled ? 0 : 0xc0010000U; break;
    case GF_PLL_CORE:
    case GF_PLL_MEMORY: *value = 0x02011402U; break;
    case GF_PLL_CORE + GF_PLL_CTRL_OFFSET: *value = 0x80011402U; break;
    default: quit(2);
    }
    return 0;
}
static int write_reg(void *p, u32 b, u32 r, u32 w, u32 v)
{ (void)p; (void)b; (void)r; (void)w; (void)v; ++writes; return 0; }
static i32 poll(struct gf5 *ctx) { REQUIRE(ctx == &g); ++polls; return 0; }
static i32 count(struct gf5 *ctx, u32 *out)
{ REQUIRE(ctx == &g); ctx->last_rc = ctx->source_rc = 0; *out = 1; return 0; }
static i32 entry(struct gf5 *ctx, u32 index, struct gf5_perf_entry *out)
{
    REQUIRE(ctx == &g && !index);
    *out = (struct gf5_perf_entry){.pstate=4, .bios_index=2};
    ctx->last_rc = ctx->source_rc = 0;
    return 0;
}
static bool crc_test;
static u8 vram[8] = {1,2,3,4,5,6,7,8}, saved_bytes[8], scratch[8];
static struct gf5_extent extent;
static struct gf5_image image;
static u32 copies;
static i32 alloc_copy(void *owner, u32 bytes, void **out)
{ REQUIRE(owner == &g && bytes == 8); *out = scratch; return 0; }
static void free_copy(void *owner, void *ptr)
{ REQUIRE(owner == &g && ptr == scratch); }
static i32 copy(void *owner, const struct gf_vram_reservation *key, u8 *out, bool to)
{
    REQUIRE(owner == &g && key->off == 4096 && key->bytes == 8 && !to);
    ++copies;
    if (failure) { g.last_rc = g.source_rc = failure; return failure; }
    for (u32 i = 0; i < 8; ++i) out[i] = vram[i];
    return 0;
}
static i32 save(struct gf5 *ctx, struct gf5_image **out)
{
    REQUIRE(ctx == &g);
    for (u32 i = 0; i < 8; ++i) saved_bytes[i] = vram[i];
    *out = &image; return 0;
}
static i32 restore(struct gf5 *ctx, const struct gf5_image *im)
{ REQUIRE(ctx == &g && im == &image); return 0; }
static void free_image(struct gf5 *ctx, struct gf5_image *im)
{ REQUIRE(ctx == &g && im == &image); }
static i32 calc(struct gf5 *ctx, const struct gf5_clock *c, struct gf5_plan *p)
{ (void)ctx; (void)c; (void)p; return crc_test ? 0 : GF5_EINVAL; }
static i32 prog(struct gf5 *ctx, const struct gf5_plan *p)
{ (void)ctx; (void)p; ++progs; if (crc_test) vram[0] ^= 1; return 0; }
const struct gf5_test_ops *geforce_feature5_ops(void) { return &ops; }
static void observe(void)
{
    host_serial_reset(); reads = 0;
    REQUIRE(!gf5_test(GF5T_INFO));
    REQUIRE(!writes && !polls && !progs);
    REQUIRE(g.last_rc == GF5_ETIME && g.source_rc == -123);
    REQUIRE(g.plan_guard.valid && g.plan_guard.epoch == 7);
    REQUIRE(host_serial_find("last_rc=-506 source_rc=-123 boundary=original"));
}
void _start(void)
{
    g.ch = &ch; ch.f = &f; ch.epoch = 7; ch.state = GF2_RUN;
    g.display = &d; d.ch = &ch; d.epoch = 7; d.legacy = &legacy;
    d.ready = d.accepting = g.accepting = f.running = true;
    f.bar0 = &window; legacy.bar0_map = &window;
    f.io = (struct gf_io){.opaque=&f, .read=read_reg, .write=write_reg};
    g.rom.bytes = rom; g.rom.size = sizeof(rom);
    g.last_rc = GF5_ETIME; g.source_rc = -123; g.poll_source = "original";
    g.plan_guard.valid = true; g.plan_guard.epoch = 7;
    ops = (struct gf5_test_ops){.ctx=&g, .perf_count=count, .perf_get=entry,
        .service_poll=poll, .clock_calc=calc, .clock_prog=prog, .ram_prog=prog};
    observe();
    REQUIRE(reads == 6);
    REQUIRE(host_serial_find("stage=decode domain=0 mux_source=3 khz=270000"));
    REQUIRE(host_serial_find("stage=decode domain=1 mux_source=2 khz=135000"));
    REQUIRE(host_serial_find("stage=decode domain=2 khz=270000"));
    REQUIRE(host_serial_find("stage=perf index=0 bios_index=2 pstate=4"));
    REQUIRE(host_serial_find("temperature=unknown sample_us=unknown"));
    host_serial_reset();
    REQUIRE(!gf5_test(GF5T_PERF_GET));
    REQUIRE(host_serial_find("stage=perf_get index=0 bios_index=2 pstate=4"));
    g.last_rc = GF5_ETIME; g.source_rc = -123;
    disabled = 1;
    observe();
    REQUIRE(host_serial_find("domain=0 mux_source=3 khz=0 reason=invalid_pll"));
    disabled = 0;
    failure = -91;
    observe();
    REQUIRE(reads == 6 && host_serial_find("offset=16388 value=unreadable rc=-91"));
    REQUIRE(host_serial_find("domain=0 mux_source=3 khz=unreadable"));
    REQUIRE(!host_serial_find("offset=16388 value=0"));
    failure = 0; mux = 0;
    observe();
    REQUIRE(host_serial_find("domain=0 mux_source=0 khz=0 reason=unknown_mux"));
    failure = -91;
    observe();
    REQUIRE(host_serial_find("domain=0 mux_source=0 khz=0 reason=unknown_mux"));
    failure = 0; mux = 0x23;
    for (u32 i = 0; i < 3; ++i) {
        if (!i) f.bar0 = NULL;
        if (i == 1) { f.bar0 = &window; f.revoked = true; }
        if (i == 2) { f.revoked = false; d.epoch = 8; }
        observe();
        REQUIRE(!reads && host_serial_find("value=unreadable rc=-509"));
    }
    d.epoch = 7;
    g.phase = GF5_FAULT; ch.fault.latched = true; f.running = false;
    observe(); REQUIRE(reads == 6 && host_serial_find("fault=1 f=1 running=0"));
    g.phase = GF5_SAVED;
    observe(); REQUIRE(reads == 6);
    host_serial_reset();
    REQUIRE(gf5_test(GF5T_RESTORE) == 1);
    REQUIRE(host_serial_find("boundary=no_image_or_owner"));
    ch.f = NULL;
    observe(); REQUIRE(!reads);
    g.ch = NULL;
    observe(); REQUIRE(!reads);
    g.phase = GF5_RUN;
    host_serial_reset();
    REQUIRE(gf5_test(GF5T_CLOCK_CALC) == 1);
    REQUIRE(gf5_test(GF5T_CLOCK_PROG) == 1 && !progs);
    REQUIRE(host_serial_find("boundary=no_plan_or_owner"));
    g.ch = &ch; ch.f = &f;
    crc_test = true; failure = 0;
    extent = (struct gf5_extent){.key={&g,4096,8,1,7},.host_copy=saved_bytes};
    d.test.value = extent.key;
    image = (struct gf5_image){.nextent=1,.extent=&extent,.complete=true};
    g.links = (struct gf5_links){.owner=&g,.alloc=alloc_copy,.free=free_copy,.copy_extent=copy};
    ops.state_save=save; ops.state_restore=restore; ops.state_free=free_image;
    host_serial_reset();
    REQUIRE(!gf5_test(GF5T_CLOCK_CALC));
    REQUIRE(!gf5_test(GF5T_RAM_PROG));
    REQUIRE(copies == 2);
    REQUIRE(host_serial_find("stage=ram_before owner="));
    REQUIRE(host_serial_find("stage=ram_after owner="));
    REQUIRE(host_serial_find("reservation_id=1 generation=7 off=4096 bytes=8 vram_crc=1070237893"));
    REQUIRE(host_serial_find("vram_crc=4083189851"));
    host_serial_reset();
    REQUIRE(!gf5_test(GF5T_SAVE));
    REQUIRE(host_serial_find("stage=save_extent owner="));
    REQUIRE(host_serial_find("vram_crc=4083189851 host_crc=4083189851 rc=0"));
    vram[0] ^= 1;
    host_serial_reset();
    REQUIRE(!gf5_test(GF5T_RESTORE));
    REQUIRE(host_serial_find("stage=restore_extent owner="));
    REQUIRE(host_serial_find("vram_crc=1070237893 host_crc=4083189851 rc=0"));
    /* Observation failure must not replace a successful restore result. */
    failure = -91; g.last_rc = -506; g.source_rc = -123;
    host_serial_reset();
    REQUIRE(!gf5_test(GF5T_RESTORE));
    REQUIRE(host_serial_find("vram_crc=unreadable host_crc=4083189851 rc=-91"));
    REQUIRE(g.last_rc == -506 && g.source_rc == -123);
    REQUIRE(!gf5_test(GF5T_FREE));
    quit(0);
}
