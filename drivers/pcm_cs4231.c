/* ======================================================================== */
/*  PCM_CS4231.C — CS4231 (MATE-X PCM) 再生ドライバの I/O と状態機械        */
/*                                                                          */
/*  判定と算数は drivers/pcm_cs4231_math.c (ホスト試験つき)。ここは         */
/*  「ポートを叩く」「メモリを写す」「状態を公開する」だけを持つ。           */
/*                                                                          */
/*  守っている約束 (票 docs/tasks/v3/TASK_PCM_CS4231.md §2-1):               */
/*   - **DMA リングを書くのは pcm_advance と、停止中の pcm_start /           */
/*     RS_RESTART だけ** (どれも IF=0)。foreground が書くのはステージング。  */
/*   - 装置を触る遷移は tick 駆動の分割状態。IRQ / tick の中で待たない。      */
/*   - foreground の待ちは IF=1・期限つき。IF=0 の一括ロックに入れない。     */
/*   - Index と Data の組は 1 つの irq_save の中 (往復 1 B9)。               */
/* ======================================================================== */

#include "pcm_cs4231.h"
#include "dma8237.h"
#include "dma_pool.h"
#include "kmalloc.h"     /* ステージングは DMA しないので KHEAP から */
#include "irq.h"
#include "idt.h"        /* tick_count */
#include "io.h"
#include "kprintf.h"
#include "kstring.h"
#include "sys.h"        /* sys_time_now */

/* fs/fd_redirect.c。drivers/ は -Ifs を持たないので extern を直に置く
 * (exec/appslot.c と同じ作法)。資源の所有者 ID の唯一の出どころ。 */
extern int res_owner_get(void);

/* ======================================================================== */
/*  この driver が持つもの                                                  */
/* ======================================================================== */
struct pcm_core g_pcm;             /* 観測点 (kernel.map から読む) */
/* 診断 (kernel.map から読む。E3 の切り分け): どこで FAULTED / drain_failed になったか。 */
/* 値の意味は pcm_cs4231.h の PCM_DIAG_*。 */
u8 pcm_diag_fault_site = 0;        /* PCM_DIAG_FAULT_* (最初の 1 回) */
u8 pcm_diag_evidence = 0;          /* 最後の証拠読み: PCM_DIAG_EV_* */
u8 pcm_diag_df_site = 0;           /* drain_failed を立てた場所: PCM_DIAG_DF_* */
u32 pcm_diag_stop_calls = 0;       /* advance_stop の呼び出し回数 */
u32 pcm_diag_stop_at = 0;          /* 最後の pcm_run の戻り (tail) */
u32 pcm_diag_deadline = 0;         /* s_deadline の写し */
u32 pcm_diag_entry_tick = 0;       /* stop_entry を呼んだ tick */
u32 pcm_diag_drs = 0;              /* 最後に読んだ I11 */

static u8  *s_ring;                /* DMA リング 16KB */
static u32  s_ring_phys;
static u8  *s_stg;                 /* ステージング 16KB (KHEAP。DMA しない) */
static int  s_owner;
static int  s_irq_reg;
static int  s_present;             /* 起動時の検出 */
static u8   s_version;             /* I25 の V2-0 (シフト前) */
static volatile int s_mce_busy;    /* MCE の列の途中 */
static u32  s_deadline;            /* tick。停止の分割状態の期限 */
static int  s_seq_at;              /* 停止列の再開位置 */
static unsigned long long s_last_us;  /* µs 時計の単調性 (64 ビットで比べる) */
static int  s_fault_sticky;        /* FAULTED を踏んだら再起動まで断る */
/* 回収 / 解放が装置を引き取った印。立っている間、advance は装置に 1 度も
 * 触らず、RS_RESTART / RS_STOP / STOP_REQ の続きも走らせない。reclaim は
 * **IF=0 の中で**立てる (終端状態の公開まで一緒に)。release は IF=1 で立てるが、
 * 呼び手は OPEN / OPENING / STOP_DONE / FAULTED か claim 済みの reclaim だけなので
 * advance が補充や restart に入る隙は無い。次の open まで下ろさない
 * (Codex 実装レビュー往復 1 の blocker 1、往復 2 の注記)。 */
static volatile int s_claimed;

/* ホスト試験の割り込み点 (pcm_cs4231.h の PCM_PP_*)。カーネルでは空。 */
#ifndef PCM_PREEMPT
#define PCM_PREEMPT(site) ((void)0)
#endif

