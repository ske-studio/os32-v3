#include "pm_console.h"
#include "console_backend.h"
#include "unicode_file.h"
#include "display_text.h"
#include "kernel/console/font_source_kcg.h"
#include "gfx/gfx.h"
#include "gfx/gfx_console.h"
#include "gfx/v2/gfx2_int.h"
#include "platform_display_fallback.h"
#include "config.h"
#include "console.h"
#include "text_screen.h"
#include "kprintf.h"
#include "rh_observe.h"
#include "exec/appslot.h"

#define KBOOT_ATTR 0x07

static struct gfx_console_target target;
static int pm_text_was_up;

static int open_target(void)
{
    struct gfx2_adapter *a = gfx2_active();
    return a ? gfx_console_open(a->index, 0, &target) : -1;
}

static void text_off(const char *step, int rc)
{
    pm_text_was_up = 0;
    pm_console_text_state(1, 0, 0, 0);
    pm_console_detach();
    if (target.sid) (void)gfx_console_close(&target);
    kcg_file_unload();
    kprintf(KBOOT_ATTR, "FB off step=%s rc=%d\r\n", step, rc);
}

int pm_console_text_start(void)
{
    int rc = kcg_file_load(SYS_FB_FONT);
    const char *step = "font";
    if (!rc) {
        step = "unicode";
        unicode_file_load();
        if (!unicode_file_debug_phys()) rc = -1;
    }
    if (!rc) {
        step = "target";
        gfx_boot_reserve();
        gfx_init();
        rc = open_target();
    }
    if (!rc) {
        step = "attach";
        rc = pm_console_attach(&target, &kcg_file_api, PM_RH_FB_ROWS);
        if (!rc && !con_backend_safe()) rc = -1;
    }
    if (!rc) {
        const struct pm_rh_colors *colors = pm_rh_full_colors();
        pm_console_set_colors(colors->foreground, colors->background);
        step = "clear";
        if (con_backend_safe()) {
            pm_console_text_state(1, 1, 1, target.generation);
            tvram_clear();
            console_hw_cursor_enable();
        }
        if (!con_backend_safe()) rc = -1;
        else kprintf(KBOOT_ATTR, "FB text ready generation=%u fg=%u bg=%u\r\n",
                     target.generation, colors->foreground, colors->background);
    }
    if (rc) text_off(step, rc);
    else pm_text_was_up = 1;
    return rc;
}

void display_provider_console_return(void)
{
    if (!pm_text_was_up || pm_console_text_ready()) return;
    gfx_init();
    int rc = gfx_console_close(&target);
    if (rc) kprintf(KBOOT_ATTR, "FB close rc=%d\r\n", rc);
    target = (struct gfx_console_target){0};
    rc = open_target();
    if (rc) { text_off("target", rc); return; }
    rc = pm_console_retarget(&target);
    if (rc) { text_off("retarget", rc); return; }
    pm_console_text_state(1, 1, 1, target.generation);
    const struct pm_rh_colors *colors = pm_rh_full_colors();
    kprintf(KBOOT_ATTR, "FB text ready generation=%u fg=%u bg=%u\r\n",
            target.generation, colors->foreground, colors->background);
}

void display_provider_owner_exit(int id)
{
    if (id == appslot_gfx_owner() && !pm_console_text_ready()) gfx_shutdown();
}
