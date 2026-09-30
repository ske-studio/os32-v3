/* ======================================================================== */
/*  FDC_DECIDE.C — FDC の純粋な判定 (I/O もタイマも触らない)                */
/*                                                                          */
/*  ここには副作用のある行を 1 つも置かない。drivers/fdc.c から切り出して   */
/*  あるのは、実機でしか踏まない分岐をホストで試験するため。                */
/*    試験: tools/tests/test_fdc_seek.py / 記録: tools/tests/fdc_seek_tdd.md */
/*                                                                          */
/*  出典: µPD765A データシート (ST0 / SENSE INTERRUPT STATUS)               */
/* ======================================================================== */

#include "fdc_decide.h"

/* ======================================================================== */
/*  SENSE INTERRUPT STATUS のリザルト長                                     */
/* ======================================================================== */
int fdc_sis_result_bytes(u8 st0)
{
    /* pending が無いときの SIS は invalid command として扱われ、ST0 = 80h
     * だけの **1 バイト**が返る。ここを 2 固定にすると、来ない 2 バイト目を
     * FDC_TIMEOUT_LOOP 回空転して待つことになる。
     *
     * 見るのは IC の 2 ビットが 10b であることで、bit7 だけではない —
     * IC=11b (C0h: Ready 線が変化した) は PCN が続く 2 バイト応答で、
     * これを 1 バイト扱いにすると PCN が FIFO に残り、以後のリザルトが
     * 1 バイトずつずれる。 */
    if ((u8)(st0 & FDC_ST0_IC_MASK) == FDC_ST0_IC_INVALID) {
        return FDC_SIS_LEN_INVALID;
    }
    return FDC_SIS_LEN_NORMAL;
}

/* ======================================================================== */
/*  シーク完了の判定                                                        */
/* ======================================================================== */
int fdc_classify_seek_end(u8 st0, u8 pcn, int want_cyl)
{
    u8 ic = (u8)(st0 & FDC_ST0_IC_MASK);

    /* 読み出す割り込みが無い = **まだ終わっていない**。
     * タイムアウト後の救済 (SE が立っていれば「エッジを取りこぼしただけ」
     * として完了扱い) を、動いている途中のヘッドにまで広げないために、
     * 失敗とは別の値で返す。 */
    if (ic == FDC_ST0_IC_INVALID) {
        return FDC_SEEK_PENDING;
    }

    /* Not Ready = 媒体もドライブも無い。**いちばん先に見る** —
     * リセットでも RECALIBRATE でも直らないので、呼び出し側が
     * リトライと回復をまるごと飛ばせるように他の失敗と分ける。 */
    if ((st0 & FDC_ST0_NR) != 0) {
        return FDC_SEEK_NOT_READY;
    }

    /* SE が立っていなければシーク系の完了通知ですらない。 */
    if ((st0 & FDC_ST0_SE) == 0) {
        return FDC_SEEK_FAIL;
    }

    /* EC = 77 ステップ踏んでもトラック 0 のセンサが反応しなかった。
     * 80 シリンダ媒体でヘッドが 77 より奥に居ると **正常な機械でも立つ**
     * ので、失敗ではなく「もう一度 RECALIBRATE を出す」合図にする。 */
    if ((st0 & FDC_ST0_EC) != 0) {
        return FDC_SEEK_RETRY_EC;
    }

    /* Ready 線が変化した (ディスクが抜かれた)。やり直しても直らない。 */
    if (ic == FDC_ST0_IC_RDYCHG) {
        return FDC_SEEK_FAIL;
    }

    /* ここまで来て IC が正常でなければ理由の分からない異常終了。
     * **成功に倒さない** ([V4])。 */
    if (ic != FDC_ST0_IC_NORMAL) {
        return FDC_SEEK_FAIL;
    }

    /* SEEK は PCN まで照合する。RECALIBRATE (want_cyl < 0) の成功条件は
     * 「SE が立ち、EC が立っていない」ことだけで、PCN は照合しない。 */
    if (want_cyl >= 0 && pcn != (u8)want_cyl) {
        return FDC_SEEK_FAIL;
    }
    return FDC_SEEK_OK;
}

/* ======================================================================== */
/*  まとめ読みの時間上限                                                    */
/* ======================================================================== */
u32 fdc_rw_timeout_ticks(int spt, int count, u32 rot_ticks, u32 find_rot,
                         u32 head_load_ticks, u32 margin, u32 floor_ticks)
{
    u32 xfer, t;

    if (spt <= 0 || count < 1) return floor_ticks;

    /* count セクタが通り過ぎるのに要る回転 (tick)。**切り上げる** —
     * 切り捨てると 1 トラックに満たない読みが 0 回転ぶんしか待たない。 */
    xfer = ((u32)count * rot_ticks + (u32)spt - 1) / (u32)spt;
    t = margin * (find_rot * rot_ticks + xfer + head_load_ticks);
    return (t < floor_ticks) ? floor_ticks : t;
}

/* ======================================================================== */
/*  受け皿の割り付け (DMA の窓は 64KB 境界をまたがない)                     */
/* ======================================================================== */
#define FDC_BANK_BYTES 0x10000UL

static int fdc_crosses_bank(u32 addr, u32 len)
{
    return ((addr & (FDC_BANK_BYTES - 1)) + len) > FDC_BANK_BYTES;
}

int fdc_buf_layout(u32 start, u32 total, u32 slot, u32 *dma_off, u32 *rest_off)
{
    if (slot == 0 || total < slot || total >= FDC_BANK_BYTES) return -1;

    /* 先頭に置けるなら先頭、先読み用はその後ろ。 */
    if (!fdc_crosses_bank(start, slot)) {
        *dma_off = 0;
        *rest_off = slot;
        return 0;
    }
    /* 先頭 slot バイトの中に境界がある。境界は 1 本しか無いので、末尾の
     * slot バイトはまたがない (total >= 2 × slot なら境界より後ろ)。 */
    if (!fdc_crosses_bank(start + total - slot, slot)) {
        *dma_off = total - slot;
        *rest_off = 0;
        return 0;
    }
    return -1;
}
