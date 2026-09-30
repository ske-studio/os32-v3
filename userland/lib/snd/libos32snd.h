/* ======================================================================== */
/*  LIBOS32SND.H — OS32 サウンドライブラリ                                 */
/*                                                                          */
/*  BGM (バックグラウンドミュージック) と SE (効果音) の再生を              */
/*  KAPI経由で簡潔に行うためのユーザーランドライブラリ。                    */
/*                                                                          */
/*  使い方:                                                                 */
/*    libos32snd_init(kapi);                                                */
/*    snd_bgm_play("T120 O4 [CDEFGAB>C<]");   // BGM開始 (非ブロッキング) */
/*    snd_se_play(SND_SE_SELECT);              // 効果音再生               */
/* ======================================================================== */

#ifndef __LIBOS32SND_H
#define __LIBOS32SND_H

#include "os32api.h"

/* ======================================================================== */
/*  初期化                                                                  */
/* ======================================================================== */

/* KAPIポインタを設定 (プログラム開始時に1回呼ぶ) */
void libos32snd_init(KernelAPI *api);

/* ======================================================================== */
/*  BGM (バックグラウンドミュージック)                                      */
/*                                                                          */
/*  BGM は FM 3ch + SSG 3ch の全チャンネルを使用可能。                      */
/*  SE 発火時は FM Ch2 / SSG ChC が一時的にミュートされ、                   */
/*  SE 終了後に自動的に BGM が復帰する。                                    */
/* ======================================================================== */

/* BGM再生開始 (非ブロッキング、即座にリターン)
 * mml: 拡張MML文字列
 *   音名: A-G (+/# でシャープ)
 *   テンポ: T120
 *   オクターブ: O4, > (上), < (下)
 *   音符長: L4 (4分音符), L8 (8分音符)
 *   休符: R
 *   ループ: [ ... ] (ブラケット内をループ再生)
 *   チャンネル: @C0 (FM Ch0), @C1 (FM Ch1), @C3 (SSG ChA)
 *   音色: @T0 (ピアノ), @T1 (ベル), @T2 (オルガン)
 */
void snd_bgm_play(const char *mml);

/* BGM再生停止 */
void snd_bgm_stop(void);

/* BGM再生中か (1=再生中, 0=停止) */
int snd_bgm_is_playing(void);

/* BGM再生完了まで待機 (ブロッキング) */
void snd_bgm_wait(void);

/* ======================================================================== */
/*  SE (効果音)                                                             */
/*                                                                          */
/*  SE は FM Ch2 または SSG ChC を一時借用して再生する。                    */
/*  BGM 再生中でも安全に呼び出せる。                                        */
/* ======================================================================== */

/* 事前定義効果音ID */
#define SND_SE_CURSOR     0    /* カーソル移動: SSG 短いピッ */
#define SND_SE_SELECT     1    /* 決定: FM ピロン */
#define SND_SE_CANCEL     2    /* キャンセル: SSG 低音 */
#define SND_SE_ERROR      3    /* エラー: SSG ブブッ */
#define SND_SE_COIN       4    /* アイテム取得: FM 高音 */
#define SND_SE_BEEP       5    /* BEEP: SSG */

/* 事前定義SEを再生 */
void snd_se_play(int se_id);

/* カスタムSEを直接指定で再生
 * note: ノート番号 (SND_NOTE マクロ使用)
 * duration_ms: 持続時間 (ミリ秒)
 * tone: FM音色番号 (0-4)
 */
void snd_se_play_custom(int note, int duration_ms, int tone);

/* ======================================================================== */
/*  マスター制御                                                            */
/* ======================================================================== */

/* サウンド全体のON/OFF (0=ミュート, 1=有効) */
void snd_set_master(int enable);

/* BGM持続フラグ (exec_exit時にBGMを停止しない)
 * persist: 1=プログラム終了後もBGM継続, 0=プログラム終了で停止(デフォルト)
 */
void snd_bgm_set_persist(int persist);

/* ======================================================================== */
/*  ヘルパーマクロ                                                          */
/* ======================================================================== */

/* ノート番号生成マクロ: SND_NOTE(octave, key) */
#define SND_NOTE(oct, key)  ((unsigned char)((oct) * 12 + (key)))
#define SND_N_C   0
#define SND_N_CS  1
#define SND_N_D   2
#define SND_N_DS  3
#define SND_N_E   4
#define SND_N_F   5
#define SND_N_FS  6
#define SND_N_G   7
#define SND_N_GS  8
#define SND_N_A   9
#define SND_N_AS  10
#define SND_N_B   11

#endif /* __LIBOS32SND_H */
