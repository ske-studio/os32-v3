/* I1 guest acceptance: mappings, audit while locked and real exec teardown. */
#include "os32api.h"
#include "memmap.h"
#include <string.h>

#define ATTR 0x07
#define CHILD "/usr/bin/gfx2_i1.bin --child"
#define AUDIT_CHILD "/sbin/t_exit.bin 0"
static int exercise(KernelAPI *api, int leave_locked);
static int child(KernelAPI *api, const char *command);
static int failed(KernelAPI *api, const char *stage, int rc);

/* OS32X requires main to be the first function. */
int main(int argc, char **argv, KernelAPI *api)
{
    MemStat before, after;
    int rc;
    if (argc == 2 && !strcmp(argv[1], "--child")) {
        rc = exercise(api, 1);
        api->kprintf(ATTR, "gfx2_i1 child %s rc=%d\n", rc ? "FAIL" : "PASS", rc);
        return rc;
    }
    api->gfx_init();
    rc = exercise(api, 0);
    if (!rc) rc = child(api, CHILD); /* warm loader/accounting before baseline */
    if (!rc) {
        int n = api->mem_stat(0, &before, sizeof(before));
        if (n != (int)sizeof(before)) { rc = n ? n : 1; failed(api, "parent-mem-before", rc); }
    }
    if (!rc) rc = child(api, CHILD);
    if (!rc) {
        int n = api->mem_stat(0, &after, sizeof(after));
        if (n != (int)sizeof(after)) { rc = n ? n : 1; failed(api, "parent-mem-after", rc); }
    }
    if (!rc && (after.phys_free_pages != before.phys_free_pages ||
                after.leftover_pages != before.leftover_pages ||
                after.ledger_bad_free != before.ledger_bad_free ||
                after.audit_fail != before.audit_fail)) {
        api->kprintf(ATTR, "gfx2_i1 accounting free=%u/%u leftover=%u/%u bad_free=%u/%u audit_fail=%u/%u\n",
                     before.phys_free_pages, after.phys_free_pages,
                     before.leftover_pages, after.leftover_pages,
                     before.ledger_bad_free, after.ledger_bad_free,
                     before.audit_fail, after.audit_fail);
        rc = failed(api, "parent-accounting", 1);
    }
    api->gfx_shutdown();
    api->kprintf(ATTR, "gfx2_i1 %s rc=%d\n", rc ? "FAIL" : "PASS", rc);
    return rc ? 1 : 0;
}

static int child(KernelAPI *api, const char *command)
{
    int kind = -1, code = -1, rc = api->exec_run(command);
    int result_rc = api->exec_last_result(&kind, &code);
    if (rc || result_rc || kind != EXEC_KIND_EXITED || code) {
        api->kprintf(ATTR, "gfx2_i1 child command=%s exec_rc=%d result_rc=%d kind=%d code=%d\n",
                     command, rc, result_rc, kind, code);
        return failed(api, "child-result", 1);
    }
    return 0;
}

static int failed(KernelAPI *api, const char *stage, int rc)
{
    api->kprintf(ATTR, "gfx2_i1 stage=%s rc=%d\n", stage, rc);
    return 1;
}

static int exercise(KernelAPI *api, int leave_locked)
{
    GFX2_Caps caps;
    GFX2_SurfaceDesc desc;
    MemStat before, after;
    u32 sid, pitch, adapter;
    void *va;
    int rc = 1, call_rc;
    for (adapter = 0; adapter < 4; adapter++) {
        call_rc = api->gfx2_caps(adapter, &caps, sizeof(caps));
        api->kprintf(ATTR, "gfx2_i1 adapter=%u caps_rc=%d\n", adapter, call_rc);
        if (call_rc) continue;
        api->kprintf(ATTR, "gfx2_i1 caps max_w=%u max_h=%u fmt=%x pitch=%u\n",
                     caps.max_w, caps.max_h, caps.fmt_surface, caps.max_pitch);
        if (caps.max_w < 64) continue;
        call_rc = api->gfx2_surface_create(adapter, 64, 64, GFX2_FMT_XRGB8888,
                                          GFX2_POOL_SYSMEM, GFX2_USE_RENDER2D, &sid);
        api->kprintf(ATTR, "gfx2_i1 adapter=%u create_rc=%d\n", adapter, call_rc);
        if (!call_rc) break;
    }
    if (adapter == 4) return failed(api, "select-surface", 1);
    call_rc = api->gfx2_lock(sid, GFX2_LOCK_DISCARD, &va, &pitch);
    if (call_rc) { failed(api, "lock", call_rc); goto free_surface; }
    api->kprintf(ATTR, "gfx2_i1 lock sid=%u va=%x pitch=%u\n", sid, (u32)va, pitch);
    if ((u32)va < MEM_GFX2_MAP_BASE || (u32)va >= MEM_GFX2_MAP_END || pitch < 256) {
        failed(api, "map-range", 1);
        goto unlock;
    }
    for (u32 y = 0; y < 64; y++) for (u32 x = 0; x < 64; x++) {
        volatile u32 *p = (volatile u32 *)((u8 *)va + y * pitch) + x;
        if (*p) {
            api->kprintf(ATTR, "gfx2_i1 zero x=%u y=%u value=%x\n", x, y, *p);
            failed(api, "map-zero", 1); goto unlock;
        }
        *p = 0x123456UL + y * 64 + x;
    }
    call_rc = api->gfx2_surface_free(sid);
    if (call_rc != OS32_ERR_BUSY) { failed(api, "free-locked", call_rc); goto unlock; }
    /* Launch audits every live AS, including this locked parent. */
    call_rc = api->mem_stat(0, &before, sizeof(before));
    if (call_rc != (int)sizeof(before)) { failed(api, "locked-mem-before", call_rc); goto unlock; }
    if (child(api, AUDIT_CHILD)) goto unlock;
    call_rc = api->mem_stat(0, &after, sizeof(after));
    if (call_rc != (int)sizeof(after)) { failed(api, "locked-mem-after", call_rc); goto unlock; }
    if (after.audit_runs <= before.audit_runs || after.audit_fail != before.audit_fail) {
        api->kprintf(ATTR, "gfx2_i1 audit runs=%u/%u fail=%u/%u\n",
                     before.audit_runs, after.audit_runs, before.audit_fail, after.audit_fail);
        failed(api, "locked-audit", 1); goto unlock;
    }
    for (u32 y = 0; y < 64; y++) for (u32 x = 0; x < 64; x++)
        if (*((volatile u32 *)((u8 *)va + y * pitch) + x) != 0x123456UL + y * 64 + x)
        {
            api->kprintf(ATTR, "gfx2_i1 preserved x=%u y=%u\n", x, y);
            failed(api, "map-preserved", 1); goto unlock;
        }
    if (leave_locked) return 0; /* exec must remove the map and backing. */
    rc = 0;
unlock:
    call_rc = api->gfx2_unlock(sid);
    if (call_rc) rc = failed(api, "unlock", call_rc);
free_surface:
    call_rc = api->gfx2_surface_free(sid);
    if (call_rc) rc = failed(api, "free", call_rc);
    if (!rc && (call_rc = api->gfx2_surface_desc(sid, &desc)) != OS32_ERR_STALE)
        rc = failed(api, "stale-desc", call_rc);
    return rc;
}
