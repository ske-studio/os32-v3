/* ======================================================================== */
/*  FDC.C — PC-98 フロッピーディスクコントローラ直接制御ドライバ            */
/*                                                                          */
/*  µPD765A FDC を I/Oポート経由で直接制御する。                            */
/*  データ転送は DMA (µPD8237A ch2) を使用する。                            */
/*  BIOS (INT 1Bh) やリアルモード遷移は一切不要。                          */
/*                                                                          */
/*  出典: PC9800Bible §2-9, §1-5, §4-3 / OSDev Wiki FDC                    */
/* ======================================================================== */

#include "fdc.h"
#include "pc98.h"
#include "../dma/dma8237.h"   /* 8237 の共通部 (票 TASK_HAL_WIRING §1-2)、dma_range_ok */
#include "memmap.h"    /* V2P (TASK_T1_LEDGER §3-4) */
#include "io.h"
#include "irq.h"
#include "kstring.h"
#include "kprintf.h"

/* ST0 のビット定義と判定は drivers/block/fdc_decide.h / .c にある
 * (I/O を引かずにホストで試験するため — tools/tests/fdc_seek_tdd.md)。 */
#define FDC_DTL_UNUSED  0xFF    /* DTL未使用時の値 */

/* 外部: tick_count (idt.c) */
extern volatile u32 tick_count;

/* IRQ11完了フラグ */
static volatile u32 fdc_irq_fired = 0;

/* 排他の完了通知。SIS と装置待ちは前景で行う。2 巡でも代入は冪等。 */
static int fdc_irq(unsigned int irq, void *arg)
{
    (void)irq;
    (void)arg;
    fdc_irq_fired = 1;
    return IRQ_HANDLED;
}

/* 最後に SEEK / RECALIBRATE で見た ST0。診断行でしか使わない。
 * R/W が seek フェーズで失敗したとき、READ/WRITE の results[] は空のままな
 * ので、**こちらを出さないと「なぜシークが失敗したか」が出ない**
 * (NR = 媒体無しなのか、本当に動かなかったのかが読めない)。 */
static u8 s_last_seek_st0 = 0;

/* 直近の fdc_init() が見たもの (fdc_get_last_init_status で取り出す)。
 * 起動時の状態行に出すためだけの記録で、制御には使わない。
 * kprintf の診断行が実機の画面に 1 行も出なかった (属性が PC/AT 流の
 * まま属性 VRAM へ書かれていた) ので、**tvram_print で必ず出す**
 * ための値をここに残す (2026-09-22)。 */
static int s_init_rc = -1;
static u8  s_init_st0 = 0;
static u8  s_init_0439_before = 0;
static u8  s_init_0439_after = 0;

/* fdc_seek / fdc_recalibrate_st0 の内部戻り値。
 * NR (媒体もドライブも無い) だけは **リトライも回復もしない** ので
 * 他の失敗と分ける。 */
#define FDC_RC_NOT_READY  (-5)

/* FD の受け皿 (fdc.h の FDC_BUF_BYTES の注記)。
 * 1 本の静的な領域を、DMA の窓 1 つ (1 トラック分) と先読みのスロット
 * (FDC_TRACK_SLOTS 本) に割る。窓の位置は fdc_buf_layout() が**実行時に**
 * 決めるので、64KB 境界をまたがない ([HW2]) ことが揃え指定にも
 * リンク順にも依存しない。16KB 揃えにしていたころは最大 16KB の
 * 詰め物が .bss に出ていた。
 * 置き場は BSS のまま (DMA プールは使わない — fdc.h の注記)。装置へ渡す
 * 物理番地は V2P で作り、16MB・64KB の検査は dma_alloc と**同じ関数**
 * (dma_range_ok) で見る (TASK_T1_LEDGER §4-3)。 */
static u8 s_fdbuf[FDC_BUF_BYTES];
static u8 *s_dma = 0;       /* DMA の窓 (FDC_DMA_BUF_SIZE) */
static u32 s_dma_pa = 0;    /* V2P(s_dma) — 装置へ渡す物理番地 */
static u8 *s_slots = 0;     /* 先読みのスロット + セクタキャッシュ (連続) */

