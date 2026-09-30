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
#include "dma8237.h"   /* 8237 の共通部 (票 TASK_HAL_WIRING §1-2) */
#include "io.h"
#include "kstring.h"
#include "kprintf.h"

/* ST0 のビット定義と判定は drivers/fdc_decide.h / .c にある
 * (I/O を引かずにホストで試験するため — tools/tests/fdc_seek_tdd.md)。 */
#define FDC_DTL_UNUSED  0xFF    /* DTL未使用時の値 */

/* 外部: tick_count (idt.c) */
extern volatile u32 tick_count;

/* IRQ11完了フラグ */
volatile u32 fdc_irq_fired = 0;

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
 * 詰め物が .bss に出ていた。 */
static u8 s_fdbuf[FDC_BUF_BYTES];
static u8 *s_dma = 0;       /* DMA の窓 (FDC_DMA_BUF_SIZE) */
static u8 *s_slots = 0;     /* 先読みのスロット + セクタキャッシュ (連続) */

static void fdc_buf_setup(void)
{
    u32 dma_off, rest_off;

    if (s_dma) return;
    if (fdc_buf_layout((u32)s_fdbuf, FDC_BUF_BYTES, FDC_DMA_BUF_SIZE,
                       &dma_off, &rest_off) != 0) {
        /* 起きない (領域は 64KB より短く、長さは窓の 3 倍) が、起きたら
         * **黙って別の番地を壊さない** よう言う。 */
        kprintf(0x07, "[fdc] no DMA window without crossing 64KB at %08x\n",
                (u32)s_fdbuf);
        dma_off = 0;
        rest_off = FDC_DMA_BUF_SIZE;
    }
    s_dma = s_fdbuf + dma_off;
    s_slots = s_fdbuf + rest_off;
}

static u8 *fdc_dma_buf(void)
{
    fdc_buf_setup();
    return s_dma;
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

/* ======================================================================== */
/*  内部ユーティリティ                                                      */
/* ======================================================================== */

/* 短い遅延 (I/Oポート読み出しで数µs) */
static void fdc_delay(void)
{
    io_wait();
    io_wait();
}

/* ======================================================================== */
/*  FDCコマンド送受信                                                       */
/* ======================================================================== */

/* MSRのRQM=1かつDIO=0 (CPU→FDC方向)を待ってからコマンドバイトを送信 */
static int fdc_send_byte(u8 val)
{
    int timeout;
    for (timeout = 0; timeout < FDC_TIMEOUT_LOOP; timeout++) {
        u8 msr = (u8)inp(FDC_MSR);
        if ((msr & (MSR_RQM | MSR_DIO)) == MSR_RQM) {
            outp(FDC_FIFO, val);
            return 0; /* 成功 */
        }
        fdc_delay();
    }
    return -1; /* タイムアウト */
}

/* MSRのRQM=1かつDIO=1 (FDC→CPU方向)を待ってからリザルトバイトを読み出し */
static int fdc_read_byte(void)
{
    int timeout;
    for (timeout = 0; timeout < FDC_TIMEOUT_LOOP; timeout++) {
        u8 msr = (u8)inp(FDC_MSR);
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) {
            return (int)inp(FDC_FIFO);
        }
        fdc_delay();
    }
    return -1; /* タイムアウト */
}

/* リザルトフェーズ: 最大10バイトのリザルトを読み出し。
 * **1 バイトごとに空回りの上限を掛ける** — CB=1 のまま RQM/DIO が揃わない
 * FDC に当たると、縛りが無ければ i-- のリトライで永久に回る。
 * 戻り値は実際に読めたバイト数 (諦めた時点の i)。 */
static int fdc_read_results(u8 *st, int max)
{
    int i, spin;

    spin = 0;
    for (i = 0; i < max; i++) {
        u8 msr = (u8)inp(FDC_MSR);
        /* CMD BSY=0 なら終了 */
        if ((msr & MSR_BUSY) == 0) break;
        /* RQM=1, DIO=1 なら読み出し */
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) {
            st[i] = (u8)inp(FDC_FIFO);
            spin = 0;       /* 上限はバイトごとに数え直す */
        } else {
            fdc_delay();
            if (++spin >= FDC_MSR_SETTLE_LOOP) break;  /* 出てこない */
            i--; /* リトライ */
        }
    }
    return i;
}

/* ======================================================================== */
/*  IRQ11 待ち                                                              */
/* ======================================================================== */
static int fdc_wait_irq(u32 timeout_ticks)
{
    u32 start = tick_count;
    while (!fdc_irq_fired) {
        if ((tick_count - start) > timeout_ticks) {
            return -1; /* タイムアウト */
        }
    }
    fdc_irq_fired = 0;
    return 0;
}

/* ======================================================================== */
/*  リザルトフェーズがまだ続いているか                                      */
/*                                                                          */
/*  CB が立ったまま FDC→CPU 方向に RQM が立てば、次のリザルトバイトがある。  */
/*  CB が落ちていればコマンドは終わっていて、もうバイトは来ない。           */
/*  直前のバイトを読んだ直後は RQM が落ちているので少しだけ待つ。           */
/* ======================================================================== */
static int fdc_result_pending(void)
{
    int i;

    for (i = 0; i < FDC_MSR_SETTLE_LOOP; i++) {
        u8 msr = (u8)inp(FDC_MSR);
        if ((msr & MSR_BUSY) == 0) return 0;   /* コマンド終了 */
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) return 1;
        fdc_delay();
    }
    return 0;
}

