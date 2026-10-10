/* Run in GUI while another app owns the display. Do not claim/init graphics. */
#include "os32api.h"
#include <string.h>

int main(int argc, char **argv, KernelAPI *api)
{
    OS32_SurfaceQueryResult before, after;
    u32 app, owner, generation;
    int screen_owner, fails;
    (void)argc; (void)argv;
    if (api->caller_identity(&app, &owner, &generation) != 0) {
        api->kprintf(ATTR_RED, "shutdown_probe: FAIL (precondition caller_identity)\n");
        return 1;
    }
    screen_owner = api->gfx_screen_owner();
    if (screen_owner == (int)app || screen_owner < 0) {
        api->kprintf(ATTR_RED, "shutdown_probe: FAIL (precondition requires another screen owner)\n");
        return 1;
    }
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    if (api->surface_query(OS32_SURFACE_CLIENT, &before) != 0) {
        api->kprintf(ATTR_RED, "shutdown_probe: FAIL (precondition surface_query)\n");
        return 1;
    }
    if (!before.count) {
        api->kprintf(ATTR_RED, "shutdown_probe: FAIL (precondition count 0)\n");
        return 1;
    }
    api->gfx_shutdown();
    fails = api->gfx_screen_owner() != screen_owner ||
            api->surface_query(OS32_SURFACE_CLIENT, &after) != 0 ||
            memcmp(&before, &after, sizeof(before)) != 0;
    api->kprintf(fails ? ATTR_RED : ATTR_GREEN, "shutdown_probe: %s\n",
                 fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
