/* Read saved device/diagnostic state only; no probing, output or yield. */
#include "ver_info.h"
#include "platform_ver.h"
#include "../hal/time/cpu_calibrate.h"
#include "port_service.h"
#include "gfx_hal.h"
#include "bootinfo.h"
#include "kstring.h"
#include "kapi_sysinfo.h"

void ver_collect(VerSnapshot *s)
{
    const struct ver_backend *backend = platform_ver_backend();

    kmemset(s, 0, sizeof(*s));
    s->cpu_measured = !cpu_calibrate_used_fallback();
    s->ser_ready = port_get_status(&s->ser_mode, &s->ser_baud,
                                     &s->ser_fifo) >= 0;
    gfx_ver_state(&s->gfx, &s->gfx_kind, &s->gfx_active);
    s->api_version = KAPI_VERSION;
    s->boot_ok = boot_image_info(&s->boot) >= 0;
    s->debug_ok = kapi_mem_stat(0, &s->debug, sizeof(s->debug)) >= 0;
    if (backend && backend->collect) backend->collect(s);
    /* Build is filled by kapi_sys_build_time_copy (V9), without reports. */
}
