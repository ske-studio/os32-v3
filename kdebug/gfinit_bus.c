/* NV44A accessor: recover owns ALL read/write authorization. VGA offsets
 * are resolved once by recover_target_address (envytools f102b82 nv_vga.xml,
 * nv3_prmvio.xml, nv3_pcrtc.xml); never issue VGA in/out on PC-98.
 * PCI lane semantics: drivers/pci.c and PC-98 UNDOCUMENTED io_pci.md.
 */
#include "gfinit_bus.h"
#include "../hwdev/gfinit_mode.h"
#include "memmap.h"
#define GF_LEGACY_LO MEM_CONV_END
#define GF_LEGACY_HI 0xc0000U

void gfinit_bus_init(struct gfinit_bus *b, const struct recover_profile *p,
                     const struct gfinit_pci_ops *pci)
{
    *b = (struct gfinit_bus){.pci = pci, .profile = p};
    recover_window_hal_init(&b->window);
}
static u32 cfg32(const struct gfinit_bus *b, u32 off)
{ return b->pci->read32(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN, off); }
static int present(const struct gfinit_bus *b)
{
    return b && b->pci && b->pci->read32 && b->pci->read16 &&
        b->pci->write32 && b->pci->write16 && cfg32(b, PCI_CFG_VENDOR_ID) == RECOVER_GF_DEVICE;
}
int gfinit_bus_raw_config(void *opaque, u32 *command, u32 bars[RECOVER_BAR_COUNT])
{
    struct gfinit_bus *b = opaque;
    if (!command || !bars || !present(b)) return -1;
    *command = b->pci->read16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN, RECOVER_COMMAND);
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) bars[i] = cfg32(b, PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE * i);
    return 0;
}
int gfinit_bus_probe(struct gfinit_bus *b)
{
    u32 command, bars[RECOVER_BAR_COUNT];
    if (!b || b->ready || !b->profile || b->profile->expected_bdf != RECOVER_GF_BDF ||
        b->profile->expected_device != RECOVER_GF_DEVICE || gfinit_bus_raw_config(b, &command, bars)) return -1;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) {
        if ((bars[i] & PCI_BAR_TYPE_MASK) || !(bars[i] & ~PCI_BAR_MEM_ATTR_MASK)) return -1;
    }
    b->identity.bdf = RECOVER_GF_BDF; b->identity.device = RECOVER_GF_DEVICE;
    b->identity.subsys = cfg32(b, PCI_CFG_SUBSYS_VENDOR);
    b->identity.revision = cfg32(b, PCI_CFG_REVISION_ID) & PCI_REVISION_MASK;
    b->identity.bar0 = bars[0];
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) b->bars[i] = bars[i];
    b->ready = true;
    return 0;
}
int gfinit_bus_identity(void *opaque, struct recover_identity *out)
{
    struct gfinit_bus *b = opaque;
    if (!out || !b || !b->ready || !present(b)) return -1;
    *out = b->identity;
    out->subsys = cfg32(b, PCI_CFG_SUBSYS_VENDOR);
    out->revision = cfg32(b, PCI_CFG_REVISION_ID) & PCI_REVISION_MASK;
    out->bar0 = cfg32(b, PCI_CFG_BAR0);
    return 0;
}
static int memory_config(struct gfinit_bus *b, u32 bars[RECOVER_BAR_COUNT])
{
    u32 command;
    if (gfinit_bus_raw_config(b, &command, bars) || !(command & RECOVER_COMMAND_MEM)) return -1;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) if (bars[i] != b->bars[i]) return -1;
    return 0;
}
static int bus_access(struct gfinit_bus *b, const struct recover_bus_access *a,
                      bool write, u32 *value)
{
    struct recover_bus_access resolved;
    if (!b || !b->ready || !a || !value || a->key >= b->profile->target_count ||
        recover_target_address(&b->profile->targets[a->key], a->dir_substep, &resolved) ||
        a->space != resolved.space || a->bar != resolved.bar ||
        a->offset != resolved.offset || a->width != resolved.width) return -1;
    if (a->space == REC_PCI_CFG) {
        if (!present(b)) return -1;
        if (a->width == 2) {
            if (write) return b->pci->write16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN, a->offset, (u16)*value);
            *value = b->pci->read16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN, a->offset);
        } else if (a->width == 4) {
            if (write) b->pci->write32(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN, a->offset, *value);
            else *value = cfg32(b, a->offset);
        } else return -1;
        return 0;
    }
    if (a->space != REC_BAR_MMIO && a->space != REC_BAR_VRAM &&
        a->space != REC_VGA_DIRECT && a->space != REC_VGA_INDEXED) return -1;
    u32 bars[RECOVER_BAR_COUNT];
    if (memory_config(b, bars)) return -1;
    u32 base = bars[a->bar] & ~PCI_BAR_MEM_ATTR_MASK;
    u32 offset = a->offset;
    /* nouveau dispnv04/nvreg.h:129,137: MISC has distinct read/write aliases.
     * Keep the logged target/write address; only the physical read differs. */
    const struct recover_target *t = &b->profile->targets[a->key];
    if (!write && t->space == REC_VGA_DIRECT && t->port == RECOVER_MISC_WRITE)
        offset += RECOVER_MISC_READ - RECOVER_MISC_WRITE;
    if (offset > ~0U - base || a->width - 1 > ~0U - base - offset) return -1;
    u32 pa = base + offset;
    if (pa < GF_LEGACY_HI && pa + a->width > GF_LEGACY_LO) return -1;
    return recover_window_access(&b->window, pa, a->width, write, value);
}
int gfinit_bus_read(void *opaque, const struct recover_bus_access *a, u32 *value)
{ return bus_access(opaque, a, false, value); }
int gfinit_bus_write(void *opaque, const struct recover_bus_access *a, u32 value)
{ return bus_access(opaque, a, true, &value); }

