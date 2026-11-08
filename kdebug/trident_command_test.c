/* TRIDENT_FULL_DESIGN §9 F17: synchronous serial observations through F16's
 * production loan only. gf_command_test.c:42-57 supplies the call/rc pattern.
 * CR39 diagnostic access borrows the production byte I/O seam; this file
 * owns no second device or persistent ops pointer.
 */
#include "trident_command_test.h"
#include "sys.h"
#include "../drivers/video/trident/trident_module.h"
#include "../lib/serial_diag.h"
#include "generated/kdebug_import_macros.h"

/* GXcopy (X11 logical ROP); test rows fit every F3 640-pixel timing. */
#define TRI_TEST_PCI_REG 0x39u
#define TRI_TEST_NO_PCI_BURST 0xF9u /* X/trident_dac.c:709-721 */
#define TRI_TEST_ROP_COPY 3u
#define TRI_TEST_HEAD 0u
#define TRI_TEST_SOURCE_Y 0u
#define TRI_TEST_DEST_Y 1u
#define TRI_TEST_ROWS 2u
#define TRI_TEST_FILL 0x35u
#define TRI_TEST_PATTERN 0x5aa59669u /* byte lanes differ, unlike solid fill */
#define TRI_TEST_STRIDE 0x01020408u
#define TRI_TEST_WHITE 0xffffffffu
#define TRI_TEST_BLACK 0xff000000u
#define TRI_TEST_THIRD 0xffff0000u
#define TRI_TEST_CURSOR_SIDE 8u /* small enough for a four-corner SW sample */
/* Foreground synchronous dispatch, no state retained from the module loan. */
static u32 result_count;
static u32 cpu_row[TRI_WIDTH];
static u32 cursor_pixels[TRI_TEST_CURSOR_SIDE * TRI_TEST_CURSOR_SIDE];

static void number(const char *key, u32 value)
{
    serial_puts_polled(key);
    serial_diag_u32(value);
}
static int emit_pci(const struct TridentDiagnosticOps *o, const char *phase, int rc,
                    bool unavailable, int pci)
{
    ++result_count;
    serial_puts_polled("TRI phase="); serial_puts_polled(phase);
    serial_puts_polled(" rc="); serial_diag_i32(rc);
    serial_puts_polled(" result=");
    serial_puts_polled(unavailable ? "unavailable" : rc ? "ng" : "ok");
    if (o && o->context) {
        const struct trident_device *d = o->context;
        number(" mode=", d->mode.selected.id);
        number(" depth=", d->mode.selected.depth);
        number(" bpp=", d->mode.selected.bpp);
        number(" pitch=", d->mode.client.pitch);
        number(" video_status=", d->video.status);
        number(" video_allocated=", d->video.allocation.allocated);
        serial_puts_polled(" video_rc="); serial_diag_i32(d->video.last_rc);
    }
    if (pci >= 0) {
        serial_puts_polled(" cr39="); serial_diag_hex((u8)pci, 2);
    }
    serial_puts_polled("\r\n");
    return rc;
}
static int emit(const struct TridentDiagnosticOps *o, const char *phase, int rc,
                bool unavailable)
{
    return emit_pci(o, phase, rc, unavailable, -1);
}
static int optional(const struct TridentDiagnosticOps *o, const char *phase,
                    int rc)
{
    return emit(o, phase, rc, rc == TRI_E_NOSYS || rc == OS32_ERR_NOSYS);
}
static void remember(int *result, int rc)
{
    if (!*result && rc && rc != TRI_E_NOSYS && rc != OS32_ERR_NOSYS)
        *result = rc;
}
static bool drawable(const struct trident_device *d)
{
    return d->active && d->screen_ready && d->mode.valid && !d->returning &&
           d->res && d->res->ready && d->res->client &&
           d->mode.generation == d->res->generation;
}
/* Check the actual CPU drawing mapping, never a write_span32 I/O substitute.
 * Bound both rows and every dword before using the borrowed CLIENT pointer. */
