/* HAL boundary; ScreenInit/EnterVT/LeaveVT/CloseScreen live in lifecycle.c.
 * MIT reference notices and source correspondence are in trident.h.
 * Loader calls prepare only after validating and pinning module text/data.
 */
#include "trident_module.h"
#include "gfx/gfx_internal.h"
#include "lib/serial_diag.h"
#include "lib/crc32_core.inc"
#include "kprintf.h"
#include "config.h"

static struct trident_device device;
static bool registers_pending;
static u32 register_entry;

static bool present_diag_done, present_error_done;

/* Post-mode snapshots only: SR0B data reads select New mode (MIT
 * trident_dac.c:918-920); Mode/Enable have already selected that mode.
 * Never enter Old mode (SR0B data write), unlock, or write register data.
 * Cursor: CR40..50; overlay: CR80..97/BE (trident_video.c:225-250,
 * 654-775). Keep selectors and diagnostic failures outside lifecycle state.
 * Six rows at each boundary, at most sixteen register values per row. */
static void diag_regs(const char *when)
{
    if (serial_gate_active()) return;
    static const struct { u16 port; const char *bank; u8 first, last; } spans[] = {
        {TRI_SR_INDEX, "SR", 0x0B, 0x0F},
        {TRI_GR_INDEX, "GR", 0x0E, 0x0F},
        {TRI_CR_COLOR, "CR", 0x0C, 0x0D},
        {TRI_CR_COLOR, "CR", 0x1E, 0x2F},
        {TRI_CR_COLOR, "CR", 0x38, 0x39},
        {TRI_CR_COLOR, "CR", 0x40, 0x5F},
        {TRI_CR_COLOR, "CR", 0x80, 0x97},
        {TRI_CR_COLOR, "CR", 0xBE, 0xBE},
        {TRI_EXT_DAC_INDEX, "XD", 0x00, 0x00},
        {TRI_EXT_DAC_INDEX, "XD", 0x50, 0x57}
    };
    u8 values[96] = {0};
    bool valid[96] = {0};
    u32 count = 0;
    int restore_rc = 0;
    for (u32 span = 0; span < sizeof(spans) / sizeof(spans[0]); ++span) {
        u8 selector = 0;
        u16 port = spans[span].port;
        /* Enable's final extended-DAC access selects 4 (route_ra266.c:90).
         * Palette/present never change it. Restore that known selector;
         * do not invent readback semantics for the write-only index port.
         * 83C8=index / 83C6=data follows regs.c:200-206 and VIDEOOUT. */
        bool ext = port == TRI_EXT_DAC_INDEX;
        int saved = 0;
        if (ext) selector = 4;
        else saved = trident_read8(&device, TRI_SPACE_REGS, port, &selector);
        for (u32 i = spans[span].first; i <= spans[span].last; ++i, ++count) {
            if (saved) continue;
            if (ext) {
                int rc = trident_write8(&device, TRI_SPACE_REGS, port, i);
                if (!rc) valid[count] = !trident_read8(&device, TRI_SPACE_REGS,
                                                     TRI_EXT_DAC_DATA, &values[count]);
            } else valid[count] = !trident_indexed_read(&device, port, i, &values[count]);
        }
        int rc = saved ? saved : trident_write8(&device, TRI_SPACE_REGS, port, selector);
        if (rc && !restore_rc) restore_rc = rc;
    }
    count = 0;
    for (u32 span = 0; span < sizeof(spans) / sizeof(spans[0]); ++span) {
        for (u32 i = spans[span].first; i <= spans[span].last; ++i, ++count) {
            if (!(count % 16)) {
                serial_puts_polled("[trident] diag regs gen=");
                serial_diag_u32(register_entry);
                serial_puts_polled(" when="); serial_puts_polled(when);
            }
            serial_puts_polled(" "); serial_puts_polled(spans[span].bank);
            serial_diag_hex(i, 2); serial_puts_polled("=");
            if (valid[count]) serial_diag_hex(values[count], 2);
            else serial_puts_polled("??");
            if (count % 16 == 15) serial_puts_polled("\n");
        }
    }
    /* The final short row also carries selector restoration failures. */
    serial_puts_polled(" restore_rc="); serial_diag_i32(restore_rc);
    serial_puts_polled("\n");
}

