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
        .fm_startup_sound = fm_startup_sound,
        .fm_play_mml = fm_play_mml,
        .fm_play_mml_kapi = fm_play_mml_kapi,
        .snd_bgm_play = snd_bgm_play,
        .snd_bgm_stop = snd_bgm_stop,
        .snd_bgm_is_playing = snd_bgm_is_playing,
        .snd_se_play = snd_se_play,
        .snd_se_play_raw = snd_se_play_raw,
        .snd_set_master = snd_set_master,
        .snd_cleanup = snd_cleanup,
        .snd_bgm_set_persist = snd_bgm_set_persist,
        .snd_focus = snd_focus,
        .snd_focus_owner = snd_focus_owner,
        .snd_owner_exit = snd_owner_exit,
        .snd_bgm_get_persist = snd_bgm_get_persist,
        .tick = snd_tick, .quiet = fm_quiet
    };
    snd_init();
    return 0;
}

const struct {
    u32 version;
    int (*initialize)(FmModuleApi *);
} fm_module_ops = {FM_MODULE_OPS_VERSION, fm_module_init};