/* ======================================================================== */
/*  Sense Interrupt Status (SIS)                                            */
/*                                                                          */
/*  ST0 を 1 バイト読み、**pending が無いときの invalid 応答 (ST0 = 80h)     */
/*  では PCN を読まずに戻る**。µPD765A の invalid command のリザルトは       */
/*  1 バイトで、2 バイト目を待つと来ないバイトを FDC_TIMEOUT_LOOP 回        */
/*  空転してから諦めることになる。排水ループは pending が尽きるまで回すので */
/*  **毎回 1 度は必ずこの空振りを踏む**。                                   */
/*  NP21/W も同じ 1 バイト応答を返す (src/io/fdc.c の FDC_SenceintStatus は  */
/*  pending 無しで fdc.buf[0] = FDCRLT_IC1 (0x80) / fdc.bufcnt = 1)。        */
/*                                                                          */
/*  戻り値: 0 = ST0 を読めた (pending の有無は *st0 で見る) / -1 = 失敗      */
/* ======================================================================== */
static int fdc_sense_interrupt(u8 *st0, u8 *cyl)
{
    int r0, r1;

    *st0 = 0;
    *cyl = 0;
    if (fdc_send_byte(FDC_CMD_SENSE_INTERRUPT) != 0) return -1;

    r0 = fdc_read_byte();
    if (r0 < 0) return -1;
    *st0 = (u8)r0;

    /* Ready 線の変化 (IC=11b) = 媒体が抜き差しされたかもしれない。
     * そのドライブの世代を進めて先読みを捨てさせ、覚えたシリンダも
     * 捨てる。どこの SIS (排水 / シーク完了待ち / 救済) で見ても同じ。 */
    if ((u8)(*st0 & FDC_ST0_IC_MASK) == FDC_ST0_IC_RDYCHG) {
        int d = (int)(*st0 & FDC_ST0_DS_MASK);
        s_stats.ready_change++;
        fdc_bump_gen(d);
        if (d < FDC_MAX_DRIVES) s_known_cyl[d] = FDC_CYL_UNKNOWN;
    }

    /* 1 バイト応答なら PCN は来ない。MSR でも裏を取る — ST0 の読み方を
     * 間違えても、CB が落ちていれば読みに行かない。 */
    if (fdc_sis_result_bytes(*st0) < FDC_SIS_LEN_NORMAL) return 0;
    if (!fdc_result_pending()) return 0;

    r1 = fdc_read_byte();
    if (r1 < 0) return -1;
    *cyl = (u8)r1;
    return 0;
}

/* ======================================================================== */
/*  未回収の割り込みを排水する                                              */
/*                                                                          */
/*  SEEK / RECALIBRATE を出す前に必ず呼ぶ。遅れて来た seek-end 割り込みを    */
/*  SIS で読み出さないまま次のコマンドを出すと、µPD765A の INT 線が上がり    */
/*  っぱなしになり、**エッジトリガの PIC に次のエッジが来ない**。以後の      */
/*  fdc_wait_irq が全部タイムアウトし、3 回リトライしても読めず f_mount が   */
/*  落ちる — 実機の root panic はこの連鎖だった。                           */
/*                                                                          */
/*  回数は FDC_SIS_DRAIN_MAX で縛る (壊れた FDC で無限ループにしない)。      */
/*  戻り値: 排水した件数。                                                  */
/* ======================================================================== */
static int fdc_drain_interrupts(void)
{
    u8 st0, cyl;
    int i;

    for (i = 0; i < FDC_SIS_DRAIN_MAX; i++) {
        if (fdc_sense_interrupt(&st0, &cyl) != 0) break;
        /* ST0 = 80h = もう pending は無い。 */
        if (fdc_sis_result_bytes(st0) < FDC_SIS_LEN_NORMAL) break;
    }
    fdc_irq_fired = 0;
    return i;
}

/* ======================================================================== */
/*  DMAセットアップ (µPD8237A チャネル2)                                    */
/* ======================================================================== */
/*  ポートを直に叩くのはやめ、drivers/dma8237.c の共通部に渡す。           */
/*  **3 段でなければならない** (票 §1-2): マスク → 積む → 成功したときだけ  */
/*  アンマスク。setup だけに置き換えると、共通部が「積んだらマスクしたまま  */
/*  返す」契約なので、マスクされたままコマンドを出してタイムアウトする。    */
/*                                                                          */
/*  戻り 0 = ch2 が開いた / 負 = 積めなかった (**ch2 は閉じたまま**)。      */
/*  負のときは呼び手が FDC コマンドを出してはいけない。                     */
static int dma_setup(u32 phys_addr, u16 byte_count, int is_write)
{
    int rc;

    dma_chan_mask(FDC_DMA_CHANNEL);
    rc = dma_chan_setup(FDC_DMA_CHANNEL, phys_addr, (u32)byte_count,
                        is_write ? DMA_DIR_FROM_MEM : DMA_DIR_TO_MEM,
                        DMA_MODE_SINGLE);
    if (rc != 0) return rc;
    dma_chan_unmask(FDC_DMA_CHANNEL);
    return 0;
}

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
        while ((tick_count - start) < 30) { /* 何もしない */ }
        motor_running = 1;
    }
}

static void fdc_motor_off(void)
{
    outp(FDC_CTRL, 0);
}

