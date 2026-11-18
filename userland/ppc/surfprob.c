#include "os32api.h"
#include <string.h>

/* Private controls supplied by the acceptance image. */
enum { PROBE_REGEN = 0x100, PROBE_LEASE_COUNT };

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    const u32 role = OS32_SURFACE_CLIENT;
    OS32_SurfaceQueryResult before, after;
    OS32_LeaseView view;
    int rc;

    rc = api->surface_query(role, &before);
    if (rc || before.count != 1 || before.desc[0].role != role ||
        before.desc[0].backend != OS32_SURFACE_OF_FB ||
        before.desc[0].format != OS32_SURFACE_PACKED8 ||
        before.desc[0].planes != 1 || !before.desc[0].ref.generation ||
        before.desc[0].access_max != OS32_SURFACE_RW)
        return report(api, "SURFACE query failed\n", 1);
    if (argc == 2 && !strcmp(argv[1], "reclaimed")) {
        if (api->surface_query(PROBE_LEASE_COUNT, &after))
            return report(api, "SURFACE exit leak\n", 2);
        return report(api, "SURFACE reclaimed\n", 0);
    }
    rc = api->surface_lease(role, &before.desc[0].ref, OS32_SURFACE_RW, &view);
    if (rc || !view.token || !view.base || view.bytes != before.desc[0].bytes ||
        before.desc[0].plane_offset[0] >= view.bytes ||
        view.planes[0] != view.base + before.desc[0].plane_offset[0])
        return report(api, "SURFACE lease failed\n", 3);

    volatile unsigned char *pixel = (volatile unsigned char *)view.planes[0];
    unsigned char saved = *pixel, pattern = saved ^ 0x5a;
    *pixel = pattern;
    unsigned char actual = *pixel;
    *pixel = saved;
    if (actual != pattern)
        return report(api, "SURFACE readback failed\n", 4);

    if (argc == 2 && !strcmp(argv[1], "leave")) {
        if (api->surface_query(PROBE_LEASE_COUNT, &after) != 1)
            return report(api, "SURFACE missing live lease\n", 5);
        return report(api, "SURFACE left lease at exit\n", 0);
    }
    if (api->surface_unlease(view.token) ||
        api->surface_unlease(view.token) != OS32_ERR_INVAL ||
        api->surface_query(PROBE_LEASE_COUNT, &after))
        return report(api, "SURFACE unlease failed\n", 6);
    if (api->surface_query(PROBE_REGEN, &after) ||
        api->surface_query(role, &after) || after.count != 1 ||
        after.desc[0].ref.sid != before.desc[0].ref.sid ||
        !after.desc[0].ref.generation ||
        after.desc[0].ref.generation == before.desc[0].ref.generation)
        return report(api, "SURFACE regeneration failed\n", 7);
    if (api->surface_lease(role, &before.desc[0].ref, OS32_SURFACE_RW, &view)
        != OS32_ERR_STALE)
        return report(api, "SURFACE stale ref failed\n", 8);
    if (api->surface_lease(role, &after.desc[0].ref, OS32_SURFACE_RW, &view) ||
        api->surface_unlease(view.token))
        return report(api, "SURFACE fresh ref failed\n", 9);
    return report(api, "SURFACE OK\n", 0);
}
