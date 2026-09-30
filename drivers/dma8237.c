/* ======================================================================== */
/*  DMA8237.C — 8237A の共通部 (I/O を出す側)                               */
/*                                                                          */
/*  算数とポート表は drivers/dma8237_math.c。ここに残るのは                  */
/*  「いつ何を out するか」と、**読むと消えるステータス**の保持だけ。        */
/*                                                                          */
/*  ■ フリップフロップ (0019h) はチャネル間で共有される                     */
/*    16bit を下位 / 上位に割って読み書きする境目がこれ 1 つしかないので、   */
/*    途中で別チャネルの ISR が触ると上位と下位が入れ替わる。**積むのも     */
/*    読むのも irq_save の中**。                                            */
/*                                                                          */
/*  ■ ステータス (0011h) は読むと全チャネルの TC が消える                   */
/*    だからこの層だけが読み、読んだ値を done[] / tc_event[] に写す          */
/*    (票 §1-2 の TC の保持)。装置ドライバが直に 0011h を読むと、            */
/*    他チャネルの完了を**黙って捨てる**。                                   */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-2                             */
/*  出典: docs/hw/undocumented/io_dma.md (UNDOCUMENTED Vol.2)               */
/* ======================================================================== */

#include "dma8237.h"
#include "io.h"
#include "kprintf.h"

/* ------------------------------------------------------------------------ */
/*  チャネルごとの保持                                                      */
/* ------------------------------------------------------------------------ */
struct dma_chan_state {
    u32 limit;      /* 積んだカウント値 (bytes - 1)。remaining の採用判定に使う */
    u8  mode;       /* DMA_MODE_SINGLE / DMA_MODE_CYCLIC */
    u8  masked;     /* 1 = この層が dma_chan_mask でマスクした */
    u8  done;       /* 1 = TC 到達 (SINGLE のみ)。次の setup まで残る */
    u8  tc_event;   /* 1 = 未 ack の TC 通知 */
};

static struct dma_chan_state s_chan[DMA_CHAN_COUNT];
static int s_ready;
static int s_a20_state = DMA_A20_UNKNOWN;
static u8  s_a20_pre;
static u8  s_a20_post;

/* ------------------------------------------------------------------------ */
/*  ステータスを 1 回読んで done / tc_event へ写す                          */
/*  **必ず irq_save の中から呼ぶ** (読むと全チャネルの TC が消える)。        */
/* ------------------------------------------------------------------------ */
static void dma_status_poll(void)
{
    u8 st = (u8)inp(DMA8237_STATUS);
    unsigned int ch;

    for (ch = 0; ch < DMA_CHAN_COUNT; ch++) {
        if (!(st & (u8)(1u << ch))) continue;
        s_chan[ch].tc_event = 1;
        /* auto-init は終わらないので、CYCLIC では done を立てない
         * (票 §1-2: remaining を停止の証拠に使わない)。 */
        if (s_chan[ch].mode != DMA_MODE_CYCLIC) s_chan[ch].done = 1;
    }
}

/* カウントレジスタを 1 組読む。irq_save の中から。 */
static u32 dma_read_count(u16 port)
{
    u32 lo, hi;

    outp(DMA8237_FF_CLEAR, 0);
    lo = (u32)(inp(port) & 0xFF);
    hi = (u32)(inp(port) & 0xFF);
    return (hi << 8) | lo;
}

