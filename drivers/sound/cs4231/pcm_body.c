#include "pcm_module.h"

int pcm_module_init(PcmModuleApi *out)
{
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
        .tick = pcm_tick, .quiet = pcm_module_quiet
    };
    pcm_init();
    return 0;
}

const struct {
    u32 version;
    int (*initialize)(PcmModuleApi *);
} pcm_module_ops = {PCM_MODULE_OPS_VERSION, pcm_module_init};
