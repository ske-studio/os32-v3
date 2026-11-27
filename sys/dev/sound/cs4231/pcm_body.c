#include "pcm_module.h"
#include "pcm_hw.h"

int pcm_module_init(PcmModuleApi *out)
{
#ifdef TICK_PROBE
    pcm_bind_diag(((struct pcm_diag_init *)out)->storage);
#endif
    *out = (PcmModuleApi){
        .open = pcm_open,
        .write_check = pcm_write_check,
        .write = pcm_write,
        .status = pcm_status,
        .close = pcm_close,
        .set_volume = pcm_set_volume,
        .state = pcm_state,
        .present = pcm_present,
        .reclaim = pcm_reclaim,
        .tick = pcm_tick, .quiet = pcm_hw_quiet
    };
    pcm_init();
    return pcm_present() ? pcm_hw_register() : 0;
}

const struct {
    u32 version;
    int (*initialize)(PcmModuleApi *);
} pcm_module_ops = {PCM_MODULE_OPS_VERSION, pcm_module_init};