static void fdc_buf_setup(void)
{
    u32 dma_off, rest_off;

    if (s_dma) return;
    (void)fdc_buf_layout(V2P(s_fdbuf), FDC_BUF_BYTES, FDC_DMA_BUF_SIZE,
                         &dma_off, &rest_off);
    s_dma = s_fdbuf + dma_off;
    s_dma_pa = V2P(s_dma);
    s_slots = s_fdbuf + rest_off;
    /* 起きない (領域は 64KB より短く、長さは窓の 3 倍で、カーネルは
     * 16MB より下) が、起きたら**黙って別の番地を壊さない**よう言う
     * (dma_chan_setup も同じ条件で断るので、転送は出ない)。 */
    if (!dma_range_ok(s_dma_pa, FDC_DMA_BUF_SIZE, DMA_PHYS_LIMIT))
        kprintf(KLOG_ATTR, "[fdc] DMA window %08x crosses 64KB or 16MB\n",
                s_dma_pa);
}

static u8 *fdc_dma_buf(void)
{
    fdc_buf_setup();
    return s_dma;
}

static u32 fdc_dma_phys(void)
{
    fdc_buf_setup();
    return s_dma_pa;
}
#define dma_buffer (fdc_dma_buf())

u8 *fdc_track_slot(int i)
{
    if (i < 0 || i >= FDC_TRACK_SLOTS) return (u8 *)0;
    fdc_buf_setup();
    return s_slots + (u32)i * FDC_TRACK_MAX_BYTES;
}

u8 *fdc_sector_slot(int j)
{
    if (j < 0 || j >= FDC_SECTOR_SLOTS) return (u8 *)0;
    fdc_buf_setup();
    return s_slots + (u32)FDC_TRACK_SLOTS * FDC_TRACK_MAX_BYTES
         + (u32)j * FDC_SECTOR_SLOT_BYTES;
}

/* ======================================================================== */
/*  覚えている現在のシリンダ (2026-09-24、同じシリンダならシークを省く)     */
/*                                                                          */
/*  旧 fdc_read_sector_geom は 1 セクタごとに SEEK + 20ms 待ちを出していた。 */
/*  同じシリンダなら SEEK も整定待ちも要らない。-1 = 知らない。             */
/*                                                                          */
/*  **捨てる時**: エラー (読み書きの最終失敗 / シーク失敗 / まとめ読みの    */
/*  失敗)、FDC リセット (fdc_reset / fdc_abort_transfer — 回復も含む)、      */
/*  RECALIBRATE の失敗、メディアの変更 (fdc_set_media / fdc_set_3mode)、    */
/*  ドライブの切り替え (前回と違うドライブへ SEEK / RECALIBRATE するとき     */
/*  全部)。RECALIBRATE が通れば 0、SEEK が通ればそのシリンダ。              */
/*                                                                          */
/*  覚えた値が実際とずれていても**別のシリンダを読み書きすることはない** —   */
/*  READ / WRITE DATA は ID 部の C を照合するので、ずれていれば ND / WC で   */
/*  失敗し、上の「エラーで捨てる」から次はシークし直す。                    */
/* ======================================================================== */
#define FDC_MAX_DRIVES   4          /* µPD765A の US1/US0 */
#define FDC_CYL_UNKNOWN  (-1)
static int s_known_cyl[FDC_MAX_DRIVES] = {
    FDC_CYL_UNKNOWN, FDC_CYL_UNKNOWN, FDC_CYL_UNKNOWN, FDC_CYL_UNKNOWN
};
static int s_last_drv = -1;         /* 最後に SEEK / RECALIBRATE したドライブ */

static void fdc_forget_all(void)
{
    int i;
    for (i = 0; i < FDC_MAX_DRIVES; i++) s_known_cyl[i] = FDC_CYL_UNKNOWN;
}

static void fdc_forget_cyl(int drv)
{
    if (drv >= 0 && drv < FDC_MAX_DRIVES) s_known_cyl[drv] = FDC_CYL_UNKNOWN;
}

static void fdc_note_cyl(int drv, int cyl)
{
    if (drv >= 0 && drv < FDC_MAX_DRIVES) s_known_cyl[drv] = cyl;
}

/* 前回と違うドライブを触るなら、覚えている値を全部捨てる。 */
static void fdc_note_drive(int drv)
{
    if (drv != s_last_drv) fdc_forget_all();
    s_last_drv = drv;
}

int fdc_get_known_cyl(int drv)
{
    if (drv < 0 || drv >= FDC_MAX_DRIVES) return FDC_CYL_UNKNOWN;
    return s_known_cyl[drv];
}

/* 診断の数 (fdc_get_stats / fdc_print_stats)。 */
static struct fdc_stats s_stats;

