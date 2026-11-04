/*
 * Copyright 2012 Nouveau Community
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Martin Peres
 */
/* GF_FEAT5_DESIGN final A2/C4 contract and PM decision 16.
 * ROM metadata follows nouveau bios/perf.c:30-77 and vmap.c:29-53.
 * Hardware sequences remain in module ops.
 * Never retain a module callback/table across a call. Only caller-owned
 * image/plan data and owner identity tokens survive a diagnostic command. */
#include "gf5_test.h"
#include "gf_command_wire.h"
#include "../hwdev/gf/feature5.h"
#include "../hwdev/gf/command.h"
#include "../hwdev/gf/foundation.h"
#include "gfinit_decode.h"
#include "../drivers/video/geforce/geforce_module.h"
#include "../lib/serial_diag.h"
#define GF_ROM_BIT_ONLY
#include "../hwdev/gf/rom.inc"
#undef GF_ROM_BIT_ONLY

static struct {
    u32 arg[GF5_WIRE_OPERANDS];
    struct gf5_plan plan;
    struct gf5 *plan_owner, *image_owner;
    struct gf5_image *image;
    bool planned;
} diagnostic;

static void number(const char *key, u32 value)
{
    serial_puts_polled(key); serial_diag_u32(value);
}
static void signed_number(const char *key, i32 value)
{
    serial_puts_polled(key); serial_diag_i32(value);
}
static void clock_record(const char *stage, const struct gf5_clock *clock)
{
    if (serial_gate_active()) return;
    serial_puts_polled("GF5 DATA stage="); serial_puts_polled(stage);
    number(" core_khz=", clock->core_khz);
    number(" shader_khz=", clock->shader_khz);
    number(" mem_khz=", clock->mem_khz);
    serial_puts_polled("\r\n");
}
/* rc/source are copied while the slot is live. A second dispatch, even a
 * last_rc query, obtains the accessor again. No logging dereferences a loan. */
static i32 result_record(u32 stage, i32 rc, i32 last, i32 source)
{
    i32 result = rc == GF5_ENOTSUP ? 77 : rc ? 1 : 0;
    if (!serial_gate_active()) {
        serial_puts_polled("GF5 ");
        serial_puts_polled(result == 77 ? "SKIP" : result ? "FAIL" : "PASS");
        number(" stage=", stage); signed_number(" rc=", rc);
        signed_number(" last_rc=", last); signed_number(" source_rc=", source);
        number(" result=", result); serial_puts_polled("\r\n");
    }
    return result;
}
static i32 finish(u32 stage, i32 rc, bool local)
{
    i32 last = rc, source = rc;
    const struct gf5_test_ops *o = geforce_feature5_ops();
    if (o && o->ctx) {
        source = local ? rc : o->ctx->source_rc;
        last = o->ctx->last_rc;
    }
    return result_record(stage, rc, last, source);
}
/* A single module call per expansion; callers must not use o afterwards. */
#define CALL(member, ...) do { \
    const struct gf5_test_ops *o = geforce_feature5_ops(); \
    if (!o || !o->ctx || !o->member) { \
        local = true; rc = GF5_ENOTSUP; \
    } else rc = o->member(o->ctx, ##__VA_ARGS__); \
} while (0)

static i32 poll(bool *local)
{
    const struct gf5_test_ops *o = geforce_feature5_ops();
    if (!o || !o->ctx || !o->service_poll) {
        *local = true; return GF5_ENOTSUP;
    }
    return o->service_poll(o->ctx);
}
/* Cooperative diagnostic wait: service_poll owns clock64 -> fan_tick.
 * Each timing loan is reacquired after poll/wait, never taken from a saved
 * ops pointer. The native wait contract limits each call to 1000 us. */