/* ======================================================================== */
/*  1. レジスタアクセス — Index と Data は 1 つの irq_save の中             */
/* ======================================================================== */
static u8 cs_read(u8 idx)
{
    unsigned int f;
    u8 v;

    f = irq_save();
    outp(PCM_PORT_R0, idx);
    v = (u8)inp(PCM_PORT_R1);
    irq_restore(f);
    return v;
}

/* idx に **bit6 (MCE) を立てて渡せば MCE が保たれる**。素の idx なら落ちる —
 * I9 / I16 はそれだと反映されない (DS139PP2 p.34)。ハードの R0 の並びのまま。
 * _locked は**呼び手が irq_save の中**にいるとき用 (判定と複数の書きを 1 つの
 * 禁止区間に入れる)。 */
static void cs_write_locked(u8 idx, u8 val)
{
    outp(PCM_PORT_R0, idx);
    outp(PCM_PORT_R1, val);
}

static void cs_write(u8 idx, u8 val)
{
    unsigned int f;

    f = irq_save();
    cs_write_locked(idx, val);
    irq_restore(f);
}

/* 期限の比較。符号付きの差で見るので tick_count の巻き戻りに強い。 */
static int pcm_past(u32 deadline)
{
    return (int)(tick_count - deadline) > 0;
}

/* ------------------------------------------------------------------------ */
/*  待ちの 1 回ぶんの判定。**0x80 を「ACI=0」と読まない** (往復 2 R3):      */
/*  INIT 中と MCE 後の再同期中は全読みが 0x80 になる (DS139PP2 p.19-21)。   */
/* ------------------------------------------------------------------------ */
static int pcm_ready(int kind)
{
    u8 v;

    if ((inp(PCM_PORT_R0) & PCM_R0_INIT) != 0) return 0;
    if (kind == PCM_OP_WAIT_INIT) return 1;
    v = cs_read(PCM_I_ERRSTAT);
    pcm_diag_drs = v;
    if (kind == PCM_OP_WAIT_ACI) return (v & PCM_ERR_ACI) == 0;
    return (v & PCM_ERR_DRS) == 0;
}

/* foreground の待ち。**IF=1 で tick の期限つき** (IF=0 のロックに入れない)。 */
static int pcm_poll(int kind)
{
    u32 start = tick_count;

    for (;;) {
        if (pcm_ready(kind)) return 0;
        if ((u32)(tick_count - start) > PCM_WAIT_TICKS) return OS32_ERR_IO;
    }
}

/* ======================================================================== */
/*  2. リングとステージングの写し (frame 単位、物理末尾で 2 分割)           */
/* ======================================================================== */
static void pcm_copy_half(u32 half, u32 off, u32 frames)
{
    u8 *dst = s_ring + half * PCM_HALF_BYTES;
    u32 a, b;

    pcm_split(off, frames, &a, &b);
    if (a) kmemcpy(dst, s_stg + off * PCM_FRAME_BYTES, a * PCM_FRAME_BYTES);
    if (b) kmemcpy(dst + a * PCM_FRAME_BYTES, s_stg, b * PCM_FRAME_BYTES);
    if (frames < PCM_HALF_FRAMES) {
        kmemset(dst + frames * PCM_FRAME_BYTES, 0,
                (PCM_HALF_FRAMES - frames) * PCM_FRAME_BYTES);
    }
    g_pcm.filled[half] = frames;
}

/* PCM_OP_RING_FILL: ステージングから半分 0 (と 1) へ配る。 */
static void pcm_ring_fill(void)
{
    u32 n0 = 0, n1 = 0, lh = 0, off = 0;
    int ds = PCM_DRAIN_IN;

    pcm_start_plan(g_pcm.stg.staged, &n0, &n1, &lh, &ds);
    n0 = pcm_stg_consume(&g_pcm.stg, n0, &off);
    pcm_copy_half(0, off, n0);
    n1 = pcm_stg_consume(&g_pcm.stg, n1, &off);
    pcm_copy_half(1, off, n1);
    g_pcm.last_data_half = (u8)lh;
    g_pcm.drain = (u8)ds;
}