/* ======================================================================== */
/*  シーク完了待ち (SEEK / RECALIBRATE 共通)                                */
/*                                                                          */
/*  **期限 (start + FDC_SEEK_TIMEOUT_TICKS) までループする。**              */
/*  IRQ 待ちをループの前に 1 回だけ置くと、別ドライブの通知 (例: drv1 の     */
/*  Ready 変化) で IRQ が上がったときに、それを読み捨てた直後の SIS が       */
/*  80h を返して **自ドライブの正常なシークを途中で失敗にする**。           */
/*  なので:                                                                 */
/*    - 別ドライブの通知なら捨てて待ちを続ける                              */
/*    - 80h (pending 無し) も期限内なら待ちを続ける                         */
/*    - 自ドライブの結果が出たらそれを返す                                  */
/*  上限は**時間で**縛る (tick_count は単調に進むので無限ループにならない)。 */
/*                                                                          */
/*  期限を過ぎたら最後にもう一度 SIS を出す — 実機では遅れて上がった INT の  */
/*  エッジを PIC が取りこぼすことがあり、そのとき ST0 の SE が立っていれば   */
/*  シーク自体は終わっている。80h なら本当に終わっていないので              */
/*  FDC_SEEK_PENDING を返す。                                               */
/*                                                                          */
/*  want_cyl >= 0 で PCN を照合する。RECALIBRATE は -1 を渡す。             */
/*  戻り値は fdc_classify_seek_end() の FDC_SEEK_*。                        */
/*  診断用に最後に見た ST0 を s_last_seek_st0 へ残す。                      */
/* ======================================================================== */
/* 完了待ちの SIS が返した結果が「自ドライブのシーク完了」ではないか。
 * 別ドライブの通知と、自ドライブの Ready 変化 (IC=11b で SE 無し) は
 * どちらも読み捨てて次を読む。 */
static int fdc_sis_is_foreign(u8 st0, u8 want_ds)
{
    if ((u8)(st0 & FDC_ST0_DS_MASK) != want_ds) return 1;
    if ((u8)(st0 & FDC_ST0_IC_MASK) == FDC_ST0_IC_RDYCHG
        && (st0 & FDC_ST0_SE) == 0) return 1;
    return 0;
}

static int fdc_wait_seek_end(int drv, u8 *st0, u8 *cyl, int want_cyl)
{
    u32 start = tick_count;
    u8 want_ds = (u8)(drv & FDC_ST0_DS_MASK);
    int i;
    int more = 0;   /* 前の周で 80h を見ずに打ち切った = まだ pending が有り得る */

    *st0 = 0;
    *cyl = 0;

    for (;;) {
        u32 elapsed = tick_count - start;

        if (elapsed >= FDC_SEEK_TIMEOUT_TICKS) break;   /* 期限切れ */
        /* 残り時間だけ待つ。来なければ期限切れとして下の救済へ。
         * **前の周が 80h を見ないまま件数の上限で止まっていたら、エッジを
         * 待たずに読み続ける** — INT 線は上がったままで次のエッジは来ない
         * (ラリー 2 の Fable)。µPD765A が溜める通知はドライブごとに 1 件
         * (4 件) なので実際には 1 周で尽きるが、上限を件数に頼らず時間
         * (FDC_SEEK_TIMEOUT_TICKS) で縛る。 */
        if (!more) {
            if (fdc_wait_irq(FDC_SEEK_TIMEOUT_TICKS - elapsed) != 0) break;
        }
        more = 0;

        /* **1 本のエッジで積まれている分を全部読む** (2026-09-24)。
         * µPD765A の INT 線は pending が尽きるまで上がったままで、エッジ
         * トリガの PIC には次のエッジが来ない。別ドライブの通知や Ready
         * 変化を 1 件読んだだけで待ちに戻ると、自分の完了が FIFO に
         * 残ったまま期限 (1.5 秒) まで空待ちし、救済の SIS で拾うことに
         * なる — NP21/W でフォントの読み込み中に CPU がここに居続けた
         * 形。80h (pending 無し) が出たら次のエッジを待つ。 */
        for (i = 0; i < FDC_SIS_DRAIN_MAX; i++) {
            if (fdc_sense_interrupt(st0, cyl) != 0) return FDC_SEEK_FAIL;
            s_last_seek_st0 = *st0;
            if (fdc_sis_result_bytes(*st0) < FDC_SIS_LEN_NORMAL) break;
            if (fdc_sis_is_foreign(*st0, want_ds)) {
                s_stats.sis_foreign++;
                continue;
            }
            return fdc_classify_seek_end(*st0, *cyl, want_cyl);
        }
        if (i == FDC_SIS_DRAIN_MAX) more = 1;
    }

    /* 期限切れ。エッジの取りこぼしだけなら救う。 */
    s_stats.seek_timeout++;
    for (i = 0; i < FDC_SIS_DRAIN_MAX; i++) {
        if (fdc_sense_interrupt(st0, cyl) != 0) return FDC_SEEK_FAIL;
        s_last_seek_st0 = *st0;
        if (fdc_sis_result_bytes(*st0) < FDC_SIS_LEN_NORMAL) {
            return FDC_SEEK_PENDING;
        }
        if (fdc_sis_is_foreign(*st0, want_ds)) {
            s_stats.sis_foreign++;
            continue;
        }
        return fdc_classify_seek_end(*st0, *cyl, want_cyl);
    }
    return FDC_SEEK_FAIL;
}

/* ======================================================================== */
/*  FDCリセット                                                             */
/* ======================================================================== */
static int fdc_reset(void)
{
    /* リセットの後は覚えているシリンダを信じない。 */
    fdc_forget_all();

    /* FDCをリセット */
    outp(FDC_CTRL, CTRL_RST);
    fdc_delay();
    fdc_delay();
    fdc_delay();
    fdc_delay();

    /* リセット解除 + Forced Ready + DMA有効 + モーターON。
     * FRY はリセット解除と同時に立てる — RDY が下りたままだと、
     * この直後の SPECIFY / RECALIBRATE が全部 NR で返る。 */
    fdc_irq_fired = 0;
    outp(FDC_CTRL, CTRL_FRY | CTRL_MTON | CTRL_DMAE);

    /* リセット完了IRQ待ち。来ない機種・エミュレータがあるので、
     * タイムアウトしても止めずに SIS の排水へ進む。 */
    (void)fdc_wait_irq(FDC_RESET_TIMEOUT_TICKS);

    /* リセット後は 4 ドライブ分の完了通知が溜まる。ST0 = 80h で尽きる。 */
    (void)fdc_drain_interrupts();

    /* Specifyコマンド: SRT=8ms, HLT=10ms, HUT=max, DMA有効 (値は fdc.h)。 */
    if (fdc_send_byte(FDC_CMD_SPECIFY) != 0) return -1;
    if (fdc_send_byte(FDC_SPECIFY_SRT_HUT) != 0) return -1;
    if (fdc_send_byte(FDC_SPECIFY_HLT_DMA) != 0) return -1;

    return 0;
}

