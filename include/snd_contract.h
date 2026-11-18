#ifndef SND_CONTRACT_H
#define SND_CONTRACT_H
/* 共通 (KAPI・exec) と音源のエンジン (drivers/sound/fm/snd_engine.c) の間の口。 */
#include "types.h"

/* BGM 制御 */
void snd_bgm_play(const char *mml);
void snd_bgm_stop(void);
int  snd_bgm_is_playing(void);

/* SE 制御 */
void snd_se_play(int se_id);
void snd_se_play_raw(int note, int duration_ticks, int tone);

/* マスター制御 */
void snd_set_master(int enable);

/* BGM持続フラグ (1=exec_exit時にBGMを停止しない) */
void snd_bgm_set_persist(int persist);

/* 音の所有者をフォーカスに追従させる。app_id = 新しいフォーカス窓の owner
 * (1 = シェル帯 / 2〜5 = アプリ)。戻り値 0 / OS32_ERR_INVAL。
 * W レーン (gshell) がフォーカス切替のたびに呼ぶ。 */
i32 snd_focus(int app_id);

/* 終了・kill 時にその ID の音の状態だけを捨てる (exec_exit / exec_kill)。
 * 鳴っているのがその ID なら止め、所有権をシェル帯 (1) へ戻す。
 * 他のアプリの退避済み BGM は 1 バイトも触らない。 */
void snd_owner_exit(int owner);

#endif