/* 起動からの µs を 64 ビットで。取れなければ tick から (同じ起点)。 */
static unsigned long long pcm_clock_us(void)
{
    u32 lo = 0, hi = 0;

    if (sys_time_now(&lo, &hi) != 0)
        return (unsigned long long)tick_count * PCM_US_PER_TICK;
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}

/* 単調性の基準を今に置く (open と TIMEBASE)。 */
static void pcm_clock_rebase(void)
{
    s_last_us = pcm_clock_us();
}

/* µs 時計。**64 ビットで**前回値と比べ、戻った分だけ抑える (sys_time_now が
 * 取れず tick の代用に落ちた回の端数ぶん)。下位 32 ビットの差で「巻き戻り」
 * を推し量ると、起動 35 分 48 秒後からの初回再生で正しい時刻を巻き戻りと
 * 読んで時計が止まり、番犬が死んでいた (Codex 実装レビュー blocker 4)。
 * 返すのは下位 32 ビット — pcm_obs は u32 の差で見るので 71 分まで正しい。 */
static u32 pcm_now(void)
{
    unsigned long long t = pcm_clock_us();

    if (t < s_last_us) t = s_last_us;
    s_last_us = t;
    return (u32)t;
}

/* PCM_OP_TIMEBASE: 観測と番犬の基準を今に戻す。 */
static void pcm_timebase(void)
{
    g_pcm.half = 0;
    g_pcm.pos = 0;
    g_pcm.gen++;
    pcm_clock_rebase();
    g_pcm.last_progress = pcm_now();
    g_pcm.now = g_pcm.last_progress;
    g_pcm.state = PCM_ST_RUNNING;
}

/* ======================================================================== */
/*  3. 列の実行                                                             */
/*                                                                          */
/*  blocking != 0 : foreground (IF=1、期限つき poll)                        */
/*  blocking == 0 : tick / IRQ — 待ちは 1 回だけ見て、まだなら index を返す  */
/*  戻り: 進んだ位置 (n = 完了) / 負 = 失敗                                 */
/* ======================================================================== */
static int pcm_run(const struct pcm_op *ops, int n, int at, int blocking)
{
    const struct pcm_op *o;
    int rc;

    for (; at < n; at++) {
        o = &ops[at];
        switch (o->kind) {
        case PCM_OP_REG:
            if (o->a & PCM_R0_MCE) s_mce_busy = 1;
            cs_write(o->a, o->b);
            break;
        case PCM_OP_PORT:      outp(PCM_PORT_ROUTE + o->a, o->b); break;
        case PCM_OP_WAIT_INIT:
        case PCM_OP_WAIT_ACI:
        case PCM_OP_WAIT_DRS:
            if (blocking) {
                rc = pcm_poll(o->kind);
                if (rc != 0) return rc;
            } else if (!pcm_ready(o->kind)) {
                return at;
            }
            if (o->kind == PCM_OP_WAIT_ACI) s_mce_busy = 0;
            break;
        case PCM_OP_CHK_MODE2:
            if ((cs_read(PCM_I_MODEID) & PCM_MODE2) == 0) return OS32_ERR_IO;
            break;
        case PCM_OP_DMA_MASK:   dma_chan_mask(PCM_DMA_CHAN); break;
        case PCM_OP_DMA_UNMASK: dma_chan_unmask(PCM_DMA_CHAN); break;
        case PCM_OP_DMA_SETUP:
            if (dma_chan_setup(PCM_DMA_CHAN, s_ring_phys, PCM_RING_BYTES,
                               DMA_DIR_FROM_MEM, DMA_MODE_CYCLIC) != 0)
                return OS32_ERR_IO;
            break;
        case PCM_OP_RING_CLEAR:
            kmemset(s_ring, 0, PCM_RING_BYTES);
            g_pcm.filled[0] = 0;
            g_pcm.filled[1] = 0;
            break;
        case PCM_OP_RING_FILL:  pcm_ring_fill(); break;
        case PCM_OP_TIMEBASE:   pcm_timebase(); break;
        case PCM_OP_DEADLINE:   s_deadline = tick_count + o->b; break;
        case PCM_OP_ACK_TC:     dma_chan_ack_tc(PCM_DMA_CHAN); break;
        default: break;
        }
    }
    return n;
}

/* 停止の入口の列 (期限もここで入る)。**IF=0 で呼ぶ** (tick / IRQ の中か、
 * foreground なら状態の公開と同じ irq_save の中)。FAULTED と回収の後は
 * 装置に書かない (Codex 実装レビュー blocker 3)。 */
