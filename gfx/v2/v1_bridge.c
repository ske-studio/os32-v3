/* HAL v2 C14: borrowed v1 surfaces and synchronous access boundaries. */
#include "gfx2_int.h"
#include "physmem.h"

#define GFX2_REBIND_DL 1000000u
#define GFX2_BRIDGE_DL 1000000u

static struct gfx2_surf *alias[GFX2_ROLE_CLIENT + 1];
/* v1 entry points retain their historical result even when a wait fails. */
static unsigned int alias_attempted;
static volatile int bridge_rc[GFX2_ROLE_CLIENT + 1];

static int v1_alias_desc(u32 role, struct gfx2_alias_desc *d)
{
    struct gfx2_adapter *a = gfx2_active();
    const struct ledger_surface *sf;
    GfxSyncCtx *ctx;
    u32 mode, ledger_role = role == GFX2_ROLE_DISPLAY ? LEDGER_ROLE_DISPLAY : LEDGER_ROLE_CLIENT;
    if (!a) return OS32_ERR_INVAL;
    /* A SHADOW scanout's authoritative pixels are the RAM CLIENT. */
    if (role == GFX2_ROLE_DISPLAY && !gfx2_sync_get(a, 0, &ctx, &mode) &&
        mode == GFXSYNC_SHADOW) ledger_role = LEDGER_ROLE_CLIENT;
    sf = ledger_surface_find(a->kind, ledger_role);
    if (!sf || !sf->npages || sf->closing) return OS32_ERR_INVAL;
    u32 phys = sf->first * PAGE_SIZE;
    int vram = sf->backing == LEDGER_SB_VRAM || sf->backing == LEDGER_SB_MMIO;
    const GfxMode *m = gfx_backend_mode(a->v1);
    *d = (struct gfx2_alias_desc){
        .adapter = a->index, .w = m ? m->width : sf->width, .h = m ? m->height : sf->height,
        .fmt = sf->format, .pool = vram ? GFX2_POOL_VRAM : GFX2_POOL_SYSMEM,
        .head = 0, .role = role, .pitch = sf->pitch, .bytes = sf->npages * PAGE_SIZE,
        .gpu_off = vram ? phys - a->vram_phys : 0,
        .kva = vram ? (u8 *)P2V_IO(phys) : (u8 *)P2V(phys)
    };
    return 0;
}

void v1_bridge_rebind(void)
{
    for (u32 role = GFX2_ROLE_DISPLAY; role <= GFX2_ROLE_CLIENT; role++) {
        struct gfx2_alias_desc d;
        struct gfx2_surf *s = alias[role];
        int has = !v1_alias_desc(role, &d);
        if (!s) {
            if (has && !(alias_attempted & (1u << role))) {
                alias_attempted |= 1u << role;
                (void)gfx2_surf_alias(gfx2_active(), &d, &alias[role]);
            }
            continue;
        }
        int rc = gfx2_surf_quiesce(s, GFX2_REBIND_DL);
        if (rc == OS32_ERR_TIMEOUT) continue;
        (void)gfx2_lock_revoke(s);
        (void)gfx2_surf_alias_update(s, has ? &d : NULL);
    }
}

int v1_bridge_begin(u32 role, u32 access)
{
    struct gfx2_surf *s;
    int rc = 0;
    if (role < GFX2_ROLE_DISPLAY || role > GFX2_ROLE_CLIENT) return OS32_ERR_INVAL;
    s = alias[role];
    if (s && s->w && s->h) {
        const struct ledger_surface *sf = ledger_surface_find(
            gfx2_adapter_get(s->adapter)->kind,
            role == GFX2_ROLE_DISPLAY ? LEDGER_ROLE_DISPLAY : LEDGER_ROLE_CLIENT);
        if (s->state == GFX2_ST_LOCKED_RW ||
            (s->state == GFX2_ST_LOCKED_RO && (access & GFX2_CPU_W)) ||
            (sf && sf->lease_count)) rc = OS32_ERR_BUSY;
        else rc = gfx2_cpu_begin(s, access, GFX2_BRIDGE_DL);
    }
    bridge_rc[role] = rc;
    return rc;
}

void v1_bridge_end(u32 role, u32 access, u32 fence)
{
    (void)fence; /* v1 hardware hooks finish synchronously; no fence is issued. */
    if (role < GFX2_ROLE_DISPLAY || role > GFX2_ROLE_CLIENT) return;
    struct gfx2_surf *s = alias[role];
    if (s && s->w && s->h) {
        struct gfx2_rect all = {0, 0, s->w, s->h};
        gfx2_cpu_end(s, access, &all);
    }
}