static i32 diagnostic_wait(u32 us, i32 *source, bool *local)
{
    while (us) {
        i32 rc = poll(local);
        if (rc) return rc;
        const struct gf5_test_ops *o = geforce_feature5_ops();
        if (!o || !o->ctx) return *source = GF5_ENOTSUP;
        if (!o->ctx->ch || !o->ctx->ch->f) return *source = GF5_ESTATE;
        struct gf_foundation *f = o->ctx->ch->f;
        if (!f->io.timing.wait_us) return *source = GF5_ENOTSUP;
        u32 part = us > GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : us;
        rc = f->io.timing.wait_us(f->io.opaque, part);
        if (rc) { *source = rc; return GF5_EIO; }
        us -= part;
    }
    return poll(local);
}
/* ROM field layouts: nouveau c03c1a1 nvkm/subdev/bios/perf.c:30-77,
 * vmap.c:29-53; BMP signature: bios/base.c:181-182. BIT directory bounds
 * are shared with the existing ROM decoder through gf_rom_bit_entry. */
enum {
    INFO_LE16_BYTES = 2, INFO_LE32_BYTES = 4,
    INFO_BIT_P_V1 = 1, INFO_BIT_P_V2 = 2,
    INFO_P_PERF = 0, INFO_P_VMAP = 0x20,
    INFO_BMP_VERSION = 6, INFO_BMP_PERF_VERSION = 0x25,
    INFO_BMP_PERF = 0x94,
    INFO_TABLE_HEADER_BYTES = 4, INFO_PERF_BIT_HEADER_BYTES = 6,
    INFO_TABLE_VERSION = 0, INFO_TABLE_HEADER = 1,
    INFO_PERF_COUNT = 2, INFO_PERF_LENGTH = 3,
    INFO_PERF_SUBCOUNT = 4, INFO_PERF_SUBSIZE = 5,
    INFO_PERF_V20 = 0x20, INFO_PERF_V40 = 0x40,
    INFO_VMAP_V10 = 0x10, INFO_VMAP_V20 = 0x20,
    INFO_VMAP_LENGTH = 2, INFO_VMAP_COUNT = 3
};
static bool rom_span(const struct gf5_rom_view *rom, u32 off, u32 bytes)
{
    return rom->bytes && off <= rom->size && bytes <= rom->size - off;
}
/* Callers check the complete field before reading. */
static u32 rom_le(const struct gf5_rom_view *rom, u32 off, u32 bytes)
{
    u32 value = 0;
    for (u32 i = 0; i < bytes; ++i)
        value |= (u32)rom->bytes[off + i] << (i * BITS_PER_BYTE);
    return value;
}
static i32 table_record(const struct gf5_rom_view *rom, const char *table,
                        u32 off, bool bmp, bool vmap)
{
    u32 version = 0;
    i32 rc = 0;
    if (off) {
        if (!rom_span(rom, off, 1)) rc = GF5_EBIOS;
        else {
            u32 version_off = bmp ? INFO_TABLE_HEADER : INFO_TABLE_VERSION;
            if (!rom_span(rom, off, version_off + 1U)) rc = GF5_EBIOS;
            else {
                version = rom->bytes[off + version_off];
                bool known = bmp || (vmap ?
                    version == INFO_VMAP_V10 || version == INFO_VMAP_V20 :
                    version >= INFO_PERF_V20 && version <= INFO_PERF_V40);
                if (!known) rc = GF5_ENOTSUP;
                else {
                    u32 minimum = vmap || bmp ? INFO_TABLE_HEADER_BYTES :
                                                           INFO_PERF_BIT_HEADER_BYTES;
                    if (!rom_span(rom, off, minimum)) rc = GF5_EBIOS;
                    else {
                        const u8 *t = rom->bytes + off;
                        u32 head = t[bmp ? INFO_TABLE_VERSION : INFO_TABLE_HEADER];
                        u32 count = t[vmap ? INFO_VMAP_COUNT : INFO_PERF_COUNT];
                        u32 stride = t[vmap ? INFO_VMAP_LENGTH : INFO_PERF_LENGTH];
                        if (!vmap && !bmp) {
                            /* v40 reorders count/length/subsize (perf.c:43-48). */
                            if (version == INFO_PERF_V40) {
                                count = t[INFO_PERF_SUBSIZE];
                                stride = t[INFO_PERF_COUNT] +
                                    (u32)t[INFO_PERF_SUBCOUNT] * t[INFO_PERF_LENGTH];
                            } else stride += (u32)t[INFO_PERF_SUBCOUNT] *
                                                                     t[INFO_PERF_SUBSIZE];
                        }
                        if (head < minimum || (count && !stride) ||
                            !rom_span(rom, off, head + count * stride)) rc = GF5_EBIOS;
                    }
                }
            }
        }
    }
    if (!serial_gate_active()) {
        serial_puts_polled("GF5 DATA stage=info table="); serial_puts_polled(table);
        number(" offset=", off); number(" version=", version);
        /* Unknown or corrupt tables are present, never labelled absent. */
        number(" absent=", off == 0); number(" bmp=", bmp);
        signed_number(" rc=", rc); serial_puts_polled("\r\n");
    }
    return rc;
}
static i32 rom_records(const struct gf5_rom_view *rom)
{
    if (!rom->bytes) return GF5_ENOTSUP; /* PM decision 8: disabled view. */
    u32 data = 0, length = 0, perf = 0, vmap = 0;
    u8 version = 0;
    bool bmp = false;
    int found = gf_rom_bit_entry(rom->bytes, rom->size, 'P',
                                 &data, &length, &version);
    if (found < 0) return GF5_EBIOS;
    if (!found && (version == INFO_BIT_P_V1 || version == INFO_BIT_P_V2)) {
        if (length < INFO_P_PERF + INFO_LE32_BYTES) return GF5_EBIOS;
        perf = rom_le(rom, data + INFO_P_PERF, INFO_LE32_BYTES);
        if (version == INFO_BIT_P_V2) {
            if (length < INFO_P_VMAP + INFO_LE32_BYTES) return GF5_EBIOS;
            vmap = rom_le(rom, data + INFO_P_VMAP, INFO_LE32_BYTES);
        }
    } else if (!found) return GF5_ENOTSUP;
    /* perf.c:60-77: BMP is the fallback when BIT P has no perf pointer. */
    if (!perf) {
        static const u8 signature[] = {0xff, 0x7f, 'N', 'V', 0};
        for (u32 off = 0; rom_span(rom, off, sizeof(signature)); ++off) {
            u32 i = 0;
            while (i < sizeof(signature) && rom->bytes[off + i] == signature[i]) ++i;
            if (i != sizeof(signature)) continue;
            if (!rom_span(rom, off, INFO_BMP_VERSION + 1U)) return GF5_EBIOS;
            if (rom->bytes[off + INFO_BMP_VERSION] >= INFO_BMP_PERF_VERSION) {
                if (!rom_span(rom, off, INFO_BMP_PERF + INFO_LE16_BYTES)) return GF5_EBIOS;
                perf = rom_le(rom, off + INFO_BMP_PERF, INFO_LE16_BYTES);
                bmp = perf != 0;
            }
            break;
        }
    }
    i32 perf_rc = table_record(rom, "perf", perf, bmp, false);
    i32 vmap_rc = table_record(rom, "vmap", vmap, false, true);
    if (perf_rc == GF5_EBIOS || vmap_rc == GF5_EBIOS) return GF5_EBIOS;
    /* Unsupported versions are logged; perf_count/get owns entry support.
     * vmap absence is normal and must not hide the performance entries. */
    return 0;
}
static i32 info(bool *local)
{
    const struct gf5_test_ops *o = geforce_feature5_ops();
    if (!o || !o->ctx) { *local = true; return GF5_ENOTSUP; }
    if (!serial_gate_active()) {
        const struct gf5 *g = o->ctx;
        serial_puts_polled("GF5 DATA stage=info target=0:13.0/10de:0221");
        number(" phase=", g->phase); number(" accepting=", g->accepting);
        number(" rom_bytes=", g->rom.size); number(" memory_pc=", g->rom.memory_pc);
        number(" fan_method=", g->fan.method); number(" fan_mode=", g->fan.mode);
        number(" fan_target=", g->fan.target); number(" fan_duty=", g->fan.duty);
        signed_number(" slope_mult=", g->fan.slope_mult);
        signed_number(" slope_div=", g->fan.slope_div);
        signed_number(" offset_num=", g->fan.offset_num);
        signed_number(" offset_den=", g->fan.offset_den);
        signed_number(" offset_constant=", g->fan.offset_constant);
        number(" image_held=", diagnostic.image != NULL);
        serial_puts_polled("\r\n");
    }
    i32 rc = rom_records(&o->ctx->rom);
    if (rc) { *local = true; return rc; }
    /* Enumerate the complete ROM performance table, not only the selected
     * state. Every get rereads the accessor after the preceding call. */
    o = geforce_feature5_ops();
    if (!o || !o->ctx || !o->perf_count) {
        *local = true; return GF5_ENOTSUP;
    }
    u32 count;
    rc = o->perf_count(o->ctx, &count);
    if (rc) return rc;
    for (u32 i = 0; i < count; ++i) {
        struct gf5_perf_entry entry;
        o = geforce_feature5_ops();
        if (!o || !o->ctx || !o->perf_get) {
            *local = true; return GF5_ENOTSUP;
        }
        rc = o->perf_get(o->ctx, i, &entry);
        if (rc) return rc;
        if (!serial_gate_active()) {
            serial_puts_polled("GF5 DATA stage=perf"); number(" index=", i);
            number(" voltage_id=", entry.voltage); number(" fanspeed=", entry.fanspeed);
            number(" core_khz=", entry.clock.core_khz);
            number(" shader_khz=", entry.clock.shader_khz);
            number(" mem_khz=", entry.clock.mem_khz); serial_puts_polled("\r\n");
        }
    }
    return 0;
}