/* Use the same byte-access seam as present; never probe or write the LFB.
 * The full fixed surface is compared even if the first present is partial. */
static void diag_after_present(void)
{
    if (present_diag_done) return;
    present_diag_done = true;
    diag_regs("present");
    const struct trident_resources *r = device.res;
    if (r->client_bytes < TRI_DISPLAY_BYTES || r->mapped_bytes < TRI_DISPLAY_BYTES ||
        r->decode_bytes < TRI_DISPLAY_BYTES || device.verified_bytes < TRI_DISPLAY_BYTES) {
        kprintf(KLOG_ATTR, "[trident] diag readback gen=%u skipped=range\n", register_entry);
        return;
    }
    u32 client_crc = CRC32_INIT, lfb_crc = CRC32_INIT;
    u32 mismatch_count = 0, read_bytes = 0;
    u32 offsets[4] = {0};
    u8 clients[4] = {0}, lfbs[4] = {0};
    int rc = 0;
    for (u32 off = 0; off < TRI_DISPLAY_BYTES; ++off) {
        u8 value, source = r->client[off];
        rc = trident_read8(&device, TRI_SPACE_LFB, off, &value);
        if (rc) break;
        client_crc = crc32_core_update(client_crc, &source, 1);
        lfb_crc = crc32_core_update(lfb_crc, &value, 1);
        ++read_bytes;
        if (source != value) {
            if (mismatch_count < 4) {
                offsets[mismatch_count] = off;
                clients[mismatch_count] = source;
                lfbs[mismatch_count] = value;
            }
            ++mismatch_count;
        }
    }
    kprintf(KLOG_ATTR, "[trident] diag crc gen=%u bytes=%u rc=%d client=%08x lfb=%08x\n",
            register_entry, read_bytes, rc,
            crc32_core_final(client_crc), crc32_core_final(lfb_crc));
    /* Count covers read_bytes; rc distinguishes a partial scan. */
    kprintf(KLOG_ATTR, "[trident] diag mismatch gen=%u count=%u bytes=%u rc=%d\n",
            register_entry, mismatch_count, read_bytes, rc);
    for (u32 i = 0; i < mismatch_count && i < 4; ++i)
        kprintf(KLOG_ATTR, "[trident] diag mismatch gen=%u n=%u offset=%u client=%02x lfb=%02x\n",
                register_entry, i, offsets[i], clients[i], lfbs[i]);
    /* Band interiors at x=32 avoid the text box; (340,260) is the
     * approximate white-rectangle center requested for the hardware trial. */
    static const u16 points[][2] = {
        {0, 0}, {320, 240}, {32, 30}, {32, 90}, {32, 150},
        {32, 210}, {32, 270}, {32, 350}, {340, 260}
    };
    for (u32 i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
        u32 off = points[i][1] * TRI_PITCH + points[i][0];
        u8 value = 0;
        int sample_rc = trident_read8(&device, TRI_SPACE_LFB, off, &value);
        kprintf(KLOG_ATTR, "[trident] diag sample gen=%u x=%u y=%u client=%02x lfb=%02x rc=%d\n",
                register_entry, points[i][0], points[i][1], r->client[off], value, sample_rc);
    }
}

static void diag_present_error(int rc, int x, int y, int w, int h)
{
    if (!rc || present_error_done) return;
    present_error_done = true;
    u32 end = 0;
    if (gfx_clip_screen(&x, &y, &w, &h, TRI_WIDTH, TRI_HEIGHT))
        end = ((u32)y + (u32)h - 1u) * TRI_PITCH + (u32)x + (u32)w;
    const struct trident_resources *r = device.res;
    kprintf(KLOG_ATTR, "[trident] diag present gen=%u rc=%d end=%u verified_bytes=%u mapped_bytes=%u decode_bytes=%u client_bytes=%u\n",
            register_entry, rc, end, device.verified_bytes,
            r->mapped_bytes, r->decode_bytes, r->client_bytes);
}

/* Diagnostic only: indexed data is never written, no unlock/probe/VRAM test.
 * Mode has already selected New mode, so reading SR0B keeps that mode.
 * AR selections retain PAS=1 throughout; an ordinary AR read with PAS=0
 * would blank the display. Restore selectors and leave AR in address phase.
 * Values are observations, not new initialization success conditions. */
