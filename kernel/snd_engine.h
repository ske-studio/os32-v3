/* ======================================================================== */
/*  SND_ENGINE.H — サウンドエンジン (BGM/SE バックグラウンド再生)           */
/*                                                                          */
/*  timer_handler (IRQ0, 100Hz) から駆動される割り込み駆動型音楽再生系。    */
/*  BGM: FM 3ch + SSG 3ch の全6チャンネルを使用可能。                       */
/*  SE:  FM Ch2 / SSG ChC を一時借用し、終了後にBGMを復帰。               */
/* ======================================================================== */

#ifndef __SND_ENGINE_H
#define __SND_ENGINE_H

#include "types.h"

/* ======================================================================== */
/*  定数                                                                    */
/* ======================================================================== */

#define SND_MAX_NOTES       512   /* BGM最大ノート数 */
#define SND_REST            0xFF  /* 休符マーカー */
#define SND_END             0xFE  /* 曲終了マーカー */
#define SND_LOOP_MARK       0xFD  /* ループ開始マーカー */

/* SE 借用チャンネル (固定) */
#define SND_FM_SE_CH        2     /* FM Ch2 を借用 */
#define SND_SSG_SE_CH       2     /* SSG ChC を借用 */
#define SND_SSG_SE_LOGICAL  5     /* 論理チャンネル番号 (3+2=5) */

/* SE ID 定数 */
#define SND_SE_CURSOR       0     /* カーソル移動: SSG 短いピッ */
#define SND_SE_SELECT       1     /* 決定: FM ピロン */
#define SND_SE_CANCEL       2     /* キャンセル: SSG 低音 */
#define SND_SE_ERROR        3     /* エラー: SSG ブブッ */
#define SND_SE_COIN         4     /* アイテム取得: FM 高音 */
#define SND_SE_BEEP         5     /* BEEP: SSG */
#define SND_SE_MAX          16

/* ======================================================================== */
/*  データ構造                                                              */
/* ======================================================================== */

/* BGMノートイベント (事前パース済み, 4bytes/event) */
typedef struct {
    u8 note;       /* ノート番号 (SND_REST=休符, SND_END=終了) */
    u8 tone;       /* 音色番号 */
    u8 channel;    /* 出力チャンネル (0-2=FM, 3-5=SSG) */
    u8 duration;   /* 持続時間 (tick数, 1tick=10ms) */
} SndNote;

/* ======================================================================== */
/*  公開API                                                                 */
/* ======================================================================== */

/* 初期化 (kernel_main から呼ぶ) */
void snd_init(void);

/* timer_handler から毎tick呼ばれるメインループ */
void snd_tick(void);

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
int  snd_bgm_get_persist(void);

/* exec_exit 時に呼ばれるクリーンアップ (所有者を見ない旧口)。
 * アプリが 4 本同時に生きる v1.3 では使わない — アプリ A の終了が
 * アプリ B の BGM を止めるため。CUI の 1 本実行と診断のために残す。 */
void snd_cleanup(void);

/* ======================================================================== */
/*  音の所有権 (票 docs/archive/gui_v13/TASK_K5_multiapp.md 決裁 D9-4、受入 G10)*/
/*                                                                          */
/*  音は **フォーカスに追従して排他**。同時には鳴らさない。カーネルは         */
/*  「音の所有者」を 1 つだけ持ち、フォーカスが移ったら                       */
/*    1. それまでの所有者の BGM を退避して止める                             */
/*    2. 移った先に退避済みの BGM があれば復元する (無ければ無音)            */
/*  退避するのは **パース済みの BGM トラック + 再生位置 + persist** だけで、  */
/*  YM2203 のレジスタ影像は持たない — 復元は「いまのノートを鳴らし直す」で    */
/*  足り (1 ノートは最大 2.55 秒)、SE は借用が最大 255 tick の一時状態なので  */
/*  捨ててよい。                                                             */
/*                                                                          */
/*  フォーカスの無い所有者が snd_bgm_play を呼んだら、鳴らさずにその所有者の  */
/*  退避へ積むだけ (フォーカスが来たら鳴り出す)。SE は捨てる。               */
/* ======================================================================== */

/* 所有者 ID の上限 + 1 (exec/appslot.h の APP_ID_MAX = 5)。
 * kernel/ は -Iexec を持たないので値で持ち、整合は snd_engine.c の
 * STATIC_ASSERT ではなく exec 側の APP_SLOT_COUNT と同じ 6 に揃えてある。 */
#define SND_OWNER_MAX   6

/* 音の所有者をフォーカスに追従させる。app_id = 新しいフォーカス窓の owner
 * (1 = シェル帯 / 2〜5 = アプリ)。戻り値 0 / OS32_ERR_INVAL。
 * W レーン (gshell) がフォーカス切替のたびに呼ぶ。 */
i32 snd_focus(int app_id);

/* いま音を出してよい所有者 (診断・試験用)。 */
int snd_focus_owner(void);

/* 終了・kill 時にその ID の音の状態だけを捨てる (exec_exit / exec_kill)。
 * 鳴っているのがその ID なら止め、所有権をシェル帯 (1) へ戻す。
 * 他のアプリの退避済み BGM は 1 バイトも触らない。 */
void snd_owner_exit(int owner);

#endif /* __SND_ENGINE_H */