static int client_rows(const struct trident_device *d)
{
    if (!drawable(d)) return TRI_E_STATE;
    const struct tri_surface *s = &d->mode.client;
    const struct tri_surface *display = &d->mode.display;
    if (d->res->ram_fallback || s->off != display->off ||
        s->pitch != display->pitch) return TRI_E_NOSYS;
    if (!s->width || s->width > TRI_WIDTH || s->height < TRI_TEST_ROWS ||
        (s->cpp != 1u && s->cpp != 2u && s->cpp != 4u) ||
        s->pitch < (u32)s->width * s->cpp ||
        (s->pitch % sizeof(u32)) || ((uptr)d->res->client % sizeof(u32)))
        return TRI_E_SURFACE;
    u32 row_bytes = (u32)s->width * s->cpp;
    if (row_bytes % sizeof(u32) || s->bytes < row_bytes ||
        s->pitch > s->bytes - row_bytes || d->res->client_bytes < row_bytes ||
        s->pitch > d->res->client_bytes - row_bytes) return TRI_E_SURFACE;
    return 0;
}
static int read_row_pci(const struct TridentDiagnosticOps *o, const char *phase,
                        u32 y, u32 count, int pci)
{
    const struct trident_device *d = o->context;
    const struct tri_surface *s = &d->mode.client;
    const volatile u32 *p = (const volatile u32 *)(d->res->client + y * s->pitch);
    int rc = 0;
    for (u32 i = 0; i < count; ++i) {
        u32 actual = p[i];
        if (actual == cpu_row[i]) continue;
        rc = TRI_E_ACCESS;
        if (serial_gate_active()) continue;
        serial_puts_polled("TRI mismatch phase="); serial_puts_polled(phase);
        number(" x=", i * sizeof(u32) / s->cpp); number(" y=", y);
        serial_puts_polled(" expected="); serial_diag_hex(cpu_row[i], 8);
        serial_puts_polled(" actual="); serial_diag_hex(actual, 8);
        serial_puts_polled("\r\n");
    }
    return emit_pci(o, phase, rc, false, pci);
}
static int read_row(const struct TridentDiagnosticOps *o, const char *phase,
                    u32 y, u32 count)
{
    return read_row_pci(o, phase, y, count, -1);
}
/* Keep the same CLIENT, width and read path for each transfer comparison.
 * Scalar stores intentionally have no intervening reads or port delays. */
static void write_row(struct trident_device *d, u32 count, bool scalar, u32 salt)
{
    const struct tri_surface *s = &d->mode.client;
    for (u32 i = 0; i < count; ++i)
        cpu_row[i] = TRI_TEST_PATTERN ^ (i * TRI_TEST_STRIDE) ^ salt;
    if (scalar) {
        volatile u32 *p = (volatile u32 *)d->res->client;
        for (u32 i = 0; i < count; ++i) p[i] = cpu_row[i];
        return;
    }
    u32 *dst = (u32 *)(d->res->client + TRI_TEST_SOURCE_Y * s->pitch);
    const u32 *src = cpu_row;
    u32 words = count;
    /* One full visible row, contiguous 32-bit stores through drawing CLIENT.
     * No byte-store loop, readback, or I/O callback between dwords. */
    __asm__ volatile ("cld; rep movsl" : "+D"(dst), "+S"(src), "+c"(words)
                      : : "memory", "cc");
}
static int cpu32_sequence(const struct TridentDiagnosticOps *o)
{
    struct trident_device *d = o->context;
    int rc = client_rows(d);
    if (rc) return optional(o, "cpu32_precheck", rc);
    bool ge = o->fill && o->copy && o->copy_direction && o->sync &&
        !d->mode.no_accel && d->accel.initialized &&
        (d->mode.features & (TRI_CAP_FILL | TRI_CAP_COPY)) ==
        (TRI_CAP_FILL | TRI_CAP_COPY);
    const struct tri_surface *s = &d->mode.client;
    u32 count = (u32)s->width * s->cpp / sizeof(u32);
    if (ge) {
        u32 fill = TRI_TEST_FILL;
        /* Match F1 ge_replicate: one byte or halfword repeated per dword. */
        if (s->cpp == 1u) fill |= fill << 8;
        if (s->cpp <= 2u) fill |= fill << 16;
        for (u32 i = 0; i < count; ++i) cpu_row[i] = fill;
        rc = o->fill(d, s, 0, TRI_TEST_SOURCE_Y, s->width, TRI_TEST_ROWS,
                     TRI_TEST_FILL, TRI_TEST_ROP_COPY);
        emit(o, "ge_fill", rc, false);
        if (rc) return rc;
        rc = read_row(o, "ge_fill_read", TRI_TEST_SOURCE_Y, count);
        if (rc) return rc;
        /* Exercise DST_ENABLE as well. Review #1: ClearSync at copy's exit only
         * polls; Sync MUST restore OPERMODE before the following CPU drawing. */
        rc = o->copy_direction(d, 1, 1);
        emit(o, "transparent_direction", rc, false);
        if (rc) return rc;
        rc = o->copy(d, s, s, 0, TRI_TEST_SOURCE_Y, 0, TRI_TEST_DEST_Y,
                     s->width, 1, TRI_TEST_ROP_COPY, true, TRI_TEST_FILL);
        emit(o, "transparent_copy", rc, false);
        if (rc) return rc;
        rc = o->sync(d);
        emit(o, "cpu32_sync", rc, false);
        if (rc) return rc;
    }
    write_row(d, count, false, 0);
    rc = read_row(o, "cpu32_read", TRI_TEST_SOURCE_Y, count);
    if (rc) return rc;
    if (!ge) return optional(o, "ge", TRI_E_NOSYS);
    rc = o->copy_direction(d, 1, 1);
    emit(o, "ge_copy_direction", rc, false);
    if (rc) return rc;
    rc = o->copy(d, s, s, 0, TRI_TEST_SOURCE_Y, 0, TRI_TEST_DEST_Y,
                 s->width, 1, TRI_TEST_ROP_COPY, false, 0);
    emit(o, "ge_copy", rc, false);
    if (rc) return rc;
    return read_row(o, "ge_copy_read", TRI_TEST_DEST_Y, count);
}
/* tri cpu32/modes: run independent comparisons even if a CPU read mismatches.
 * "no_ge" means no GE command issued by this comparison group yet; the GUI
 * and a preceding group may have used GE earlier. This is not an engine reset or a claim of power-on state.
 * A:226-277 is unchanged in production. Extra Sync is diagnostic only. */