/* ======================================================================== */
/*  初期化 — 0439h の RMW と 3 値の診断                                     */
/*                                                                          */
/*  fdc_init がやっていたものをここへ移した。**プールもカーネルも 1MB 超**   */
/*  にあるので、bit2 が落ちない機種では OS32 の DMA そのものが動かない。    */
/*  それでも起動は止めない — 3 値を出して W2 (実機の FD 起動) に判定を       */
/*  任せる ([V4]: 黙って成功にしない)。                                      */
/* ======================================================================== */
void dma8237_init(void)
{
    unsigned int flags;
    u8 v, after;

    flags = irq_save();

    /* bit7 (プリンタ I/F 選択) を壊さないよう **必ず RMW**。
     * **読みが FFh でも書く** — 実機は未使用ビットが 1 で読めれば正当に
     * FFh を返し得るので、そこを避けると DMA 禁止が残る (fdc.h の註)。 */
    v = (u8)inp(SYSPORT_DMA_CTRL);
    outp(SYSPORT_DMA_CTRL, (u8)(v & ~SYSPORT_DMA_MASK_1MB));
    after = (u8)inp(SYSPORT_DMA_CTRL);

    s_a20_pre = v;
    s_a20_post = after;
    if (!(after & SYSPORT_DMA_MASK_1MB)) {
        s_a20_state = DMA_A20_VERIFIED;
    } else if (after == 0xFF) {
        s_a20_state = DMA_A20_UNREADABLE;
    } else {
        s_a20_state = DMA_A20_BLOCKED;
    }

    s_ready = 1;
    irq_restore(flags);

    /* 起動行。fdc_init の `[fdc] dma>1MB: 0439h xx -> yy` と同じ中身に
     * 3 値を足したもの。**書かなかった場合が無くなった**ので必ず出る。 */
    kprintf(0x07, "[dma] 0439h %02x -> %02x state=%s\n", v, after,
            (s_a20_state == DMA_A20_VERIFIED)   ? "VERIFIED" :
            (s_a20_state == DMA_A20_UNREADABLE) ? "UNREADABLE" : "BLOCKED");
}

int dma_above_1mb_state(void)
{
    return s_a20_state;
}

void dma_above_1mb_raw(u8 *pre, u8 *post)
{
    if (pre)  *pre  = s_a20_pre;
    if (post) *post = s_a20_post;
}

int dma8237_ready(void)
{
    return s_ready;
}

/* ======================================================================== */
/*  マスク / アンマスク                                                     */
/* ======================================================================== */
void dma_chan_mask(unsigned int ch)
{
    unsigned int flags;

    if (ch >= DMA_CHAN_COUNT) return;
    flags = irq_save();
    outp(DMA8237_MASK1, dma_mask_byte(ch, 1));
    s_chan[ch].masked = 1;
    irq_restore(flags);
}

void dma_chan_unmask(unsigned int ch)
{
    unsigned int flags;

    if (ch >= DMA_CHAN_COUNT) return;
    flags = irq_save();
    outp(DMA8237_MASK1, dma_mask_byte(ch, 0));
    s_chan[ch].masked = 0;
    irq_restore(flags);
}

/* ======================================================================== */
/*  チャネルを積む                                                          */
/*                                                                          */
/*  **積んだだけでマスクは外さない。** 外すのは呼び手が装置の準備を          */
/*  終えてから (FDC なら Read/Write Data コマンドを出す直前)。               */
/* ======================================================================== */
int dma_chan_setup(unsigned int ch, u32 phys, u32 bytes, int dir, int mode)
{
    unsigned int flags;
    u32 count;
    u16 addr16, port_addr, port_count, port_bank;
    u8 bank8;
    int rc;

    /* 検査を全部通してから初めてハードウェアに触る (票 §1-6)。
     * 失敗した setup は旧 done / tc_event を消さない。 */
    rc = dma_check_args(ch, phys, bytes, dir, mode);
    if (rc != 0) return rc;
    if (!s_ready) return DMA_ERR_STATE;

    (void)dma_port_addr(ch, &port_addr);
    (void)dma_port_count(ch, &port_count);
    (void)dma_port_bank(ch, &port_bank);

    dma_split_addr(phys, &addr16, &bank8);
    count = dma_bytes_to_count(bytes);

    flags = irq_save();

    /* マスク中が前提。開いたまま積むと、積んでいる途中の半端な番地で
     * 転送が走る。 */
    if (!s_chan[ch].masked) {
        irq_restore(flags);
        return DMA_ERR_STATE;
    }

    /* **自分の done / tc_event を消す前に 1 回読む** — ステータスは
     * 読むと全チャネルぶん消えるので、他チャネルの TC をここで保存する。 */
    dma_status_poll();
    s_chan[ch].done = 0;
    s_chan[ch].tc_event = 0;

    s_chan[ch].mode = (u8)mode;
    s_chan[ch].limit = count;

    outp(DMA8237_FF_CLEAR, 0);
    outp(DMA8237_MODE, dma_mode_byte(ch, dir, mode));
    outp(port_addr, (u8)(addr16 & 0xFF));
    outp(port_addr, (u8)((addr16 >> 8) & 0xFF));
    outp(port_bank, bank8);
    outp(port_count, (u8)(count & 0xFF));
    outp(port_count, (u8)((count >> 8) & 0xFF));

    irq_restore(flags);
    return 0;
}

