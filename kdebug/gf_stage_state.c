/* Initial values are read once, after main4 and before any C write. */
#include "gf_stage_state.h"
#include "gfinit_plan_c.h"
#include "recover_gf.h"
#include "../hwdev/gfinit_mode.h"

static int stage_read(struct recover *r, u32 key, u32 *value)
{
    struct recover_bus_access a;
    if (key >= r->profile->target_count ||
        recover_target_address(&r->profile->targets[key], REC_READ, &a)) return -1;
    if (a.space != REC_PCI_CFG) {
        const struct recover_bar_extent *b = &r->bars[a.bar];
        if (!b->valid || a.width > b->length || a.offset > b->length-a.width ||
            (unsigned long long)b->base+a.offset+a.width > RECOVER_ADDRESS_END) return -1;
    }
    a.key = key;
    return r->ops->read(r->opaque, &a, value);
}
int gf_stage_capture(struct recover *r, void *arg)
{
    struct gf_stage_state *s = arg;
    if (s->initialized) return 0;
    if (r->ops->identity(r->opaque, &s->identity)) return -1;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) s->bars[i] = r->bars[i];
    for (u32 i = 0; i < GF_STAGE_VALUES; i++) {
        if (recover_safe_point(r)) return -1;
        u32 key = s->keys[i], old = 0;
        const struct recover_target *t = &r->profile->targets[key];
        struct recover_bus_access a;
        bool indexed = t->space == REC_VGA_INDEXED;
        if (indexed) {
            if (recover_target_address(t, REC_SELECT, &a)) return -1;
            a.key = key;
            if (r->ops->read(r->opaque, &a, &old) ||
                r->local_saved_count == RECOVER_MAX_REGS) return -1;
            /* Keep the original index available to S0 even if a bus call fails. */
            r->local_saved[r->local_saved_count++] =
                (struct recover_local_saved){key, REC_SELECT, old};
            if (r->ops->write(r->opaque, &a, t->index)) return -1;
        }
        int rc = stage_read(r, key, &s->values[i]);
        if (indexed) {
            if (r->ops->write(r->opaque, &a, old)) return -1;
            r->local_saved_count--;
        }
        if (rc) return -1;
    }
    s->initialized = true;
    return 0;
}
int gf_stage_entry(struct recover *r)
{
    struct gf_stage_state *s = r->gf_stage;
    struct recover_identity id;
    u32 command, bars[RECOVER_BAR_COUNT], value;
    if (!s || !s->initialized || !s->s0_ok || !r->s0_verified ||
        r->ops->identity(r->opaque, &id) ||
        id.boot != s->identity.boot || id.image_version != s->identity.image_version ||
        id.kdebug_version != s->identity.kdebug_version || id.bdf != s->identity.bdf ||
        id.device != s->identity.device || id.subsys != s->identity.subsys ||
        id.revision != s->identity.revision || id.bar0 != s->identity.bar0 ||
        r->ops->raw_config(r->opaque, &command, bars) || command != RECOVER_COMMAND_OVERRIDE)
        return -1;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
        if (!s->bars[i].valid || bars[i] != s->bars[i].raw) return -1;
    if (s->bars[GF_MODE_VRAM_BAR].length < GF_MODE_VRAM_BYTES) return -1;
    for (u32 key = REC_GF_T_CFG50; key <= REC_GF_T_PBUS1854; key++)
        if (stage_read(r, key, &value) || value) return -1;
    if (stage_read(r, REC_GF_T_PMC_ENABLE, &value) || value != s->values[GF_STAGE_PMC] ||
        r->ops->route_read(r->opaque, &value) || (value & RECOVER_ROUTE_MASK)) return -1;
    return 0;
}
