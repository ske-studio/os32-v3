#ifndef SND2_INT_H
#define SND2_INT_H
#include "av_contract.h"

#define SND_HAL2_ABI 2
#define SND2_MAX_RING_FRAMES 4096U
#define SND2_MAX_SEGMENTS 16U
typedef struct Snd2Stream {          /* sys/hal/sound/snd2_int.h。書くのは sound/hal だけ */
    u32 gen; u8 state, paused, v1, closing; int owner;   /* 前景の文脈が書く */
    SND2_Fmt fmt; u32 period, nseg, lead, cap, margin;   /* open で決め、閉じるまで不変 */
    u32 written;                     /* 前景が IF=0 の短い区間で publish */
    u32 played, copied, epoch, underruns;                /* advance (IF=0) だけが書く */
    u32 seg_first[SND2_MAX_SEGMENTS]; u16 seg_n[SND2_MAX_SEGMENTS];                    /* advance だけ */
    u8 *stg; u32 stg_frames;         /* ステージング 8192 frame = 32KB (kmalloc、DMA しない) */
    u8 *ring; u32 last_hw, last_obs_ms;                  /* ring は backend が渡す DMA の面 */
    u32 repeats; /* observations that show DMA may have repeated ring data */
    u32 last_progress_ms;
#ifdef SND2_REFILL_DIAG
    u32 refill_tried, gap_tried; /* per-ring-slot bits and first underrun evidence */
    u32 min_stg, gap_frame, gap_n, gap_next, gap_stg, max_dma_step;
#endif
    u8 stop_pending;                 /* resync waiting for DMA to stop */
    u8 restart_pending;              /* DMA proven stopped during resync */
    u16 resume_silence; /* protected empty slots at resume, cleared on retirement */
    u8 had_data;                     /* distinguishes a silent start from starvation */
} Snd2Stream;

#define SND2_STG_FRAMES 8192U
int snd2_stg_alloc(Snd2Stream *s);
void snd2_stg_free(Snd2Stream *s);
/* Reserve under the IRQ lock. Copy outside the lock, then publish under it. */
u32 snd2_stg_reserve(const Snd2Stream *s, u32 frames, u32 *off);
void snd2_stg_write(Snd2Stream *s, u32 off, const void *src, u32 frames);
void snd2_stg_publish(Snd2Stream *s, u32 frames);
void snd2_stg_read(const Snd2Stream *s, u32 first, void *dst, u32 frames);
u32 snd2_frame_bytes(const Snd2Stream *s);
/* reset/prime require stopped DMA. resync advances the epoch after stop proof. */
void snd2_seg_reset(Snd2Stream *s, u32 now);
void snd2_seg_prime(Snd2Stream *s);
void snd2_seg_resync(Snd2Stream *s, u32 now);
void snd2_seg_resume(Snd2Stream *s);
void snd2_seg_pause(Snd2Stream *s);
u32 snd2_seg_played_now(const Snd2Stream *s);
/* Negative means continuity lost: stop before changing the ring. */
void snd2_seg_after_publish(Snd2Stream *s, u32 hw, u32 now);
int snd2_seg_observe(Snd2Stream *s, int have_pos, u32 hw, u32 now);
typedef struct SndBackend {          /* sys/hal/sound/snd2_int.h、SND_HAL2_ABI 2 */
    u32 abi;
    int  (*caps)(void *ctx, SND2_Caps *o);
    int  (*hw_open)(void *ctx, const SND2_Fmt *f, u32 period, u8 **ring);   /* リングは [HW2] を守った DMA の面 */
    int  (*hw_start)(void *ctx);                    /* リングは中核が埋めてから */
    int  (*hw_pos)(void *ctx, u32 *frame);          /* IF=0。読めなければ <0 */
    int  (*hw_ack)(void *ctx);                      /* IRQ。自分の要因なら 1 */
    int  (*hw_stop)(void *ctx, int closing);        /* IF=0。再同期の正値は停止待ち。close は停止を要求し、release で停止確認。 */
    int  (*hw_release)(void *ctx, int leak);        /* 停止・復元を確認できなければ隔離し、負を返す。 */
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
typedef struct SndChipBackend { u32 abi;                                  /* sys/hal/sound/snd2_int.h */
    int (*caps)(void *c, SND2_ChipCaps *o);  int (*open)(void *c);  int (*close)(void *c);   /* open: A460h 保存・bit0=1・静止 */
    int (*write)(void *c, const SND2_RegW *w, u32 n);  int (*read)(void *c, u32 bank, u32 what, u32 reg);
    int (*quiet)(void *c);   /* 静止の列。0 = 証拠あり */
} SndChipBackend;
int snd2_chip_register(const SndChipBackend *be, void *c, const SND2_DevInfo *i);  int snd2_chip_unregister(u32 dev);
int snd2_chip_dev_info(u32 index, SND2_DevInfo *out);
void snd2_chip_fault(u32 dev);
#ifdef TICK_PROBE
int snd2_chip_diag_close(u32 h, u32 reg);
int snd2_chip_diag_open(u32 dev, u32 *h);
#endif
int snd2_chip_implicit(int owner);
#endif