/* ======================================================================== */
/*  残バイト数 (票 §1-2 の手順)                                             */
/*                                                                          */
/*  irq_save は DMA を止めない。だから count とステータスは**一体**として    */
/*  扱い、count を読むたびにステータスを見直す。SINGLE で done が立ったら    */
/*  count は見ない (TC 後の FFFFh を拾うだけ)。                             */
/* ======================================================================== */
int dma_chan_remaining(unsigned int ch, u32 *bytes_left, int *tc_seen)
{
    unsigned int flags;
    u16 port_count;
    u32 c1, c2;
    int tries;
    int single;

    if (ch >= DMA_CHAN_COUNT || !bytes_left || !tc_seen) return DMA_ERR_ARG;
    (void)dma_port_count(ch, &port_count);

    flags = irq_save();
    single = (s_chan[ch].mode != DMA_MODE_CYCLIC);

    for (tries = 0; tries < DMA_READ_TRIES; tries++) {
        /* 1. ステータス → done / tc_event。SINGLE で done なら count は見ない */
        dma_status_poll();
        if (single && s_chan[ch].done) goto done_zero;

        /* 2. 1 組目 */
        c1 = dma_read_count(port_count);

        /* 3. 読みの直後に終わっていないか */
        dma_status_poll();
        if (single && s_chan[ch].done) goto done_zero;

        /* 4. 2 組目 → もう 1 度ステータス */
        c2 = dma_read_count(port_count);
        dma_status_poll();
        if (single && s_chan[ch].done) goto done_zero;

        /* 5. 採用判定 */
        if (dma_accept_pair(c1, c2, s_chan[ch].limit)) {
            *bytes_left = dma_count_to_bytes(c2);
            *tc_seen = s_chan[ch].tc_event ? 1 : 0;
            irq_restore(flags);
            return 0;
        }
    }

    irq_restore(flags);
    return DMA_ERR_AGAIN;

done_zero:
    /* 返却の規則 (票 §1-2、早期 return を含む全成功経路で同じ):
     * bytes_left は done から、tc_seen は **tc_event** から (ack 済みなら 0)。 */
    *bytes_left = 0;
    *tc_seen = s_chan[ch].tc_event ? 1 : 0;
    irq_restore(flags);
    return 0;
}

void dma_chan_ack_tc(unsigned int ch)
{
    unsigned int flags;

    if (ch >= DMA_CHAN_COUNT) return;
    flags = irq_save();
    s_chan[ch].tc_event = 0;
    irq_restore(flags);
}

int dma_chan_state(unsigned int ch, int *done, int *tc_event)
{
    unsigned int flags;

    if (ch >= DMA_CHAN_COUNT) return DMA_ERR_ARG;
    flags = irq_save();
    if (done)     *done = s_chan[ch].done ? 1 : 0;
    if (tc_event) *tc_event = s_chan[ch].tc_event ? 1 : 0;
    irq_restore(flags);
    return 0;
}