static int cpu32_cases(const struct TridentDiagnosticOps *o, u8 pci)
{
    struct trident_device *d = o->context;
    int rc = client_rows(d);
    if (rc) return optional(o, "cpu32_precheck", rc);
    u32 count = (u32)d->mode.client.width * d->mode.client.cpp / sizeof(u32);
    write_row(d, count, false, TRI_TEST_STRIDE);
    int result = read_row_pci(o, "cpu32_no_ge_read", TRI_TEST_SOURCE_Y, count, pci);
    bool ge = o->fill && o->copy && o->copy_direction && o->sync &&
        !d->mode.no_accel && d->accel.initialized &&
        (d->mode.features & (TRI_CAP_FILL | TRI_CAP_COPY)) ==
        (TRI_CAP_FILL | TRI_CAP_COPY);
    if (!ge) {
        optional(o, "ge", TRI_E_NOSYS);
        return result ? result : TRI_E_NOSYS;
    }
    static const char *const phases[] = {
        "cpu32_fill_read", "cpu32_double_sync_read", "cpu32_scalar_read"
    };
    const struct tri_surface *s = &d->mode.client;
    for (u32 test = 0; test < 3u; ++test) {
        /* Each case starts with the same GE fill and verifies real VRAM.
         * fill's exit already performs the first Sync, exactly as in A. */
        u32 fill = TRI_TEST_FILL;
        if (s->cpp == 1u) fill |= fill << 8;
        if (s->cpp <= 2u) fill |= fill << 16;
        for (u32 i = 0; i < count; ++i) cpu_row[i] = fill;
        rc = o->fill(d, s, 0, TRI_TEST_SOURCE_Y, s->width, TRI_TEST_ROWS,
                     TRI_TEST_FILL, TRI_TEST_ROP_COPY);
        emit(o, "probe_ge_fill", rc, false);
        if (rc) return rc;
        rc = read_row(o, "probe_ge_fill_read", TRI_TEST_SOURCE_Y, count);
        if (rc) return rc;
        if (test == 1u) {
            rc = o->sync(d);
            emit(o, "probe_second_sync", rc, false);
            if (rc) return rc;
        }
        write_row(d, count, test == 2u, (test + 2u) * TRI_TEST_STRIDE);
        remember(&result, read_row_pci(o, phases[test], TRI_TEST_SOURCE_Y, count, pci));
    }
    /* Retain transparent-copy + Sync + rep and the subsequent GE copy test. */
    remember(&result, cpu32_sequence(o));
    return result;
}
/* D:539-541/581-583: bit0 controls MMIO. X/trident_dac.c:709-721:
 * NoPciBurst clears bits1/2 together, preserving bit0 and every upper bit.
 * The reference does not identify separate read/write burst bits.
 * Use the loan's byte callbacks: module-local indexed helpers cannot be linked
 * into kdebug. Always restore, including a failed disable or comparison. */
