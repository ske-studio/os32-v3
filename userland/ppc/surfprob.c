#include "os32api.h"
#include <string.h>

/* Private controls supplied by the acceptance image. */
enum { PROBE_REGEN = 0x100, PROBE_LEASE_COUNT, PROBE_WATCH, PROBE_COPIED };

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
    if (rc == OS32_ERR_NOSYS)
        return report(api, "SURFACE provider absent\n", 10);
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
    rc = api->surface_lease(role, &before.desc[0].ref, OS32_SURFACE_RW, &view);
    if (rc == 0)
        return report(api, "SURFACE stale ref accepted\n", 8);
    if (rc != OS32_ERR_STALE)
        return report(api, "SURFACE stale ref unexpected error\n", 11);
    if (api->surface_lease(role, &after.desc[0].ref, OS32_SURFACE_RW, &view) ||
        api->surface_unlease(view.token))
        return report(api, "SURFACE fresh ref failed\n", 9);
    /* The fixture watches this address at the caller-input copy boundary. */
    union { OS32_SurfaceQueryResult watch; OS32_SurfaceRef refs[2]; } input;
    OS32_LeaseResult bundle;
    input.refs[0] = input.refs[1] = after.desc[0].ref;
    if (api->surface_query(PROBE_WATCH, &input.watch))
        return report(api, "SURFACE watch failed\n", 12);
    rc = api->gfx_surface_lease(role, input.refs, 2, OS32_SURFACE_RW, &bundle);
    if (rc == OS32_ERR_NOSYS)
        return report(api, "SURFACE bundle absent\n", 13);
    if (rc != OS32_ERR_INVAL || api->surface_query(PROBE_COPIED, &after))
        return report(api, "SURFACE bundle count failed\n", 14);
    if (api->gfx_surface_lease(OS32_SURFACE_DISPLAY, input.refs, 1, OS32_SURFACE_RW,
                               &bundle) != OS32_ERR_INVAL ||
        api->gfx_surface_lease(role, &before.desc[0].ref, 1, OS32_SURFACE_RW,
                               &bundle) != OS32_ERR_STALE ||
        api->gfx_surface_lease(role, input.refs, 1, OS32_SURFACE_RW, &bundle) ||
        bundle.count != 1 || !bundle.views[0].token || !bundle.views[0].base ||
        bundle.views[0].bytes != after.desc[0].bytes ||
        api->surface_unlease(bundle.views[0].token))
        return report(api, "SURFACE bundle acquisition failed\n", 15);
    return report(api, "SURFACE OK\n", 0);
}
