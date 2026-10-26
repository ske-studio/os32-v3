/* Actual collector linked against saved-state getter stubs, without I/O,
 * output, allocator, probe or scheduler substitutes. */
#include "support.h"
#include "ver_info.h"
#include "cpu_calibrate.h"
#include "idt.h"
#include "serial.h"
#include "pcm_cs4231.h"
#include "gfx_hal.h"
#include "bootinfo.h"
#include "kstring.h"

static struct pit_setup saved_pit;
static u32 calls[8];
static int fallback, serial_rc, pcm, gfx_rc, boot_rc, debug_rc;
static u32 mode, baud, fifo, active, kind;
static GFX_ScreenInfo saved_gfx;
static BootImageInfo saved_boot;
static MemStat saved_debug;
static ServicesVerState saved_services;

int cpu_calibrate_used_fallback(void) { calls[0]++; return fallback; }
const struct pit_setup *pit_get_setup(void) { calls[1]++; return &saved_pit; }
int serial_get_status(u32 *m, u32 *b, u32 *f)
{
    calls[2]++;
    CHECK(*m == 0 && *b == 0 && *f == 0);
    *m = mode; *b = baud; *f = fifo;
    return serial_rc;
}
int pcm_present(void) { calls[3]++; return pcm; }
int gfx_ver_state(GFX_ScreenInfo *out, u32 *k, u32 *a)
{
    calls[4]++;
    CHECK(out->width == 0 && *k == GFX_VER_UNKNOWN && *a == 0);
    if (gfx_rc == 0) { *out = saved_gfx; *k = kind; *a = active; }
    return gfx_rc;
}
int boot_image_info(void *out)
{
    calls[5]++;
    CHECK(((BootImageInfo *)out)->commit[0] == 0);
    if (boot_rc == 0) kmemcpy(out, &saved_boot, sizeof(saved_boot));
    return boot_rc;
}
i32 kapi_mem_stat(i32 app_id, void *out, u32 size)
{
    calls[6]++;
    CHECK(app_id == 0 && size == sizeof(MemStat));
    CHECK(((MemStat *)out)->kdebug_state == 0);
    if (debug_rc >= 0) kmemcpy(out, &saved_debug, sizeof(saved_debug));
    return debug_rc;
}
void services_ver_state(ServicesVerState *out)
{
    calls[7]++;
    CHECK(out->stack_used == 0 && out->state[0] == 0);
    *out = saved_services;
}

static int bytes_equal(const void *a, const void *b, u32 size)
{
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < size; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static void snapshot(void)
{
    struct { u32 before; VerSnapshot s; u32 after; } guarded;
    VerSnapshot *s = &guarded.s;
    kmemset(&guarded, 0xcc, sizeof(guarded));
    kmemset(calls, 0, sizeof(calls));
    guarded.before = 0x12345678; guarded.after = 0x87654321;
    ver_collect(s);
    CHECK(guarded.before == 0x12345678 && guarded.after == 0x87654321);
    for (u32 i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) CHECK(calls[i] == 1);
    CHECK(s->cpu_measured == (u32)!fallback);
    CHECK(s->pit_valid == saved_pit.valid);
    CHECK(s->pit_hz == saved_pit.hz && s->pit_input == saved_pit.clk_hz);
    CHECK(s->ser_ready == (u32)(serial_rc >= 0));
    CHECK(s->ser_mode == mode && s->ser_baud == baud && s->ser_fifo == fifo);
    CHECK(s->pcm_present == (u32)pcm);
    CHECK(s->gfx_kind == (gfx_rc ? GFX_VER_UNKNOWN : kind));
    CHECK(s->gfx_active == (gfx_rc ? 0 : active));
    if (!gfx_rc) CHECK(bytes_equal(&s->gfx, &saved_gfx, sizeof(s->gfx)));
    else {
        GFX_ScreenInfo zero = {0};
        CHECK(bytes_equal(&s->gfx, &zero, sizeof(zero)));
    }
    CHECK(s->api_version == KAPI_VERSION);
    for (u32 i = 0; i < sizeof(s->build); i++) CHECK(s->build[i] == 0);
    CHECK(s->boot_ok == (boot_rc >= 0));
    if (!boot_rc) CHECK(bytes_equal(&s->boot, &saved_boot, sizeof(s->boot)));
    else {
        BootImageInfo zero = {0};
        CHECK(bytes_equal(&s->boot, &zero, sizeof(zero)));
    }
    CHECK(s->debug_ok == (debug_rc >= 0));
    if (debug_rc >= 0) CHECK(bytes_equal(&s->debug, &saved_debug, sizeof(s->debug)));
    else {
        MemStat zero = {0};
        CHECK(bytes_equal(&s->debug, &zero, sizeof(zero)));
    }
    CHECK(bytes_equal(&s->services, &saved_services, sizeof(s->services)));
}

void _start(void)
{
    /* Initialized state is deliberately different from the old fixed labels. */
    saved_pit = (struct pit_setup){.valid=1, .hz=100, .clk_hz=2457600};
    mode = 1; baud = 38400; fifo = 1; pcm = 1;
    kind = GFX_VER_PEGC;
    saved_gfx = (GFX_ScreenInfo){.width=640, .height=480, .bpp=8,
        .format=GFX_FMT_PACKED8, .lease_first=32, .lease_count=16};
    saved_boot = (BootImageInfo){.image_crc=0xabcdef01, .image_size=123456,
        .crc_valid=1, .source=BOOTINFO_SRC_HDD, .commit="66828b7e-dirty"};
    saved_debug.kdebug_state = 16; saved_debug.kdebug_pair_crc = 0x12345678;
    saved_debug.kdebug_pages = 12; saved_debug.kdebug_owner_pages = 9;
    debug_rc = sizeof(MemStat);
    saved_services = (ServicesVerState){.stack_used=1024, .stack_size=32768,
        .public=1, .state="SERVICES_READY"};
    snapshot(); /* GUI target */
    active = 1; kind = GFX_VER_PC98;
    saved_gfx.height = 400; saved_gfx.bpp = 4;
    snapshot(); /* Active mode, not a GUI target. */
    kind = GFX_VER_CIRRUS; snapshot();
    /* Status values may still be supplied on serial failure (actual contract). */
    fallback = 1; saved_pit.valid = 0; serial_rc = -1; pcm = 0;
    mode = 0; baud = 9600; fifo = 0;
    saved_services.public = 0;
    kstrncpy(saved_services.state, "MINIMAL_QUARANTINED", sizeof(saved_services.state));
    snapshot();
    gfx_rc = OS32_ERR_NOSYS; boot_rc = OS32_ERR_INVAL; debug_rc = OS32_ERR_INVAL;
    saved_services = (ServicesVerState){.stack_size=32768, .state="STARTING"};
    snapshot(); /* Failed/absent getters leave no previous snapshot behind. */
    say("PASS collect: getter mapping, absence/failure, zeroing, canaries, once-only reads\n");
    die(0);
}