/* ドライブの中身の世代 (fdc.h の fdc_media_gen の注記)。 */
static u32 s_media_gen[FDC_MAX_DRIVES];

static void fdc_bump_gen(int drv)
{
    if (drv >= 0 && drv < FDC_MAX_DRIVES) s_media_gen[drv]++;
}

u32 fdc_media_gen(int drv)
{
    if (drv < 0 || drv >= FDC_MAX_DRIVES) return 0;
    return s_media_gen[drv];
}

void fdc_get_stats(struct fdc_stats *out)
{
    if (out) *out = s_stats;
}
/* まとめ読みの失敗は最終失敗ではない (1 セクタずつ読み直す) ので、
 * 行は最初の数回だけ出す。**黙りはしない** — 実機で束ねた読みが毎回
 * 失敗していると、速度が元に戻ったことが画面で分からない。 */
#define FDC_MULTI_FAIL_REPORT_MAX  4

#include "fdc_io.inc"

/* ======================================================================== */
/*  モーター制御                                                            */
/* ======================================================================== */
static void fdc_motor_on(void)
{
    /* スタティックフラグで前回の状態を記憶し、初回のみスピンアップ待ちを行う。
     * NP21/W の 0x94 リードはリードスイッチを返すため MTON 状態は判別不能。 */
    static int motor_running = 0;

    /* FRY (bit6) を必ず添える — 実機では RDY が上がっていないと µPD765A は
     * コマンドを実行せず ST0 の NR を立てて返す (fdc.h の CTRL_FRY の注記)。 */
    outp(FDC_CTRL, CTRL_FRY | CTRL_MTON | CTRL_DMAE);
    if (!motor_running) {
        /* 初回のみ 約300ms スピンアップ待ち (100Hzタイマで30tick) */
        u32 start = tick_count;
        while ((tick_count - start) < FDC_MOTOR_SPINUP_TICKS) { /* 何もしない */ }
        motor_running = 1;
    }
}

#include "fdc_seek_reset.inc"

#include "fdc_transfer.inc"

void fdc_print_stats(const char *tag)
{
    const struct fdc_stats *t = &s_stats;

    if (t->seek_issued + t->seek_skipped + t->single_reads + t->multi_ok
        + t->multi_fail + t->writes == 0) {
        return;
    }
    /* 80 桁に収めるため 2 行に分ける (1 行だと画面の幅で切れた)。 */
    kprintf(KLOG_ATTR, "[fdc] %s: seek=%u skip=%u recal=%u tmo=%u foreign=%u rdy=%u\n",
            tag, (unsigned int)t->seek_issued, (unsigned int)t->seek_skipped,
            (unsigned int)t->recal_issued, (unsigned int)t->seek_timeout,
            (unsigned int)t->sis_foreign, (unsigned int)t->ready_change);
    kprintf(KLOG_ATTR, "[fdc] %s: multi=%u/%u nr=%u single=%u retry=%u write=%u\n",
            tag, (unsigned int)t->multi_ok, (unsigned int)t->multi_fail,
            (unsigned int)t->multi_nr, (unsigned int)t->single_reads,
            (unsigned int)t->single_retries, (unsigned int)t->writes);
}

/* ======================================================================== */
/*  セクタ読み込み (ドライブの現在ジオメトリを使う — 既存API互換)            */
/* ======================================================================== */
int fdc_read_sector(int drv, int cyl, int head, int sect, void *buf)
{
    return fdc_read_sector_geom(drv, cyl, head, sect, fdc_get_geom(drv), buf);
}

/* ======================================================================== */
/*  セクタ書き込み (ドライブの現在ジオメトリを使う — 既存API互換)            */
/* ======================================================================== */
int fdc_write_sector(int drv, int cyl, int head, int sect, const void *buf)
{
    return fdc_write_sector_geom(drv, cyl, head, sect, fdc_get_geom(drv), buf);
}

