/* Read saved device/diagnostic state only; no probing, output or yield. */
#include "ver_info.h"
#ifndef __powerpc__
#include "cpu.h"
#endif
#include "time/cpu_calibrate.h"
#include "boot_diag.h"
#include "port_service.h"
#include "pcm_contract.h" /* pcm_present is the resident pcm_module.c facade. */
#include "gfx_hal.h"
#include "bootinfo.h"
#include "kstring.h"

/* Internal implementation in kapi_sys.c, not the generated KAPI wrapper. */
extern i32 kapi_mem_stat(i32 app_id, void *out, u32 size);

void ver_collect(VerSnapshot *s)
{
    struct boot_diag diag;

    kmemset(s, 0, sizeof(*s));
#ifdef __powerpc__
    s->fpu_present = 1;
#else
    s->fpu_present = arch_fp_present();
#endif
    s->cpu_measured = !cpu_calibrate_used_fallback();
    platform_boot_diag(&diag);
    s->timer_kind = diag.timer_kind;
    s->pit_valid = diag.timer_valid;
    s->pit_hz = diag.timer_hz;
    s->pit_input = diag.timer_input;
    s->irq_master_vector = diag.irq_master_vector;
    s->irq_slave_vector = diag.irq_slave_vector;
    s->ser_ready = port_get_status(&s->ser_mode, &s->ser_baud,
                                     &s->ser_fifo) >= 0;
    s->pcm_present = pcm_present();
    gfx_ver_state(&s->gfx, &s->gfx_kind, &s->gfx_active);
    s->api_version = KAPI_VERSION;
    s->boot_ok = boot_image_info(&s->boot) >= 0;
    s->debug_ok = kapi_mem_stat(0, &s->debug, sizeof(s->debug)) >= 0;
    services_ver_state(&s->services);
    /* Build is filled by kapi_sys_build_time_copy (V9), without reports. */
}