static int cpu32_pci_write(struct trident_device *d, u8 value)
{
    int rc = d->io->write8(d->io_opaque, TRI_SPACE_REGS,
                           d->cur_vga_base, TRI_TEST_PCI_REG);
    if (rc) return rc;
    return d->io->write8(d->io_opaque, TRI_SPACE_REGS,
                          d->cur_vga_base + 1u, value);
}
static int cpu32(const struct TridentDiagnosticOps *o)
{
    struct trident_device *d = o->context;
    int rc = client_rows(d);
    if (rc) return optional(o, "cpu32_precheck", rc);
    if (!d->io || !d->io->write8 || !d->io->read8)
        return optional(o, "cpu32_pci_io", TRI_E_NOSYS);
    rc = d->io->write8(d->io_opaque, TRI_SPACE_REGS,
                       d->cur_vga_base, TRI_TEST_PCI_REG);
    u8 saved = 0;
    if (!rc) rc = d->io->read8(d->io_opaque, TRI_SPACE_REGS,
                              d->cur_vga_base + 1u, &saved);
    if (rc) return emit(o, "cpu32_pci_save", rc, false);
    emit_pci(o, "cpu32_pci_original", 0, false, saved);
    int original = cpu32_cases(o, saved);
    int result = 0;
    remember(&result, original);
    u8 disabled = saved & TRI_TEST_NO_PCI_BURST;
    rc = cpu32_pci_write(d, disabled);
    emit_pci(o, "cpu32_pci_no_burst", rc, false, disabled);
    if (!rc) remember(&result, cpu32_cases(o, disabled));
    else remember(&result, rc);
    rc = cpu32_pci_write(d, saved);
    emit_pci(o, "cpu32_pci_restore", rc, false, saved);
    remember(&result, rc);
    return result ? result : original;
}
/* Compare the current image without yielding or modifying CLIENT. Only the
 * present itself is timed; serial output and VRAM verification are outside. */
