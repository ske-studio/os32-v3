/* GFX_ACCEL_HAL_DESIGN G5m. Run in foreground with other diagnostics idle. */
#include "sys/dev/video/geforce/accel.h"
#include "sys/dev/video/geforce/gf_direct_rows.h"
#include "sys/dev/video/geforce/geforce_module.h"
#include "sys/dev/video/geforce/gfinit_mode.h"
#include "sys/dev/video/geforce/diag/gfinit_plan.h"
#include "sys/hal/gfx/gfx.h"
#include "sys/kern/mm/pgalloc.h"
#include "sys/kern/exec/surface_query.h"
#include "video_wc.h"
#include "lib/serial_diag.h"
#include "generated/kdebug_import_macros.h"

static const struct { u16 w, h; } shapes[] = {
    {8,20},{160,1},{16,64},{512,2},{32,64},{512,4},
    {63,65},{455,9},{64,64},{512,8},{17,241},{241,17},
    {64,128},{512,16},{64,256},{512,32},{128,256},{512,64},
    {256,256},{512,128}
};
#define GROUPS 50U
#define COUNT 100U
#define GUARD 0xa7U
#define RAM_PAGES 17U
static bool stopped;
static struct gf_vram_view view;
static int (*close_view)(struct gf_vram_view *);

static i32 close_window(void *owner, struct gf_vram_view *v)
{
    (void)owner;
    return close_view(v);
}

struct trial {
    struct gf3_surface face;
    struct gf3_rect rect;
    struct gf3_ram ram;
    struct gf_io *io;
    struct gf2_chan *channel;
    volatile u8 *vram;
    u8 *storage;
    u32 offset;
    u32 groups_done;
    bool get;
};

static int now(struct trial *t, u32 *us)
{
    return t->io->timing.clock_us(t->io->opaque, us);
}

static void drain(struct trial *t)
{
    (void)*(volatile u32 *)(t->channel->f->bar0 + GF_PLAN_PFB_AMOUNT);
}

static int transfer(struct trial *t, bool dma)
{
    if (dma) {
        const struct gf3_test_ops *o = geforce_accel_ops();
        u32 dl;
        if (!o || now(t, &dl)) return GF3_ESTATE;
        dl += GF3_TIMEOUT_US;
        int saved_rc = o->ctx->last_rc;
        int rc = t->get ? o->download(o->ctx, &t->face, t->rect, &t->ram, dl) :
                          o->upload(o->ctx, &t->face, t->rect, &t->ram, true, dl);
        /* Outstanding DMA and channel faults still require product recovery. */
        if (!rc || (!t->channel->staging_pending && t->channel->state == GF2_RUN))
            o->ctx->last_rc = saved_rc;
        if (rc) {
            stopped = true;
            serial_puts_polled("G5m: transfer failed; reboot before measuring again\r\n");
        }
        return rc;
    }
    for (u32 y = 0; y < t->rect.h; y++) {
        int rc = t->io->point(t->io->opaque);
        if (rc) return rc;
        volatile u8 *v = t->vram + y * t->face.pitch;
        u8 *r = (u8 *)t->ram.ptr + y * t->ram.pitch;
        if (t->get) gf_direct_read_row(r, v, t->rect.w);
        else gf_direct_write_row(v, r, t->rect.w);
    }
    if (!t->get) drain(t);
    return 0;
}

static void field(u32 n)
{
    serial_puts_polled(" ");
    serial_diag_u32(n);
}

static int group(struct trial *t, u32 index, bool dma)
{
    u32 w = t->rect.w, h = t->rect.h;
    u32 vp = t->face.pitch, rp = t->ram.pitch;
    u32 seed = index * 17 + dma * 97;
    for (u32 i = 0; i < 4 + t->offset; i++) t->storage[i] = GUARD;
    for (int i = -4; i < 0; i++) t->vram[i] = GUARD;
    u8 *ram = t->ram.ptr;
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < vp; x++) {
            u8 pixel = (u8)(x + 37U * y + seed);
            t->vram[y * vp + x] = x < w && t->get ? pixel : GUARD;
            if (x < rp) ram[y * rp + x] = x < w && !t->get ? pixel : GUARD;
        }
    }
    ram[h * rp] = GUARD;
    t->vram[h * vp] = GUARD;
    drain(t);
    int rc = 0;
    u32 start = 0, end = 0, count = 0, diff = 0, guard = 0;
    u32 seq = t->channel->seq;
    rc = now(t, &start);
    while (!rc && count < COUNT) {
        rc = transfer(t, dma);
        if (!rc) count++;
    }
    if (!rc) rc = now(t, &end);
    if (!rc) {
        for (u32 y = 0; y < h; y++) {
            for (u32 x = 0; x < vp; x++) {
                u8 value = x < w ? (u8)(x + 37U * y + seed) : GUARD;
                if (x < w) {
                    diff += ram[y * rp + x] != value;
                    diff += t->vram[y * vp + x] != value;
                } else {
                    guard += t->vram[y * vp + x] != GUARD;
                    if (x < rp) guard += ram[y * rp + x] != GUARD;
                }
            }
        }
        for (u32 i = 0; i < 4 + t->offset; i++) guard += t->storage[i] != GUARD;
        for (int i = -4; i < 0; i++) guard += t->vram[i] != GUARD;
        guard += ram[h * rp] != GUARD;
        guard += t->vram[h * vp] != GUARD;
    }
    serial_puts_polled(t->get ? "G5M1 get" : "G5M1 put");
    field(w); field(h); field(rp); field(t->offset);
    serial_puts_polled(dma ? " m2mf" : " cpu");
    field(index); field(count); field(rc ? 0 : end - start);
    serial_puts_polled(" "); serial_diag_i32(rc);
    field(diff); field(guard); field(dma ? t->channel->seq - seq : t->get ? 0 : count);
    serial_puts_polled("\r\n");
    return rc ? rc : diff || guard ? GF2_EIO : 0;
}

