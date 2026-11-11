/* I1 guest acceptance: mappings, audit while locked and real exec teardown. */
#include "os32api.h"
#include "memmap.h"
#include <string.h>

#define ATTR 0x07
#define CHILD "/usr/bin/gfx2_i1.bin --child"
#define AUDIT_CHILD "/sbin/t_exit.bin 0"
static int exercise(KernelAPI *api, int leave_locked);
static int child(KernelAPI *api, const char *command);

/* OS32X requires main to be the first function. */
int main(int argc, char **argv, KernelAPI *api)
{
    MemStat before, after;
    int rc;
    if (argc == 2 && !strcmp(argv[1], "--child")) return exercise(api, 1);
    api->gfx_init();
    rc = exercise(api, 0);
    if (!rc) rc = child(api, CHILD); /* warm loader/accounting before baseline */
    if (!rc) rc = api->mem_stat(0, &before, sizeof(before));
    if (!rc) rc = child(api, CHILD);
    if (!rc) rc = api->mem_stat(0, &after, sizeof(after));
    if (!rc && (after.phys_free_pages != before.phys_free_pages ||
                after.leftover_pages != before.leftover_pages ||
                after.ledger_bad_free != before.ledger_bad_free ||
                after.audit_fail != before.audit_fail)) rc = 1;
    api->gfx_shutdown();
    api->kprintf(ATTR, "gfx2_i1 %s rc=%d\n", rc ? "FAIL" : "PASS", rc);
    return rc ? 1 : 0;
}

static int child(KernelAPI *api, const char *command)
{
    int kind, code, rc = api->exec_run(command);
    if (rc || api->exec_last_result(&kind, &code) || kind != EXEC_KIND_EXITED || code)
        return 1;
    return 0;
}

static int exercise(KernelAPI *api, int leave_locked)
{
    GFX2_Caps caps;
    GFX2_SurfaceDesc desc;
    MemStat before, after;
    u32 sid, pitch, adapter;
    void *va;
    int rc = 1;
    for (adapter = 0; adapter < 4; adapter++)
        if (!api->gfx2_caps(adapter, &caps, sizeof(caps)) && caps.max_w >= 64 &&
            !api->gfx2_surface_create(adapter, 64, 64, GFX2_FMT_XRGB8888,
                                      GFX2_POOL_SYSMEM, GFX2_USE_RENDER2D, &sid)) break;
    if (adapter == 4) return 1;
    if (api->gfx2_lock(sid, GFX2_LOCK_DISCARD, &va, &pitch)) goto free_surface;
    if ((u32)va < MEM_GFX2_MAP_BASE || (u32)va >= MEM_GFX2_MAP_END || pitch < 256)
        goto unlock;
    for (u32 y = 0; y < 64; y++) for (u32 x = 0; x < 64; x++) {
        volatile u32 *p = (volatile u32 *)((u8 *)va + y * pitch) + x;
        if (*p) goto unlock;
        *p = 0x123456UL + y * 64 + x;
    }
    if (api->gfx2_surface_free(sid) != OS32_ERR_BUSY) goto unlock;
    /* Launch audits every live AS, including this locked parent. */
    if (api->mem_stat(0, &before, sizeof(before)) || child(api, AUDIT_CHILD) ||
        api->mem_stat(0, &after, sizeof(after)) ||
        after.audit_runs <= before.audit_runs || after.audit_fail != before.audit_fail) goto unlock;
    for (u32 y = 0; y < 64; y++) for (u32 x = 0; x < 64; x++)
        if (*((volatile u32 *)((u8 *)va + y * pitch) + x) != 0x123456UL + y * 64 + x)
            goto unlock;
    if (leave_locked) return 0; /* exec must remove the map and backing. */
    rc = 0;
unlock:
    if (api->gfx2_unlock(sid)) rc = 1;
free_surface:
    if (api->gfx2_surface_free(sid)) rc = 1;
    if (!rc && api->gfx2_surface_desc(sid, &desc) != OS32_ERR_STALE) rc = 1;
    return rc;
}
