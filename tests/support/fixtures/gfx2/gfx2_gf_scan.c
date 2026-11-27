#include "sys/dev/video/geforce/geforce_module.h"
#include "sys/dev/video/geforce/gf_v2.h"
#include "sys/dev/video/geforce/display/api.h"

static int height, changes, binds;
static struct geforce_platform platform;
static GfxBackend backend;
static GeforceModuleContext module = {.height = &height, .backend = &backend, .platform = &platform};
const GeforceModuleContext *geforce_context = &module;
struct gf_v2 gf_v2_inst;
void gfx_policy_scan_changed(void) { changes++; }
void gf_hook_scan_bind(bool active, const struct gf3_surface *s) { if (active && s) binds++; }

static int run(void)
{
    static struct gfx2_surf owner;
    struct gf3_surface s = {.width = 640, .height = 480, .pitch = 640, .fmt = GF3_INDEX8,
                            .reservation_id = 7, .generation = 11};
    gf_v2_inst.node[0].value = (struct gf_vram_reservation){.owner = &owner, .reservation_id = 7, .generation = 11};
    gf_hook_scan_changed(0, &s, 1);
    if (changes || binds || height || backend.selected_mode) return 1;
    s.generation++;
    gf_hook_scan_changed(0, &s, 1);
    if (changes != 1 || binds != 1 || height != 480 || !backend.selected_mode ||
        backend.selected_mode->width != 640) return 2;
    return 0;
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