/* ======================================================================== */
/*  実行フェーズごと打ち切る (転送の後始末)                                 */
/*                                                                          */
/*  **DMA ch2 を先にマスクするのが肝**。READ が実行フェーズ中にタイム       */
/*  アウトしたまま -1 を返すと、ch2 はアンマスクのまま残る。次に WRITE が    */
/*  dma_buffer へ書くべき中身を写した後で、遅れて来た旧 READ の DMA が       */
/*  **そこを上書きし得る**。WRITE は最初の SEEK が失敗しても fdc_recover で  */
/*  先へ進むので、気づかないまま**旧データをディスクに書いて成功を返す**。  */
/*                                                                          */
/*  IRQ は待たない — 最短で戻す。積み残しは SIS の排水で回収する。          */
/* ======================================================================== */
static void fdc_abort_transfer(void)
{
    /* 1. まず DMA を止める。以後 dma_setup が「マスク→設定→アンマスク」
     *    するので、ここではマスクしたままにしておいてよい。 */
    dma_chan_mask(FDC_DMA_CHANNEL);

    /* 2. FDC をリセットして実行フェーズを畳む。覚えているシリンダも捨てる。 */
    fdc_forget_all();
    outp(FDC_CTRL, CTRL_RST);
    fdc_delay();
    fdc_delay();
    fdc_delay();
    fdc_delay();
    fdc_irq_fired = 0;
    outp(FDC_CTRL, CTRL_FRY | CTRL_MTON | CTRL_DMAE);

    /* 3. Specify を入れ直す。µPD765A の資料では SRT/HUT/HLT は RESET で
     *    保持されるが、既知の値へ戻しておくほうが状態を追いやすい。 */
    (void)fdc_send_byte(FDC_CMD_SPECIFY);
    (void)fdc_send_byte(FDC_SPECIFY_SRT_HUT);
    (void)fdc_send_byte(FDC_SPECIFY_HLT_DMA);

    /* 4. 積み残しの割り込みを回収する (IRQ 待ちはしない)。 */
    (void)fdc_drain_interrupts();
}

/* ======================================================================== */
/*  Recalibrate (ヘッドをシリンダ0に移動)                                   */
/* ======================================================================== */
/* 成功条件は「SE が立ち、EC が立っていない」こと。PCN は照合しない。
 * 戻り値: 0 = 成功 / -1 = コマンド送信失敗 / -2 = 未完了 (ST0 = 80h)
 *         -3 = EC が取れなかった / -4 = その他の失敗
 *         FDC_RC_NOT_READY (-5) = NR (媒体もドライブも無い。回復しない)
 * 最後に見た ST0 を *out_st0 に返す (診断用。NULL 可)。 */
/* ヘッドの整定待ち (SEEK / RECALIBRATE の後、READ / WRITE の前)。
 * **RECALIBRATE の後にも要る** (2026-09-24 ラリー 2、Codex と Fable):
 * RECALIBRATE が通ると 0 を覚えるので、続く C=0 の読みは SEEK を省く。
 * 整定を SEEK にだけ任せていると、回復 (リセット + RECALIBRATE) の直後の
 * C=0 の READ が整定前に出る。 */
#define FDC_HEAD_SETTLE_TICKS  2    /* 約 15ms を tick (10ms) に切り上げ */
static void fdc_head_settle(void)
{
    u32 start = tick_count;
    while ((tick_count - start) < FDC_HEAD_SETTLE_TICKS) { /* 何もしない */ }
}

static int fdc_recalibrate_st0(int drv, u8 *out_st0)
{
    u8 st0, cyl;
    int rc, attempt;

    st0 = 0;
    rc = FDC_SEEK_FAIL;

    /* ヘッドが動く。通るまでは知らないことにする。 */
    fdc_note_drive(drv);
    fdc_forget_cyl(drv);

    for (attempt = 0; attempt < FDC_RECAL_ATTEMPTS; attempt++) {
        /* 前のコマンドの取りこぼしを片付けてから出す。 */
        (void)fdc_drain_interrupts();

        fdc_irq_fired = 0;
        s_stats.recal_issued++;
        if (fdc_send_byte(FDC_CMD_RECALIBRATE) != 0) {
            if (out_st0) *out_st0 = st0;
            return -1;
        }
        if (fdc_send_byte((u8)drv) != 0) {
            if (out_st0) *out_st0 = st0;
            return -1;
        }

        rc = fdc_wait_seek_end(drv, &st0, &cyl, -1);
        if (rc == FDC_SEEK_OK) {
            if (out_st0) *out_st0 = st0;
            /* 整定してから 0 を覚える (上の fdc_head_settle の注記)。 */
            fdc_head_settle();
            fdc_note_cyl(drv, 0);
            return 0;
        }
        if (rc != FDC_SEEK_RETRY_EC) break;
        /* EC = 77 ステップでトラック 0 に届かなかった。80 シリンダ媒体で
         * ヘッドが 77 より奥に居ると起きる。もう一度出せば残りを踏む。 */
    }

    if (out_st0) *out_st0 = st0;
    if (rc == FDC_SEEK_NOT_READY) return FDC_RC_NOT_READY;
    if (rc == FDC_SEEK_PENDING)  return -2;
    if (rc == FDC_SEEK_RETRY_EC) return -3;
    return -4;
}

static int fdc_recalibrate(int drv)
{
    return fdc_recalibrate_st0(drv, (u8 *)0);
}

