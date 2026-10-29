/* P0 contract example only: no production service exists yet. The two fake
 * adapters below implement the same eight entry points without hardware.
 * This checks expressibility/signatures, not production driver behavior. */
#include "video_service.h"
#include "hwdev/gf/internal.h"
void gf_init_contract_test(void);

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg) - 1) : "memory"); \
    finish(1); \
} } while (0)

STATIC_ASSERT(sizeof(u32) == 4, ilp32);
STATIC_ASSERT(sizeof(struct video_mode) == 7 * sizeof(u32), mode_fields);
STATIC_ASSERT(sizeof(struct video_info) == 4 * sizeof(u32), info_fields);
STATIC_ASSERT(sizeof(struct video_fb) == 4 * sizeof(u32), fb_fields);
STATIC_ASSERT(VIDEO_PALETTE_BYTES == GF_MODE_DAC_BYTES, shared_palette_size);
STATIC_ASSERT(sizeof(((struct gf_init_context *)0)->call) ==
              GF_INIT_CALL_DEPTH * sizeof(struct gfinit_exec_frame), call_bound);
STATIC_ASSERT(sizeof(((struct gf_init_context *)0)->repeat) ==
              GF_INIT_REPEAT_DEPTH * sizeof(struct gfinit_exec_frame), repeat_bound);
STATIC_ASSERT(_Generic(&gf_load_row,
    int (*)(struct gf_display_context *, const struct gfinit_mode *, u32): 1,
    default: 0), row_signature);
#define OP_CHECK(name) STATIC_ASSERT(_Generic(&gf_op_##name##_run, \
    gf_op_run_fn: 1, default: 0), name##_signature)
OP_CHECK(io_restrict_prog);
OP_CHECK(repeat);
OP_CHECK(end_repeat);
OP_CHECK(copy);
OP_CHECK(not);
OP_CHECK(io_flag_condition);
OP_CHECK(io_restrict_pll2);
OP_CHECK(pll2);
OP_CHECK(cr_index_address_latched);
OP_CHECK(cr);
OP_CHECK(zm_cr);
OP_CHECK(zm_cr_group);
OP_CHECK(zm_reg_sequence);
OP_CHECK(sub_direct);
OP_CHECK(copy_nv_reg);
OP_CHECK(zm_index_io);
OP_CHECK(compute_mem);
OP_CHECK(reset);
OP_CHECK(sub);
OP_CHECK(nv_reg);
OP_CHECK(macro);
OP_CHECK(done);
OP_CHECK(resume);
OP_CHECK(time);
OP_CHECK(condition);
OP_CHECK(index_io);
OP_CHECK(zm_reg);

/* IDs intentionally differ from the array index. Trident's different mode,
 * DAC precision and reservation demonstrate no GF layout in the public API.
 * These are synthetic descriptors, not actual Trident hardware support. */
#define FAKE_GF_MODE_ID 7U
#define FAKE_TRIDENT_MODE_ID 19U
#define FAKE_TRIDENT_WIDTH 800U
#define FAKE_TRIDENT_HEIGHT 600U
#define FAKE_TRIDENT_DAC_BITS 6U
#define FAKE_FB_PHYS 0x10000000U
struct fake_video {
    struct video_mode mode;
    u32 supported_caps, io, devinits;
    bool initialized, active, blank, reusable, fail, zero_start;
    u8 palette[VIDEO_PALETTE_BYTES];
};

