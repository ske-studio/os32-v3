#include "tick.h"
#include "tick_order.h"
_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, "PC98 tick slots exceed table");
/* ======================================================================== */
/*  LINK.C — OS32 リンクプロトコル v2 (非ブロッキング)                        */
/*                                                                          */
/*  契約の正典: docs/archive/network/TASK_N0.md §1b (ワイヤ) / §1a (KAPI の   */
/*  ABI) / §2 (駆動・排他・状態機械)。ドライバ API (drivers/net/ne2000/ne2000.c) だけに  */
/*  依存し、NIC の詳細は持たない。                                            */
/*                                                                          */
/*  設計の 3 本柱:                                                           */
/*   (1) **状態機械を進めるのは `link_tick()` だけ** (100Hz、IRQ0 文脈)。      */
/*       KAPI は状態を読み書きするだけで待たず、`link_tick` を呼ばない。      */
/*   (2) KAPI と `link_tick` の排他は **IF 保存復元の短い cli 区間**          */
/*       (include/io.h の irq_save / irq_restore)。区間内では待たない。       */
/*   (3) ワイヤ上の値は **バイト単位の LE アクセサ**で読み書きする。構造体     */
/*       キャストも非アラインアクセスもしない (`rid` は 4B 境界に載らない)。  */
/* ======================================================================== */

#include "link.h"

#include "ne2000.h"
#include "kstring.h"
#include "kprintf.h"
#include "le_internal.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* ([C4] 共有定数の正典) */

#define LINK_RX_BUFFER_SIZE 1520
#define LINK_CTRL_RELEASE 0
#define LINK_CTRL_ACK 1
#define LINK_CTRL_WINDOW 2
#define LINK_CTRL_STATUS 3
#define LINK_CTRL_KIND_COUNT 4
#define LINK_TX_CLASS_COUNT 2
#define LINK_HANDSHAKE_BUDGET 2

/* ------------------------------------------------------------------------ */
/*  カーネル / ホスト試験の切り替え                                          */
/*                                                                          */
/*  tools/tests/net_link_host.c はこのファイルを #include して、タイマ・NIC・ */
/*  cli / sti を贋物に差し替える。カーネルでは include/io.h の実物を使う。    */
/* ------------------------------------------------------------------------ */
#ifdef LINK_HOST_TEST
extern volatile u32 tick_count;
unsigned int link_test_irq_save(void);
void link_test_irq_restore(unsigned int flags);
void link_test_idle(void);
#define LINK_IRQ_SAVE()      link_test_irq_save()
#define LINK_IRQ_RESTORE(f)  link_test_irq_restore(f)
#define LINK_IDLE()          link_test_idle()
#else
#include "idt.h"            /* tick_count */
#include "io.h"             /* irq_save / irq_restore / _idle */
#define LINK_IRQ_SAVE()      irq_save()
#define LINK_IRQ_RESTORE(f)  irq_restore(f)
/* IF=1 で次の割り込みまで寝る。「許可する」と「眠る」は io.h の _idle() が
 * 1 つの不可分な原始命令として持つ (間に割り込みを入れない =
 * docs/design/arch/x86/kernel/ring3_entry.md#落とし穴 と同じ作法)。_enable(); _halt(); に分けてはいけない
 * 理由は io.h の _idle() の註。順序 3 ではここが HAL の待ち 1 関数になる。 */
#define LINK_IDLE()          _idle()
#endif

