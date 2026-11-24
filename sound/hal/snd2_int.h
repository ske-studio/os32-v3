#ifndef SND2_INT_H
#define SND2_INT_H
#include "av_contract.h"

#define SND_HAL2_ABI 1
#define SND2_MAX_RING_FRAMES 4096U
#define SND2_MAX_SEGMENTS 16U
typedef struct Snd2Stream {          /* sound/hal/snd2_int.h。書くのは sound/hal だけ */
    u32 gen; u8 state, paused, v1, closing; int owner;   /* 前景の文脈が書く */
    SND2_Fmt fmt; u32 period, nseg, lead, cap, margin;   /* open で決め、閉じるまで不変 */
    u32 written;                     /* 前景が IF=0 の短い区間で publish */
    u32 played, copied, epoch, underruns;                /* advance (IF=0) だけが書く */
    u32 seg_first[SND2_MAX_SEGMENTS]; u16 seg_n[SND2_MAX_SEGMENTS];                    /* advance だけ */
    u8 *stg; u32 stg_frames;         /* ステージング 8192 frame = 32KB (kmalloc、DMA しない) */
    u8 *ring; u32 last_hw, last_obs_ms;                  /* ring は backend が渡す DMA の面 */
} Snd2Stream;

#define SND2_STG_FRAMES 8192U
typedef struct SndBackend {          /* sound/hal/snd2_int.h、SND_HAL2_ABI 1 */
    u32 abi;
    int  (*caps)(void *ctx, SND2_Caps *o);
    int  (*hw_open)(void *ctx, const SND2_Fmt *f, u32 period, u8 **ring);   /* リングは [HW2] を守った DMA の面 */
    int  (*hw_start)(void *ctx);                    /* リングは中核が埋めてから */
    int  (*hw_pos)(void *ctx, u32 *frame);          /* IF=0。読めなければ <0 */
    int  (*hw_ack)(void *ctx);                      /* IRQ。自分の要因なら 1 */
    int  (*hw_stop)(void *ctx);                     /* IF=0、上限つき。0 = 止まった証拠あり、<0 = 無し */
    void (*hw_release)(void *ctx, int leak);        /* leak=1: dma_mark_leaked でリングを隔離 */
    int  (*hw_volume)(void *ctx, u32 l, u32 r);
    int  (*hw_timer_arm)(void *ctx, u32 after_us);  /* NULL = 無い。after_us=0 で解除。V86 の FM の起床 (§9-4) */
    void (*hw_timer_ack)(void *ctx);                /* タイマの要因を消す (IRQ の中、backend が要因を分けた後) */
    int  (*dsp_unit)(void *ctx, u32 unit, SND2_DspUnit *o);   /* NULL = DSP 無し */
    int  (*dsp_param)(void *ctx, u32 unit, u32 i, SND2_DspParam *o);
    int  (*dsp_insert)(void *ctx, u32 unit, const i32 *p, u32 n);
    int  (*dsp_process)(void *ctx, u32 unit, const i32 *p, u32 n, const void *in, void *out, u32 frames, u32 fi);
} SndBackend;
int  snd2_register(const SndBackend *be, void *ctx, const SND2_DevInfo *info);  /* 戻り dev */
int  snd2_unregister(u32 dev);       /* 流れがあれば teardown。FAULTED なら BUSY を返し ctx を解放させない */
void snd2_advance(u32 dev);          /* IRQ・tick から */
void snd2_reclaim(int owner);        /* exec の (6b) */
typedef struct SndChipBackend { u32 abi;                                  /* sound/hal/snd2_int.h */
    int (*caps)(void *c, SND2_ChipCaps *o);  int (*open)(void *c);  int (*close)(void *c);   /* open: A460h 保存・bit0=1・静止 */
    int (*write)(void *c, const SND2_RegW *w, u32 n);  int (*read)(void *c, u32 bank, u32 what, u32 reg);
    int (*quiet)(void *c);   /* 静止の列。0 = 証拠あり */
} SndChipBackend;
int snd2_chip_register(const SndChipBackend *be, void *c, const SND2_DevInfo *i);  int snd2_chip_unregister(u32 dev);
#endif