static int sweep(struct trial *t)
{
    static const u8 offsets[] = {0,1,3};
    for (u32 i = 0; i < sizeof(shapes)/sizeof(shapes[0]); i++) {
        t->rect = (struct gf3_rect){4, 0, shapes[i].w, shapes[i].h};
        for (u32 pad = 0; pad <= 7; pad += 7) {
            t->ram.pitch = t->rect.w + pad;
            for (u32 j = 0; j < sizeof(offsets); j++) {
                t->offset = offsets[j];
                t->ram.ptr = t->storage + 4 + t->offset;
                t->ram.bytes = RAM_PAGES * PAGE_SIZE - 4 - t->offset;
                for (u32 get = 0; get < 2; get++) {
                    t->get = get;
                    for (u32 n = 0; n <= GROUPS; n++)
                        for (u32 route = 0; route < 2; route++) {
                            int rc = group(t, n, route ^ (n & 1U));
                            if (rc) return rc;
                            t->groups_done++;
                        }
                }
            }
        }
    }
    return 0;
}

bool gfx_transfer_stopped(void) { return stopped; }

int gfx_transfer_bench(void)
{
    if (stopped || serial_gate_active()) return GF3_ESTATE;
    struct surface_query_source display;
    if (gfx_surface_source(LEDGER_ROLE_DISPLAY, &display) ||
        display.backend != LEDGER_SF_GEFORCE || display.count != 1) return GF3_ESTATE;
    const struct ledger_surface *scan = ledger_surface_find(display.backend, display.role);
    if (!scan || scan->pitch != GF_MODE_PITCH || scan->format != GFX_FMT_PACKED8)
        return GF3_ESTATE;
    GFX_ScreenInfo screen = {0};
    gfx_screen_info(&screen);
    const struct gf3_test_ops *o = geforce_accel_ops();
    if (!(screen.flags & GFX_CAP_DIRECT) || screen.format != GFX_FMT_PACKED8 ||
        !o || !o->test_surface || !o->ctx || !o->ctx->ready || !o->ctx->ch ||
        !o->ctx->ch->f || !o->upload || !o->download || !o->map || !o->unmap)
        return GF3_ESTATE;
    struct trial t = {.face = *o->test_surface, .io = &o->ctx->ch->f->io,
                      .channel = o->ctx->ch};
    if (!arch_video_wc_active() || t.face.fmt != GF3_INDEX8 ||
        t.face.pitch != GF_MODE_PITCH || !t.io->point || !t.channel->f->bar0 ||
        !t.io->timing.clock_us || t.face.off < GF_MODE_VRAM_BYTES)
        return GF3_ESTATE;
    u32 span = 0;
    for (u32 i = 0; i < sizeof(shapes)/sizeof(shapes[0]); i++) {
        u32 w = shapes[i].w, h = shapes[i].h, bytes = h * t.face.pitch + 8;
        if (w + 4 > t.face.width || h > t.face.height || bytes > t.face.bytes ||
            bytes > GEFORCE_WC_BYTES || t.face.off > GEFORCE_WC_BYTES - bytes ||
            (w + 7) * h + 8 > RAM_PAGES * PAGE_SIZE) return GF3_ERANGE;
        if (bytes > span) span = bytes;
    }
    if (view.mapped && close_window(0, &view)) return GF2_EMAP;
    u32 phys = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, RAM_PAGES);
    if (!phys) return GF2_ENOSPC;
    t.storage = P2V(phys);
    view.close = close_window;
    close_view = o->unmap;
    int rc = o->map(&view, o->ctx->ch, t.face.off, span, &t.vram);
    if (!rc) {
        serial_puts_polled("G5m PCI dev="); serial_diag_u32(recover_gf_dev());
        serial_puts_polled("\r\n");
        serial_puts_polled("G5M1 map WC"); field(t.face.pitch); field(t.face.off);
        field(span); serial_puts_polled("\r\n");
        /* Retire earlier channel work before the first CPU store. */
        t.get = true;
        t.rect = (struct gf3_rect){0, 0, 1, 1};
        t.ram = (struct gf3_ram){t.storage, RAM_PAGES * PAGE_SIZE, 1};
        rc = transfer(&t, true);
        if (!rc) {
            t.vram += 4; /* aligned left guard before x=4 */
            rc = sweep(&t);
        }
        else {
            serial_puts_polled("G5M1 invalid preflight ");
            serial_diag_i32(rc);
            serial_puts_polled("\r\n");
        }
        int unmap = close_window(0, &view);
        if (!rc) rc = unmap;
    }
    if (!pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, phys / PAGE_SIZE, RAM_PAGES) && !rc)
        rc = GF2_EIO;
    if (!rc) {
        serial_puts_polled("G5M1 done"); field(t.groups_done);
        serial_puts_polled("\r\n");
    }
    return rc;
}