#define TRI_PRESENT_RUNS 8u
static int present_compare(const struct TridentDiagnosticOps *o)
{
    struct trident_device *d = o->context;
    if (!d->res->ram_fallback || !o->present || !d->io || !d->io->read8)
        return optional(o, "present_precheck", TRI_E_NOSYS);
    enum tri_present_method saved = d->present_method;
    int result = 0;
    for (u32 method = TRI_PRESENT_BYTE; method <= TRI_PRESENT_DWORD_SYNC; ++method) {
        const struct tri_surface *c = &d->mode.client, *v = &d->mode.display;
        u32 minimum = (u32)-1, maximum = 0, count = 0, mismatches = 0;
        unsigned long long total = 0;
        int rc = 0;
        d->present_method = (enum tri_present_method)method;
        for (u32 run = 0; run < TRI_PRESENT_RUNS; ++run) {
            u32 before, after, hi;
            rc = sys_time_now(&before, &hi);
            if (rc) break;
            rc = o->present(d, 0, 0, c->width, c->height);
            if (rc) break;
            rc = sys_time_now(&after, &hi);
            if (rc) break;
            u32 elapsed = after - before;
            if (elapsed < minimum) minimum = elapsed;
            if (elapsed > maximum) maximum = elapsed;
            total += elapsed;
            ++count;
        }
        if (!rc) {
            for (u32 row = 0; !rc && row < c->height; ++row) {
                for (u32 i = 0; i < (u32)c->width * c->cpp; ++i) {
                    u8 actual;
                    rc = d->io->read8(d->io_opaque, TRI_SPACE_LFB,
                                      v->off + row * v->pitch + i, &actual);
                    if (rc) break;
                    if (actual != d->res->client[row * c->pitch + i]) ++mismatches;
                }
            }
        }
        if (!rc && mismatches) rc = TRI_E_ACCESS;
        ++result_count;
        serial_puts_polled("TRI phase=present");
        number(" mode=", d->mode.selected.id);
        serial_puts_polled(" method=");
        serial_puts_polled(method == TRI_PRESENT_BYTE ? "byte" : "dword_sync");
        number(" count=", count);
        number(" min_us=", count ? minimum : 0);
        number(" max_us=", maximum);
        number(" avg_us=", count == TRI_PRESENT_RUNS ? (u32)(total >> 3) : 0);
        number(" mismatches=", mismatches);
        serial_puts_polled(" rc="); serial_diag_i32(rc);
        serial_puts_polled("\r\n");
        remember(&result, rc);
    }
    d->present_method = saved;
    return result;
}
static int dpms(const struct TridentDiagnosticOps *o)
{
    if (!o->dpms) return optional(o, "dpms", TRI_E_NOSYS);
    int result = 0;
    for (u32 state = TRI_DPMS_STANDBY; state <= TRI_DPMS_OFF; ++state) {
        number("TRI dpms_state=", state); serial_puts_polled("\r\n");
        int rc = o->dpms(TRI_TEST_HEAD, state);
        optional(o, "dpms_low_power", rc); remember(&result, rc);
        /* Always attempt On, including when entry to low power failed. */
        rc = o->dpms(TRI_TEST_HEAD, TRI_DPMS_ON);
        optional(o, "dpms_on", rc); remember(&result, rc);
        if (rc) break;
    }
    return result;
}
static int cursor(const struct TridentDiagnosticOps *o)
{
    struct trident_device *d = o->context;
    if (!o->cursor_image || !o->cursor_move || !o->cursor_show ||
        !(d->mode.features & TRI_CAP_CURSOR) || !d->cursor.available)
        return optional(o, "cursor", TRI_E_NOSYS);
    struct GfxCursorImage im = {.argb_pm = cursor_pixels,
        .bytes = sizeof(cursor_pixels),
        .pitch = TRI_TEST_CURSOR_SIDE * sizeof(u32),
        .width = TRI_TEST_CURSOR_SIDE, .height = TRI_TEST_CURSOR_SIDE};
    for (u32 y = 0; y < im.height; ++y)
        for (u32 x = 0; x < im.width; ++x)
            cursor_pixels[y * im.width + x] = x == y ? 0 :
                x < im.width / 2u ? TRI_TEST_BLACK : TRI_TEST_WHITE;
    int rc = o->cursor_image(TRI_TEST_HEAD, &im);
    optional(o, "cursor_two_color_transparent", rc);
    if (rc == TRI_E_NOSYS || rc == OS32_ERR_NOSYS) return rc;
    int result = 0; remember(&result, rc);
    if (!rc) {
        rc = o->cursor_show(TRI_TEST_HEAD, 1);
        optional(o, "cursor_show", rc); remember(&result, rc);
        for (u32 corner = 0; !rc && corner < 4u; ++corner) {
            i32 x = corner & 1u ? (i32)d->mode.client.width - 1 : 0;
            i32 y = corner & 2u ? (i32)d->mode.client.height - 1 : 0;
            number("TRI cursor_corner=", corner); serial_puts_polled("\r\n");
            rc = o->cursor_move(TRI_TEST_HEAD, x, y);
            optional(o, "cursor_move", rc); remember(&result, rc);
        }
    }
    int hidden = o->cursor_show(TRI_TEST_HEAD, 0);
    optional(o, "cursor_hide", hidden); remember(&result, hidden);
    /* An opaque third color must request SW fallback. The public SW cursor
     * renderer belongs to H; observe its prerequisite here without claiming
     * that an unconnected public renderer ran. */
    cursor_pixels[0] = TRI_TEST_THIRD;
    rc = o->cursor_image(TRI_TEST_HEAD, &im);
    optional(o, "cursor_sw_request", rc);
    int fallback = rc == OS32_ERR_NOSYS ? 0 : rc ? rc : TRI_E_STATE;
    emit(o, "cursor_sw_fallback", fallback, false);
    remember(&result, fallback);
    return result;
}
static int poll_video(const struct TridentDiagnosticOps *o)
{
    if (!o->poll) return optional(o, "video_poll", TRI_E_NOSYS);
    int rc = o->poll(o->context); /* F4 reads a fresh clock for this call. */
    return optional(o, "video_poll", rc);
}
static int mode(const struct TridentDiagnosticOps *o, u32 id)
{
    if (!o->set_mode) return optional(o, "mode", TRI_E_NOSYS);
    number("TRI requested_mode=", id); serial_puts_polled("\r\n");
    return optional(o, "mode", o->set_mode(id));
}
static int modes(const struct TridentDiagnosticOps *o)
{
    if (!o->set_mode) return optional(o, "modes", TRI_E_NOSYS);
    /* F10 exposes INDEX8, RGB565, XRGB8888. RGB555/packed24 await H.
     * F3's IDs are 1 + timing * TRI_FORMAT_COUNT + format, not table indices.
     * F10 may filter candidates by capacity: retain their rejection rc. */
    static const u8 formats[] = {TRI_FMT_INDEX8, TRI_FMT_RGB565, TRI_FMT_XRGB8888};
    u32 saved = o->context->mode.selected.id;
    int result = 0;
    for (u32 timing = 0; timing < TRI_TIMING_COUNT; ++timing) {
        for (u32 f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
            int rc = mode(o, 1u + timing * TRI_FORMAT_COUNT + formats[f]);
            if (rc == OS32_ERR_INVAL || rc == OS32_ERR_NOSYS) continue;
            remember(&result, rc);
            if (rc) goto restore;
            remember(&result, cpu32(o));
            remember(&result, cursor(o));
        }
    }
restore:
    remember(&result, mode(o, saved));
    return result;
}
/* Wire validation precedes borrowing the production device or changing it.
 * Like gfinit.c's gf_command_dispatch, copy a complete fixed-size request and
 * return the same rc in the reply and the call result. */
