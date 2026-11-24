/* ======================================================================== */
/*  FM.C — PC-9801-26K (YM2203/OPN) FM音源ドライバ                         */
/*                                                                          */
/*  snddrv (DOS版, 動作確認済み) からOS32 (32ビットプロテクトモード) に移植   */
/*  変更点: conio.h → インラインASM, memset → 不使用                       */
/*                                                                          */
/*  出典: PC9800Bible §2-13, snddrv/src/opn.c, fm.c, ssg.c                 */
/* ======================================================================== */

#include "fm.h"
#include "io.h"
#include "memmap.h"

#define OPN_OPERATOR_COUNT 4
#define OPN_OPERATOR_REG_STRIDE 4
#define OPN_BLOCK_SHIFT 3
#define OPN_FNUM_HI_MASK 0x07
#define SSG_PERIOD_HI_MASK 0x0F
#define SSG_VOLUME_MASK 0x1F
#define SSG_NOISE_MASK 0x1F
#define SSG_TONE_REG_STRIDE 2

#define OPN_ADDR_WAIT_COUNT 6
#define OPN_DATA_WAIT_COUNT 20
#define FM_TONE_TL_OFF 4
#define FM_TONE_KS_AR_OFF 8
#define FM_TONE_DR_OFF 12
#define FM_TONE_SR_OFF 16
#define FM_TONE_SL_RR_OFF 20
#define FM_TONE_FB_CON_OFF 24


/* ======================================================================== */
/*  OPN低レベルI/O — snddrv/src/opn.c 移植                                 */
/* ======================================================================== */

/* BUSY 待ちの上限。1 周 ≈ 1µs (inp) なので 10000 回 ≈ 10ms。
 * OPN の BUSY は通常数µs で落ちる。落ちないのは音源が居ない/固まって
 * いる場合で、無期限スピンだとカーネルがハングする。 */
#define OPN_BUSY_TIMEOUT  10000UL

static int opn_ready(void)
{
    for (u32 i = 0; i < OPN_BUSY_TIMEOUT; ++i)
        if (!(inp(OPN_STATUS) & OPN_BUSY)) return 1;
    return 0;
}

static uchar ssg_mixer_state = SSG_MIXER_ALL_OFF;

/* OPNレジスタ書込み
 * PC9800Bible推奨手順:
 * 1. BUSYフラグ確認
 * 2. アドレス送信 + ウェイト6回
 * 3. データ送信 + ウェイト20回
 *
 * アドレスラッチ→データ送信の 2 段階なので、途中で割り込まれて別の
 * opn_write が走るとレジスタ番号が化ける。レジスタ選択からデータ転送までを irq_save で保護する。 */
static int opn_write_checked(uchar reg, uchar val)
{
    unsigned int flags;
    flags = irq_save();

    if (!opn_ready()) {
        /* 音源が応答しない。書いても捨てられるだけなので諦める */
        irq_restore(flags);
        return 0;
    }

    /* アドレス送信 */
    outp(OPN_ADDR, reg);

    /* ウェイト: 約 3.6μs */
    io_wait_n(OPN_ADDR_WAIT_COUNT);

    if (!opn_ready()) {
        irq_restore(flags);
        return 0;
    }
    /* データ送信 */
    outp(OPN_DATA, val);
    if (reg == SSG_REG_MIXER) ssg_mixer_state = val;

    /* データ書込み後のウェイト: 約 12μs */
    io_wait_n(OPN_DATA_WAIT_COUNT);

    irq_restore(flags);
    return 1;
}

void opn_write(uchar reg, uchar val) { (void)opn_write_checked(reg, val); }

/* YM2203 Key-ON/OFF has no readback. Confirm every silence write passed
 * BUSY and the final write completed; timeout cannot certify quiescence. */
int fm_quiet(void)
{
    unsigned int flags = irq_save();
    int ok = 1;
    for (int ch = 0; ch < OPN_CHANNEL_COUNT; ch++) {
        ok &= opn_write_checked(OPN_REG_KEY_ONOFF, (uchar)ch);
        ok &= opn_write_checked((uchar)(SSG_REG_VOL_A + ch), 0);
    }
    ok &= opn_write_checked(SSG_REG_MIXER, SSG_MIXER_ALL_OFF | SSG_MIXER_IO_FLAG);
    ok &= opn_ready();
    irq_restore(flags);
    return ok;
}