/* ======================================================================== */
/*  fdc_init — FDC初期化                                                    */
/* ======================================================================== */
int fdc_init(void)
{
    int ret;
    u8 st0 = 0;

    /* [HW2] DMA の窓を 64KB 境界をまたがない位置に決める
     * (fdc_buf_setup — 取れなければそこで言う)。 */
    fdc_buf_setup();

    /* 0439h (1MB 超への DMA 禁止) は **dma8237_init() が起動の早い段階で
     * 落としている** (票 §1-2)。FDC だけの都合ではなくなったので、ここは
     * 起動時の状態行の書式を変えないための**写し**だけ。 */
    dma_above_1mb_raw(&s_init_0439_before, &s_init_0439_after);

    /* 表の公開と IRQ11 の有効化を motor/reset より先に済ませる。
     * 同一登録での再初期化と、reset/recalibrate 失敗後も登録を維持する。 */
    ret = irq_register(FDC_IRQ, fdc_irq, 0, 0);
    if (ret != 0 && ret != IRQ_ERR_EXIST) {
        s_init_rc = -1;
        s_init_st0 = 0;
        return -1;
    }

    /* 前回の取りこぼし IRQ をクリア (冪等化対策) */
    fdc_irq_fired = 0;

    /* モーターON (既にONの場合はスピンアップ待ちをスキップ) */
    fdc_motor_on();

    /* FDCリセット + Specify */
    ret = fdc_reset();
    if (ret != 0) {
        s_init_rc = ret;
        s_init_st0 = st0;
        return ret;
    }

    /* Recalibrate (ヘッドをシリンダ0に移動)。
     * fdc_recalibrate_st0 は中で SIS 排水 → RECALIBRATE を
     * FDC_RECAL_ATTEMPTS 回 (EC のときだけ) 繰り返す。
     * ここでの 1 回目失敗はリセット直後の安定化待ち (100ms) 後にもう一度。 */
    ret = fdc_recalibrate_st0(0, &st0);
    if (ret != 0) {
        u32 start = tick_count;
        while ((tick_count - start) < FDC_RECAL_RESET_SETTLE_TICKS) { /* 100ms ウェイト */ }
        ret = fdc_recalibrate_st0(0, &st0);
    }
    if (ret != 0) {
        /* [V4] 失敗はそのまま言う。ここで諦めると MOUNT の root panic に
         * なるが、画面には「なぜ」が出ていなかった。 */
        kprintf(KLOG_ATTR, "[fdc] recalibrate drv=%d rc=%d st0=%02x\n", 0, ret, st0);
    }

    /* ドライブ1は未接続時にタイムアウトするためエラーは無視する。
     * **黙って捨てない** — 失敗した完了通知は次の排水で回収される。
     * FRY を立てたので、未装着のドライブは NR ではなく EC を返すように
     * なる (NP21/W の FDC_Recalibrate も実機も同じ形)。EC は再試行の
     * 合図なので FDC_RECAL_ATTEMPTS 回ぶん出してから失敗になるが、
     * どちらも IRQ は上がるので空待ちはしない。戻り値は元から捨てる。 */
    (void)fdc_recalibrate(1);

    s_init_rc = ret;
    s_init_st0 = st0;
    return ret;
}

/* ======================================================================== */
/*  直近の fdc_init() の観測値 (起動時の状態行用。fdc.h に説明)             */
/* ======================================================================== */
int fdc_get_last_init_status(u8 *st0, u8 *p0439_before, u8 *p0439_after)
{
    if (st0) *st0 = s_init_st0;
    if (p0439_before) *p0439_before = s_init_0439_before;
    if (p0439_after) *p0439_after = s_init_0439_after;
    return s_init_rc;
}

/* ======================================================================== */
/*  既知メディアジオメトリ定義 (fdc.h で extern 宣言済み)                    */
/* ======================================================================== */
const struct fdc_geom fdc_geom_2hd = {
    77, 2, 8, 3, 1024, 0x74, 0x90
};
const struct fdc_geom fdc_geom_2dd_640 = {
    80, 2, 8, 2, 512, 0x2A, 0x10  /* GAP3=0x2A: MFM 512B/sec 標準値 */
};
const struct fdc_geom fdc_geom_2dd_720 = {
    80, 2, 9, 2, 512, 0x2A, 0x10
};
const struct fdc_geom fdc_geom_2d_256 = {
    77, 2, 16, 1, 256, 0x0E, 0x90  /* GAP3=0x0E: MFM 256B/sec, DAUA=0x90 */
};
/* 1.44MB (PC/AT 標準の 2HD)。PC-98 では DA/UA 0x30 系の
 * 「1.44MB 対応両用インタフェース」でアクセスする (PC9800Bible 表 2-34)。
 * GAP3=0x1B は NP21/W の src/bios/fdfmt.h fdfmt144[] の MFM R/W GPL 値。 */
