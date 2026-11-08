/* GFX_ACCEL_HAL P0b / ADDENDUM1: synchronous primitives on C1's test
 * reservation only. No scan writes, CPU WC mapping, or policy changes. */
#include "gf_prim_bench.h"
#include "../hwdev/gf/accel.h"
#include "../hwdev/gf/gf1_test.h"
#include "../drivers/video/geforce/geforce_module.h"
#include "../lib/serial_diag.h"
#include "../mm/pgalloc.h"
#ifdef HWDEV_MODULE
#include "../hwdev/generated/hwdev_import_macros.h"
#endif

#define BENCH_BATCH 100U
#define BENCH_GROUPS 50U
#define BENCH_WIDTH 640U
#define BENCH_HEIGHT 480U
#define BENCH_UPLOAD_BYTES 65536U
#define BENCH_MONO_BYTES 1024U
#define BENCH_TRANSFER_WIDTH 256U
#define BENCH_BAR0_BOOT 0U
static const char *const names[] = {"all", "fence", "fill", "copy", "mono", "ifc", "m2mf", "bar0"};

static const struct gf3_test_ops *dispatch(void)
{
    const struct gf3_test_ops *o = geforce_accel_ops();
    if (!o || !o->ctx || !o->ctx->ch || !o->test_surface ||
        !o->ctx->ready || o->ctx->epoch != o->ctx->ch->epoch ||
        o->ctx->ch->state != GF2_RUN || o->ctx->ch->fault.latched)
        return 0;
    return o;
}
static struct gf_foundation *foundation(void)
{
    const struct gf1_test_ops *o = geforce_foundation_ops();
    return o ? o->production : 0;
}
static int clock_now(u32 *now)
{
    struct gf_foundation *f = foundation();
    if (!f || !f->io.timing.clock_us) return GF3_ESTATE;
    return f->io.timing.clock_us(f->io.opaque, now) ? GF2_EIO : 0;
}
static int geometry(struct gf3_surface *s)
{
    const struct gf3_test_ops *o = dispatch();
    if (!o) return GF3_ESTATE;
    *s = *o->test_surface;
    if (s->fmt != GF3_INDEX8) return GF3_EFORMAT;
    if (s->width > BENCH_WIDTH) s->width = BENCH_WIDTH;
    if (s->height > BENCH_HEIGHT) s->height = BENCH_HEIGHT;
    return s->width && s->height ? 0 : GF3_ERANGE;
}
static int operation(u32 item, u8 *pixels)
{
    if (item == 7U) {
        struct gf_foundation *f = foundation();
        u32 value;
        if (!f || !f->io.read) return GF3_ESTATE;
        return f->io.read(f->io.opaque, 0, BENCH_BAR0_BOOT, sizeof(value), &value) ? GF2_EIO : 0;
    }
    u32 now;
    int rc = clock_now(&now);
    if (rc) return rc;
    struct gf3_surface s;
    rc = geometry(&s);
    if (rc) return rc;
    const struct gf3_test_ops *o = dispatch();
    if (!o) return GF3_ESTATE;
    struct gf3_rect r = {0, 0, s.width, s.height};
    u32 dl = now + GF3_TIMEOUT_US;
    if (item == 1U) r.w = r.h = 1;
    if (item <= 2U)
        return o->fill ? o->fill(o->ctx, &s, r, 0x5a, 3, 0xffffffffU, dl) : GF3_ESTATE;
    if (item == 3U) {
        r.h = s.height / 2U;
        if (!r.h) return GF3_ERANGE;
        return o->copy ? o->copy(o->ctx, &s, &s, r, 0, r.h, 3, 0xffffffffU, dl) : GF3_ESTATE;
    }
    if (r.w > BENCH_TRANSFER_WIDTH) r.w = BENCH_TRANSFER_WIDTH;
    u32 pitch = item == 4U ? (r.w + 7U) / 8U : r.w;
    u32 rows = (item == 4U ? BENCH_MONO_BYTES : BENCH_UPLOAD_BYTES) / pitch;
    if (r.h > rows) r.h = rows;
    struct gf3_ram ram = {pixels, pitch * r.h, pitch};
    if (item == 4U)
        return o->mono ? o->mono(o->ctx, &s, r, &ram, 0xe3, 0x19, r, dl) : GF3_ESTATE;
    return o->upload ? o->upload(o->ctx, &s, r, &ram, item == 6U, dl) : GF3_ESTATE;
}
static void number(const char *key, u32 value)
{
    serial_puts_polled(key); serial_diag_u32(value);
}
static int measure(u32 item, u8 *pixels)
{
    struct gf3_surface s;
    int rc = geometry(&s);
    if (rc) return rc;
    serial_puts_polled("GFP1 geometry "); serial_puts_polled(names[item]);
    number(" w=", s.width); number(" h=", s.height);
    serial_puts_polled("\r\n");
    /* Group zero warms caches and objects but is never a reported sample.
     * Dispatch is reacquired for every module call, including clock reads. */
    for (u32 group = 0; group <= BENCH_GROUPS; group++) {
        u32 start, end, elapsed = 0;
        rc = clock_now(&start);
        if (!rc) {
            for (u32 i = 0; i < BENCH_BATCH && !rc; i++) rc = operation(item, pixels);
            int clock_rc = clock_now(&end);
            if (!clock_rc) elapsed = end - start;
            if (!rc) rc = clock_rc;
        }
        if (group || rc) {
            serial_puts_polled("GFP1 "); serial_puts_polled(names[item]);
            number(" ", group); number(" ", elapsed);
            number(" ", elapsed * (1000U / BENCH_BATCH));
            serial_puts_polled(" "); serial_diag_i32(rc);
            if (item == 3U) {
                serial_puts_polled(" measured=half");
                number(" derived_full=", elapsed * (2000U / BENCH_BATCH));
                serial_puts_polled("ns");
            }
            serial_puts_polled("\r\n");
        }
        if (rc) return rc;
    }
    return 0;
}
int gf_prim_bench(u32 item)
{
    int rc = item > 7U ? GF3_EINVAL : 0;
    /* Keep 64KiB out of HWDEV's 256KiB resident body. This borrowed RAM
     * lives across batches only; allocation and seeding are not timed. */
    u32 phys = 0;
    u8 *pixels = 0;
    if (!rc && (!item || (item >= 4U && item <= 6U))) {
        phys = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, BENCH_UPLOAD_BYTES / PAGE_SIZE);
        if (!phys) rc = GF2_ENOSPC;
        else {
            pixels = P2V(phys);
            for (u32 i = 0; i < BENCH_UPLOAD_BYTES; i++) pixels[i] = (u8)(i * 37U);
        }
    }
    for (u32 i = item ? item : 1U; !rc && i <= (item ? item : 7U); i++) rc = measure(i, pixels);
    if (phys) {
        int free_rc = pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, phys / PAGE_SIZE,
                                           BENCH_UPLOAD_BYTES / PAGE_SIZE);
        if (!rc && free_rc) rc = GF2_EIO;
    }
    serial_puts_polled("GFP1 done rc="); serial_diag_i32(rc);
    serial_puts_polled("\r\n");
    return rc;
}