/* OPN初期化 — snddrv/src/opn.c opn_init() 移植 */
void opn_init(void)
{
    int i;

    /* SSGレジスタクリア (00H-0DH) */
    for (i = 0; i <= SSG_REG_ENV_SHAPE; i++) {
        opn_write((uchar)i, 0);
    }

    /* SSGミキサー: 全チャンネルOFF */
    opn_write(SSG_REG_MIXER, SSG_MIXER_ALL_OFF);

    /* FM全チャンネル Key-OFF */
    opn_write(OPN_REG_KEY_ONOFF, 0x00);
    opn_write(OPN_REG_KEY_ONOFF, 0x01);
    opn_write(OPN_REG_KEY_ONOFF, OPN_CHANNEL_COUNT - 1);

    /* FMレジスタクリア (30H-B2H) */
    for (i = 0; i < OPN_CHANNEL_COUNT; i++) {
        int s;
        for (s = 0; s < OPN_OPERATOR_COUNT; s++) {
            opn_write(OPN_REG_DT_ML + s * OPN_OPERATOR_REG_STRIDE + i, 0);         /* DT/ML */
            opn_write(OPN_REG_TL   + s * OPN_OPERATOR_REG_STRIDE + i, OPN_TL_MAX); /* TL=最大減衰 */
            opn_write(OPN_REG_KS_AR + s * OPN_OPERATOR_REG_STRIDE + i, 0);         /* KS/AR */
            opn_write(OPN_REG_DR   + s * OPN_OPERATOR_REG_STRIDE + i, 0);          /* DR */
            opn_write(OPN_REG_SR   + s * OPN_OPERATOR_REG_STRIDE + i, 0);          /* SR */
            opn_write(OPN_REG_SL_RR + s * OPN_OPERATOR_REG_STRIDE + i, 0xFF);      /* SL/RR */
        }
        opn_write(OPN_REG_FNUM_LO + i, 0);   /* F-Number下位 */
        opn_write(OPN_REG_FNUM_HI + i, 0);   /* Block/F-Number上位 */
        opn_write(OPN_REG_FB_CON  + i, 0);   /* FB/CON */
    }

    /* タイマー停止 */
    opn_write(OPN_REG_TIMER_CTRL, OPN_TIMER_STOP);

    /* プリスケーラ標準設定 (FM:1/6, SSG:1/4) */
    opn_write(OPN_REG_PRESCALER, 0);
}

/* ======================================================================== */
/*  FM音源 — snddrv/src/fm.c 移植                                          */
/* ======================================================================== */

/* 事前定義音色テーブル — snddrv から移植 (動作確認済み) */
static const uchar tone_piano[] = {
    /* DT/ML */  0x71, 0x0D, 0x33, 0x01,
    /* TL    */  0x23, 0x2D, 0x26, 0x00,
    /* KS/AR */  0x1F, 0x1F, 0x1F, 0x1F,
    /* DR    */  0x15, 0x0B, 0x0F, 0x05,
    /* SR    */  0x01, 0x03, 0x03, 0x02,
    /* SL/RR */  0x11, 0xA6, 0x57, 0x17,
    /* FB/CON */ 0x20
};

static const uchar tone_bell[] = {
    /* DT/ML */  0x0E, 0x01, 0x0E, 0x01,
    /* TL    */  0x30, 0x00, 0x30, 0x00,
    /* KS/AR */  0x1F, 0x1F, 0x1F, 0x1F,
    /* DR    */  0x0C, 0x00, 0x0C, 0x00,
    /* SR    */  0x02, 0x02, 0x02, 0x02,
    /* SL/RR */  0x36, 0xA7, 0x36, 0xA7,
    /* FB/CON */ 0x07
};