static int fake_query(void *context, struct video_info *out)
{
    struct fake_video *f = context;
    if (!out) return OS32_ERR_INVAL;
    *out = (struct video_info){0};
    if (!f) return OS32_ERR_INVAL;
    *out = (struct video_info){f->active ? f->supported_caps : 0, 1,
        f->initialized ? f->mode.id : VIDEO_MODE_NONE, f->active};
    return 0;
}
static int fake_mode_info(void *context, u32 index, struct video_mode *out)
{
    struct fake_video *f = context;
    if (!out) return OS32_ERR_INVAL;
    *out = (struct video_mode){0};
    if (!f || index != 0) return OS32_ERR_INVAL;
    *out = f->mode;
    return 0;
}
static int fake_start(void *context, u32 id, const u8 *rgb)
{
    struct fake_video *f = context;
    if (!f || !rgb || id != f->mode.id) return OS32_ERR_INVAL;
    /* Display reservation arithmetic, checked before the simulated I/O. */
    if (!f->mode.width || f->mode.pitch < f->mode.width || !f->mode.height ||
        f->mode.pitch > U32_MAX / f->mode.height) return OS32_ERR_INVAL;
    if (!f->reusable) return -1;
    f->io++;
    if (f->fail) { f->reusable = false; return -1; }
    if (!f->initialized) { f->devinits++; f->initialized = true; }
    for (u32 i = 0; i < VIDEO_PALETTE_BYTES; i++) f->palette[i] = rgb[i];
    f->active = true;
    f->blank = false;
    return 0;
}
static int fake_framebuffer(void *context, struct video_fb *out)
{
    struct fake_video *f = context;
    if (!out) return OS32_ERR_INVAL;
    *out = (struct video_fb){0};
    if (!f) return OS32_ERR_INVAL;
    if (!f->active) return -1;
    *out = (struct video_fb){FAKE_FB_PHYS, f->mode.pitch * f->mode.height,
                            f->mode.pitch, f->mode.format};
    return 0;
}
static int fake_palette(void *context, u32 first, u32 count, const u8 *rgb)
{
    struct fake_video *f = context;
    if (!f || !rgb || first >= VIDEO_PALETTE_ENTRIES || !count ||
        count > VIDEO_PALETTE_ENTRIES - first) return OS32_ERR_INVAL;
    if (!(f->supported_caps & VIDEO_CAP_PALETTE)) return OS32_ERR_NOSYS;
    if (!f->active) return -1;
    f->io++;
    for (u32 i = 0; i < count * VIDEO_RGB_COMPONENTS; i++)
        f->palette[first * VIDEO_RGB_COMPONENTS + i] = rgb[i];
    return 0;
}
static int fake_display_start(void *context, u32 offset)
{
    struct fake_video *f = context;
    if (!f) return OS32_ERR_INVAL;
    return !f->zero_start || offset ? OS32_ERR_NOSYS : 0;
}
static int fake_blank(void *context, int blank)
{
    struct fake_video *f = context;
    if (!f || (blank != 0 && blank != 1)) return OS32_ERR_INVAL;
    if (!(f->supported_caps & VIDEO_CAP_BLANK)) return OS32_ERR_NOSYS;
    if (f->blank == (bool)blank) return 0;
    if (!f->active) return -1;
    f->io++;
    f->blank = blank;
    return 0;
}
static int fake_stop(void *context)
{
    struct fake_video *f = context;
    if (!f) return OS32_ERR_INVAL;
    if (!f->active) return 0;
    f->io++;
    if (f->fail) { f->reusable = false; return -1; }
    f->active = false;
    return 0;
}
static const struct video_ops fake_ops = {
    .query = fake_query, .mode_info = fake_mode_info, .start = fake_start,
    .framebuffer = fake_framebuffer, .palette = fake_palette,
    .display_start = fake_display_start, .blank = fake_blank, .stop = fake_stop
};

/* Test-only facade: definitions must agree with all public declarations.
 * Use the table through the device; no board-specific branching in callers. */
#define CONTEXT(d) ((d) ? (d)->context : NULL)
int video_query(struct video_device *d, struct video_info *out)
{ return (d ? d->ops : &fake_ops)->query(CONTEXT(d), out); }
int video_mode_info(struct video_device *d, u32 index, struct video_mode *out)
{ return (d ? d->ops : &fake_ops)->mode_info(CONTEXT(d), index, out); }
int video_start(struct video_device *d, u32 id, const u8 *rgb)
{ return d ? d->ops->start(d->context, id, rgb) : OS32_ERR_INVAL; }
int video_framebuffer(struct video_device *d, struct video_fb *out)
{ return (d ? d->ops : &fake_ops)->framebuffer(CONTEXT(d), out); }
int video_palette(struct video_device *d, u32 first, u32 count, const u8 *rgb)
{ return d ? d->ops->palette(d->context, first, count, rgb) : OS32_ERR_INVAL; }
int video_display_start(struct video_device *d, u32 offset)
{ return d ? d->ops->display_start(d->context, offset) : OS32_ERR_INVAL; }
int video_blank(struct video_device *d, int blank)
{ return d ? d->ops->blank(d->context, blank) : OS32_ERR_INVAL; }
int video_stop(struct video_device *d)
{ return d ? d->ops->stop(d->context) : OS32_ERR_INVAL; }