const struct fdc_geom fdc_geom_144 = {
    80, 2, 18, 2, 512, 0x1B, 0x30
};

/* ======================================================================== */
/*  ドライブごとの現在ジオメトリ                                            */
/*                                                                          */
/*  既定は 2HD 1232KB — 既存構成 (1232KB の FD / NHD 起動) を変えない。      */
/* ======================================================================== */
static const struct fdc_geom *s_geom[FDC_DRIVE_COUNT] = {
    &fdc_geom_2hd, &fdc_geom_2hd
};

/* このドライバが 04BEh で 1.44MB モードへ切り替えたか (ドライブごと)。
 * **触っていないポートを戻そうとしない**ため。2HD だけの機種で 04BEh を
 * 書くと、00BEh のデコードイメージが出る機種では 1MB/640KB I/F の切替に
 * 化ける恐れがある (io_fdd.md の注意)。 */
static int s_3mode_on[FDC_DRIVE_COUNT] = { 0, 0 };

/* ======================================================================== */
/*  3モードFD I/F (I/O 04BEh) — 1.44MB アクセスモードの切り替え             */
/* ======================================================================== */
int fdc_set_3mode(int drv, int on)
{
    u8 sel, v;

    if (drv < 0 || drv > 1) {
        return -1;
    }
    sel = (u8)((u8)drv << FDC_3M_DRV_SHIFT);

    /* アクセスモードが変わる = 別のメディアとして扱う。 */
    fdc_forget_cyl(drv);

    /* ドライブ指定 + モード指定を 1 回で書く (bit4=1 で bit0 が有効)。 */
    outp(FDC_IO_3MODE,
         (u8)(sel | FDC_3M_APPLY | (on ? FDC_3M_MODE_144 : 0)));

    /* 読む前にドライブを指定し直す (bit4=0 = 無動作)。資料の指示どおり。 */
    outp(FDC_IO_3MODE, sel);
    v = (u8)inp(FDC_IO_3MODE);

    /* **FFh 判定で搭載を見ない。** 要求した値になったかだけを見る。 */
    if (on) {
        if (!(v & FDC_3M_CUR_144)) {
            return -1;
        }
    } else {
        if (v & FDC_3M_CUR_144) {
            return -1;
        }
    }
    s_3mode_on[drv] = on ? 1 : 0;
    return 0;
}

const struct fdc_geom *fdc_get_geom(int drv)
{
    if (drv < 0 || drv > 1) {
        return &fdc_geom_2hd;
    }
    return s_geom[drv];
}

int fdc_set_media(int drv, fdc_media_t media)
{
    const struct fdc_geom *g;

    if (drv < 0 || drv > 1) {
        return -1;
    }
    switch (media) {
    case FDC_MEDIA_2HD_1232: g = &fdc_geom_2hd;      break;
    case FDC_MEDIA_2DD_640:  g = &fdc_geom_2dd_640;  break;
    case FDC_MEDIA_2DD_720:  g = &fdc_geom_2dd_720;  break;
    case FDC_MEDIA_2D_256:   g = &fdc_geom_2d_256;   break;
    case FDC_MEDIA_2HD_1440: g = &fdc_geom_144;      break;
    default:                 return -1;
    }
    s_geom[drv] = g;
    /* メディアが変わった。覚えているシリンダは信じず、先読みも捨てさせる。 */
    fdc_forget_cyl(drv);
    fdc_bump_gen(drv);

    /* アクセスモードをメディアに合わせる。**1.44MB に入るときと、自分で
     * 入れたものを戻すときだけ** 04BEh に触る (上の s_3mode_on の注記)。 */
    if (media == FDC_MEDIA_2HD_1440) {
        fdc_set_3mode(drv, 1);
    } else if (s_3mode_on[drv]) {
        fdc_set_3mode(drv, 0);
    }
    return 0;
}

int fdc_set_media_by_daua(int drv, u32 daua)
{
    /* DA/UA の上位ニブルが装置種別。0x30 / 0xB0 系が 1.44MB
     * (undocumented/memsys.md の 0000:0584h DISK_BOOT)。
     * 知らない値では 2HD のままにする — 起動経路を勝手に変えない。 */
    switch (daua & BIOS_DA_TYPE_MASK) {
    case BOOT_DRIVE_FDD_144:
    case BIOS_DA_FD_144_ALT:
        return fdc_set_media(drv, FDC_MEDIA_2HD_1440);
    default:
        return 0;
    }
}