static void pcm_stop_entry(void)
{
    const struct pcm_op *ops;
    int n;

    if (s_claimed || g_pcm.state == PCM_ST_FAULTED) return;
    n = pcm_seq_stop_entry(&ops);
    pcm_run(ops, n, 0, 0);
    s_seq_at = 0;
    pcm_diag_entry_tick = tick_count;
}

static void pcm_enter_faulted(u8 site)
{
    if (pcm_diag_fault_site == 0) pcm_diag_fault_site = site;
    g_pcm.state = PCM_ST_FAULTED;
    s_fault_sticky = 1;
}

/* ======================================================================== */
/*  4. advance — IRQ と tick はこれだけを呼ぶ                               */
/* ======================================================================== */

/* RS_STOP / STOP_REQ の後続 tick。期限は入口で入ったものを**再設定しない**。 */
static void pcm_advance_stop(void)
{
    const struct pcm_op *ops;
    int n, at;

    n = pcm_seq_stop_tail(&ops);
    at = pcm_run(ops, n, s_seq_at, 0);
    pcm_diag_stop_calls++;
    pcm_diag_stop_at = (u32)at;
    pcm_diag_deadline = s_deadline;
    if (at < 0) { pcm_enter_faulted(PCM_DIAG_FAULT_TAIL); return; }
    if (at < n) {
        s_seq_at = at;
        if (!pcm_past(s_deadline)) return;
        if (g_pcm.state == PCM_ST_RS_STOP) {
            g_pcm.drain_failed = 1;
            pcm_diag_df_site |= PCM_DIAG_DF_RS_STOP_DL;
            g_pcm.state = PCM_ST_STOP_REQ;
            pcm_stop_entry();
        } else {
            pcm_enter_faulted(PCM_DIAG_FAULT_TAIL_DL);
        }
        return;
    }
    s_seq_at = 0;
    if (g_pcm.state == PCM_ST_RS_STOP) {
        g_pcm.state = PCM_ST_RS_RESTART;
        return;
    }
    /* STOP_REQ の**証拠**: PEN=0、PI=0、R0 != 0x80。 */
    {
        u8 ev = 0;
        if ((inp(PCM_PORT_R0) & PCM_R0_INIT) != 0) ev |= PCM_DIAG_EV_INIT;
        if ((cs_read(PCM_I_IFACE) & PCM_IFACE_PEN) != 0) ev |= PCM_DIAG_EV_PEN;
        if ((cs_read(PCM_I_ALTSTAT) & PCM_ALT_PI) != 0) ev |= PCM_DIAG_EV_PI;
        pcm_diag_evidence = ev;
        if (ev != 0) {
            if (pcm_past(s_deadline)) pcm_enter_faulted(PCM_DIAG_FAULT_EVID_DL);
            return;
        }
    }
    g_pcm.state = PCM_ST_STOP_DONE;
}

static void pcm_advance_restart(void)
{
    const struct pcm_op *ops;
    int n;

    n = pcm_seq_restart(&ops);
    if (pcm_run(ops, n, 0, 0) < 0) {
        g_pcm.drain_failed = 1;
        pcm_diag_df_site |= PCM_DIAG_DF_RESTART;
        g_pcm.state = PCM_ST_STOP_REQ;
        pcm_stop_entry();
        return;
    }
    g_pcm.resyncs++;
    if (g_pcm.close_pending) g_pcm.state = PCM_ST_DRAINING;
}

static void pcm_advance_run(void)
{
    struct pcm_act act;
    u32 left = 0, p1 = 0;
    int tc = 0, have = 0;

    /* close が RS_* の途中で立てた close_pending を RUNNING でも拾う
     * (RS_RESTART の完了と close の入口がどちらの順でも drain に入る)。 */
    if (g_pcm.state == PCM_ST_RUNNING && g_pcm.close_pending)
        g_pcm.state = PCM_ST_DRAINING;

    if (dma_chan_remaining(PCM_DMA_CHAN, &left, &tc) == 0) {
        p1 = pcm_pos_frames(left);
        have = 1;
        /* tc は**判定に使わない** (往復 6 R1/R2)。診断の通知だけ落とす。 */
        if (tc) dma_chan_ack_tc(PCM_DMA_CHAN);
    }
    pcm_obs(&g_pcm, have, p1, pcm_now(), &act);
    if (act.refill) pcm_copy_half(act.to_half, act.src_off, act.frames);
    if (act.entered) pcm_stop_entry();
}

