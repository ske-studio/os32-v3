#ifndef OS32_SND2_H
#define OS32_SND2_H

#define SND2_KIND_PCM_OUT 1
#define SND2_DEV_PRESENT 0x01
#define SND2_DEV_DEFAULT 0x02
#define SND2_DEV_READY 0x04
#define SND2_DEV_FAULTED 0x08
#define SND2_ENC_S16LE 1
#define SND2_ENC_U8 2
#define SND2_MAX_FMT 16
#define SND2_ST_IDLE 0
#define SND2_ST_RUN 1
#define SND2_ST_PAUSE 2
#define SND2_ST_DRAIN 3
#define SND2_ST_FAULTED 4
#define SND2_CLOSE_DRAIN 0
#define SND2_CLOSE_ABORT 1
#define SND2_FMQ_LEN 512
typedef struct { unsigned long rate; unsigned char bits; unsigned char channels; unsigned char enc; unsigned char flags; } SND2_Fmt;
typedef struct { unsigned long size, dev, kind, flags; char name[32]; char driver[16]; } SND2_DevInfo;
typedef struct {
    unsigned long size, dev, kind, fmt_count;
    SND2_Fmt fmt[SND2_MAX_FMT];
    unsigned long period_min, period_max, ring_frames;
    unsigned long rsv_lead, latency_min_us, volume_steps;
    unsigned long hw_mix_streams;
    unsigned long hw_3d_streams;
    unsigned long dsp_units;
    unsigned long reserved[19];
} SND2_Caps;
#define SND2_DSP_MODE_INSERT 0x01
#define SND2_DSP_MODE_PROCESS 0x02
typedef struct { unsigned long id, kind, modes, in_fmt_mask, out_fmt_mask, param_count, max_frames, reserved; } SND2_DspUnit;
typedef struct { unsigned long id; signed long min, max, def; char name[16]; } SND2_DspParam;
#define SND2_DSP_MAX_PARAMS 16
typedef struct { unsigned long size, dev; SND2_Fmt fmt; unsigned long period_frames, lead_periods, flags, reserved; } SND2_StreamDesc;
typedef struct {
    unsigned long size, state, played, written, cap, underruns, epoch, time_ms;
} SND2_Pos;
typedef struct { unsigned char kind, bank, reg, val; unsigned long at_us; unsigned long seq; } SND2_FmEv;
#define SND2_KIND_FM 2
#define SND2_KIND_PSG 3
#define SND2_CHIP_OPN 1
#define SND2_CHIP_OPNA 2
#define SND2_CHIP_OPN3 3
#define SND2_CHIP_OPL3 4
#define SND2_CHF_RHYTHM 0x01
#define SND2_CHF_ADPCM 0x02
#define SND2_CHF_TIMER 0x04
#define SND2_CHF_PROBED 0x08
typedef struct {
    unsigned long size, dev, kind, chip;
    unsigned long clock_hz;
    unsigned short base_port, id_port;
    unsigned char  banks, fm_ch, ssg_ch, rhythm_ch;
    unsigned long flags, adpcm_bytes;
    unsigned long reserved[7];
} SND2_ChipCaps;
typedef struct { unsigned char bank, reg, val, rsv; } SND2_RegW;

#endif
