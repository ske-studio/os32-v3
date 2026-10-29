#ifndef KDEBUG_GF_STAGE_STATE_H
#define KDEBUG_GF_STAGE_STATE_H
#include "recover.h"
#include "gfinit_pll.h"

#define GF_STAGE_FIELDS 8U
#define GF_STAGE_GATE GF_STAGE_FIELDS
#define GF_STAGE_PMC (GF_STAGE_GATE + 1U)
#define GF_STAGE_VALUES (GF_STAGE_PMC + 1U)
#define GF_C_EXEC_SECONDS 60U
#define GF_C_TX_SECONDS 1800U
#define GF_C_RESTORE_SECONDS 180U
#define GF_C_S0_SECONDS 60U
#define GF_C_EXEC_POLLS 65536U
#define GF_C_TX_POLLS 4000000U
#define GF_C_RESTORE_POLLS 262144U
#define GF_C_RESTORE_BYTES 131072U
#define GF_C_S0_POLLS 131072U
#define GF_C_S0_BYTES 65536U

/* One boot-lifetime value set. Backing plans, bus and ROM stay with the owner. */
struct gf_stage_state {
    bool initialized, entry_ok, s0_ok;
    struct recover_identity identity;
    struct recover_bar_extent bars[RECOVER_BAR_COUNT];
    struct gfinit_pll_limits limits;
    u32 rom_crc, rom_size;
    u32 keys[GF_STAGE_VALUES], values[GF_STAGE_VALUES];
};
static inline bool gf_stage_reentry(const struct recover *r)
{
    return r->c_only && r->gf_stage && r->gf_stage->initialized &&
        r->gf_stage->entry_ok && r->gf_stage->s0_ok && r->s0_verified;
}
int gf_stage_capture(struct recover *r, void *arg);
int gf_stage_entry(struct recover *r);
#endif