static void register_row(const char *group, u32 first, const u8 *values,
                         const bool *valid, u32 count)
{
    serial_puts_polled("[trident] stage=registers entry=");
    serial_diag_u32(register_entry);
    serial_puts_polled(" "); serial_puts_polled(group);
    serial_diag_hex(first, first > 0xffu ? 8 : 2); serial_puts_polled("=");
    for (u32 i = 0; i < count; ++i) {
        if (i) serial_puts_polled(" ");
        if (valid[i]) serial_diag_hex(values[i], 2);
        else serial_puts_polled("??");
    }
    serial_puts_polled("\n");
}

static void register_bank(u16 port, const char *group, u32 first, u32 last,
                          u8 *values, bool *valid)
{
    u8 selector;
    int saved = trident_read8(&device, TRI_SPACE_REGS, port, &selector);
    for (u32 i = first; i <= last; ++i)
        valid[i] = !trident_indexed_read(&device, port, i, &values[i]);
    int restored = saved ? saved :
        trident_write8(&device, TRI_SPACE_REGS, port, selector);
    for (u32 i = first; i <= last; i += 16) {
        u32 count = last - i + 1;
        if (count > 16) count = 16;
        register_row(group, i, values + i, valid + i, count);
    }
    if (restored) {
        serial_puts_polled("[trident] register selector rc=");
        serial_diag_i32(restored); serial_puts_polled("\n");
    }
}

/* vgaHW.c:876-896,1121-1126: DAC components are raw 6-bit values;
 * DACDelay uses two current-ST01 reads. Only select the read address here,
 * never write the mask or RGB data. Reading 3C8 first exits hidden-DAC access.
 * The DAC read cursor ends at color 16; VGA cannot read back that cursor.
 * Every palette writer explicitly selects its own write address. */
static void palette_before_present(void)
{
    u8 selector = 0, mask = 0, ignored;
    u8 rgb[GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP) * GFX_RGB_COMPONENTS] = {0};
    bool valid[sizeof(rgb)] = {0};
    int saved = trident_read8(&device, TRI_SPACE_REGS, TRI_DAC_WRITE_INDEX, &selector);
    int rc = saved ? saved : trident_read8(&device, TRI_SPACE_REGS, TRI_DAC_MASK, &mask);
    bool mask_valid = !rc;
    register_row("DAC.MASK", 0, &mask, &mask_valid, 1);
    if (!rc) rc = trident_write8(&device, TRI_SPACE_REGS, TRI_DAC_READ_INDEX, 0);
    for (u32 i = 0; i < sizeof(rgb) && !rc; ++i) {
        rc = trident_read8(&device, TRI_SPACE_REGS, TRI_DAC_DATA, &rgb[i]);
        if (!rc) rc = trident_read8(&device, TRI_SPACE_REGS, device.cur_vga_base + 6u, &ignored);
        if (!rc) rc = trident_read8(&device, TRI_SPACE_REGS, device.cur_vga_base + 6u, &ignored);
        valid[i] = !rc;
    }
    int restore = saved ? saved :
        trident_write8(&device, TRI_SPACE_REGS, TRI_DAC_WRITE_INDEX, selector);
    for (u32 i = 0; i < GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP); ++i)
        register_row("DAC", i, rgb + i * GFX_RGB_COMPONENTS,
                     valid + i * GFX_RGB_COMPONENTS, GFX_RGB_COMPONENTS);
    if (rc || restore) {
        serial_puts_polled("[trident] register DAC rc=");
        serial_diag_i32(rc ? rc : restore); serial_puts_polled("\n");
    }
}