i32 gf5_test(u32 session)
{
    u32 stage = session & GF5_WIRE_BYTE_MASK;
    u32 *a = diagnostic.arg;
    i32 rc = 0;
    bool local = false;
    if (stage == GF5T_ARG_LO || stage == GF5T_ARG_HI) {
        u32 index = (session >> GF5_WIRE_INDEX_SHIFT) & GF5_WIRE_BYTE_MASK;
        u32 value = session >> GF5_WIRE_VALUE_SHIFT;
        if (index >= GF5_WIRE_OPERANDS) return finish(stage, GF5_EINVAL, true);
        a[index] = stage == GF5T_ARG_LO ?
            (a[index] & ~GF5_WIRE_HALF_MASK) | value :
            (a[index] & GF5_WIRE_HALF_MASK) | (value << GF5_WIRE_VALUE_SHIFT);
        diagnostic.planned = false;
        return finish(stage, 0, true);
    }
    if ((session & ~GF5_WIRE_BYTE_MASK) || stage > GF5T_LAST_RC)
        return finish(stage, GF5_EINVAL, true);
    const struct gf5_test_ops *o = geforce_feature5_ops();
    if (!o || !o->ctx) return finish(stage, GF5_ENOTSUP, true);
    if (o->ctx->phase != GF5_RUN && stage != GF5T_RESTORE &&
        stage != GF5T_FREE && stage != GF5T_LAST_RC)
        return finish(stage, GF5_ESTATE, true);
    switch (stage) {
    case GF5T_INFO: rc = info(&local); break;
    case GF5T_CLOCK_READ: {
        struct gf5_clock clock;
        CALL(clock_read, &clock);
        if (!rc) clock_record("clock_read", &clock);
        break;
    }
    case GF5T_CLOCK_CALC: {
        struct gf5_clock clock = {a[0], a[1], a[2]};
        diagnostic.planned = false;
        o = geforce_feature5_ops();
        if (!o || !o->ctx || !o->clock_calc) { rc = GF5_ENOTSUP; local = true; break; }
        diagnostic.plan_owner = o->ctx;
        rc = o->clock_calc(o->ctx, &clock, &diagnostic.plan);
        if (!rc) {
            diagnostic.planned = true;
            clock_record("requested", &clock);
            clock_record("actual", &diagnostic.plan.actual);
            if (!serial_gate_active()) {
                serial_puts_polled("GF5 DATA stage=plan");
                number(" nc=", diagnostic.plan.nc); number(" nn=", diagnostic.plan.nn);
                number(" sc=", diagnostic.plan.sc); number(" mux=", diagnostic.plan.mux);
                number(" mc=", diagnostic.plan.mc); number(" mn=", diagnostic.plan.mn);
                serial_puts_polled("\r\n");
            }
        }
        break;
    }
    case GF5T_CLOCK_PROG:
    case GF5T_RAM_PROG:
        o = geforce_feature5_ops();
        if (!o || !o->ctx || !diagnostic.planned || o->ctx != diagnostic.plan_owner) {
            rc = GF5_ESTATE; local = true; break;
        }
        if (stage == GF5T_CLOCK_PROG) { CALL(clock_prog, &diagnostic.plan); }
        else { CALL(ram_prog, &diagnostic.plan); }
        break;
    case GF5T_PERF_COUNT:
    case GF5T_FAN_GET:
    case GF5T_VOLT_GET:
    case GF5T_TILE_FIND: {
        u32 value = 0;
        if (stage == GF5T_PERF_COUNT) { CALL(perf_count, &value); }
        else if (stage == GF5T_FAN_GET) { CALL(fan_get, &value); }
        else if (stage == GF5T_VOLT_GET) { CALL(volt_get, &value); }
        else { CALL(tile_find, &value); }
        if (!rc && !serial_gate_active()) {
            serial_puts_polled("GF5 DATA"); number(" stage=", stage);
            /* volt_get is VID/table-derived requested uV, never ADC. */
            number(stage == GF5T_VOLT_GET ? " requested_uv=" : " value=", value);
            serial_puts_polled("\r\n");
        }
        break;
    }
    case GF5T_PERF_GET: {
        struct gf5_perf_entry entry;
        CALL(perf_get, a[0], &entry);
        if (!rc) {
            clock_record("perf_get", &entry.clock);
            if (!serial_gate_active()) {
                serial_puts_polled("GF5 DATA stage=perf_get"); number(" index=", a[0]);
                number(" voltage_id=", entry.voltage); number(" fanspeed=", entry.fanspeed);
                serial_puts_polled("\r\n");
            }
        }
        break;
    }
    case GF5T_PERF_SELECT:
        if (a[0] > U8_MAX) { rc = GF5_EINVAL; local = true; }
        else { CALL(perf_select, (u8)a[0]); }
        break;
    case GF5T_THERM: {
        struct gf5_thermal thermal;
        CALL(therm_read, &thermal);
        if (!rc && !serial_gate_active()) {
            serial_puts_polled("GF5 DATA stage=therm"); number(" raw=", thermal.raw);
            signed_number(" milli_c=", thermal.milli_c);
            number(" calibrated=", thermal.calibrated); serial_puts_polled("\r\n");
        }
        break;
    }
    case GF5T_FAN_MODE:
        if (a[0] > GF5_FAN_AUTO) { rc = GF5_EINVAL; local = true; }
        else { CALL(fan_mode_set, (enum gf5_fan_mode)a[0]); }
        break;
    case GF5T_FAN_CSTATE:
        if (a[0] > 100U) { rc = GF5_EINVAL; local = true; }
        else { CALL(fan_cstate, (u8)a[0], (i32)a[1]); }
        break;
    case GF5T_VOLT_SET: CALL(volt_set, a[0]); break;
    case GF5T_VOLT_ID:
        if (a[0] > U8_MAX || a[1] > U8_MAX) { rc = GF5_EINVAL; local = true; }
        else { CALL(volt_id, (u8)a[0], (u8)a[1], (i32)a[2]); }
        break;
    case GF5T_GPIO_FIND: {
        u8 line, log[2];
        if (a[0] > U8_MAX) { rc = GF5_EINVAL; local = true; break; }
        CALL(gpio_find, (u8)a[0], &line, log);
        if (!rc && !serial_gate_active()) {
            serial_puts_polled("GF5 DATA stage=gpio_find"); number(" line=", line);
            number(" log0=", log[0]); number(" log1=", log[1]); serial_puts_polled("\r\n");
        }
        break;
    }
    case GF5T_GPIO_GET:
    case GF5T_GPIO_SET: {
        bool value;
        if (a[0] > U8_MAX || a[1] > U8_MAX || a[2] > U8_MAX ||
            (stage == GF5T_GPIO_SET && a[3] > 1U)) {
            rc = GF5_EINVAL; local = true; break;
        }
        u8 log[2] = {a[1], a[2]};
        if (stage == GF5T_GPIO_SET) { CALL(gpio_set, (u8)a[0], log, a[3] != 0); }
        else {
            CALL(gpio_get, (u8)a[0], log, &value);
            if (!rc && !serial_gate_active()) {
                serial_puts_polled("GF5 DATA stage=gpio_get"); number(" value=", value);
                serial_puts_polled("\r\n");
            }
        }
        break;
    }
    case GF5T_TILE_SET: {
        struct gf5_tile tile = {a[1], a[2], a[3], a[4], a[5]};
        CALL(tile_set, a[0], &tile);
        break;
    }
    case GF5T_TILE_CLEAR: CALL(tile_clear, a[0]); break;
    case GF5T_SAVE:
        if (diagnostic.image) { rc = GF5_EBUSY; local = true; break; }
        o = geforce_feature5_ops();
        if (!o || !o->ctx || !o->state_save) { rc = GF5_ENOTSUP; local = true; break; }
        diagnostic.image_owner = o->ctx;
        /* Direct out retains a partial image even if save/stop fails. */
        rc = o->state_save(o->ctx, &diagnostic.image);
        if (diagnostic.image && !serial_gate_active()) {
            serial_puts_polled("GF5 DATA stage=save"); number(" bytes=", diagnostic.image->bytes);
            number(" extents=", diagnostic.image->nextent);
            number(" complete=", diagnostic.image->complete);
            number(" epoch=", diagnostic.image->saved_epoch); serial_puts_polled("\r\n");
        }
        break;
    case GF5T_RESTORE:
    case GF5T_FREE:
        o = geforce_feature5_ops();
        if (!o || !o->ctx || !diagnostic.image || o->ctx != diagnostic.image_owner) {
            rc = GF5_ESTATE; local = true; break;
        }
        if (stage == GF5T_RESTORE) {
            if (!o->state_restore) { rc = GF5_ENOTSUP; local = true; break; }
            rc = o->state_restore(o->ctx, diagnostic.image);
        } else {
            if (!o->state_free) { rc = GF5_ENOTSUP; local = true; break; }
            o->state_free(o->ctx, diagnostic.image);
            diagnostic.image = NULL; diagnostic.image_owner = NULL;
        }
        break;
    case GF5T_POLL: rc = poll(&local); break;
    case GF5T_WAIT: {
        i32 source = 0;
        rc = diagnostic_wait(a[0], &source, &local);
        if (source) {
            /* Timing failures are outside the module's public exit mapper. */
            o = geforce_feature5_ops();
            i32 last = o && o->ctx ? o->ctx->last_rc : rc;
            return result_record(stage, rc, last, source);
        }
        break;
    }
    case GF5T_LAST_RC:
        o = geforce_feature5_ops();
        if (!o || !o->ctx || !o->last_rc) { rc = GF5_ENOTSUP; local = true; }
        else rc = o->last_rc(o->ctx);
        break;
    default: rc = GF5_EINVAL; local = true; break;
    }
    return finish(stage, rc, local);
}
#undef CALL