/* ======================================================================== */
/*  リトライのあいだの回復                                                  */
/*                                                                          */
/*  DMA マスク → FDC リセット (0x94 bit7) → Specify → SIS 排水 →           */
/*  recalibrate。排水と recalibrate は fdc_recalibrate_st0() の中で行う。   */
/*  1 回目の転送が失敗した時点で INT 線が上がりっぱなしになっている可能性   */
/*  があり、そのまま 2 回目を出しても必ず同じ形で失敗する。                 */
/*                                                                          */
/*  **先頭で DMA ch2 をマスクする** — 直前の試行が実行フェーズで落ちて      */
/*  いると ch2 がアンマスクのまま残っている。これで、最終失敗が seek        */
/*  フェーズ (dma_setup を通らない) で起きた場合も ch2 は必ず閉じている。   */
/* ======================================================================== */
static int fdc_recover(int drv)
{
    int rc;

    dma_chan_mask(FDC_DMA_CHANNEL);
    rc = fdc_reset();
    if (rc != 0) return rc;
    return fdc_recalibrate(drv);
}

/* ======================================================================== */
/*  Seek (指定シリンダに移動)                                               */
/* ======================================================================== */
static int fdc_seek(int drv, int cyl, int head)
{
    u8 st0, result_cyl;
    int rc;

    fdc_note_drive(drv);

    /* 未回収の割り込みを片付けてから出す (上の fdc_drain_interrupts の注記)。
     * シークを省くときも片付ける — 続く READ / WRITE DATA の完了も IRQ の
     * エッジで待つので、INT 線が上がったままだと同じ形で失敗する。
     * 排水は pending 無しなら SIS 1 回 (1 バイト応答) で終わる。 */
    (void)fdc_drain_interrupts();

    /* 同じシリンダに居ると分かっていれば SEEK も整定待ちも出さない。
     * ヘッド (表裏) の切り替えは READ / WRITE DATA の HD で選ぶので、
     * シリンダが同じなら SEEK は要らない。 */
    if (drv >= 0 && drv < FDC_MAX_DRIVES && s_known_cyl[drv] == cyl) {
        s_stats.seek_skipped++;
        return 0;
    }
    fdc_forget_cyl(drv);

    fdc_irq_fired = 0;
    s_stats.seek_issued++;
    if (fdc_send_byte(FDC_CMD_SEEK) != 0) return -1;
    if (fdc_send_byte((u8)((head << 2) | drv)) != 0) return -1;
    if (fdc_send_byte((u8)cyl) != 0) return -1;

    /* 完了待ち。PCN が要求シリンダと一致することまで見る。 */
    rc = fdc_wait_seek_end(drv, &st0, &result_cyl, cyl);
    /* NR = 媒体もドライブも無い。**リトライも回復もしない** ので分けて返す。 */
    if (rc == FDC_SEEK_NOT_READY) return FDC_RC_NOT_READY;
    if (rc == FDC_SEEK_PENDING) return -2;
    if (rc != FDC_SEEK_OK) return -4;

    /* ヘッド安定待ち: 約15ms */
    fdc_head_settle();

    fdc_note_cyl(drv, cyl);
    return 0;
}

/* ======================================================================== */
/*  最終失敗の 1 行 ([V4])                                                  */
/*                                                                          */
/*  リトライごとには出さない — 画面が流れて元の失敗が見えなくなる。        */
/*  リザルトフェーズまで届いていない (seek / cmd で落ちた) ときは           */
/*  results[] がまだ空なので、**最後の SEEK / RECALIBRATE の ST0** を出す。 */
/*  NR (媒体もドライブも無い) なら印を付ける — いちばん多い失敗なので       */
/*  「なぜ読めなかったか」が 1 行で分かるようにする。                      */
/* ======================================================================== */
static void fdc_report_fail(const char *op, int drv, int cyl, int head,
                            int sect, const char *phase, int have_results,
                            const u8 *results)
{
    u8 st0, st1, st2;
    const char *mark;

    if (!have_results) {
        st0 = s_last_seek_st0;
        st1 = 0;
        st2 = 0;
    } else {
        st0 = results[0];
        st1 = results[1];
        st2 = results[2];
    }
    mark = (st0 & FDC_ST0_NR) ? " (NR)" : "";

    kprintf(0x07,
            "[fdc] %s fail drv=%d chs=%d/%d/%d phase=%s st0=%02x st1=%02x st2=%02x%s\n",
            op, drv, cyl, head, sect, phase, st0, st1, st2, mark);
}