static void registers_before_present(void)
{
    if (!registers_pending || serial_gate_active()) return;
    registers_pending = false;
    u8 sr[16] = {0}, cr[96] = {0}, gr[16] = {0}, ar[21] = {0};
    bool sv[16] = {0}, cv[96] = {0}, gv[16] = {0}, av[21] = {0};
    /* Same bank/table order as H: SR, standard CR, extended CR, GR, AR.
     * Extended CR starts at 19, kept separate from the standard 00..18. */
    register_bank(TRI_SR_INDEX, "SR", 0, 15, sr, sv);
    register_bank(device.cur_vga_base, "CR", 0, 24, cr, cv);
    register_bank(device.cur_vga_base, "CR", 25, 95, cr, cv);
    register_bank(TRI_GR_INDEX, "GR", 0, 15, gr, gv);
    u8 selector = 0, ignored;
    int saved = trident_read8(&device, TRI_SPACE_REGS, TRI_AR_INDEX, &selector);
    for (u32 i = 16; i <= 20; ++i)
        av[i] = !trident_ar_read(&device, device.cur_vga_base, i | 0x20, &ar[i]);
    int reset = trident_read8(&device, TRI_SPACE_REGS, device.cur_vga_base + 6u, &ignored);
    int restore = trident_write8(&device, TRI_SPACE_REGS, TRI_AR_INDEX,
                                 saved ? 0x20 : selector);
    int phase = trident_read8(&device, TRI_SPACE_REGS, device.cur_vga_base + 6u, &ignored);
    register_row("AR", 16, ar + 16, av + 16, 5);
    if (saved || reset || restore || phase) {
        serial_puts_polled("[trident] register AR selector rc=");
        serial_diag_i32(saved ? saved : reset ? reset : restore ? restore : phase);
        serial_puts_polled("\n");
    }
    u8 misc = 0;
    bool valid = !trident_read8(&device, TRI_SPACE_REGS, TRI_MISC_READ, &misc);
    register_row("MISC", 0, &misc, &valid, 1);
    const u8 indices[] = {0x0C, 0x0D, 0x1E, 0x27};
    for (u32 i = 0; i < sizeof(indices); ++i) {
        u8 index = indices[i];
        register_row("START.CR", index, cr + index, cv + index, 1);
    }
    palette_before_present();
    /* Read the actual transfer source, including background and text-area
     * samples. Offsets are byte offsets in the fixed 640x480 packed CLIENT.
     * register_row prints the full RAM offset for samples beyond byte 255. */
    const u32 offsets[] = {0, 160u * TRI_PITCH,
                           168u * TRI_PITCH + 172u, 240u * TRI_PITCH + TRI_WIDTH / 2};
    for (u32 row = 0; row < sizeof(offsets) / sizeof(offsets[0]); ++row) {
        u8 values[8];
        bool valid[8];
        const volatile u8 *src = device.res->client + offsets[row];
        for (u32 i = 0; i < sizeof(values); ++i) {
            values[i] = src[i];
            valid[i] = true;
        }
        register_row("CLIENT", offsets[row], values, valid, sizeof(values));
    }
}


static void report(u32 stage, int rc)
{
    if (!rc || serial_gate_active()) return;
    serial_puts_polled("[trident] stage="); serial_diag_u32(stage);
    serial_puts_polled(" rc="); serial_diag_i32(rc);
    serial_puts_polled(" restore_rc="); serial_diag_i32(device.restore_rc);
    serial_puts_polled(" disable_rc="); serial_diag_i32(device.disable_rc);
    serial_puts_polled("\n");
}

static int remember(u32 stage, int rc)
{
    if (rc && !device.last_rc) {
        device.last_rc = rc;
        device.failed_step = stage;
    }
    return rc;
}

int trident_backend_prepare(void)
{
    if (device.last_rc) return device.last_rc;
    const TridentModuleContext *c = trident_context;
    if (!c || !c->backend || !c->counters || !c->resources || !c->arm ||
        !c->height || !c->flip || !c->page || !c->display_owner)
        return remember(TRI_STEP_RESOURCES, TRI_E_INVAL);
    const struct trident_resources *r = c->resources;
    if (!r->ready || !r->regs || !r->lfb || !r->client ||
        r->client_bytes < TRI_DISPLAY_BYTES || r->mapped_bytes < TRI_DISPLAY_BYTES ||
        r->decode_bytes < TRI_DISPLAY_BYTES || r->regs_bytes < TRI_REG_MAPPED_BYTES)
        return remember(TRI_STEP_RESOURCES, TRI_E_RESOURCE);
    int rc = trident_pci_validate(&r->pci);
    if (rc) return remember(TRI_STEP_RESOURCES, rc);
    device.res = r;
    rc = trident_regs_bind(&device); /* descriptor-only, no hardware access */
    if (rc) return remember(TRI_STEP_RESOURCES, rc);
    device.pinned = true; /* loader's prepare boundary guarantees the pin */
    if (!device.armed) device.step = TRI_STEP_PIN;
    c->backend->bb_base = r->client;
    c->backend->bb_size = r->client_bytes;
    return 0;
}
static void prepare(void) { int rc = trident_backend_prepare(); report(device.failed_step, rc); }
static int probe(void) { return !device.last_rc && device.pinned && device.res && device.res->ready; }