/* Same DWORD aperture/restore path as A4, with no per-word plan or REC.
 * The caller checks safe points between the fixed 640-byte rows. */
int gfinit_bus_mode_row(struct gfinit_bus *b, u32 y, bool write, const u8 *row,
                        u32 *bad_offset, u32 *expected, u32 *actual)
{
    u32 bars[RECOVER_BAR_COUNT];
    if (!b || !b->ready || y >= GF_MODE_HEIGHT || !row ||
        memory_config(b, bars)) return -1;
    u32 base = bars[GF_MODE_VRAM_BAR] & ~PCI_BAR_MEM_ATTR_MASK;
    if (base > ~0U - (GF_MODE_VRAM_BYTES - 1U) ||
        (base < GF_LEGACY_HI && base + GF_MODE_VRAM_BYTES > GF_LEGACY_LO)) return -1;
    for (u32 x = 0; x < GF_MODE_PITCH; x += GF_MODE_VRAM_WORD_BYTES) {
        u32 wanted = 0;
        for (u32 byte = 0; byte < GF_MODE_VRAM_WORD_BYTES; byte++)
            wanted |= (u32)row[x + byte] << (byte * BITS_PER_BYTE);
        u32 value = wanted, off = GF_MODE_VRAM_OFFSET + y * GF_MODE_PITCH + x;
        if (recover_window_access(&b->window, base + off, GF_MODE_VRAM_WORD_BYTES, write, &value)) return -1;
        if (!write && value != wanted && *bad_offset == RECOVER_NONE) {
            for (u32 byte = 0; byte < GF_MODE_VRAM_WORD_BYTES; byte++) {
                u32 got = (value >> (byte * BITS_PER_BYTE)) & GF_MODE_BYTE_MASK;
                if (got != row[x + byte]) {
                    *bad_offset = off + byte; *expected = row[x + byte]; *actual = got;
                    break;
                }
            }
        }
    }
    return 0;
}

/* UNLOCK -> DATA (CR11, CR44) -> LOCK -> original INDEX.
 * CR1F saved reads (0/3) are translated into write commands (99/57).
 * No COLD DATA or capture/PLL temporary value is replayed here. */
