/* Private paired FM/snd facade. Public KAPI signatures stay unchanged. */
#ifndef FM_MODULE_H
#define FM_MODULE_H
#include "fm.h"
#include "snd_engine.h"
#include "module_runtime.h"
#define FM_MODULE_OPS_VERSION 1u
typedef struct {
    void (*opn_write)(uchar reg, uchar val);
    void (*opn_init)(void);
    void (*fm_set_tone)(int ch, const uchar *tone_data);
    void (*fm_set_tone_num)(int ch, int tone_num);
    void (*fm_note_on)(int ch, int note);
    void (*fm_note_off)(int ch);
    void (*fm_all_off)(void);
    void (*ssg_tone)(int ch, u16 period);
    void (*ssg_volume)(int ch, uchar vol);
    void (*ssg_noise)(uchar period);
    void (*ssg_mixer)(uchar mask);
    void (*ssg_all_off)(void);
    void (*fm_startup_sound)(void);
    void (*fm_play_mml)(const char *mml);
    void (*fm_play_mml_kapi)(const char *mml);
    void (*snd_bgm_play)(const char *mml);
    void (*snd_bgm_stop)(void);
    int (*snd_bgm_is_playing)(void);
    void (*snd_se_play)(int se_id);
    void (*snd_se_play_raw)(int note, int duration_ticks, int tone);
    void (*snd_set_master)(int enable);
    void (*snd_cleanup)(void);
    void (*snd_bgm_set_persist)(int persist);
    i32 (*snd_focus)(int app_id);
    int (*snd_focus_owner)(void);
    void (*snd_owner_exit)(int owner);
    int (*snd_bgm_get_persist)(void);
    void (*tick)(void);
    int (*quiet)(void);
} FmModuleApi;
#define FM_MODULE_API_SLOTS 29u
int fm_module_load(void);
int fm_module_abort(void);
#endif