static const uchar tone_organ[] = {
    /* DT/ML */  0x01, 0x02, 0x04, 0x01,
    /* TL    */  0x1A, 0x1D, 0x1C, 0x00,
    /* KS/AR */  0x1F, 0x1F, 0x1F, 0x1F,
    /* DR    */  0x00, 0x00, 0x00, 0x00,
    /* SR    */  0x00, 0x00, 0x00, 0x00,
    /* SL/RR */  0x0F, 0x0F, 0x0F, 0x0F,
    /* FB/CON */ 0x34
};

static const uchar tone_brass[] = {
    /* DT/ML */  0x71, 0x34, 0x01, 0x01,
    /* TL    */  0x1E, 0x2A, 0x27, 0x00,
    /* KS/AR */  0x1F, 0x1F, 0x1F, 0x1D,
    /* DR    */  0x12, 0x0E, 0x10, 0x05,
    /* SR    */  0x00, 0x00, 0x00, 0x00,
    /* SL/RR */  0x1F, 0x1F, 0x1F, 0x1F,
    /* FB/CON */ 0x38
};

static const uchar tone_strings[] = {
    /* DT/ML */  0x01, 0x01, 0x01, 0x01,
    /* TL    */  0x22, 0x1E, 0x20, 0x00,
    /* KS/AR */  0x15, 0x15, 0x15, 0x15,
    /* DR    */  0x03, 0x03, 0x03, 0x03,
    /* SR    */  0x00, 0x00, 0x00, 0x00,
    /* SL/RR */  0x1F, 0x1F, 0x1F, 0x1F,
    /* FB/CON */ 0x30
};

static const uchar *tone_table[] = {
    tone_piano,    /* 0: ピアノ */
    tone_bell,     /* 1: ベル */
    tone_organ,    /* 2: オルガン */
    tone_brass,    /* 3: ブラス */
    tone_strings   /* 4: ストリングス */
};
#define NUM_TONES  5

/* F-Numberテーブル (オクターブ4基準) — snddrv から移植 */
static const u16 fnumber_table[FM_SEMITONES_PER_OCTAVE] = {
    617,   /* C   */
    653,   /* C#  */
    692,   /* D   */
    733,   /* D#  */
    777,   /* E   */
    823,   /* F   */
    872,   /* F#  */
    924,   /* G   */
    979,   /* G#  */
    1038,  /* A   */
    1100,  /* A#  */
    1165   /* B   */
};

/* The resident facade already acquired this device for the current owner. */
void (*fm_write_failed)(void);
int (*fm_read_mixer)(void);

static int legacy_write(uchar reg, uchar val)
{
    if (opn_write_checked(reg, val))
        return 1;
    if (fm_write_failed) fm_write_failed();
    return 0;
}

/* FM音色設定 — snddrv fm_set_tone() 移植 */
void fm_set_tone(int ch, const uchar *tone_data)
{
    if (ch < 0 || ch >= OPN_CHANNEL_COUNT) return;

    int s;
    static const int slot_offset[] = {0, 8, 4, 12};

    for (s = 0; s < OPN_OPERATOR_COUNT; s++) {
        int ofs = slot_offset[s];
        if (!legacy_write(OPN_REG_DT_ML + ofs + ch, tone_data[s])) return;       /* DT/ML */
        if (!legacy_write(OPN_REG_TL    + ofs + ch, tone_data[FM_TONE_TL_OFF + s])) return;   /* TL */
        if (!legacy_write(OPN_REG_KS_AR + ofs + ch, tone_data[FM_TONE_KS_AR_OFF + s])) return;   /* KS/AR */
        if (!legacy_write(OPN_REG_DR    + ofs + ch, tone_data[FM_TONE_DR_OFF + s])) return;  /* DR */
        if (!legacy_write(OPN_REG_SR    + ofs + ch, tone_data[FM_TONE_SR_OFF + s])) return;  /* SR */
        if (!legacy_write(OPN_REG_SL_RR + ofs + ch, tone_data[FM_TONE_SL_RR_OFF + s])) return;  /* SL/RR */
    }
    if (!legacy_write(OPN_REG_FB_CON + ch, tone_data[FM_TONE_FB_CON_OFF])) return;               /* FB/CON */
}