i32 trident_command_call(void *buf, u32 size)
{
    if (!buf || size != sizeof(struct tri_request)) return TRI_E_INVAL;
    struct tri_request q = *(const struct tri_request *)buf;
    result_count = 0;
    int rc;
    if (q.version != TRI_COMMAND_VERSION || q.size != sizeof(q) ||
        q.test > TRI_TEST_PRESENT || q.rc || q.result_count ||
        (q.test != TRI_TEST_MODE && q.arg) ||
        (q.test == TRI_TEST_MODE && (!q.arg || q.arg > TRI_MODE_COUNT))) {
        rc = TRI_E_INVAL;
        goto reply;
    }
    if (serial_gate_active()) {
        rc = TRI_E_BUSY;
        goto reply;
    }
    const struct TridentDiagnosticOps *o = trident_diagnostic_ops();
    if (!o || o->version != TRIDENT_DIAGNOSTIC_OPS_VERSION ||
        o->size != TRIDENT_DIAGNOSTIC_OPS_SIZE || !o->context) {
        rc = emit(NULL, "precheck", TRI_E_MODULE, true);
        goto reply;
    }
    if (!drawable(o->context)) {
        rc = emit(o, "precheck", TRI_E_STATE, true);
        goto reply;
    }
    switch (q.test) {
    case TRI_TEST_ALL:
        rc = modes(o);
        /* A failed mode change can leave the display inactive. The individual
         * adapters retain their own unavailable/error reports in that case. */
        remember(&rc, dpms(o));
        remember(&rc, poll_video(o));
        break;
    case TRI_TEST_MODES: rc = modes(o); break;
    case TRI_TEST_MODE: rc = mode(o, q.arg); break;
    case TRI_TEST_DPMS: rc = dpms(o); break;
    case TRI_TEST_CURSOR: rc = cursor(o); break;
    case TRI_TEST_CPU32: rc = cpu32(o); break;
    case TRI_TEST_POLL: rc = poll_video(o); break;
    case TRI_TEST_PRESENT: rc = present_compare(o); break;
    default: rc = TRI_E_INVAL; break;
    }
    optional(o, "end", rc);
reply:
    q.rc = rc;
    q.result_count = result_count;
    *(struct tri_request *)buf = q;
    return rc;
}
