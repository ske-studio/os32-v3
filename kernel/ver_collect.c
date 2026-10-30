/* Read saved device/diagnostic state only; no probing, output or yield. */
#include "ver_info.h"
#include "time/cpu_calibrate.h"
#include "../arch/x86/kernel/idt.h"
#include "port_service.h"
#include "pcm_contract.h" /* pcm_present is the resident pcm_module.c facade. */
#include "gfx_hal.h"
#include "bootinfo.h"
#include "kstring.h"

/* Internal implementation in kapi_sys.c, not the generated KAPI wrapper. */
extern i32 kapi_mem_stat(i32 app_id, void *out, u32 size);

void ver_collect(VerSnapshot *s)
{
    const struct pit_setup *pit;

    kmemset(s, 0, sizeof(*s));
    s->cpu_measured = !cpu_calibrate_used_fallback();
    pit = pit_get_setup();
    s->pit_valid = pit->valid;
    s->pit_hz = pit->hz;
    s->pit_input = pit->clk_hz;
    s->ser_ready = port_get_status(&s->ser_mode, &s->ser_baud,
                                     &s->ser_fifo) >= 0;
    s->pcm_present = pcm_present();
    gfx_ver_state(&s->gfx, &s->gfx_kind, &s->gfx_active);
    s->api_version = KAPI_VERSION;
    s->boot_ok = boot_image_info(&s->boot) >= 0;
    s->debug_ok = kapi_mem_stat(0, &s->debug, sizeof(s->debug)) >= 0;
    services_ver_state(&s->services);
    /* Build is filled by build_time_copy in kapi_sys.c (V9), without reports. */
}
