#include "gfx2_int.h"
#include "gfx2_exec.h"
#include "paging.h"

struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];
struct gfx2_adapter gfx2_adapters[GFX2_MAX_ADAPTERS];
u32 gfx2_quar_owner = 90;
extern u32 gfx2_leak_pages;
static struct addrspace as;
static int fail_unmap, fail_destroy, fail_transfer, wait_rc, done;
static int unmaps, detaches, staging, drops, restores, destroys, transfers, waits;
static int violation, kept_staging, staging_pages, owner_pages, quar_pages;
static u32 seq, detach_seq, staging_seq, restore_seq, wait_seq, destroy_seq, drop_seq;
#define EXPECT(c) do { if (!(c)) return __LINE__; } while (0)
struct gfx2_adapter *gfx2_adapter_get(u32 i)
{ return i == 0 ? &gfx2_adapters[0] : 0; }
int gfx2_unmap(struct gfx2_surf *s)
{
    unmaps++;
    if (fail_unmap) return OS32_ERR_IO;
    s->map_as = 0; s->map_va = s->map_npages = 0;
    return 0;
}
void gfx2_map_detach(struct gfx2_surf *s)
{
    detaches++; detach_seq = ++seq;
    s->map_as = 0; s->map_va = s->map_npages = 0;
}
void gfx2_staging_drop(struct gfx2_surf *s)
{
    staging++; staging_seq = ++seq;
    if (s->map_as) violation = 1;
    if (s->staging) {
        if (s->flags & GFX2_SF_KEEP) kept_staging += staging_pages;
        staging_pages = 0;
        s->staging = 0;
    }
}
void gfx2_vaspace_drop(struct addrspace *a)
{ if (a != &as) violation = 1; drops++; drop_seq = ++seq; }
int gfx2_surf_state_set(struct gfx2_surf *s, u32 st)
{ s->state = st; return 0; }
int gfx2_head_restore_v1(struct gfx2_adapter *a, u32 h)
{
    restores++; restore_seq = ++seq;
    u32 ids[2] = { a->head[h].scan_sid, a->head[h].pend_sid };
    for (u32 j = 0; j < 2; j++) {
        struct gfx2_surf *s = &gfx2_surfs[ids[j] % GFX2_MAX_SURFACES];
        if (s->sid == ids[j] && s->scan_ref) s->scan_ref--;
    }
    a->head[h].scan_sid = a->head[h].pend_sid = 0;
    return OS32_ERR_IO; /* restore failure still detaches references */
}
int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl)
{
    waits++; wait_seq = ++seq;
    if (s->map_as || dl != 1000000u) violation = 1;
    return wait_rc;
}
int gfx2_surf_destroy(struct gfx2_surf *s)
{
    destroys++; destroy_seq = ++seq;
    if (s->map_as || s->scan_ref || (s->flags & (GFX2_SF_KEEP | GFX2_SF_ALIAS))) violation = 1;
    if (fail_destroy) { s->flags |= GFX2_SF_KEEP; return OS32_ERR_BUSY; }
    if (s->pool == GFX2_POOL_GART) {
        if (s->owner == gfx2_quar_owner) quar_pages = 0;
        else owner_pages = 0;
    }
    s->state = GFX2_ST_FREE; s->owner = 0; s->gen++;
    return 0;
}
int gfx2_gart_quarantine(struct gfx2_surf *s)
{
    transfers++;
    if (s->map_as) violation = 1;
    if (fail_transfer || !gfx2_quar_owner) return OS32_ERR_IO;
    s->gart_moved = s->gart_n;
    quar_pages += owner_pages; owner_pages = 0;
    return 0;
}
int gfx2_fence_done(struct gfx2_adapter *a, u32 f)
{ (void)a; return !f || done; }
static void reset(void)
{
    unsigned char *p = (unsigned char *)gfx2_surfs;
    for (u32 i = 0; i < sizeof(gfx2_surfs); i++) p[i] = 0;
    p = (unsigned char *)gfx2_adapters;
    for (u32 i = 0; i < sizeof(gfx2_adapters); i++) p[i] = 0;
    as.owner = 7; gfx2_quar_owner = 90;
    fail_unmap = fail_destroy = fail_transfer = wait_rc = 0; done = 1;
    unmaps = detaches = staging = drops = restores = destroys = transfers = waits = 0;
    violation = kept_staging = staging_pages = owner_pages = quar_pages = 0;
    seq = detach_seq = staging_seq = restore_seq = wait_seq = destroy_seq = drop_seq = 0;
    gfx2_adapters[0].heads = 2;
}
static struct gfx2_surf *surface(u32 i, u32 pool)
{
    struct gfx2_surf *s = &gfx2_surfs[i];
    s->sid = GFX2_MAX_SURFACES + i; s->owner = as.owner;
    s->pool = pool; s->state = GFX2_ST_LOCKED_RW;
    s->map_as = &as; s->map_va = MEM_GFX2_MAP_BASE; s->map_npages = 3;
    s->gart_n = 3; s->bytes = 3 * MEM_PAGE_SIZE;
    if (pool == GFX2_POOL_GART) owner_pages = 3;
    return s;
}
static int cases(void)
{
    struct gfx2_surf *s, *alias, *other;
    reset();
    EXPECT(gfx2_owner_release(&as) == 0 && drops == 1 && !destroys);
    reset(); s = surface(1, GFX2_POOL_GART);
    s->scan_ref = 1; gfx2_adapters[0].head[1].pend_sid = s->sid;
    EXPECT(gfx2_owner_release(&as) == 0);
    EXPECT(!violation && restores == 1 && s->state == GFX2_ST_FREE && !owner_pages);
    EXPECT(detach_seq < staging_seq && staging_seq < restore_seq && restore_seq < wait_seq);
    EXPECT(wait_seq < destroy_seq && destroy_seq < drop_seq);
    reset(); s = surface(1, GFX2_POOL_GART); wait_rc = OS32_ERR_TIMEOUT;
    EXPECT(gfx2_owner_release(&as) == 0);
    EXPECT(!violation && transfers == 1 && destroys == 0 && owner_pages == 0 && quar_pages == 3);
    EXPECT(s->owner == gfx2_quar_owner && s->state == GFX2_ST_QUAR && !s->map_as);
    /* AS may be freed: drain must use only detached surface/adapter data. */
    as.owner = 0xccccccccu;
    s->last_read = 12; done = 0;
    gfx2_quarantine_drain(&gfx2_adapters[0]); EXPECT(!destroys && quar_pages == 3);
    done = 1; gfx2_adapters[0].state = GFX2_AD_LOST_HELD;
    gfx2_quarantine_drain(&gfx2_adapters[0]); EXPECT(!destroys);
    gfx2_adapters[0].state = GFX2_AD_LOST;
    gfx2_quarantine_drain(&gfx2_adapters[0]);
    EXPECT(destroys == 1 && !quar_pages && !violation);
    reset(); s = surface(1, GFX2_POOL_GART); fail_unmap = 1; fail_destroy = 1;
    EXPECT(gfx2_owner_release(&as) == OS32_ERR_IO);
    EXPECT(detaches == 1 && !s->map_as && !s->map_va && !s->map_npages && drops == 1);
    EXPECT(s->owner != as.owner && s->state == GFX2_ST_QUAR && (s->flags & GFX2_SF_KEEP));
    as.owner = 0xccccccccu;
    gfx2_quarantine_drain(&gfx2_adapters[0]);
    EXPECT(!destroys && gfx2_leak_pages == 3 && !violation);
    reset(); s = surface(1, GFX2_POOL_VRAM); fail_destroy = 1;
    EXPECT(gfx2_owner_release(&as) == 0 && destroys == 1 && (s->flags & GFX2_SF_KEEP));
    gfx2_quarantine_drain(&gfx2_adapters[0]); EXPECT(destroys == 1);
    reset(); s = surface(1, GFX2_POOL_GART); wait_rc = OS32_ERR_STALE; fail_transfer = 1;
    EXPECT(gfx2_owner_release(&as) == OS32_ERR_IO);
    EXPECT(s->owner == gfx2_quar_owner && s->state == GFX2_ST_QUAR && owner_pages == 3);
    reset(); s = surface(1, GFX2_POOL_GART); wait_rc = OS32_ERR_TIMEOUT; gfx2_quar_owner = 0;
    EXPECT(gfx2_owner_release(&as) == OS32_ERR_IO && s->owner == 0 && s->state == GFX2_ST_QUAR);
    reset(); alias = surface(2, GFX2_POOL_VRAM);
    alias->owner = GFX2_OWNER_SCREEN; alias->flags = GFX2_SF_ALIAS;
    alias->staging = (u8 *)1; staging_pages = 2;
    other = surface(3, GFX2_POOL_SYSMEM); other->owner = 8; other->map_as = 0;
    EXPECT(gfx2_owner_release(&as) == 0);
    EXPECT(!staging_pages && !kept_staging && !alias->staging && !alias->map_as);
    EXPECT(alias->state == GFX2_ST_IDLE && alias->owner == GFX2_OWNER_SCREEN && !destroys);
    EXPECT(other->owner == 8 && other->state == GFX2_ST_LOCKED_RW && !violation);
    reset(); alias = surface(2, GFX2_POOL_VRAM);
    alias->owner = GFX2_OWNER_SCREEN; alias->flags = GFX2_SF_ALIAS;
    alias->state = GFX2_ST_LOCKED_RO; alias->staging = (u8 *)1;
    staging_pages = 2; fail_unmap = 1;
    EXPECT(gfx2_owner_release(&as) == OS32_ERR_IO);
    EXPECT(kept_staging == 2 && !alias->staging && !alias->map_as);
    EXPECT(!(alias->flags & GFX2_SF_KEEP) && alias->state == GFX2_ST_IDLE && !destroys);
    reset(); s = surface(1, GFX2_POOL_GART);
    EXPECT(gfx2_surf_quarantine(s) == 0 && !s->map_as && transfers == 1);
    s->adapter = 1; gfx2_quarantine_drain(&gfx2_adapters[0]); EXPECT(!destroys);
    s->adapter = 0; s->scan_ref = 1; gfx2_quarantine_drain(&gfx2_adapters[0]); EXPECT(!destroys);
    return 0;
}
void _start(void)
{
    int rc = cases();
    /* Any failed assertion is runtime RED, including line numbers >255. */
    rc = rc ? 1 : 0;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