/* ======================================================================== */
/*  セクタ読み込み (ジオメトリ指定版)                                        */
/* ======================================================================== */
int fdc_read_sector_geom(int drv, int cyl, int head, int sect,
                         const struct fdc_geom *g, void *buf)
{
    u8 results[7];
    int n, retry;
    u32 phys = (u32)dma_buffer;
    u16 bps = g->bps;
    const char *phase = "seek";
    int have_results = 0;   /* results[] が埋まったか (診断の出し分け) */
    int dma_armed = 0;      /* DMA ch2 をアンマスクしたまま抜けていないか */

    kmemset(results, 0, sizeof(results));
    s_stats.single_reads++;

    for (retry = 0; retry < FDC_RW_RETRIES; retry++) {
        int seek_rc;

        /* 0. 前の試行が失敗している。**次を出す前に回復する** —
         *    リセットと recalibrate を挟まないと、上がりっぱなしの INT 線の
         *    まま同じ形で失敗し続ける。最後の失敗の後には呼ばない。
         *    fdc_recover は先頭で ch2 をマスクするので dma_armed も下りる。
         *    診断は**最後の試行だけ**を映すので results[] も消す。 */
        if (retry > 0) {
            s_stats.single_retries++;
            (void)fdc_recover(drv);
            dma_armed = 0;
            have_results = 0;
            kmemset(results, 0, sizeof(results));
        }

        /* 1. シーク */
        phase = "seek";
        seek_rc = fdc_seek(drv, cyl, head);
        /* NR = 媒体もドライブも無い。**リトライも回復もしない** —
         * 回復で直るものではないし、HDD 起動時の /fd0 サブマウント試行が
         * 空のドライブに対して毎回 3 回踏むと起動が数秒伸びる。 */
        if (seek_rc == FDC_RC_NOT_READY) break;
        if (seek_rc != 0) continue;

        /* 2. DMAセットアップ (FDC→メモリ = read)。
         *    ここで ch2 がアンマスクされる。以後どこで抜けても
         *    **閉じてからでないと戻れない** (下の fdc_abort_transfer)。
         *    **負が返ったら ch2 は閉じたままなのでコマンドを出さない** —
         *    出すと実行フェーズで固まってタイムアウトを待つだけになる。 */
        if (dma_setup(phys, bps, 0) != 0) { phase = "dma"; break; }
        dma_armed = 1;
        phase = "cmd";

        /* 3. Read Data コマンド送信 */
        fdc_irq_fired = 0;
        if (fdc_send_byte(FDC_OPT_MF | FDC_CMD_READ_DATA) != 0) continue;
        if (fdc_send_byte((u8)((head << 2) | drv)) != 0) continue;
        if (fdc_send_byte((u8)cyl) != 0) continue;      /* C */
        if (fdc_send_byte((u8)head) != 0) continue;     /* H */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* R (1始まり) */
        if (fdc_send_byte(g->sec_n) != 0) continue;     /* N */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* EOT (最終セクタ) */
        if (fdc_send_byte(g->gap3) != 0) continue;      /* GPL */
        if (fdc_send_byte(FDC_DTL_UNUSED) != 0) continue;     /* DTL */

        /* 4. IRQ待ち (データ転送完了) */
        phase = "irq";
        if (fdc_wait_irq(FDC_RW_TIMEOUT_TICKS) != 0) continue;

        /* 5. リザルト読み出し (7バイト) */
        phase = "result";
        n = fdc_read_results(results, 7);
        have_results = (n > 0);
        if (n < 7) continue;

        /* 6. エラーチェック: ST0のbit6-7が00なら成功 */
        if ((results[0] & FDC_ST0_IC_MASK) == FDC_ST0_IC_NORMAL) {
            /* DMAバッファからユーザーバッファにコピー */
            kmemcpy((u8 *)buf, dma_buffer, (u32)bps);
            return 0;
        }

        /* NR (媒体無し) は**回復 (リセット・RECALIBRATE) より前に**打ち切る
         * (ラリー 2 の Codex)。リザルトを読み終えたコマンドは実行フェーズを
         * 抜けているので、DMA を閉じるだけでよい (リセットは要らない)。 */
        if ((results[0] & FDC_ST0_NR) != 0) {
            dma_chan_mask(FDC_DMA_CHANNEL);
            dma_armed = 0;
            phase = "nr";
            break;
        }
    }

    /* 実行フェーズで落ちたまま戻らない。**DMA ch2 を閉じる** —
     * 開いたままだと、次の WRITE が dma_buffer を用意した後に遅れて来た
     * 旧 READ の DMA がそれを上書きし、古い内容が書かれてしまう。
     * 判断は phase の文字列ではなく dma_armed で行う — コマンド送信が
     * 途中で失敗した場合も ch2 は既に開いている。 */
    if (dma_armed) fdc_abort_transfer();
    fdc_forget_cyl(drv);

    fdc_report_fail("read", drv, cyl, head, sect, phase, have_results, results);
    return -1;
}

/* ======================================================================== */
/*  セクタ書き込み (ジオメトリ指定版)                                        */
/* ======================================================================== */
int fdc_write_sector_geom(int drv, int cyl, int head, int sect,
                          const struct fdc_geom *g, const void *buf)
{
    u8 results[7];
    int n, retry;
    u32 phys = (u32)dma_buffer;
    u16 bps = g->bps;
    const char *phase = "seek";
    int have_results = 0;   /* results[] が埋まったか (診断の出し分け) */
    int dma_armed = 0;      /* DMA ch2 をアンマスクしたまま抜けていないか */

    kmemset(results, 0, sizeof(results));

    /* **書く前に世代を進める** — 先読み (drivers/fdc_track.c) がこの
     * セクタの古い中身を持っていても当てさせない。dev.c の fd0/fd1 や
     * KAPI の dev_blk_write はここを直に呼ぶので、diskio.c の破棄だけ
     * では足りない (レビューの指摘)。失敗しても進めたままでよい
     * (途中まで書かれたかもしれない)。 */
    fdc_bump_gen(drv);
    s_stats.writes++;

    /* ユーザーバッファからDMAバッファにコピー */
    kmemcpy(dma_buffer, (const u8 *)buf, (u32)bps);

    for (retry = 0; retry < FDC_RW_RETRIES; retry++) {
        int seek_rc;

        /* 0. 前の試行が失敗している。次を出す前に回復する (read 側と同じ)。 */
        if (retry > 0) {
            (void)fdc_recover(drv);
            dma_armed = 0;
            have_results = 0;
            kmemset(results, 0, sizeof(results));
        }

        /* 1. シーク */
        phase = "seek";
        seek_rc = fdc_seek(drv, cyl, head);
        if (seek_rc == FDC_RC_NOT_READY) break;   /* read 側と同じ理由 */
        if (seek_rc != 0) continue;

        /* 2. DMAセットアップ (メモリ→FDC = write)。read 側と同じ (ch2 が開く)。
         *    負なら ch2 は閉じたままなのでコマンドを出さない (read 側と同じ)。 */
        if (dma_setup(phys, bps, 1) != 0) { phase = "dma"; break; }
        dma_armed = 1;
        phase = "cmd";

        /* 3. Write Data コマンド送信 */
        fdc_irq_fired = 0;
        if (fdc_send_byte(FDC_OPT_MF | FDC_CMD_WRITE_DATA) != 0) continue;
        if (fdc_send_byte((u8)((head << 2) | drv)) != 0) continue;
        if (fdc_send_byte((u8)cyl) != 0) continue;      /* C */
        if (fdc_send_byte((u8)head) != 0) continue;     /* H */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* R */
        if (fdc_send_byte(g->sec_n) != 0) continue;     /* N */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* EOT */
        if (fdc_send_byte(g->gap3) != 0) continue;      /* GPL */
        if (fdc_send_byte(FDC_DTL_UNUSED) != 0) continue;     /* DTL */

        /* 4. IRQ待ち */
        phase = "irq";
        if (fdc_wait_irq(FDC_RW_TIMEOUT_TICKS) != 0) continue;

        /* 5. リザルト読み出し */
        phase = "result";
        n = fdc_read_results(results, 7);
        have_results = (n > 0);
        if (n < 7) continue;

        /* 6. エラーチェック */
        if ((results[0] & FDC_ST0_IC_MASK) == FDC_ST0_IC_NORMAL) {
            return 0;
        }

        /* NR (媒体無し) は回復より前に打ち切る (read 側と同じ)。リザルトを
         * 読み終えているので DMA を閉じるだけでよい (リセットは要らない)。 */
        if ((results[0] & FDC_ST0_NR) != 0) {
            dma_chan_mask(FDC_DMA_CHANNEL);
            dma_armed = 0;
            phase = "nr";
            break;
        }
    }

    if (dma_armed) fdc_abort_transfer();
    fdc_forget_cyl(drv);

    fdc_report_fail("write", drv, cyl, head, sect, phase, have_results, results);
    return -1;
}

