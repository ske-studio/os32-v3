/* Shared PCM contract; device registers remain in drivers/. */
#ifndef PCM_CONTRACT_H
#define PCM_CONTRACT_H
#include "types.h"
#define PCM_FRAME_BYTES    4U       /* 16 ビット × 2ch */
#include "os32_kapi_shared.h"

/*  レート。この 2 つだけ (票 §4)。 */
#define PCM_RATE_44100     44100U
#define PCM_RATE_22050     22050U


/*  起動時の表示に出す版の番号 (CS4231 = 100、CS4231A = 101)。 */
#define PCM_VER_NAME_4231  100
#define PCM_VER_NAME_4231A 101


/* ------------------------------------------------------------------------ */
/*  エラーの写し (票 §2-2 の BUSY / NOMEM)                                  */
/*                                                                          */
/*  `OS32_ERR_*` には BUSY も NOMEM も無いので、意味のいちばん近い既存の     */
/*  番号へ写す。**新しい番号は足さない** (KAPI_SPEC §3-2 の予約表を汚さない)。*/
/*    BUSY  = 他の owner が open 中 / IRQ10 に結べない → 資源が満杯          */
/*    NOMEM = DMA プールが取れない                    → 空き不足            */
/* ------------------------------------------------------------------------ */
#define PCM_ERR_BUSY   OS32_ERR_FULL    /* -13 */
#define PCM_ERR_NOMEM  OS32_ERR_NOSPC   /* -4  */

/*  音量: percent 1〜100 を 6 ビットの減衰へ線形に写す。 */
#define PCM_VOL_MAX        100U

/*  カウンタの飽和 (pcm_status の詰め方)。 */
#define PCM_CNT_U8_MAX     255U
#define PCM_CNT_U16_MAX    65535U
#define PCM_CNT_UND_SHIFT  24
#define PCM_CNT_REP_SHIFT  16

#define PCM_ST_CLOSED      0
#define PCM_ST_OPENING     1
#define PCM_ST_OPEN        2
#define PCM_ST_RUNNING     3
#define PCM_ST_DRAINING    4
#define PCM_ST_RS_STOP     5
#define PCM_ST_RS_RESTART  6
#define PCM_ST_STOP_REQ    7
#define PCM_ST_STOP_DONE   8
#define PCM_ST_FAULTED     9


/* 起動時 (pci_bind_all の後)。検出だけ。装置が無くても静かに戻る。 */
void pcm_init(void);

/* tick フック (arch/x86/kernel/isr_handlers.c の timer_handler)。 */
void pcm_tick(void);

/* 所有者の回収 (exec/exec.c の exec_reclaim_owned)。 */
void pcm_reclaim(int owner);

/* KAPI の実体 (kapi/ の __cdecl ラッパから呼ばれる — [C3])。 */
int pcm_open(u32 rate);
/* write の受付の検査だけ (owner と状態)。0 / OS32_ERR_INVAL。
 * KAPI の wrapper が**入力の検査より先に**呼ぶ — 未 open・非 owner は
 * bytes = 0 でも負を返す契約 (票 §2-2)。 */
int pcm_write_check(void);
int pcm_write(const void *buf, u32 bytes);
int pcm_status(u32 *free_bytes, u32 *counters);
int pcm_close(void);
int pcm_set_volume(u32 percent);

/* kselftest / 診断の覗き口 (KAPI にはしない)。 */
int pcm_state(void);
int pcm_present(void);


#endif
