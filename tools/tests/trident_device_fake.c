/* Minimal device model. Observed blocked BAR0 is distinct from the inferred
 * Init/linear gate; see header. No hardware or libc; storage belongs to caller.
 * This is not a chip specification, nor an Init/Save/Restore implementation. */
#include "trident_device_fake.h"

static void zero(void *ptr, u32 bytes)
{
    u8 *p = ptr;
    for (u32 i = 0; i < bytes; ++i) p[i] = 0;
}
int trident_fake_reset(struct trident_fake *f, u8 *storage, u32 bytes,
                       struct trident_fake_event *trace, u32 count)
{
    if (!f || !storage || bytes < TRI_FAKE_REAL_BYTES || (count && !trace))
        return TRI_E_INVAL;
    zero(f, sizeof(*f));
    zero(storage, bytes);
    f->vram = storage;
    f->storage_bytes = bytes;
    f->real_bytes = TRI_FAKE_REAL_BYTES;
    f->upper = TRI_UPPER_INDEPENDENT; /* above real_bytes is absent, no mirror */
    f->trace = trace;
    f->trace_capacity = count;
    f->hole_start = U32_MAX;
    f->fail_rc = TRI_E_ACCESS;
    f->dac_readable = true;
    f->pc98_initialized = false;
    f->resources = (struct trident_resources){
        .regs = f->regs, .lfb = storage,
        .decode_bytes = TRI_BAR0_DECODE_BYTES,
        .mapped_bytes = TRI_BAR0_DECODE_BYTES,
        .bar0_phys = TRI_BAR0_PHYS, .bar1_phys = TRI_BAR1_PHYS,
        .regs_bytes = TRI_REG_MAPPED_BYTES,
        .pci = {
            .vendor = TRI_PCI_VENDOR, .device = TRI_PCI_DEVICE,
            .command = TRI_PCI_COMMAND, .bus = TRI_PCI_BUS,
            .slot = TRI_PCI_SLOT, .function = TRI_PCI_FUNCTION, .revision = 0xD3,
            .bar0 = TRI_BAR0_PHYS, .bar1 = TRI_BAR1_PHYS
        }
    };
    f->sr[9] = TRI_CHIP_9682;
    f->cr_color[0x1F] = 0x7F;
    f->cr_mono[0x1F] = 0x7F;
    f->gr[6] = 0x0C;
    return 0;
}
void trident_fake_bind(struct trident_fake *f, struct trident_device *d)
{
    d->res = &f->resources;
    d->io = &trident_fake_io;
    d->io_opaque = f;
}
int trident_fake_memory(struct trident_fake *f, u32 bytes, enum trident_upper upper)
{
    if (!bytes || bytes > f->storage_bytes || bytes > TRI_BAR0_DECODE_BYTES ||
        upper < TRI_UPPER_UNKNOWN || upper > TRI_UPPER_INDEPENDENT)
        return TRI_E_INVAL;
    f->real_bytes = bytes;
    f->upper = upper;
    return 0;
}
void trident_fake_trace_reset(struct trident_fake *f)
{
    f->trace_count = 0;
    f->trace_overflow = false;
}
void trident_fake_fail(struct trident_fake *f, u32 operation, int rc)
{
    f->fail_at = operation;
    f->fail_rc = rc;
}
int trident_fake_accept_init(struct trident_fake *f, u32 first,
                             const struct trident_fake_event *expected, u32 count)
{
    if (!expected || !count || f->trace_overflow || first > f->trace_count ||
        count > f->trace_count - first) return TRI_E_INVAL;
    for (u32 i = 0; i < count; ++i) {
        const struct trident_fake_event *a = &f->trace[first + i], *b = &expected[i];
        if (a->space != b->space || a->offset != b->offset || a->value != b->value ||
            a->count != b->count || a->width != b->width || a->write != b->write || a->rc || b->rc)
            return TRI_E_STATE;
    }
    f->pc98_initialized = true;
    return 0;
}
void trident_fake_restored(struct trident_fake *f)
{
    if (f->block_after_restore) f->pc98_initialized = false;
}
/* Inference from VGA MISC bit0 decode: inactive CRTC/status ports read FF
 * and ignore writes. This has not been measured on the Ra266. */
