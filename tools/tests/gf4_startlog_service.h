/* Fault seams around dependencies; execute the production start composition. */
static i32 log_surfaces(struct gf3_service *s,const struct gf3_surface *scan,
                         const struct gf3_surface *test)
{
    i32 rc=gf3_service_surfaces(s,scan,test);
    /* Exercise the real restore entry validation without a fake restore. */
    if (!rc && log_gate("restore_check"))
        ((struct gf4_ctx *)s->owner->owner)->ready=false;
    return rc;
}
#define gf_master_enable(...) (log_gate("master_enable") ? LOG_INJECT : gf_master_enable(__VA_ARGS__))
#define gf4_dcb_parse(...) (log_gate("dcb_parse") ? LOG_INJECT : gf4_dcb_parse(__VA_ARGS__))
#define gfinit_pll_parse(...) (log_gate("pll_parse") ? LOG_INJECT : gfinit_pll_parse(__VA_ARGS__))
#define gf_init_run(...) (log_gate("devinit") ? LOG_INJECT : log_same(log_inject_stage,"baseline") ? 0 : gf_init_run(__VA_ARGS__))
#define gf_display_baseline(...) (log_gate("baseline") ? LOG_INJECT : gf_display_baseline(__VA_ARGS__))
#define gf2_start(...) (log_gate("channel_start") ? LOG_INJECT : gf2_start(__VA_ARGS__))
#define gf4_scan_reserve_initial(...) (log_gate("scan_reserve") ? LOG_INJECT : gf4_scan_reserve_initial(__VA_ARGS__))
#define gf3_service_objects_init(...) (log_gate("objects_init") ? LOG_INJECT : gf3_service_objects_init(__VA_ARGS__))
#define gf4_detect(...) (log_gate("detect") ? LOG_INJECT : gf4_detect(__VA_ARGS__))
#define gf3_service_surfaces(...) (log_gate("surfaces") ? LOG_INJECT : log_surfaces(__VA_ARGS__))
#include "../../hwdev/gf/display/service.c"
#undef gf_master_enable
#undef gf4_dcb_parse
#undef gfinit_pll_parse
#undef gf_init_run
#undef gf_display_baseline
#undef gf2_start
#undef gf4_scan_reserve_initial
#undef gf3_service_objects_init
#undef gf4_detect
#undef gf3_service_surfaces
