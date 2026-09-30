/* ======================================================================== */
/*  LIBOS32SND.C — OS32 サウンドライブラリ実装                             */
/*  KAPIへの薄いラッパー + ユーティリティ関数                              */
/* ======================================================================== */

#include "libos32snd.h"

static KernelAPI *snd_api;

void libos32snd_init(KernelAPI *api)
{
    snd_api = api;
}

/* ======================================================================== */
/*  BGM                                                                     */
/* ======================================================================== */

void snd_bgm_play(const char *mml)
{
    snd_api->snd_bgm_play(mml);
}

void snd_bgm_stop(void)
{
    snd_api->snd_bgm_stop();
}

int snd_bgm_is_playing(void)
{
    return snd_api->snd_bgm_is_playing();
}

void snd_bgm_wait(void)
{
    while (snd_api->snd_bgm_is_playing()) {
        snd_api->sys_halt();
    }
}

/* ======================================================================== */
/*  SE                                                                      */
/* ======================================================================== */

void snd_se_play(int se_id)
{
    snd_api->snd_se_play(se_id);
}

void snd_se_play_custom(int note, int duration_ms, int tone)
{
    int ticks = duration_ms / 10;
    if (ticks < 1) ticks = 1;
    if (ticks > 255) ticks = 255;
    snd_api->snd_se_play_raw(note, ticks, tone);
}

/* ======================================================================== */
/*  マスター制御                                                            */
/* ======================================================================== */

void snd_set_master(int enable)
{
    snd_api->snd_set_master(enable);
}

void snd_bgm_set_persist(int persist)
{
    snd_api->snd_bgm_set_persist(persist);
}
