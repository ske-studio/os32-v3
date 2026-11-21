#include "fm_module.h"

int fm_module_init(FmModuleApi *out)
{
    *out = (FmModuleApi){
        .opn_write = opn_write,
        .opn_init = opn_init,
        .fm_set_tone = fm_set_tone,
        .fm_set_tone_num = fm_set_tone_num,
        .fm_note_on = fm_note_on,
        .fm_note_off = fm_note_off,
        .fm_all_off = fm_all_off,
        .ssg_tone = ssg_tone,
        .ssg_volume = ssg_volume,
        .ssg_noise = ssg_noise,
        .ssg_mixer = ssg_mixer,
        .ssg_all_off = ssg_all_off,
        .quiet = fm_quiet
    };
    return 0;
}

const struct {
    u32 version;
    int (*initialize)(FmModuleApi *);
} fm_module_ops = {FM_MODULE_OPS_VERSION, fm_module_init};