/* FM音色設定 (番号指定) */
void fm_set_tone_num(int ch, int tone_num)
{
    if (tone_num >= 0 && tone_num < NUM_TONES) {
        fm_set_tone(ch, tone_table[tone_num]);
    }
}

/* FM Key-ON — snddrv fm_note_on() 移植 */
void fm_note_on(int ch, int note)
{
    if (ch < 0 || ch >= OPN_CHANNEL_COUNT) return;

    int block, key;
    u16 fnum;

    if (note < 0) return;
    block = note / FM_SEMITONES_PER_OCTAVE;
    key = note % FM_SEMITONES_PER_OCTAVE;
    if (block > 7) block = 7;

    fnum = fnumber_table[key];

    /* F-Number上位(+Block)を先に書く (PC9800Bible指定) */
    if (!legacy_write(OPN_REG_FNUM_HI + ch, (uchar)((block << OPN_BLOCK_SHIFT) | ((fnum >> 8) & OPN_FNUM_HI_MASK)))) return;
    if (!legacy_write(OPN_REG_FNUM_LO + ch, (uchar)(fnum & 0xFF))) return;

    /* Key-ON: 全4スロットON */
    (void)legacy_write(OPN_REG_KEY_ONOFF, (uchar)(OPN_KEY_ALL_SLOTS | ch));
}

/* FM Key-OFF */
void fm_note_off(int ch)
{
    if (ch < 0 || ch >= OPN_CHANNEL_COUNT) return;

    (void)legacy_write(OPN_REG_KEY_ONOFF, (uchar)(0x00 | ch));
}

/* FM全チャンネルKey-OFF */
void fm_all_off(void)
{
    fm_note_off(0);
    fm_note_off(1);
    fm_note_off(2);
}

/* ======================================================================== */
/*  SSG音源 — snddrv/src/ssg.c 移植                                       */
/* ======================================================================== */

void ssg_tone(int ch, u16 period)
{
    if (ch < 0 || ch >= OPN_CHANNEL_COUNT) return;

    uchar lo_reg = (uchar)(ch * SSG_TONE_REG_STRIDE);
    uchar hi_reg = (uchar)(ch * SSG_TONE_REG_STRIDE + 1);
    if (!legacy_write(lo_reg, (uchar)(period & 0xFF))) return;
    (void)legacy_write(hi_reg, (uchar)((period >> 8) & SSG_PERIOD_HI_MASK));
}

static int volume_checked(int ch, uchar vol)
{
    unsigned int flags = irq_save();
    int ok = 1;
    vol &= SSG_VOLUME_MASK;
    /* HAL quiet and raw register writes can also change the mixer. */
    if (vol && fm_read_mixer) {
        int mixer = fm_read_mixer();
        if (mixer < 0) {
            fm_write_failed();
            irq_restore(flags);
            return 0;
        }
        ssg_mixer_state = (uchar)mixer;
    }
    if (vol && (ssg_mixer_state & (1u << ch)))
        ok = legacy_write(SSG_REG_MIXER,
                          (ssg_mixer_state & ~(1u << ch)) | SSG_MIXER_IO_FLAG);
    if (ok)
        ok = legacy_write(SSG_REG_VOL_A + (uchar)ch, vol);
    irq_restore(flags);
    return ok;
}

void ssg_volume(int ch, uchar vol)
{
    if (ch < 0 || ch >= OPN_CHANNEL_COUNT) return;
    (void)volume_checked(ch, vol);
}

void ssg_noise(uchar period)
{
    (void)legacy_write(SSG_REG_NOISE, period & SSG_NOISE_MASK);
}

void ssg_mixer(uchar mask)
{
    (void)legacy_write(SSG_REG_MIXER, mask | SSG_MIXER_IO_FLAG);
}

void ssg_all_off(void)
{
    for (int ch = 0; ch < OPN_CHANNEL_COUNT; ++ch)
        if (!volume_checked(ch, 0)) return;
    ssg_mixer(SSG_MIXER_ALL_OFF);
}
