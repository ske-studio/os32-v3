#include "command.h"

/* RAMHT hash and linear probing: nvkm/core/ramht.c:27-37,107-126.
 * NV40 context encoding: nvkm/engine/fifo/nv40.c:111-120. */
static u32 ramht_hash(u32 handle)
{
    u32 hash = GF2_CHID << (GF2_RAMHT_BITS - 4U);
    while (handle) {
        hash ^= handle & (GF2_RAMHT_ENTRIES - 1U);
        handle >>= GF2_RAMHT_BITS;
    }
    return hash;
}

static bool ramht_used(const struct gf2_chan *c, u32 slot)
{
    return (c->bitmap[slot / 8U] & (1U << (slot % 8U))) != 0;
}

int gf2_ramht_add(struct gf2_chan *c, u32 handle, u8 engine, u32 inst_off,
                  u32 *cookie)
{
    u32 start = ramht_hash(handle), free_slot = GF2_RAMHT_ENTRIES;
    struct gf_inst *table = &c->f->imem.ramht;

    /* Scan the complete probe sequence: deletions leave holes. */
    for (u32 i = 0; i < GF2_RAMHT_ENTRIES; i++) {
        u32 slot = (start + i) & (GF2_RAMHT_ENTRIES - 1U);
        if (ramht_used(c, slot)) {
            u32 existing;
            if (gf_inst_read32(c->f, table, slot * GF2_RAMHT_ENTRY_BYTES,
                               &existing) != GF1_OK) return GF2_EIO;
            if (existing == handle) return GF2_EEXIST;
        } else if (free_slot == GF2_RAMHT_ENTRIES) {
            free_slot = slot;
        }
    }
    if (free_slot == GF2_RAMHT_ENTRIES) return GF2_ENOSPC;
    u32 off = free_slot * GF2_RAMHT_ENTRY_BYTES;
    u32 context = (GF2_CHID << 23) | ((u32)engine << 20) | (inst_off >> 4);
    if (gf_inst_write32(c->f, table, off, handle) != GF1_OK ||
        gf_inst_write32(c->f, table, off + 4U, context) != GF1_OK)
        return GF2_EIO;
    c->bitmap[free_slot / 8U] |= (u8)(1U << (free_slot % 8U));
    *cookie = free_slot + 1U;
    return GF2_OK;
}

int gf2_ramht_del(struct gf2_chan *c, u32 cookie)
{
    if (!cookie || cookie > GF2_RAMHT_ENTRIES || !ramht_used(c, cookie - 1U))
        return GF2_EINVAL;
    u32 slot = cookie - 1U, off = slot * GF2_RAMHT_ENTRY_BYTES;
    struct gf_inst *table = &c->f->imem.ramht;
    if (gf_inst_write32(c->f, table, off, 0) != GF1_OK ||
        gf_inst_write32(c->f, table, off + 4U, 0) != GF1_OK)
        return GF2_EIO;
    c->bitmap[slot / 8U] &= (u8)~(1U << (slot % 8U));
    return GF2_OK;
}