static void finish(u32 stage, int rc, bool recover)
{
    if (!rc) return;
    remember(stage, rc);
    if (recover) {
        int recovery = trident_restore98(&device);
        /* Restore/Disable outcomes are retained separately by lifecycle. */
        if (recovery && !device.last_rc) remember(TRI_STEP_RESTORE, recovery);
    }
    report(device.failed_step, rc);
}
static void init(void)
{
    int rc = trident_backend_prepare();
    if (rc) { finish(device.failed_step, rc, false); return; }
    if (!device.armed) {
        device.display_owner = (u32)trident_context->display_owner();
        rc = trident_context->arm(trident_restore98, &device, device.display_owner);
        if (rc) { finish(TRI_STEP_ARM, rc, false); return; }
        device.armed = true;
        device.step = TRI_STEP_ARM;
    }
    rc = trident_screen_init(&device);
    if (rc) { finish(device.failed_step, rc, true); return; }
    ++register_entry;
    diag_regs("init");
    present_diag_done = present_error_done = false;
    kprintf(KLOG_ATTR, "[trident] diag buffer gen=%u bb=%08x client=%08x\n",
            register_entry, (u32)(uptr)trident_context->backend->bb_base,
            (u32)(uptr)device.res->client);
    registers_pending = true;
    *trident_context->height = TRI_HEIGHT;
    *trident_context->flip = *trident_context->page = 0;
}
static int query(GFX_ScreenInfo *info)
{
    if (device.last_rc) return device.last_rc;
    if (!info) return TRI_E_INVAL;
    if (!probe()) return TRI_E_STATE;
    *info = (GFX_ScreenInfo){0};
    info->width = TRI_WIDTH; info->height = TRI_HEIGHT;
    info->bpp = GFX_FMT_PACKED8_BPP; info->format = GFX_FMT_PACKED8;
    info->lease_first = GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP);
    info->lease_count = TRI_DAC_COLORS - info->lease_first;
    return 0;
}
static void enter(void)
{
    bool was_active = device.active;
    int rc = trident_enter(&device);
    if (!rc && !was_active) { ++register_entry; registers_pending = true; }
    finish(device.failed_step ? device.failed_step : TRI_STEP_MODE, rc, true);
}
/* No serial/console output inside the emergency callback trident_restore98. */
static void leave(void) { int rc = trident_leave(&device); finish(device.failed_step, rc, false); }
static void shutdown(void) { int rc = trident_close(&device); finish(device.failed_step, rc, false); }
static void present(int x, int y, int w, int h)
{
    if (device.last_rc || !device.active) return;
    registers_before_present();
    int rc = trident_present(&device, x, y, w, h);
    diag_present_error(rc, x, y, w, h);
    if (rc) { finish(TRI_STEP_ACTIVE, rc, true); return; }
    diag_after_present();
    if (gfx_clip_screen(&x, &y, &w, &h, TRI_WIDTH, TRI_HEIGHT)) {
        trident_context->counters->present_bytes += (u32)w * (u32)h;
        ++trident_context->counters->commits;
    }
}
static void set_palette(int first, int count, const u8 *rgb)
{
    if (device.last_rc || !device.active) return;
    int rc = trident_palette(&device, first, count, rgb);
    if (rc) { finish(TRI_STEP_CLIENT, rc, true); return; }
    for (int i = 0; i < count && first + i < (int)GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP); ++i)
        palette_shadow_set(first + i, rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
}
static const GfxMode modes[] = {
    {TRI_WIDTH, TRI_HEIGHT, GFX_FMT_PACKED8, 6, 0, 0, TRI_PITCH}
};
GfxBackend trident_backend_ops = {
    .name = "trident", .probe = probe, .prepare = prepare, .init = init,
    .query = query, .shutdown = shutdown, .present_rect = present,
    .set_palette = set_palette, .enter = enter, .leave = leave,
    .bb_pitch = TRI_PITCH, .bb_format = GFX_BB_PACKED8,
    .modes = modes, .mode_count = sizeof(modes) / sizeof(modes[0])
};