static int pcm_advance(void)
{
    int handled = 0;

    /* 0. **入口ガード**: ここでは装置に 1 度も触らない (Index 書きも 0 回)。
     *    回収 / 解放が引き取った後も同じ (再始動・補充を走らせない)。 */
    if (s_claimed || s_mce_busy) return IRQ_NONE;
    if (g_pcm.state == PCM_ST_CLOSED || g_pcm.state == PCM_ST_OPENING ||
        g_pcm.state == PCM_ST_FAULTED) {
        return IRQ_NONE;
    }

    /* 共通の PI の ack。**受理の判定にだけ**使い、連続性には使わない。 */
    if ((cs_read(PCM_I_ALTSTAT) & PCM_ALT_PI) != 0) {
        cs_write(PCM_I_ALTSTAT, 0);
        handled = 1;
    }

    switch (g_pcm.state) {
    case PCM_ST_RUNNING:
    case PCM_ST_DRAINING:   pcm_advance_run(); break;
    case PCM_ST_RS_STOP:
    case PCM_ST_STOP_REQ:   pcm_advance_stop(); break;
    case PCM_ST_RS_RESTART: pcm_advance_restart(); break;
    default: break;         /* OPEN / STOP_DONE は PI の ack だけ */
    }
    return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int pcm_irq(unsigned int irq, void *arg)
{
    (void)irq; (void)arg;
    return pcm_advance();
}

void pcm_tick(void)
{
    (void)pcm_advance();
}

/* ======================================================================== */
/*  5. 検出 (票 §1 の一続きの手順)                                          */
/*                                                                          */
/*  MODE1 のまま I25 を選ぶと IA4 が無視されて I9 に化けるので、             */
/*  **MODE2 を書いて読み戻してから** I25 を読む (往復 2 R1)。                */
/* ======================================================================== */
static int pcm_detect(void)
{
    u8 v;

    if (((u8)inp(PCM_PORT_WSSID) & PCM_WSSID_MASK) != PCM_WSSID_VALUE)
        return 0;
    if (pcm_poll(PCM_OP_WAIT_INIT) != 0) return 0;
    if ((cs_read(PCM_I_MODEID) & PCM_ID_MASK) != PCM_ID_VALUE) return 0;
    cs_write(PCM_I_MODEID, PCM_MODE2);
    if ((cs_read(PCM_I_MODEID) & PCM_MODE2) == 0) return 0;
    v = (u8)(cs_read(PCM_I_VERSION) & PCM_VER_MASK);
    if (v != PCM_VER_CS4231 && v != PCM_VER_CS4231A) return 0;
    s_version = v;
    return 1;
}

void pcm_init(void)
{
    g_pcm.state = PCM_ST_CLOSED;
    if (!pcm_detect()) {
        kprintf(0x07, "[pcm] none\n");
        return;
    }
    s_present = 1;
    kprintf(0x07, "[pcm] CS4231 v=%d irq %d dma %d fmt 0x%X\n",
            (int)(s_version == PCM_VER_CS4231 ? PCM_VER_NAME_4231
                                              : PCM_VER_NAME_4231A),
            (int)PCM_IRQ, (int)PCM_DMA_CHAN, (int)PCM_FMT_44100);
}

int pcm_state(void)   { return (int)g_pcm.state; }
int pcm_present(void) { return s_present; }

/* ======================================================================== */
/*  6. 資源の解放 — **1 度だけ** (契約として finalized で固定する)          */
/* ======================================================================== */
static void pcm_release(int leak)
{
    if (g_pcm.finalized) return;
    g_pcm.finalized = 1;
    s_claimed = 1;              /* 以後 advance は装置にもリングにも触らない */

    if (s_irq_reg) {
        irq_unregister(PCM_IRQ, pcm_irq, 0);
        s_irq_reg = 0;
    }
    /* **ステージングは必ず kfree してよい** — 装置が触らないメモリなので、
     * リングが「止まった証拠」を取れなかった (leak) かどうかと独立。 */
    if (s_stg) kfree(s_stg);
    if (leak) {
        if (s_ring) dma_pool_mark_leaked(s_ring);
        pcm_enter_faulted(PCM_DIAG_FAULT_RECLAIM);
    } else {
        if (s_ring) dma_pool_free(s_ring);
        g_pcm.state = PCM_ST_CLOSED;
    }
    s_ring = 0;
    s_stg = 0;
    s_ring_phys = 0;
    s_owner = 0;
    s_mce_busy = 0;
}

static int pcm_owner_ok(void)
{
    return s_owner != 0 && res_owner_get() == s_owner;
}

/* ======================================================================== */
/*  7. KAPI の実体                                                          */
/* ======================================================================== */
/* open の**巻き戻し (逆順)**。DMA は未開始でマスク中なので解放して安全。
 * `0F40h` は 0x1A のまま残す — detach 状態で装置レジスタを書かせない。 */
static int pcm_open_fail(int rc)
{
    pcm_release(0);
    return rc;
}

int pcm_open(u32 rate)
{
    struct pcm_op ops[PCM_SEQ_MAX];
    const struct pcm_op *tbl;
    u32 phys = 0;
    u8 fmt = 0;
    int rc;

    if (s_fault_sticky) return OS32_ERR_IO;
    if (g_pcm.state != PCM_ST_CLOSED) return PCM_ERR_BUSY;
    if (pcm_fmt_for_rate(rate, &fmt) != 0) return OS32_ERR_INVAL;

    /* 巻き戻しも pcm_release を通るので、**1 度だけ**の錠を先に外す。 */
    g_pcm.finalized = 0;
    g_pcm.state = PCM_ST_OPENING;
    s_claimed = 0;
    rc = pcm_seq_prologue(&tbl);
    pcm_run(tbl, rc, 0, 1);
    if (!pcm_detect()) return pcm_open_fail(OS32_ERR_NOSYS);

    /* **DMA するのはリングだけ**。プールは 64KB しかなく 82557 と分け合う
     * ので、DMA しないステージングは KHEAP から取る (PM 2026-09-23)。 */
    s_ring = (u8 *)dma_pool_alloc(PCM_RING_BYTES, PCM_POOL_ALIGN, &phys);
    s_ring_phys = phys;
    s_stg = (u8 *)kmalloc(PCM_STG_BYTES);
    if (!s_ring || !s_stg) return pcm_open_fail(PCM_ERR_NOMEM);
    kmemset(s_ring, 0, PCM_RING_BYTES);
    kmemset(s_stg, 0, PCM_STG_BYTES);

    if (irq_register(PCM_IRQ, pcm_irq, 0, IRQ_F_SHARED) != 0)
        return pcm_open_fail(PCM_ERR_BUSY);
    s_irq_reg = 1;

    rc = pcm_run(ops, pcm_seq_init(fmt, ops), 0, 1);
    s_mce_busy = 0;
    if (rc < 0) return pcm_open_fail(OS32_ERR_IO);

    kmemset(&g_pcm, 0, sizeof(g_pcm));
    g_pcm.rate = rate;
    g_pcm.state = PCM_ST_OPEN;
    s_owner = res_owner_get();
    s_deadline = 0;
    s_seq_at = 0;
    pcm_clock_rebase();         /* 長い空白の後でも時計の基準は今 */
    return 0;
}

/* OPEN → RUNNING。装置は停止中で、リングを書く例外の 1 つ (IF=0)。 */
static void pcm_start(void)
{
    const struct pcm_op *ops;
    unsigned int f;
    int n;

    n = pcm_seq_start(&ops);
    f = irq_save();
    pcm_run(ops, n, 0, 0);
    if (g_pcm.close_pending) g_pcm.state = PCM_ST_DRAINING;
    irq_restore(f);
}

/* 受付の検査 (owner と状態)。KAPI の wrapper は入力の検査より先にこれを呼ぶ。 */
int pcm_write_check(void)
{
    int st;

    if (!pcm_owner_ok()) return OS32_ERR_INVAL;
    st = (int)g_pcm.state;
    if (st != PCM_ST_OPEN && st != PCM_ST_RUNNING &&
        st != PCM_ST_RS_STOP && st != PCM_ST_RS_RESTART) {
        return OS32_ERR_INVAL;
    }
    return 0;
}

int pcm_write(const void *buf, u32 bytes)
{
    unsigned int f;
    u32 frames, off = 0, n, a = 0, b = 0;
    int rc;

    /* 順序: owner と状態 → NULL → 長さ (bytes = 0 でも未 open は負)。 */
    rc = pcm_write_check();
    if (rc != 0) return rc;
    if (!buf) return OS32_ERR_INVAL;
    frames = bytes / PCM_FRAME_BYTES;      /* frame の倍数だけ受ける */
    if (frames == 0) return 0;

    f = irq_save();
    n = pcm_stg_reserve(&g_pcm.stg, frames, &off);
    irq_restore(f);
    if (n == 0) return 0;                  /* 満杯。アプリは sys_yield で再試行 */

    /* **公開前の区間なので advance は読まない**。IF=1 で写す。 */
    pcm_split(off, n, &a, &b);
    if (a) kmemcpy(s_stg + off * PCM_FRAME_BYTES, buf, a * PCM_FRAME_BYTES);
    if (b) kmemcpy(s_stg, (const u8 *)buf + a * PCM_FRAME_BYTES,
                   b * PCM_FRAME_BYTES);

    f = irq_save();
    pcm_stg_publish(&g_pcm.stg, n);
    irq_restore(f);

    /* 満杯を作った write が開始しないと次の write は 0 のまま (往復 6 R7)。 */
    if (g_pcm.state == PCM_ST_OPEN && g_pcm.stg.staged >= PCM_RING_FRAMES)
        pcm_start();
    return (int)(n * PCM_FRAME_BYTES);
}

int pcm_status(u32 *free_bytes, u32 *counters)
{
    if (!pcm_owner_ok()) return OS32_ERR_INVAL;
    if (free_bytes) *free_bytes = pcm_stg_free(&g_pcm.stg) * PCM_FRAME_BYTES;
    if (counters)
        *counters = pcm_counters_pack(g_pcm.underruns, g_pcm.repeats,
                                      g_pcm.resyncs);
    return 0;
}

int pcm_set_volume(u32 percent)
{
    u8 r = 0;

    unsigned int f;

    if (!pcm_owner_ok()) return OS32_ERR_INVAL;
    if (pcm_vol_reg(percent, &r) != 0) return OS32_ERR_INVAL;
    /* 判定と I6 / I7 の 2 つの書きを**1 つの禁止区間**に入れる。別々だと
     * 判定の後や I6 と I7 の間で tick が FAULTED にし、その後に装置へ書いて
     * いた (「FAULTED では Index 書きも 0 回」を破る。Codex 実装レビュー
     * blocker 5)。 */
    f = irq_save();
    if (s_claimed || s_mce_busy || g_pcm.state == PCM_ST_FAULTED) {
        irq_restore(f);
        return OS32_ERR_AGAIN;
    }
    cs_write_locked(PCM_I_LDA, r);
    cs_write_locked(PCM_I_RDA, r);
    irq_restore(f);
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  pcm_close = drain、期限つき。**「PI が来ない」は証拠にしない**。         */
/* ------------------------------------------------------------------------ */
static int pcm_wait_stopped(u32 ticks)
{
    u32 dl = tick_count + ticks;
    /* state は ISR (tick / IRQ10) が書く。**volatile で読む** — 素の読みだと
     * コンパイラがループの外へ持ち上げ、期限まで回って必ず失敗した
     * (E3 で close が毎回 IO になった、2026-09-23)。 */
    volatile u8 *st = &g_pcm.state;

    while (*st != PCM_ST_STOP_DONE && *st != PCM_ST_FAULTED) {
        if (pcm_past(dl)) return 0;
        PCM_PREEMPT(PCM_PP_WAIT);
    }
    return 1;
}

int pcm_close(void)
{
    unsigned int f;
    int rc, st, idle;

    if (!pcm_owner_ok()) return OS32_ERR_INVAL;

    /* 入口: 状態の判定と DRAINING / close_pending の公開を**1 つの禁止区間**で。
     * 外で読むと、読んでから書くまでに tick が RUNNING → RS_STOP や
     * RS_RESTART → RUNNING を挟み、止まった装置を DRAINING で上書きしたり
     * close_pending を誰も見なくなったりした (Codex 実装レビュー blocker 2)。 */
    f = irq_save();
    st = (int)g_pcm.state;
    idle = (st == PCM_ST_OPEN && g_pcm.stg.staged == 0);
    PCM_PREEMPT(PCM_PP_CLOSE_ENTRY);
    if (st == PCM_ST_OPEN) {
        if (!idle) {
            g_pcm.close_pending = 1;
            pcm_start();                   /* 短いストリームは close で始める */
        }
    } else if (st == PCM_ST_RUNNING) {
        g_pcm.state = PCM_ST_DRAINING;
    } else if (st != PCM_ST_DRAINING && st != PCM_ST_FAULTED) {
        g_pcm.close_pending = 1;           /* RS_* / STOP_* の途中 */
    }
    irq_restore(f);

    if (st == PCM_ST_FAULTED) { pcm_release(1); return OS32_ERR_IO; }
    if (idle) { pcm_release(0); return 0; }

    if (g_pcm.drain_failed) pcm_diag_df_site |= PCM_DIAG_DF_OBS;
    if (!pcm_wait_stopped(pcm_close_ticks(g_pcm.stg.staged, g_pcm.rate))) {
        /* 期限切れは番犬と同じ扱い: drain 失敗 → STOP_REQ を待つ。
         * **判定・状態の公開・停止の入口 (新しい期限) を 1 つの禁止区間で**。
         * STOP_REQ だけ先に見せると、入口の前の tick が古い期限で FAULTED に
         * していた (Codex 実装レビュー blocker 3)。すでに STOP_* / FAULTED
         * なら装置は止まりに向かっているので触らない。 */
        f = irq_save();
        st = (int)g_pcm.state;
        if (st != PCM_ST_STOP_REQ && st != PCM_ST_STOP_DONE &&
            st != PCM_ST_FAULTED) {
            if (g_pcm.drain_failed) pcm_diag_df_site |= PCM_DIAG_DF_OBS;
            g_pcm.drain_failed = 1;
            pcm_diag_df_site |= PCM_DIAG_DF_CLOSE_DL;
            g_pcm.state = PCM_ST_STOP_REQ;
            PCM_PREEMPT(PCM_PP_CLOSE_TIMEOUT);
            pcm_stop_entry();
        }
        irq_restore(f);
        if (!pcm_wait_stopped(PCM_STOP_TICKS + PCM_STOP_WAIT_SLACK)) {
            /* 期限と tick の完了が重なっても STOP_DONE を FAULTED で消さない。 */
            f = irq_save();
            if (g_pcm.state != PCM_ST_STOP_DONE)
                pcm_enter_faulted(PCM_DIAG_FAULT_CLOSE2);
            irq_restore(f);
        }
    }

    if (g_pcm.state == PCM_ST_FAULTED) { pcm_release(1); return OS32_ERR_IO; }
    rc = g_pcm.drain_failed ? OS32_ERR_IO : 0;
    pcm_release(0);
    return rc;
}

/* ======================================================================== */
/*  8. 回収 — **待たない abort**。IF=0 (fault の回収) からも来る            */
/* ======================================================================== */
void pcm_reclaim(int owner)
{
    const struct pcm_op *ops;
    unsigned int f;
    int st, evid, n;

    if (owner <= 0 || owner != s_owner) return;

    f = irq_save();
    st = (int)g_pcm.state;
    if (st == PCM_ST_CLOSED || st == PCM_ST_OPENING) {
        irq_restore(f);
        return;
    }
    /* **IF を戻す前に**装置を引き取り、advance が再始動・補充しない終端の
     * 状態へ移す。状態を残したまま IF を戻すと、tick が RS_RESTART を実行
     * して (unmask、PEN=1) から foreground がリングを free していた
     * (Codex 実装レビュー blocker 1)。`irq_unregister` は tick を止めない。 */
    s_claimed = 1;
    evid = 1;
    if (st == PCM_ST_FAULTED) {
        evid = 0;
    } else if (st != PCM_ST_OPEN && st != PCM_ST_STOP_DONE) {
        /* RUNNING / DRAINING / RS_* / STOP_REQ: 境界は問わずに止める。 */
        n = pcm_seq_abort(&ops);
        pcm_run(ops, n, 0, 0);
        evid = ((inp(PCM_PORT_R0) & PCM_R0_INIT) == 0 &&
                (cs_read(PCM_I_IFACE) & PCM_IFACE_PEN) == 0);
    }
    if (evid) g_pcm.state = PCM_ST_STOP_DONE;
    else pcm_enter_faulted(PCM_DIAG_FAULT_RECLAIM);
    irq_restore(f);
    pcm_release(evid ? 0 : 1);
}