static bool inactive_vga(const struct trident_fake *f, u32 off)
{
    bool color = (f->regs[TRI_MISC_WRITE] & 1u) != 0;
    u32 base = color ? TRI_CR_MONO : TRI_CR_COLOR;
    return off == base || off == base + 1 || off == base + 6;
}
static u8 read_reg(struct trident_fake *f, u32 off)
{
    if (inactive_vga(f, off)) return 0xFF;
    switch (off) {
    case TRI_SR_INDEX + 1: return f->sr[f->sr_index];
    case TRI_CR_COLOR + 1: return f->cr_color[f->color_index];
    case TRI_CR_MONO + 1: return f->cr_mono[f->mono_index];
    case TRI_GR_INDEX + 1: return f->gr[f->gr_index];
    case TRI_AR_DATA: return f->ar[f->ar_index & 31];
    case TRI_CR_COLOR + 6: case TRI_CR_MONO + 6:
        f->ar_data = false;
        return f->regs[off];
    case TRI_MISC_READ: return f->regs[TRI_MISC_WRITE];
    case TRI_DAC_WRITE_INDEX:
        f->hidden_reads = 0;
        return f->regs[off];
    case TRI_DAC_MASK:
        if (f->hidden_reads < 4) { ++f->hidden_reads; return f->regs[off]; }
        return f->hidden_dac;
    case TRI_DAC_DATA: {
        u8 v = f->dac_readable ? f->dac[f->dac_read % TRI_DAC_BYTES] : 0xFF;
        f->dac_read = (f->dac_read + 1) % TRI_DAC_BYTES;
        return v;
    }
    case TRI_EXT_DAC_DATA: return f->ext_dac[f->ext_index];
    default: return f->regs[off];
    }
}
static void write_reg(struct trident_fake *f, u32 off, u8 v)
{
    if (inactive_vga(f, off)) return;
    switch (off) {
    case TRI_SR_INDEX: f->sr_index = v; break;
    case TRI_SR_INDEX + 1: f->sr[f->sr_index] = v; break;
    case TRI_CR_COLOR: f->color_index = v; break;
    case TRI_CR_COLOR + 1: f->cr_color[f->color_index] = v; break;
    case TRI_CR_MONO: f->mono_index = v; break;
    case TRI_CR_MONO + 1: f->cr_mono[f->mono_index] = v; break;
    case TRI_GR_INDEX: f->gr_index = v; break;
    case TRI_GR_INDEX + 1: f->gr[f->gr_index] = v; break;
    case TRI_AR_INDEX:
        if (f->ar_data) f->ar[f->ar_index & 31] = v;
        else f->ar_index = v;
        f->ar_data = !f->ar_data;
        break;
    case TRI_DAC_READ_INDEX: f->dac_read = (u32)v * 3; break;
    case TRI_DAC_WRITE_INDEX:
        f->dac_write = (u32)v * 3;
        f->hidden_reads = 0;
        break;
    case TRI_DAC_MASK:
        if (f->hidden_reads >= 4) f->hidden_dac = v;
        break;
    case TRI_DAC_DATA:
        f->dac[f->dac_write % TRI_DAC_BYTES] = v & 0x3F;
        f->dac_write = (f->dac_write + 1) % TRI_DAC_BYTES;
        break;
    case TRI_EXT_DAC_INDEX: f->ext_index = v; break;
    case TRI_EXT_DAC_DATA: f->ext_dac[f->ext_index] = v; break;
    default: break;
    }
    /* Hidden DAC command must not overwrite the ordinary palette mask. */
    if (off != TRI_DAC_MASK || f->hidden_reads < 4) f->regs[off] = v;
}
static void record(struct trident_fake *f, u32 space, u32 off, u32 v,
                   u8 width, bool write, int rc)
{
    if (space == TRI_SPACE_LFB && !f->trace_lfb) return;
    if (!f->trace_capacity) return;
    if (f->trace_count == f->trace_capacity) { f->trace_overflow = true; return; }
    f->trace[f->trace_count++] = (struct trident_fake_event){
        .space = space, .offset = off, .value = v, .width = width, .count = 1,
        .write = write, .rc = rc
    };
}
static int access(struct trident_fake *f, enum trident_space space, u32 off,
                   u8 width, bool write, u32 *value)
{
    u32 limit = space == TRI_SPACE_REGS ? f->resources.regs_bytes :
                f->resources.mapped_bytes;
    int rc = 0;
    ++f->operations;
    if (f->fail_at && f->operations == f->fail_at) rc = f->fail_rc;
    else if ((space != TRI_SPACE_REGS && space != TRI_SPACE_LFB) ||
             (off & (width - 1)) || off > limit || width > limit - off)
        rc = TRI_E_ACCESS;
    else if (space == TRI_SPACE_LFB &&
             (off >= f->hole_start || width > f->hole_start - off))
        rc = TRI_E_ACCESS;
    if (!rc) {
        u32 result = 0;
        for (u32 i = 0; i < width; ++i) {
            if (space == TRI_SPACE_REGS) {
                if (write) write_reg(f, off + i, (u8)(*value >> (i * 8)));
                else result |= (u32)read_reg(f, off + i) << (i * 8);
            } else {
                bool ready = f->pc98_initialized && (f->cr_color[0x21] & 0x20);
                u32 at = off + i;
                bool exists = at < f->real_bytes || f->upper == TRI_UPPER_MIRROR;
                if (f->upper == TRI_UPPER_MIRROR) at %= f->real_bytes;
                if (write) {
                    if (ready && exists) f->vram[at] = (u8)(*value >> (i * 8));
                } else {
                    u8 v = !ready ? (u8)(TRI_FAKE_BLOCKED_WORD >> (((off+i)&3)*8)) :
                           exists ? f->vram[at] : 0xFF;
                    result |= (u32)v << (i * 8);
                }
            }
        }
        if (!write) *value = result;
    }
    u32 slot = width == 1 ? 0 : width == 2 ? 1 : 2;
    if (write) ++f->writes[slot]; else ++f->reads[slot];
    record(f, space, off, *value, width, write, rc);
    return rc;
}
#define READ_FN(bits, type) \
static int read##bits(void *opaque, enum trident_space s, u32 off, type *out) \
{ \
    u32 v = 0; \
    int rc = access(opaque, s, off, sizeof(type), false, &v); \
    if (!rc) *out = (type)v; \
    return rc; \
}
#define WRITE_FN(bits, type) \
static int write##bits(void *opaque, enum trident_space s, u32 off, type value) \
{ \
    u32 v = value; \
    return access(opaque, s, off, sizeof(type), true, &v); \
}
READ_FN(8, u8)
READ_FN(16, u16)
READ_FN(32, u32)
WRITE_FN(8, u8)
WRITE_FN(16, u16)
WRITE_FN(32, u32)
static int port8(void *opaque, u16 port, u8 value)
{
    struct trident_fake *f = opaque;
    ++f->operations;
    ++f->port_writes;
    int rc = f->fail_at && f->operations == f->fail_at ? f->fail_rc : 0;
    if (!rc) { f->last_port = port; f->last_port_value = value; }
    record(f, TRI_FAKE_PORT_SPACE, port, value, 1, true, rc);
    return rc;
}
/* One transfer operation/event for a whole row, not count callbacks. */
static int write_span32(void *opaque, u32 off, const u32 *src, u32 count)
{
    struct trident_fake *f = opaque;
    u32 limit = f->resources.mapped_bytes;
    int rc = 0;
    ++f->operations;
    if (f->fail_at && f->operations == f->fail_at) rc = f->fail_rc;
    else if (!src || !count || ((uptr)src & 3) || (off & 3) || off > limit ||
             count > (limit - off) / 4 || off >= f->hole_start ||
             count > (f->hole_start - off) / 4) rc = TRI_E_ACCESS;
    if (!rc) {
        for (u32 j = 0; j < count; ++j) {
            for (u32 i = 0; i < 4; ++i) {
                u32 at = off + j * 4 + i;
                bool exists = at < f->real_bytes || f->upper == TRI_UPPER_MIRROR;
                if (f->upper == TRI_UPPER_MIRROR) at %= f->real_bytes;
                if (f->pc98_initialized && (f->cr_color[0x21] & 0x20) && exists)
                    f->vram[at] = (u8)(src[j] >> (i * 8));
            }
        }
    }
    ++f->writes[2]; /* one span operation, elements are recorded in count */
    u32 before = f->trace_count;
    record(f, TRI_SPACE_LFB, off, !rc ? src[0] : 0, 4, true, rc);
    if (f->trace_count > before) f->trace[before].count = count;
    return rc;
}
const struct trident_io trident_fake_io = {
    .read8 = read8, .read16 = read16, .read32 = read32,
    .write8 = write8, .write16 = write16, .write32 = write32, .port8 = port8,
    .write_span32 = write_span32
};