static void exercise(struct fake_video *f)
{
    struct video_device d = {f, &fake_ops};
    struct video_info info;
    struct video_mode mode;
    struct video_fb fb;
    u8 rgb[VIDEO_PALETTE_BYTES];
    for (u32 i = 0; i < VIDEO_PALETTE_BYTES; i++) rgb[i] = (u8)i;
    CHECK(video_query(&d, &info) == 0 && info.active == 0 && info.caps == 0 &&
          info.current_mode == VIDEO_MODE_NONE && info.mode_count == 1);
    CHECK(video_mode_info(&d, 0, &mode) == 0 && mode.id == f->mode.id &&
          mode.width == f->mode.width && mode.dac_bits == f->mode.dac_bits);
    CHECK(video_mode_info(&d, f->mode.id, &mode) == OS32_ERR_INVAL);
    CHECK(mode.id == 0 && mode.width == 0 && mode.height == 0 && mode.pitch == 0 &&
          mode.format == 0 && mode.dac_bits == 0 && mode.clock_khz == 0);
    fb = (struct video_fb){1, 1, 1, 1};
    CHECK(video_framebuffer(&d, &fb) == -1);
    CHECK(fb.phys == 0 && fb.bytes == 0 && fb.pitch == 0 && fb.format == 0);
    CHECK(video_start(&d, 0, rgb) == OS32_ERR_INVAL);
    CHECK(video_start(&d, f->mode.id, NULL) == OS32_ERR_INVAL);
    CHECK(f->io == 0);
    CHECK(video_stop(&d) == 0 && f->io == 0);
    CHECK(video_start(&d, f->mode.id, rgb) == 0 && f->devinits == 1);
    rgb[0] ^= U8_MAX;
    CHECK(f->palette[0] != rgb[0]); /* Caller storage was not retained. */
    u32 before = f->io;
    CHECK(video_query(&d, &info) == 0 && info.active == 1 &&
          info.current_mode == f->mode.id && info.caps == f->supported_caps);
    CHECK(video_framebuffer(&d, &fb) == 0 && fb.phys == FAKE_FB_PHYS &&
          fb.bytes == f->mode.pitch * f->mode.height &&
          fb.pitch == f->mode.pitch && fb.format == GFX_FMT_PACKED8);
    CHECK(video_mode_info(&d, 0, &mode) == 0 && mode.clock_khz == f->mode.clock_khz);
    CHECK(video_query(&d, NULL) == OS32_ERR_INVAL);
    CHECK(video_mode_info(&d, 0, NULL) == OS32_ERR_INVAL);
    CHECK(video_framebuffer(&d, NULL) == OS32_ERR_INVAL);
    CHECK(video_palette(&d, VIDEO_PALETTE_ENTRIES, 1, rgb) == OS32_ERR_INVAL);
    CHECK(video_palette(&d, 1, U32_MAX, rgb) == OS32_ERR_INVAL);
    CHECK(video_palette(&d, 0, 0, rgb) == OS32_ERR_INVAL);
    CHECK(video_palette(&d, 0, 1, NULL) == OS32_ERR_INVAL);
    CHECK(video_display_start(&d, 0) == (f->zero_start ? 0 : OS32_ERR_NOSYS));
    CHECK(video_display_start(&d, 1) == OS32_ERR_NOSYS);
    CHECK(video_display_start(&d, U32_MAX) == OS32_ERR_NOSYS);
    CHECK(video_blank(&d, 2) == OS32_ERR_INVAL);
    CHECK(video_blank(&d, 0) == ((f->supported_caps & VIDEO_CAP_BLANK) ? 0 : OS32_ERR_NOSYS));
    CHECK(f->io == before);
    CHECK(video_palette(&d, VIDEO_PALETTE_ENTRIES - 1, 1, rgb) == 0);
    CHECK(f->palette[VIDEO_PALETTE_BYTES - VIDEO_RGB_COMPONENTS] == rgb[0]);
    CHECK(f->palette[0] != rgb[0]); /* Partial update kept other entries. */
    before = f->io;
    if (f->supported_caps & VIDEO_CAP_BLANK) {
        CHECK(video_blank(&d, 1) == 0 && f->io == before + 1);
        CHECK(video_blank(&d, 1) == 0 && f->io == before + 1);
        CHECK(video_blank(&d, 0) == 0 && f->io == before + 2);
    } else {
        CHECK(video_blank(&d, 1) == OS32_ERR_NOSYS && f->io == before);
    }
    CHECK(video_stop(&d) == 0);
    before = f->io;
    CHECK(video_stop(&d) == 0 && f->io == before);
    CHECK(video_query(&d, &info) == 0 && !info.active && info.caps == 0 && f->io == before);
    CHECK(video_start(&d, f->mode.id, rgb) == 0 && f->devinits == 1);
    f->fail = true;
    CHECK(video_stop(&d) == -1 && !f->reusable);
    before = f->io;
    CHECK(video_start(&d, f->mode.id, rgb) == -1 && f->io == before && f->devinits == 1);
}