int gfinit_bus_local_cleanup(struct gfinit_bus *b, struct recover *r)
{
    int rc = recover_window_cleanup(&b->window);
    const u32 order[] = {RECOVER_CR1F, RECOVER_CR11, RECOVER_CR44, RECOVER_CR1F};
    for (u32 k = 0; !rc && k < sizeof(order) / sizeof(order[0]); k++) {
        for (u32 i = 0; !rc && i < r->local_saved_count; i++) {
            const struct recover_local_saved *v = &r->local_saved[i];
            const struct recover_target *t = &r->profile->targets[v->key];
            if (v->dir_substep != REC_DATA || t->index != order[k]) continue;
            u32 index = t->index, value = v->value;
            if (t->index == RECOVER_CR1F)
                value = !k || v->value == RECOVER_CR1F_UNLOCKED ?
                    RECOVER_CR1F_UNLOCK : RECOVER_CR1F_LOCK;
            if (recover_restore_point(r) ||
                recover_access(r, RECOVER_NONE, REC_SELECT, v->key, 1, &index) ||
                recover_access(r, RECOVER_NONE, REC_DATA, v->key, 1, &value)) rc = -1;
        }
    }
    for (u32 i = 0; !rc && i < r->local_saved_count; i++) {
        const struct recover_local_saved *v = &r->local_saved[i];
        if (v->dir_substep != REC_SELECT) continue;
        u32 key = v->key, value = v->value;
        /* Use the descriptor's representative, regardless of first SELECT. */
        for (u32 j = 0; j < r->profile->index_save_count; j++) {
            u32 candidate = r->profile->index_save_targets[j];
            if (r->index_port[candidate] == r->index_port[key]) { key = candidate; break; }
        }
        if (recover_restore_point(r) || recover_access(r, RECOVER_NONE, REC_SELECT, key, 1, &value)) rc = -1;
    }
    if (recover_window_cleanup(&b->window)) rc = -1;
    if (!rc) r->local_saved_count = 0;
    return rc;
}

/* Fixed VGA aliases, not recover MMIO targets. Each window is released before
 * a safe point; AR flip-flop reset precedes every index or gate write. */
static int mode_byte(struct gfinit_bus *b, u32 base, u32 off, bool write, u32 *value)
{
    return recover_window_access(&b->window,base+off,1,write,value);
}
static int mode_base(struct gfinit_bus *b, u32 *base)
{
    u32 bars[RECOVER_BAR_COUNT];
    if (!b || !b->ready || memory_config(b,bars)) return -1;
    *base=bars[GF_MODE_REGISTER_BAR] & ~PCI_BAR_MEM_ATTR_MASK;
    if (*base > ~0U-GF_MODE_DAC_DATA ||
        (*base < GF_LEGACY_HI && *base+GF_MODE_DAC_DATA >= GF_LEGACY_LO)) return -1;
    return 0;
}
int gfinit_bus_mode_ar(struct gfinit_bus *b, bool data, u32 index, u32 value)
{
    u32 base, ignored=0;
    if (mode_base(b,&base) || mode_byte(b,base,GF_MODE_AR_STATUS,false,&ignored)) return -1;
    if (data && mode_byte(b,base,GF_MODE_AR_PORT,true,&index)) return -1;
    return mode_byte(b,base,GF_MODE_AR_PORT,true,&value);
}
int gfinit_bus_mode_dac(struct gfinit_bus *b, struct recover *r)
{
    u32 base, value=GF_MODE_BYTE_MASK;
    if (mode_base(b,&base) || mode_byte(b,base,GF_MODE_DAC_MASK,true,&value)) return -1;
    value=0;
    if (mode_byte(b,base,GF_MODE_DAC_INDEX,true,&value)) return -1;
    for (u32 i=0;i<GF_MODE_PALETTE_SIZE;i++) {
        u8 rgb[GF_MODE_RGB];
        if (recover_safe_point(r)) return -1;
        gfinit_mode_palette(i,rgb);
        for (u32 c=0;c<GF_MODE_RGB;c++) {
            value=rgb[c];
            if (mode_byte(b,base,GF_MODE_DAC_DATA,true,&value)) return -1;
        }
    }
    return 0;
}