/* ======================================================================== */
/*  同じトラックの複数セクタを 1 回の READ DATA で読む (2026-09-24)         */
/*                                                                          */
/*  EOT = sect + count - 1、DMA の転送長 = count × bps。µPD765A は R が     */
/*  EOT に達するか DMA の TC が来るまで次のセクタへ進む。長さを EOT と揃え  */
/*  てあるので、**最後のセクタの最後のバイトで TC と EOT が同時に来て正常   */
/*  終了する** — 単発 (R = EOT、長さ = 1 セクタ) と同じ組み立てで、実機で   */
/*  通っている形をセクタ数だけ伸ばしたもの。NP21/W も同じ (src/io/fdc.c の  */
/*  FDC_ReadData は FDCEVENT_NEXTDATA で `R++ == eot` まで readsector を     */
/*  繰り返し、fdc_dmafunc の DMAEXT_END で tc を立てる)。                    */
/*                                                                          */
/*  **MT (マルチトラック) は使わない。** Bible 2-9 の INT 1Bh READ DATA は   */
/*  「MT を指定し開始ヘッドが 0 なら同じシリンダのヘッド 1 も読める」とし、  */
/*  NP21/W も MT で H を 0→1 に進める。使えば 1 コマンドで 1 シリンダ       */
/*  (2HD 16KB) 読めるが、今回は入れない:                                    */
/*   - 受け皿が 1 トラック分 (9KB) で足りる (票の指示: 転送長 1 トラック以下)*/
/*   - 同資料は書き込みの MT を禁じている (µPD765 が正しく動かない)。読みで */
/*     実機の挙動を確かめた記録がまだ無い                                   */
/*   - 得はヘッド 1 の先頭セクタの待ち (最悪 1 回転) をシリンダごとに 1 回  */
/*     省くことだけで、1 セクタ 1 回転の今の損に比べて小さい                */
/*  実機で速度を測ってから別の段で入れる (票 TASK_FDC_REALHW の v3 の節)。  */
/*                                                                          */
/*  **リトライしない**。失敗したら DMA を閉じ、覚えているシリンダを捨てて   */
/*  -1 を返す。呼び手 (drivers/fdc_track.c) がその範囲を                    */
/*  fdc_read_sector_geom で 1 セクタずつ読み直す (そちらは従来どおり        */
/*  リトライ・回復・NR の早期終了を持つ)。                                  */
/* ======================================================================== */
int fdc_read_sectors(int drv, int cyl, int head, int sect, int count,
                     const struct fdc_geom *g, void *buf)
{
    u8 results[7];
    int n, eot;
    u32 bytes, timeout;
    u32 phys = (u32)dma_buffer;
    const char *phase = "seek";
    int have_results = 0;
    int dma_armed = 0;
    int seek_failed = 0;

    if (g == 0 || count < 1 || sect < 1) return -2;
    eot = sect + count - 1;
    if (eot > (int)g->spt) return -2;          /* トラックをまたぐ (MT 無し) */
    bytes = (u32)count * (u32)g->bps;
    if (bytes > FDC_DMA_BUF_SIZE) return -2;    /* 受け皿に入らない */

    kmemset(results, 0, sizeof(results));
    timeout = fdc_rw_timeout_ticks((int)g->spt, count, FDC_ROT_TICKS_WORST,
                                   FDC_FIND_ROTATIONS, FDC_HEAD_LOAD_TICKS,
                                   FDC_TIMEOUT_MARGIN, FDC_RW_TIMEOUT_TICKS);

    /* 1. シーク (同じシリンダなら省かれる)。NR (媒体無し) は失敗に
     *    数えない — 1 セクタずつの読みも NR で即座に終わる。 */
    {
        int seek_rc = fdc_seek(drv, cyl, head);
        if (seek_rc == FDC_RC_NOT_READY) {
            fdc_forget_cyl(drv);
            s_stats.multi_nr++;
            return -3;
        }
        if (seek_rc != 0) { seek_failed = 1; goto fail; }
    }

    /* 2. DMA (FDC→メモリ)。長さはちょうど count セクタ = EOT と揃える。
     *    負なら ch2 は閉じたままなのでコマンドを出さない。 */
    if (dma_setup(phys, (u16)bytes, 0) != 0) { phase = "dma"; goto fail; }
    dma_armed = 1;
    phase = "cmd";

    /* 3. READ DATA (MT = 0) */
    fdc_irq_fired = 0;
    if (fdc_send_byte(FDC_OPT_MF | FDC_CMD_READ_DATA) != 0) goto fail;
    if (fdc_send_byte((u8)((head << 2) | drv)) != 0) goto fail;
    if (fdc_send_byte((u8)cyl) != 0) goto fail;      /* C */
    if (fdc_send_byte((u8)head) != 0) goto fail;     /* H */
    if (fdc_send_byte((u8)sect) != 0) goto fail;     /* R (1始まり) */
    if (fdc_send_byte(g->sec_n) != 0) goto fail;     /* N */
    if (fdc_send_byte((u8)eot) != 0) goto fail;      /* EOT */
    if (fdc_send_byte(g->gap3) != 0) goto fail;      /* GPL */
    if (fdc_send_byte(FDC_DTL_UNUSED) != 0) goto fail;  /* DTL */

    /* 4. 完了の IRQ。上限は機構の最悪値 (fdc.h の FDC_ROT_TICKS_WORST)。 */
    phase = "irq";
    if (fdc_wait_irq(timeout) != 0) goto fail;

    /* 5. リザルト */
    phase = "result";
    n = fdc_read_results(results, 7);
    have_results = (n > 0);
    if (n < 7) goto fail;
    /* リザルトを読み終えた = 実行フェーズは終わっている。DMA を閉じる。 */
    dma_chan_mask(FDC_DMA_CHANNEL);
    dma_armed = 0;
    if ((results[0] & FDC_ST0_IC_MASK) != FDC_ST0_IC_NORMAL) {
        /* NR は回復より前に打ち切る (単発と同じ。ラリー 2 の Codex)。 */
        if ((results[0] & FDC_ST0_NR) != 0) {
            fdc_forget_cyl(drv);
            s_stats.multi_nr++;
            return -3;
        }
        goto fail;
    }

    kmemcpy((u8 *)buf, dma_buffer, bytes);
    s_stats.multi_ok++;
    return 0;

fail:
    /* 実行フェーズの途中なら DMA を閉じて FDC を畳み、**RECALIBRATE まで
     * 済ませる** (単発のリトライの間の fdc_recover と同じ形)。リセットの
     * 後の µPD765A の PCN はヘッドの実際の位置と合っている保証が無く、
     * そのまま次の単発が SEEK すると別のシリンダへ出て WC で 1 回ぶん
     * 無駄にする。覚えている値は fdc_abort_transfer が全部捨て、
     * RECALIBRATE が通れば 0 を覚え直す (ヘッドは実際に 0 に居る)。
     * DMA を積む前に落ちた (シークの失敗 / DMA を積めない) ときは
     * リセットを通らないので、ここで捨てる — 次の単発がシークし直す。 */
    if (dma_armed) {
        fdc_abort_transfer();
        (void)fdc_recalibrate(drv);
    } else if (seek_failed) {
        /* シークの失敗 (-2 未完了 / -4 異常) も回復を通す (ラリー 2 の
         * Fable)。INT 線やヘッドの位置が分からないまま単発へ渡さない。 */
        (void)fdc_recover(drv);
    } else {
        fdc_forget_cyl(drv);
    }
    s_stats.multi_fail++;
    if (s_stats.multi_fail <= FDC_MULTI_FAIL_REPORT_MAX) {
        kprintf(0x07, "[fdc] multi-read n=%d falls back to single\n", count);
        fdc_report_fail("read-multi", drv, cyl, head, sect, phase,
                        have_results, results);
    }
    return -1;
}