void _start(void)
{
    gf_init_contract_test();
    struct fake_video gf = {
        .mode = {FAKE_GF_MODE_ID, GF_MODE_WIDTH, GF_MODE_HEIGHT, GF_MODE_PITCH,
                 GFX_FMT_PACKED8, VIDEO_RGB_BITS, GF_MODE_CLOCK_KHZ},
        .supported_caps = VIDEO_CAP_PALETTE | VIDEO_CAP_BLANK,
        .reusable = true, .zero_start = true
    };
    struct fake_video trident = {
        .mode = {FAKE_TRIDENT_MODE_ID, FAKE_TRIDENT_WIDTH, FAKE_TRIDENT_HEIGHT,
                 FAKE_TRIDENT_WIDTH, GFX_FMT_PACKED8, FAKE_TRIDENT_DAC_BITS, 0},
        .supported_caps = VIDEO_CAP_PALETTE, .reusable = true
    };
    exercise(&gf);
    exercise(&trident);
    /* Invalid FB geometry must fail before the fake bus is touched. */
    struct fake_video bad = {.mode = gf.mode, .reusable = true};
    struct video_device d = {&bad, &fake_ops};
    u8 rgb[VIDEO_PALETTE_BYTES] = {0};
    bad.mode.pitch = bad.mode.width - 1;
    CHECK(video_start(&d, bad.mode.id, rgb) == OS32_ERR_INVAL && bad.io == 0);
    bad.mode.pitch = U32_MAX;
    CHECK(video_start(&d, bad.mode.id, rgb) == OS32_ERR_INVAL && bad.io == 0);
    bad.mode = gf.mode;
    bad.fail = true;
    CHECK(video_start(&d, bad.mode.id, rgb) == -1 && !bad.reusable && bad.devinits == 0);
    struct video_info info = {1, 1, 1, 1};
    CHECK(video_query(NULL, &info) == OS32_ERR_INVAL && !info.caps &&
          !info.mode_count && !info.current_mode && !info.active);
    struct video_mode mode = {.id = 1};
    struct video_fb fb = {.phys = 1};
    CHECK(video_mode_info(NULL, 0, &mode) == OS32_ERR_INVAL && !mode.id);
    CHECK(video_framebuffer(NULL, &fb) == OS32_ERR_INVAL && !fb.phys);
    CHECK(video_start(NULL, 0, NULL) == OS32_ERR_INVAL);
    CHECK(video_palette(NULL, 0, 1, NULL) == OS32_ERR_INVAL);
    CHECK(video_display_start(NULL, 0) == OS32_ERR_INVAL);
    CHECK(video_blank(NULL, 0) == OS32_ERR_INVAL);
    CHECK(video_stop(NULL) == OS32_ERR_INVAL);
    finish(0);
}
