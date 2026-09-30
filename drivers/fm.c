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

/* ======================================================================== */
/*  OPN低レベルI/O — snddrv/src/opn.c 移植                                 */
/* ======================================================================== */

/* BUSY 待ちの上限。1 周 ≈ 1µs (inp) なので 10000 回 ≈ 10ms。
 * OPN の BUSY は通常数µs で落ちる。落ちないのは音源が居ない/固まって
 * いる場合で、無期限スピンだと ISR (snd_tick) ごとカーネルがハングする。 */
#define OPN_BUSY_TIMEOUT  10000UL

/* OPNレジスタ書込み
 * PC9800Bible推奨手順:
 * 1. BUSYフラグ確認
 * 2. アドレス送信 + ウェイト6回
 * 3. データ送信 + ウェイト20回
 *
 * アドレスラッチ→データ送信の 2 段階なので、途中で割り込まれて別の
 * opn_write が走るとレジスタ番号が化ける。通常コンテキストと ISR
 * (snd_tick) の両方から呼ばれるため、全体を irq_save で保護する。 */
void opn_write(uchar reg, uchar val)
{
    unsigned int flags;
    u32 guard;

    flags = irq_save();

    /* BUSY待ち (タイムアウト付き) */
    guard = OPN_BUSY_TIMEOUT;
    while ((inp(OPN_STATUS) & OPN_BUSY) && --guard != 0);
    if (guard == 0) {
        /* 音源が応答しない。書いても捨てられるだけなので諦める */
        irq_restore(flags);
        return;
    }

    /* アドレス送信 */
    outp(OPN_ADDR, reg);

    /* ウェイト: 約 3.6μs */
    io_wait_n(6);

    /* データ送信 */
    outp(OPN_DATA, val);

    /* データ書込み後のウェイト: 約 12μs */
    io_wait_n(20);

    irq_restore(flags);
}