void fdc_print_stats(const char *tag)
{
    const struct fdc_stats *t = &s_stats;

    if (t->seek_issued + t->seek_skipped + t->single_reads + t->multi_ok
        + t->multi_fail + t->writes == 0) {
        return;
    }
    /* 80 桁に収めるため 2 行に分ける (1 行だと画面の幅で切れた)。 */
    kprintf(0x07, "[fdc] %s: seek=%u skip=%u recal=%u tmo=%u foreign=%u rdy=%u\n",
            tag, (unsigned int)t->seek_issued, (unsigned int)t->seek_skipped,
            (unsigned int)t->recal_issued, (unsigned int)t->seek_timeout,
            (unsigned int)t->sis_foreign, (unsigned int)t->ready_change);
    kprintf(0x07, "[fdc] %s: multi=%u/%u nr=%u single=%u retry=%u write=%u\n",
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
        while ((tick_count - start) < 10) { /* 100ms ウェイト */ }
        ret = fdc_recalibrate_st0(0, &st0);
    }
    if (ret != 0) {
        /* [V4] 失敗はそのまま言う。ここで諦めると MOUNT の root panic に
         * なるが、画面には「なぜ」が出ていなかった。 */
        kprintf(0x07, "[fdc] recalibrate drv=%d rc=%d st0=%02x\n", 0, ret, st0);
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
static const struct fdc_geom *s_geom[2] = {
    &fdc_geom_2hd, &fdc_geom_2hd
};

/* このドライバが 04BEh で 1.44MB モードへ切り替えたか (ドライブごと)。
 * **触っていないポートを戻そうとしない**ため。2HD だけの機種で 04BEh を
 * 書くと、00BEh のデコードイメージが出る機種では 1MB/640KB I/F の切替に
 * 化ける恐れがある (io_fdd.md の注意)。 */
static int s_3mode_on[2] = { 0, 0 };

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
    switch (daua & 0xF0) {
    case 0x30:
    case 0xB0:
        return fdc_set_media(drv, FDC_MEDIA_2HD_1440);
    default:
        return 0;
    }
}