static const u8 link_bcast[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static int mac_eq(const u8 *a, const u8 *b)
{
    int i;
    for (i = 0; i < ETH_ALEN; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* ======================================================================== */
/*  状態                                                                    */
/* ======================================================================== */
static u8  link_my_mac[ETH_ALEN];
static int link_ready = 0;          /* link_init 済み (= link_tick が働く) */
static u8  link_txbuf[LINK_ETH_HDR_LEN + LINK_HDR_LEN + LINK_MAX_PAYLOAD];
static u8  link_rxbuf[LINK_RX_BUFFER_SIZE];

/* ---- セッション ---- */
u16 link_sess = 0;
u16 link_epoch = 0;
u16 link_agent_gen = 0;
static int link_state = LINK_S_DOWN;
static u32 link_nonce = 0;          /* HELLO ごとに +1 する自分の nonce */
static u32 link_agent_nonce = 0;
static u16 link_syn_sess = 0;       /* 最後に送った SYN の sess (写しの照合用) */
static u16 link_syn_epoch = 0;
static u32 link_hs_tick = 0;
static u32 link_hs_tries = 0;
static int link_hs_due = 0;         /* 1 = SYN を送る / 2 = CONFIRM を送る */
static u32 link_next_rid = 1;       /* セッション内で単調増加、0 は使わない */

/* ---- ハンドル (TASK_N0 §2c) ---- */
struct link_handle {
    u8  state;
    u16 gen;
    int owner;
    u32 rid;
    u16 epoch;
    u16 req_len;
    u8  req_acked;
    u8  req_due;
    u32 status;
    u32 length;
    u32 recv_bytes;
    u32 read_bytes;
    u8  ring_owner;
    u32 wseq;
    u16 wlen;
    u8  w_due;
    u32 decl_len;
    u32 wsent_total;
    u32 last_tx_tick;
    u32 retries;
    u32 last_rx_tick;
    u32 probes;
    u8  rel_pending;                /* ハンドル専用の RELEASE スロット */
    u8  rel_due;
    u32 rel_rid;
    u16 rel_epoch;
    u32 rel_tick;
    u32 rel_tries;
    u8  want_ack;
    u8  want_window;
    u8  want_status;
    u32 ack_seq;                    /* 累積 ACK (受けた最終 in-order DATA seq) */
    u8  ctrl_cursor;                /* 制御の種類を巡回する位置 */
    u8  req_copy[LINK_MAX_PAYLOAD];
    u8  wcopy[LINK_MAX_PAYLOAD];
    u8  relay[LINK_MAX_PAYLOAD];    /* host_read の中継 (成功確定点の写し先) */
};
static struct link_handle link_h[LINK_HANDLES];

/* ---- 本文リング 1 本 ---- */
static u8  link_stream_buf[LINK_STREAM_BUF];
static u32 link_stream_head = 0;
static u32 link_stream_count = 0;
static int link_ring_owner = -1;    /* リングを持つハンドル (-1 = 誰も) */

/* ---- TX の交互送信 ---- */
static int link_tx_turn = 0;        /* 0 = 制御、1 = 通常 */
static int link_ctrl_next = 0;
static int link_norm_next = 0;

/* ---- 観測 ---- */
u32 link_hello_ok = 0;
u32 link_rt_ok = 0;              /* 現在の自己試験区間の成功往復数 (区間ごとに reset) */
u32 link_rt_fail = 0;           /* 現在の区間の失敗往復数 */
u32 link_l0_ok = 0;             /* L0 selftest 専用スナップショット (最終読み出し用) */
u32 link_l0_fail = 0;
u32 link_retransmits = 0;
u32 link_rx_frames = 0;
u32 link_rx_dropped = 0;
u8  link_peer_mac[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
u32 link_resyncs = 0;
u32 link_tombstones = 0;
u32 link_no_slots = 0;
u32 link_processing = 0;
u32 link_tx_deferred = 0;

u32 link_l1_recv = 0;
u32 link_l1_bytes = 0;
u32 link_l1_ooo = 0;
u32 link_l1_windows = 0;
u32 link_l1_max_credit = 0;
u32 link_l1_min_credit = 0;
u32 link_l1_done = 0;
u32 link_l1_meas_pages = 0;

u32 link_l2_bytes = 0;
u32 link_l2_read = 0;
u32 link_l2_gaps = 0;
u32 link_l2_bad = 0;
u32 link_l2_eof = 0;
u32 link_l2_overflow = 0;

u32 link_l3_get_status = 0;
u32 link_l3_get_len = 0;
u32 link_l3_get_read = 0;
u32 link_l3_get_bad = 0;
u32 link_l3_404 = 0;
u32 link_l3_http_status = 0;
u32 link_l3_http_read = 0;
u32 link_l3_time_len = 0;

/* ======================================================================== */
/*  リング (本文ストリーム)                                                  */
/* ======================================================================== */
static u32 link_stream_free(void)
{
    return LINK_STREAM_BUF - link_stream_count;
}

static void link_stream_push(const u8 *p, u16 n)
{
    u32 tail, first;
    tail = (link_stream_head + link_stream_count) % LINK_STREAM_BUF;
    first = LINK_STREAM_BUF - tail;
    if (first > n) first = n;
    kmemcpy(link_stream_buf + tail, p, first);
    if (n > first) kmemcpy(link_stream_buf, p + first, (u32)n - first);
    link_stream_count += n;
    link_l2_bytes += n;
}

static void link_stream_reset(void)
{
    link_stream_head = 0;
    link_stream_count = 0;
}

/* ======================================================================== */
/*  送信 — 20B ヘッダを LE アクセサで組む                                    */
/*  戻り値 1 = NIC が受理、0 = 受理せず (次の周回に持ち越す)。                */
/* ======================================================================== */
static int link_send_ex(u8 op, u8 flags, u16 sess, u16 epoch, u32 seq, u32 ack,
                        u32 rid, const void *payload, unsigned int len)
{
    u8 *p = link_txbuf;
    u8 *h;
    const u8 *dst;

    if (len > LINK_MAX_PAYLOAD) return 1;    /* 呼び手の誤り。捨てる (再送しない) */
    dst = link_hello_ok ? link_peer_mac : link_bcast;
    kmemcpy(p, dst, ETH_ALEN);
    kmemcpy(p + ETH_ALEN, link_my_mac, ETH_ALEN);
    /* EtherType はワイヤ上 big-endian。以降の自前ヘッダは LE。 */
    p[ETH_TYPE_OFF] = (u8)(LINK_ETHERTYPE >> 8);
    p[ETH_TYPE_OFF + 1] = (u8)(LINK_ETHERTYPE & 0xFF);
    h = p + LINK_ETH_HDR_LEN;
    h[LINK_OFF_OP] = op;
    h[LINK_OFF_FLAGS] = flags;
    le_write16(h + LINK_OFF_EPOCH, epoch);
    le_write32(h + LINK_OFF_SEQ, seq);
    le_write32(h + LINK_OFF_ACK, ack);
    le_write16(h + LINK_OFF_LENGTH, (u16)len);
    le_write32(h + LINK_OFF_RID, rid);
    le_write16(h + LINK_OFF_SESS, sess);
    if (len) kmemcpy(h + LINK_HDR_LEN, payload, len);
    return (ne2k_send(p, LINK_ETH_HDR_LEN + LINK_HDR_LEN + len) == NE2K_OK) ? 1 : 0;
}

static int link_send(u8 op, u8 flags, u32 seq, u32 ack, u32 rid,
                     const void *payload, unsigned int len)
{
    return link_send_ex(op, flags, link_sess, link_epoch, seq, ack, rid, payload, len);
}

/* ======================================================================== */
/*  セッション                                                              */
/* ======================================================================== */
static void link_drop_all(void)
{
    int i;
    for (i = 0; i < LINK_HANDLES; i++) {
        struct link_handle *e = &link_h[i];
        if (e->state != LINK_H_FREE && e->state != LINK_H_STALE) {
            e->state = LINK_H_STALE;
            link_rt_fail++;
        }
        /* 保留 TX・制御・専用スロットは全部捨てる (旧 epoch のものは
         * Agent 側でも消えている。TASK_N0 §2c の再同期の行)。 */
        e->req_due = e->w_due = 0;
        e->wlen = 0;
        e->want_ack = e->want_window = e->want_status = 0;
        e->rel_pending = e->rel_due = 0;
        e->ring_owner = 0;
        e->probes = 0;
    }
    link_ring_owner = -1;
    link_stream_reset();
}

/* 新しい HELLO を出す。sess_hint = 0 なら新セッション。 */
static void link_start_hello(u16 sess_hint, u16 epoch_hint)
{
    link_nonce++;                       /* HELLO ごとに +1 (応答の対応付け) */
    link_syn_sess = sess_hint;
    link_syn_epoch = epoch_hint;
    link_state = LINK_S_SYN;
    link_hs_tries = 0;
    link_hs_due = 1;
    link_hello_ok = 0;
}

/* 再同期 (TASK_N0 §1b「再同期の契機」)。epoch 周回と rid 枯渇は新セッション。 */
static void link_resync(void)
{
    link_resyncs++;
    link_drop_all();
    if (link_sess == 0 || link_epoch >= U16_MAX || link_next_rid >= U32_MAX)
        link_start_hello(0, 1);
    else
        link_start_hello(link_sess, (u16)(link_epoch + 1));
}

#include "link_rx.inc"

/* ======================================================================== */
/*  credit (LINK_PLAN.md §2-2)                                              */
/* ======================================================================== */
static u16 link_credit_pages(void)
{
    unsigned int ring = ne2k_rx_ring_free_pages();
    unsigned int qpages = ne2k_rx_queue_free() * LINK_MAXFRAME_PAGES;
    unsigned int c = (ring < qpages) ? ring : qpages;
    unsigned int spages = link_stream_free() / LINK_PAGE_BYTES;
    if (spages < c) c = spages;
    c = (c > LINK_CREDIT_MARGIN) ? c - LINK_CREDIT_MARGIN : 0;
    return (u16)c;
}

/* ======================================================================== */
/*  タイマ — 再送・probe・handshake の期限を見て「送る印」を立てる            */
/* ======================================================================== */
static u32 since(u32 t) { return tick_count - t; }

static void link_timers(void)
{
    int i;

    if (link_state != LINK_S_UP) {
        /* 出す印が立っている間 (= まだ NIC が受けていない) は期限を数えない。
         * RTO は **NIC が送信を受理した tick から** (往復 2 の R9)。 */
        if (!link_hs_due && since(link_hs_tick) >= LINK_RTO_TICKS) {
            if (link_hs_tries >= LINK_TRIES) {
                /* CONFIRM が通らない → SYN からやり直す */
                link_start_hello(link_syn_sess, link_syn_epoch);
            } else if (link_state == LINK_S_SYN) {
                /* SYN-ACK が来ない → **nonce を進めて**出し直す。同じ提案
                 * (sess / epoch) のままなので、Agent 側の候補が差し替わる。 */
                link_nonce++;
                link_hs_due = 1;
                link_hs_tries++;
                link_retransmits++;
            } else {
                link_hs_due = LINK_HS_CONFIRM;            /* ESTABLISHED 待ち → CONFIRM 再送 */
                link_hs_tries++;
                link_retransmits++;
            }
        }
        return;                                     /* 未確立の間は通常 TX 無し */
    }

    for (i = 0; i < LINK_HANDLES; i++) {
        struct link_handle *e = &link_h[i];

        if (e->rel_pending && !e->rel_due && since(e->rel_tick) >= LINK_RTO_TICKS) {
            if (e->rel_tries >= LINK_TRIES) { link_resync(); return; }
            e->rel_due = 1;
            e->rel_tries++;
            link_retransmits++;
        }
        if (e->state == LINK_H_FREE || e->state == LINK_H_STALE) continue;

        if (!e->req_acked && !e->req_due && since(e->last_tx_tick) >= LINK_RTO_TICKS) {
            if (e->retries >= LINK_TRIES) { link_resync(); return; }
            e->req_due = 1;
            e->retries++;
            link_retransmits++;
        }
        if (e->wlen && !e->w_due && since(e->last_tx_tick) >= LINK_RTO_TICKS) {
            if (e->retries >= LINK_TRIES) { link_resync(); return; }
            e->w_due = 1;
            e->retries++;
            link_retransmits++;
        }
        /* 生存確認 (T_probe)。req_acked なのに RESPONSE が来ない、または
         * 本文が途中で止まった場合に STATUS を出す。k 回無応答で再同期。 */
        if ((e->state == LINK_H_SENT && e->req_acked) ||
            (e->state == LINK_H_RESP && e->recv_bytes < e->length)) {
            if (since(e->last_rx_tick) >= LINK_PROBE_TICKS) {
                if (e->probes >= LINK_PROBE_MAX) { link_resync(); return; }
                e->want_status = 1;
                e->probes++;
                e->last_rx_tick = tick_count;
            }
        }
        /* WINDOW は本文を待っている間ずっと (制御枠で 1 周回 1 本に集約される) */
        if (e->ring_owner && e->state == LINK_H_RESP && e->recv_bytes < e->length)
            e->want_window = 1;
    }
}

/* ======================================================================== */
/*  TX — 制御と通常の交互送信 (TASK_N0 §2a、往復 2 の R9)                    */
/* ======================================================================== */

/* 1 本の制御フレームを出す。1 = 送った、0 = 出すものが無い、-1 = NIC が受けず。 */
static int link_tx_control(void)
{
    int n, k;

    /* handshake はセッションそのものの制御。最優先。 */
    if (link_hs_due) {
        int ok;
        if (link_hs_due == 1)
            ok = link_send_ex(LINK_OP_HELLO, LINK_HS_SYN, link_syn_sess,
                              link_syn_epoch, link_nonce, 0, 0, 0, 0);
        else
            ok = link_send(LINK_OP_HELLO, LINK_HS_CONFIRM, link_nonce,
                           link_agent_nonce, 0, 0, 0);
        if (!ok) return -1;
        link_hs_tick = tick_count;
        link_hs_due = 0;
        return 1;
    }
    if (link_state != LINK_S_UP) return 0;

    for (n = 0; n < LINK_HANDLES; n++) {
        struct link_handle *e = &link_h[(link_ctrl_next + n) % LINK_HANDLES];
        /* rid ごとに種類を巡回する (STATUS を WINDOW / ACK より後回しにしない) */
        for (k = 0; k < LINK_CTRL_KIND_COUNT; k++) {
            u8 kind = (u8)((e->ctrl_cursor + k) % LINK_CTRL_KIND_COUNT);
            if (kind == LINK_CTRL_RELEASE && e->rel_due) {
                if (!link_send_ex(LINK_OP_RELEASE, 0, link_sess, e->rel_epoch,
                                  0, 0, e->rel_rid, 0, 0)) return -1;
                e->rel_due = 0;
                e->rel_tick = tick_count;
            } else if (kind == LINK_CTRL_ACK && e->want_ack) {
                if (!link_send(LINK_OP_ACK, 0, 0, e->ack_seq, e->rid, 0, 0)) return -1;
                e->want_ack = 0;
            } else if (kind == LINK_CTRL_WINDOW && e->want_window) {
                u8 pl[LINK_WINDOW_LEN];
                u16 credit = link_credit_pages();
                le_write16(pl, credit);
                if (!link_send(LINK_OP_WINDOW, 0, 0, e->ack_seq, e->rid,
                               pl, LINK_WINDOW_LEN)) return -1;
                e->want_window = 0;
                link_l1_windows++;
                if (credit > link_l1_max_credit) link_l1_max_credit = credit;
                if (credit && (link_l1_min_credit == 0 || credit < link_l1_min_credit))
                    link_l1_min_credit = credit;
            } else if (kind == LINK_CTRL_STATUS && e->want_status) {
                if (!link_send(LINK_OP_STATUS, 0, 0, 0, e->rid, 0, 0)) return -1;
                e->want_status = 0;
            } else {
                continue;
            }
            e->ctrl_cursor = (u8)((kind + 1) % LINK_CTRL_KIND_COUNT);
            link_ctrl_next = (int)(((link_ctrl_next + n) % LINK_HANDLES) + 1) % LINK_HANDLES;
            return 1;
        }
    }
    return 0;
}

/* 1 本の通常フレーム (REQUEST / WDATA) を出す。 */
static int link_tx_normal(void)
{
    int n;
    if (link_state != LINK_S_UP) return 0;
    for (n = 0; n < LINK_HANDLES; n++) {
        int idx = (link_norm_next + n) % LINK_HANDLES;
        struct link_handle *e = &link_h[idx];
        if (e->state == LINK_H_FREE || e->state == LINK_H_STALE) continue;
        if (e->req_due) {
            if (!link_send(LINK_OP_REQUEST, 0, 0, 0, e->rid,
                           e->req_copy, e->req_len)) return -1;
            e->req_due = 0;
            e->last_tx_tick = tick_count;        /* RTO は NIC 受理から数える */
            link_norm_next = (idx + 1) % LINK_HANDLES;
            return 1;
        }
        if (e->w_due && e->wlen) {
            if (!link_send(LINK_OP_WDATA, 0, e->wseq, 0, e->rid,
                           e->wcopy, e->wlen)) return -1;
            e->w_due = 0;
            e->last_tx_tick = tick_count;
            link_norm_next = (idx + 1) % LINK_HANDLES;
            return 1;
        }
    }
    return 0;
}

static void link_tx_round(void)
{
    int idle = 0;
    int budget = LINK_TX_CLASS_COUNT * (LINK_HANDLES * LINK_CTRL_KIND_COUNT + LINK_HANDSHAKE_BUDGET);
    while (budget-- > 0 && idle < LINK_TX_CLASS_COUNT) {
        int rc = link_tx_turn ? link_tx_normal() : link_tx_control();
        if (rc < 0) { link_tx_deferred++; return; } /* NIC busy → 位置を進めず次の周回へ */
        link_tx_turn ^= 1;                          /* 送れた / 空のときだけ次の種類へ */
        idle = rc ? 0 : idle + 1;
    }
}

/* ======================================================================== */
/*  link_tick — プロトコルの唯一の駆動元 (100Hz、IRQ0 文脈)                  */
/* ======================================================================== */
void link_tick(void)
{
    if (!link_ready) return;
    if (ne2k_state() != NE2K_STATE_RUNNING) return;
    if (ne2k_is_busy()) return;      /* foreground の send/recv を中断した tick */
    link_rx_pump();
    link_timers();
    link_tx_round();
}

/* ======================================================================== */
/*  初期化                                                                  */
/* ======================================================================== */
void link_init(const u8 my_mac[ETH_ALEN])
{
    if (tick_register(PC98_TICK_LINK, link_tick) != 0) {
        kprintf(ATTR_YELLOW, "[link] tick registration failed\n");
        return;
    }
    int i;
    kmemcpy(link_my_mac, my_mac, ETH_ALEN);
    kmemset(link_peer_mac, 0, ETH_ALEN);
    kmemset((void *)link_h, 0, sizeof(link_h));
    for (i = 0; i < LINK_HANDLES; i++) link_h[i].owner = -1;
    link_sess = 0;
    link_epoch = 0;
    link_agent_gen = 0;
    link_state = LINK_S_DOWN;
    link_hello_ok = 0;
    link_next_rid = 1;
    link_ring_owner = -1;
    link_stream_reset();
    link_tx_turn = 0;
    link_ctrl_next = link_norm_next = 0;
    /* nonce の初期値は tick を種にする (一意性は要らない — 応答の対応付けだけ)。
     * 同じ値で再起動しても、sess を採番するのは Agent なので別セッションになる
     * (TASK_N0 §1b、往復 3 の B3)。 */
    link_nonce = tick_count;
    link_ready = 1;
    link_start_hello(0, 1);
}

#include "link_host_api.inc"

#include "link_selftest.inc"