/* OPN初期化 — snddrv/src/opn.c opn_init() 移植 */
void opn_init(void)
{
    int i;

    /* SSGレジスタクリア (00H-0DH) */
    for (i = 0; i <= 0x0D; i++) {
        opn_write((uchar)i, 0);
    }

    /* SSGミキサー: 全チャンネルOFF */
    opn_write(SSG_REG_MIXER, SSG_MIXER_ALL_OFF);

    /* FM全チャンネル Key-OFF */
    opn_write(OPN_REG_KEY_ONOFF, 0x00);
    opn_write(OPN_REG_KEY_ONOFF, 0x01);
    opn_write(OPN_REG_KEY_ONOFF, 0x02);

    /* FMレジスタクリア (30H-B2H) */
    for (i = 0; i < 3; i++) {
        int s;
        for (s = 0; s < 4; s++) {
            opn_write(OPN_REG_DT_ML + s * 4 + i, 0);         /* DT/ML */
            opn_write(OPN_REG_TL   + s * 4 + i, OPN_TL_MAX); /* TL=最大減衰 */
            opn_write(OPN_REG_KS_AR + s * 4 + i, 0);         /* KS/AR */
            opn_write(OPN_REG_DR   + s * 4 + i, 0);          /* DR */
            opn_write(OPN_REG_SR   + s * 4 + i, 0);          /* SR */
            opn_write(OPN_REG_SL_RR + s * 4 + i, 0xFF);      /* SL/RR */
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
static const u16 fnumber_table[12] = {
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

/* FM音色設定 — snddrv fm_set_tone() 移植 */
void fm_set_tone(int ch, const uchar *tone_data)
{
    int s;
    static const int slot_offset[] = {0, 8, 4, 12};

    for (s = 0; s < 4; s++) {
        int ofs = slot_offset[s];
        opn_write(OPN_REG_DT_ML + ofs + ch, tone_data[s]);       /* DT/ML */
        opn_write(OPN_REG_TL    + ofs + ch, tone_data[4 + s]);   /* TL */
        opn_write(OPN_REG_KS_AR + ofs + ch, tone_data[8 + s]);   /* KS/AR */
        opn_write(OPN_REG_DR    + ofs + ch, tone_data[12 + s]);  /* DR */
        opn_write(OPN_REG_SR    + ofs + ch, tone_data[16 + s]);  /* SR */
        opn_write(OPN_REG_SL_RR + ofs + ch, tone_data[20 + s]);  /* SL/RR */
    }
    opn_write(OPN_REG_FB_CON + ch, tone_data[24]);               /* FB/CON */
}

/* FM音色設定 (番号指定) */
void fm_set_tone_num(int ch, int tone_num)
{
    if (tone_num < NUM_TONES) {
        fm_set_tone(ch, tone_table[tone_num]);
    }
}

/* FM Key-ON — snddrv fm_note_on() 移植 */
void fm_note_on(int ch, int note)
{
    int block, key;
    u16 fnum;

    block = note / 12;
    key = note % 12;
    if (block > 7) block = 7;

    fnum = fnumber_table[key];

    /* F-Number上位(+Block)を先に書く (PC9800Bible指定) */
    opn_write(OPN_REG_FNUM_HI + ch, (uchar)((block << 3) | ((fnum >> 8) & 0x07)));
    opn_write(OPN_REG_FNUM_LO + ch, (uchar)(fnum & 0xFF));

    /* Key-ON: 全4スロットON */
    opn_write(OPN_REG_KEY_ONOFF, (uchar)(OPN_KEY_ALL_SLOTS | ch));
}

/* FM Key-OFF */
void fm_note_off(int ch)
{
    opn_write(OPN_REG_KEY_ONOFF, (uchar)(0x00 | ch));
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
    uchar lo_reg = (uchar)(ch * 2);
    uchar hi_reg = (uchar)(ch * 2 + 1);
    opn_write(lo_reg, (uchar)(period & 0xFF));
    opn_write(hi_reg, (uchar)((period >> 8) & 0x0F));
}

void ssg_volume(int ch, uchar vol)
{
    opn_write(SSG_REG_VOL_A + (uchar)ch, vol & 0x1F);
}

void ssg_noise(uchar period)
{
    opn_write(SSG_REG_NOISE, period & 0x1F);
}

void ssg_mixer(uchar mask)
{
    opn_write(SSG_REG_MIXER, mask | SSG_MIXER_IO_FLAG);
}

void ssg_all_off(void)
{
    ssg_volume(0, 0);
    ssg_volume(1, 0);
    ssg_volume(2, 0);
    ssg_mixer(SSG_MIXER_ALL_OFF);
}

/* ======================================================================== */
/*  高レベルAPI                                                            */
/* ======================================================================== */

/* ウェイト (tickカウンタ使用) */
extern volatile u32 tick_count;
static void wait_ms(u32 ms)
{
    u32 ticks = ms / 10;   /* 100Hz → 10ms/tick */
    u32 start = tick_count;
    if (ticks == 0) ticks = 1;
    while ((tick_count - start) < ticks) {
        io_wait(); /* PC-98エミュレータ固有のhltハング対策 */
    }
}

/* 起動ジングル — ベル音色でC-E-G-C(上) アルペジオ */
void fm_startup_sound(void)
{
    opn_init();

    /* Ch1にベル音色設定 */
    fm_set_tone_num(0, 1);  /* ベル */

    /* C5 */
    fm_note_on(0, NOTE(5, N_C));
    wait_ms(120);
    fm_note_off(0);
    wait_ms(30);

    /* E5 */
    fm_note_on(0, NOTE(5, N_E));
    wait_ms(120);
    fm_note_off(0);
    wait_ms(30);

    /* G5 */
    fm_note_on(0, NOTE(5, N_G));
    wait_ms(120);
    fm_note_off(0);
    wait_ms(30);

    /* C6 (長め) */
    fm_note_on(0, NOTE(6, N_C));
    wait_ms(400);
    fm_note_off(0);
}

/* 簡易MML再生 — "CDEFGAB" + 数字(オクターブ) + "+#"(シャープ)
 * Ch1/ピアノ音色で単音再生 */
void fm_play_mml(const char *mml)
{
    int octave = 4;
    int note_map[7] = { N_A, N_B, N_C, N_D, N_E, N_F, N_G };
    /*                   A     B    C     D    E    F    G  */
    const char *p = mml;

    opn_init();
    fm_set_tone_num(0, 0);  /* ピアノ */

    while (*p) {
        char ch = *p++;
        int note_idx = -1;
        int sharp = 0;

        /* オクターブ指定 (O1-O8 or 数字) */
        if (ch == 'O' || ch == 'o') {
            if (*p >= '1' && *p <= '8') {
                octave = *p - '0';
                p++;
            }
            continue;
        }

        /* 音名 */
        if (ch >= 'A' && ch <= 'G') {
            note_idx = ch - 'A';
        } else if (ch >= 'a' && ch <= 'g') {
            note_idx = ch - 'a';
        } else if (ch == 'R' || ch == 'r') {
            /* 休符 */
            wait_ms(200);
            continue;
        } else if (ch == ' ' || ch == ',') {
            continue;  /* 区切り文字 */
        } else {
            continue;
        }

        /* シャープ */
        if (*p == '+' || *p == '#') {
            sharp = 1;
            p++;
        }

        /* Key-OFF → Key-ON */
        fm_note_off(0);
        wait_ms(20);
        fm_note_on(0, NOTE(octave, note_map[note_idx] + sharp));
        wait_ms(200);
    }

    fm_note_off(0);
}
