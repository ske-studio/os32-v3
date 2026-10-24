/* NV44A accessor: recover owns ALL read/write authorization. VGA offsets
 * are resolved once by recover_target_address (envytools f102b82 nv_vga.xml,
 * nv3_prmvio.xml, nv3_pcrtc.xml); never issue VGA in/out on PC-98.
 * PCI lane semantics: drivers/pci.c and PC-98 UNDOCUMENTED io_pci.md.
 */
#include "gfinit_bus.h"
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
    u32 command, bars[RECOVER_BAR_COUNT];
    if (gfinit_bus_raw_config(b, &command, bars) || !(command & RECOVER_COMMAND_MEM)) return -1;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) if (bars[i] != b->bars[i]) return -1;
    u32 base = bars[a->bar] & ~PCI_BAR_MEM_ATTR_MASK;
    if (a->offset > ~0U - base || a->width - 1 > ~0U - base - a->offset) return -1;
    u32 pa = base + a->offset;
    if (pa < GF_LEGACY_HI && pa + a->width > GF_LEGACY_LO) return -1;
    return recover_window_access(&b->window, pa, a->width, write, value);
}
int gfinit_bus_read(void *opaque, const struct recover_bus_access *a, u32 *value)
{ return bus_access(opaque, a, false, value); }
int gfinit_bus_write(void *opaque, const struct recover_bus_access *a, u32 value)
{ return bus_access(opaque, a, true, &value); }

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
