/* ======================================================================== */
/*  LINK.C — OS32 リンクプロトコル v2 (非ブロッキング)                        */
/*                                                                          */
/*  契約の正典: docs/archive/network/TASK_N0.md §1b (ワイヤ) / §1a (KAPI の   */
/*  ABI) / §2 (駆動・排他・状態機械)。ドライバ API (drivers/ne2000.c) だけに  */
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
#include "os32_kapi_shared.h"   /* OS32_ERR_* ([C4] 共有定数の正典) */

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
 * POLICY_DEBUG §4-19 と同じ作法)。_enable(); _halt(); に分けてはいけない
 * 理由は io.h の _idle() の註。順序 3 ではここが HAL の待ち 1 関数になる。 */
#define LINK_IDLE()          _idle()
#endif

/* ======================================================================== */
/*  LE アクセサ — ワイヤ上の値はすべてここを通す                             */
/* ======================================================================== */
static u16 rd16(const u8 *p) { return (u16)(p[0] | ((u16)p[1] << 8)); }
static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static const u8 link_bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static int mac_eq(const u8 *a, const u8 *b)
{
    int i;
    for (i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* ======================================================================== */
/*  状態                                                                    */
/* ======================================================================== */
static u8  link_my_mac[6];
static int link_ready = 0;          /* link_init 済み (= link_tick が働く) */
static u8  link_txbuf[LINK_ETH_HDR_LEN + LINK_HDR_LEN + LINK_MAX_PAYLOAD];
static u8  link_rxbuf[1520];

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
    u8  wacked;
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
u8  link_peer_mac[6] = { 0, 0, 0, 0, 0, 0 };
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
    if (n > link_stream_free()) { link_l2_overflow++; return; }
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
    kmemcpy(p, dst, 6);
    kmemcpy(p + 6, link_my_mac, 6);
    /* EtherType はワイヤ上 big-endian。以降の自前ヘッダは LE。 */
    p[12] = (u8)(LINK_ETHERTYPE >> 8);
    p[13] = (u8)(LINK_ETHERTYPE & 0xFF);
    h = p + LINK_ETH_HDR_LEN;
    h[LINK_OFF_OP] = op;
    h[LINK_OFF_FLAGS] = flags;
    wr16(h + LINK_OFF_EPOCH, epoch);
    wr32(h + LINK_OFF_SEQ, seq);
    wr32(h + LINK_OFF_ACK, ack);
    wr16(h + LINK_OFF_LENGTH, (u16)len);
    wr32(h + LINK_OFF_RID, rid);
    wr16(h + LINK_OFF_SESS, sess);
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
        if (e->state != LINK_H_FREE) {
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
    if (link_sess == 0 || link_epoch >= 0xFFFF || link_next_rid >= 0xFFFFFFFFu)
        link_start_hello(0, 1);
    else
        link_start_hello(link_sess, (u16)(link_epoch + 1));
}

/* ======================================================================== */
/*  受信 dispatch                                                           */
/* ======================================================================== */
static int link_paylen_ok(u8 op, u8 flags, u16 plen)
{
    switch (op) {
    case LINK_OP_HELLO:
        if (flags == LINK_HS_SYN || flags == LINK_HS_CONFIRM) return plen == 0;
        if (flags == LINK_HS_SYNACK) return plen == LINK_HELLO_SA_LEN;
        if (flags == LINK_HS_ESTAB)  return plen == LINK_HELLO_ES_LEN;
        return 0;
    case LINK_OP_REQUEST:
    case LINK_OP_WDATA:
    case LINK_OP_DATA:
        return plen >= 1 && plen <= LINK_MAX_PAYLOAD;
    case LINK_OP_RESPONSE: return plen == LINK_RESP_LEN;
    case LINK_OP_WINDOW:   return plen == LINK_WINDOW_LEN;
    case LINK_OP_ACK:
    case LINK_OP_STATUS:
    case LINK_OP_RELEASE:
    case LINK_OP_EOF:      return plen == 0;
    default:               return 0;
    }
}

static struct link_handle *link_by_rid(u32 rid)
{
    int i;
    for (i = 0; i < LINK_HANDLES; i++) {
        struct link_handle *e = &link_h[i];
        if (e->state != LINK_H_FREE && e->state != LINK_H_STALE && e->rid == rid)
            return e;
    }
    return (struct link_handle *)0;
}

static void link_on_hello(const u8 *src, u8 flags, u16 epoch, u32 seq, u32 ack,
                          u16 sess, const u8 *pl)
{
    if (flags == LINK_HS_SYNACK) {
        if (link_state != LINK_S_SYN) return;
        if (seq != link_nonce) return;                  /* 自分の SYN の応答か */
        if (rd16(pl + 2) != link_syn_sess) return;      /* req_sess の写し */
        if (rd16(pl + 4) != link_syn_epoch) return;     /* req_epoch の写し */
        link_agent_gen = rd16(pl);
        link_agent_nonce = ack;
        kmemcpy(link_peer_mac, src, 6);
        if (sess != link_sess) link_next_rid = 1;       /* 新セッション */
        link_sess = sess;
        link_epoch = epoch;                             /* CONFIRM 送出で切替 */
        link_state = LINK_S_CONFIRM;
        link_hs_tries = 0;
        link_hs_due = 2;
        return;
    }
    if (flags == LINK_HS_ESTAB) {
        if (link_state != LINK_S_CONFIRM) return;
        if (sess != link_sess || epoch != link_epoch) return;
        if (ack != link_nonce) return;
        if (!mac_eq(src, link_peer_mac)) return;
        link_agent_gen = rd16(pl);
        link_state = LINK_S_UP;
        link_hs_due = 0;
        link_hello_ok = 1;
        return;
    }
    /* SYN / CONFIRM は OS32 発。受けても何もしない。 */
}

static void link_on_response(struct link_handle *e, u8 flags, const u8 *pl)
{
    u16 st = rd16(pl);
    u32 ln = rd32(pl + 2);

    e->last_rx_tick = tick_count;
    e->probes = 0;
    if (flags & LINK_F_CTRL) {
        switch (st) {
        case LINK_CTL_PROCESSING:
            link_processing++;
            break;
        case LINK_CTL_NO_SLOT: {
            /* RELEASE 未達 = Agent の受付枠が空いていない。**未 ACK の RELEASE を
             * 先に送り直してから** REQUEST を再送する。未達の RELEASE は必ずしも
             * この rid のハンドルのものではない (閉じた方の専用スロットに残って
             * いる) ので、保留中の RELEASE は全部立て直す。 */
            int k;
            link_no_slots++;
            for (k = 0; k < LINK_HANDLES; k++)
                if (link_h[k].rel_pending) link_h[k].rel_due = 1;
            link_tx_turn = 0;               /* 次に出すのは制御 (= RELEASE) から */
            e->req_due = 1;
            break;
        }
        case LINK_CTL_TOMBSTONE:
            link_tombstones++;
            e->state = LINK_H_STALE;
            link_rt_fail++;
            if (e->ring_owner) { e->ring_owner = 0; link_ring_owner = -1;
                                 link_stream_reset(); }
            break;
        default:
            break;
        }
        return;
    }
    /* 業務結果。重複は無視する。 */
    if (e->state != LINK_H_SENT) return;
    e->status = st;
    e->length = ln;
    e->req_due = 0;
    e->req_acked = 1;
    link_rt_ok++;
    e->state = (ln == 0) ? LINK_H_DONE : LINK_H_RESP;
}

static void link_dispatch(const u8 *f, unsigned int flen)
{
    const u8 *h;
    const u8 *pl;
    u8 op, flags;
    u16 epoch, plen, sess;
    u32 seq, ack, rid;
    struct link_handle *e;

    if (flen < LINK_ETH_HDR_LEN + LINK_HDR_LEN) { link_rx_dropped++; return; }
    if ((((u16)f[12] << 8) | f[13]) != LINK_ETHERTYPE) { link_rx_dropped++; return; }
    if (!mac_eq(f, link_my_mac) && !mac_eq(f, link_bcast)) { link_rx_dropped++; return; }

    h = f + LINK_ETH_HDR_LEN;
    op = h[LINK_OFF_OP];
    flags = h[LINK_OFF_FLAGS];
    epoch = rd16(h + LINK_OFF_EPOCH);
    seq = rd32(h + LINK_OFF_SEQ);
    ack = rd32(h + LINK_OFF_ACK);
    plen = rd16(h + LINK_OFF_LENGTH);
    rid = rd32(h + LINK_OFF_RID);
    sess = rd16(h + LINK_OFF_SESS);
    if ((unsigned int)(LINK_ETH_HDR_LEN + LINK_HDR_LEN + plen) > flen) {
        link_rx_dropped++; return;
    }
    if (!link_paylen_ok(op, flags, plen)) { link_rx_dropped++; return; }
    pl = h + LINK_HDR_LEN;
    link_rx_frames++;

    if (op == LINK_OP_HELLO) {
        link_on_hello(f + 6, flags, epoch, seq, ack, sess, pl);
        return;
    }
    /* HELLO 以外は sess / epoch が控えと一致し、相手が peer のものだけ。 */
    if (link_state != LINK_S_UP || sess != link_sess || epoch != link_epoch ||
        !mac_eq(f + 6, link_peer_mac)) {
        link_rx_dropped++;
        return;
    }

    switch (op) {
    case LINK_OP_ACK:
        if (flags & LINK_F_RELACK) {
            int i;
            for (i = 0; i < LINK_HANDLES; i++) {
                if (link_h[i].rel_pending && link_h[i].rel_rid == rid) {
                    link_h[i].rel_pending = 0;
                    link_h[i].rel_due = 0;
                    link_h[i].rel_tries = 0;
                }
            }
            break;
        }
        e = link_by_rid(rid);
        if (!e) { link_rx_dropped++; break; }
        e->last_rx_tick = tick_count;
        e->probes = 0;
        if (!e->req_acked) { e->req_acked = 1; e->req_due = 0; e->retries = 0; }
        if (e->wlen && !e->wacked && ack >= e->wseq) {
            e->wacked = 1;
            e->w_due = 0;
            e->wlen = 0;
            e->wseq++;
            e->retries = 0;
        }
        break;

    case LINK_OP_RESPONSE:
        e = link_by_rid(rid);
        if (!e) { link_rx_dropped++; break; }
        link_on_response(e, flags, pl);
        break;

    case LINK_OP_DATA:
        e = link_by_rid(rid);
        if (!e || !e->ring_owner) { link_rx_dropped++; break; }
        e->last_rx_tick = tick_count;
        e->probes = 0;
        if (seq == e->ack_seq + 1) {
            if (plen <= link_stream_free()) {
                link_stream_push(pl, plen);
                e->ack_seq = seq;
                e->recv_bytes += plen;
                e->want_ack = 1;
                link_l1_recv++;
                link_l1_bytes += plen;
            } else {
                link_l2_overflow++;     /* 空き不足 → ack を止めてホストに待たせる */
            }
        } else if (seq > e->ack_seq + 1) {
            link_l2_gaps++;             /* 先行 → 捨てて再 ACK (Go-Back-N) */
            link_l1_ooo++;
            e->want_ack = 1;
        } else {
            e->want_ack = 1;            /* 重複 → 累積 ACK を返すだけ */
        }
        break;

    case LINK_OP_EOF:
        e = link_by_rid(rid);
        if (!e || !e->ring_owner) { link_rx_dropped++; break; }
        e->last_rx_tick = tick_count;
        link_l2_eof = 1;
        link_l1_done = 1;
        break;

    default:
        link_rx_dropped++;              /* OS32 発の op を受けても使わない */
        break;
    }
}

static void link_rx_pump(void)
{
    int n;
    for (n = 0; n < LINK_RX_BUDGET; n++) {
        unsigned int len = 0;
        if (ne2k_recv(link_rxbuf, sizeof(link_rxbuf), &len) != NE2K_OK) break;
        link_dispatch(link_rxbuf, len);
    }
}

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
                link_hs_due = 2;            /* ESTABLISHED 待ち → CONFIRM 再送 */
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
        if (e->wlen && !e->wacked && !e->w_due && since(e->last_tx_tick) >= LINK_RTO_TICKS) {
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
        for (k = 0; k < 4; k++) {
            u8 kind = (u8)((e->ctrl_cursor + k) % 4);
            if (kind == 0 && e->rel_due) {
                if (!link_send_ex(LINK_OP_RELEASE, 0, link_sess, e->rel_epoch,
                                  0, 0, e->rel_rid, 0, 0)) return -1;
                e->rel_due = 0;
                e->rel_tick = tick_count;
            } else if (kind == 1 && e->want_ack) {
                if (!link_send(LINK_OP_ACK, 0, 0, e->ack_seq, e->rid, 0, 0)) return -1;
                e->want_ack = 0;
            } else if (kind == 2 && e->want_window) {
                u8 pl[LINK_WINDOW_LEN];
                u16 credit = link_credit_pages();
                wr16(pl, credit);
                if (!link_send(LINK_OP_WINDOW, 0, 0, e->ack_seq, e->rid,
                               pl, LINK_WINDOW_LEN)) return -1;
                e->want_window = 0;
                link_l1_windows++;
                if (credit > link_l1_max_credit) link_l1_max_credit = credit;
                if (credit && (link_l1_min_credit == 0 || credit < link_l1_min_credit))
                    link_l1_min_credit = credit;
            } else if (kind == 3 && e->want_status) {
                if (!link_send(LINK_OP_STATUS, 0, 0, 0, e->rid, 0, 0)) return -1;
                e->want_status = 0;
            } else {
                continue;
            }
            e->ctrl_cursor = (u8)((kind + 1) % 4);
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
    int budget = 2 * (LINK_HANDLES * 4 + 2);
    while (budget-- > 0 && idle < 2) {
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
void link_init(const u8 my_mac[6])
{
    int i;
    kmemcpy(link_my_mac, my_mac, 6);
    kmemset(link_peer_mac, 0, 6);
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

/* ======================================================================== */
/*  要求行の宣言長 (ECHO / CLIP PUT / PRINT DATA / PUT)                      */
/* ======================================================================== */
static int tok(const char *s, u32 len, int n, u32 *start, u32 *end)
{
    u32 i = 0;
    int k = 0;
    for (;;) {
        while (i < len && s[i] == ' ') i++;
        if (i >= len) return 0;
        *start = i;
        while (i < len && s[i] != ' ') i++;
        *end = i;
        if (k == n) return 1;
        k++;
    }
}

static int tok_eq(const char *s, u32 a, u32 b, const char *lit)
{
    u32 i;
    for (i = a; i < b; i++) {
        if (lit[i - a] == '\0' || s[i] != lit[i - a]) return 0;
    }
    return lit[b - a] == '\0';
}

static u32 tok_num(const char *s, u32 a, u32 b)
{
    u32 v = 0, i;
    if (a >= b) return 0;
    for (i = a; i < b; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (u32)(s[i] - '0');
        if (v > LINK_DECL_MAX) return 0;
    }
    return v;
}

u32 link_decl_len(const char *req, u32 len)
{
    u32 a0, b0, a1, b1, a, b;
    int which = -1;

    if (!tok(req, len, 0, &a0, &b0)) return 0;
    if (tok_eq(req, a0, b0, "ECHO")) which = 1;
    else if (tok_eq(req, a0, b0, "PUT")) which = 2;
    else if (tok(req, len, 1, &a1, &b1)) {
        if (tok_eq(req, a0, b0, "CLIP") && tok_eq(req, a1, b1, "PUT")) which = 2;
        else if (tok_eq(req, a0, b0, "PRINT") && tok_eq(req, a1, b1, "DATA")) which = 3;
    }
    if (which < 0) return 0;
    if (which == 1) {
        if (!tok(req, len, 1, &a, &b)) return 0;
        return tok_num(req, a, b);
    }
    /* 末尾の語を宣言長とする (PUT /file/x <len> / CLIP PUT <len> / PRINT DATA <id> <len>) */
    {
        int n = 0;
        u32 la = 0, lb = 0;
        while (tok(req, len, n, &a, &b)) { la = a; lb = b; n++; if (n > 16) break; }
        if (n < which + 1) return 0;
        return tok_num(req, la, lb);
    }
}

/* ======================================================================== */
/*  KAPI v51 の実体 — どれも待たない。排他は短い cli 区間。                   */
/* ======================================================================== */
static struct link_handle *link_slot(i32 h, int owner)
{
    struct link_handle *e;
    if (h < 0 || h >= LINK_HANDLES) return (struct link_handle *)0;
    e = &link_h[h];
    if (e->state == LINK_H_FREE) return (struct link_handle *)0;
    if (e->owner != owner) return (struct link_handle *)0;
    return e;
}

i32 link_host_open(const char *req, u32 len, int owner)
{
    unsigned int f;
    int i, hh = -1, busy_rel = 0;
    u16 gen;

    if (!link_ready || ne2k_state() != NE2K_STATE_RUNNING) return OS32_ERR_NOSYS;
    if (req == 0 || len == 0 || len > LINK_MAX_PAYLOAD) return OS32_ERR_INVAL;

    f = LINK_IRQ_SAVE();
    if (link_state != LINK_S_UP) { LINK_IRQ_RESTORE(f); return OS32_ERR_STALE; }
    if (link_next_rid >= 0xFFFFFFFFu) {          /* rid 枯渇 = 新セッションへ */
        link_resync();
        LINK_IRQ_RESTORE(f);
        return OS32_ERR_STALE;
    }
    for (i = 0; i < LINK_HANDLES; i++) {
        if (link_h[i].state != LINK_H_FREE) continue;
        if (link_h[i].rel_pending) { busy_rel = 1; continue; }
        hh = i;
        break;
    }
    gen = (hh >= 0) ? link_h[hh].gen : 0;
    LINK_IRQ_RESTORE(f);
    if (hh < 0) return busy_rel ? OS32_ERR_AGAIN : OS32_ERR_FULL;

    /* FREE のハンドルは link_tick が触らないので、写しは IF=1 で作ってよい。 */
    kmemcpy(link_h[hh].req_copy, req, len);

    f = LINK_IRQ_SAVE();
    if (link_h[hh].state != LINK_H_FREE || link_h[hh].gen != gen ||
        link_h[hh].rel_pending) {
        LINK_IRQ_RESTORE(f);
        return OS32_ERR_AGAIN;
    }
    if (link_state != LINK_S_UP) { LINK_IRQ_RESTORE(f); return OS32_ERR_STALE; }
    {
        struct link_handle *e = &link_h[hh];
        e->state = LINK_H_SENT;
        e->owner = owner;
        e->rid = link_next_rid++;
        e->epoch = link_epoch;
        e->req_len = (u16)len;
        e->req_acked = 0;
        e->req_due = 1;
        e->status = e->length = e->recv_bytes = e->read_bytes = 0;
        e->ring_owner = 0;
        e->wseq = 1;
        e->wlen = 0;
        e->wacked = 1;
        e->w_due = 0;
        e->decl_len = link_decl_len((const char *)e->req_copy, len);
        e->wsent_total = 0;
        e->last_tx_tick = tick_count;   /* 未送信は RTO 対象外。req_due=1 が即送信を担保 */
        e->last_rx_tick = tick_count;
        e->retries = 0;
        e->probes = 0;
        e->ack_seq = 0;
        e->want_ack = e->want_window = e->want_status = 0;
        e->ctrl_cursor = 0;
    }
    LINK_IRQ_RESTORE(f);
    return hh;
}

i32 link_host_status(i32 h, u32 *status, u32 *length, int owner)
{
    unsigned int f;
    struct link_handle *e;
    i32 rc;

    f = LINK_IRQ_SAVE();
    e = link_slot(h, owner);
    if (!e) rc = OS32_ERR_INVAL;
    else if (e->state == LINK_H_STALE) rc = OS32_ERR_STALE;
    else if (e->state == LINK_H_SENT) rc = OS32_ERR_AGAIN;
    else {
        if (status) *status = e->status;
        if (length) *length = e->length;
        rc = 0;
    }
    LINK_IRQ_RESTORE(f);
    return rc;
}

/* host_read の**成功確定点はこの cli 区間**。ここでリング → 中継バッファへ
 * 写し、read_bytes を進め、リングを消費する。戻ったあとユーザー領域へ写す
 * 間に再同期 / close が起きても戻り値は変えない (TASK_N0 §2a、往復 4 の R4)。 */
i32 link_host_read_stage(i32 h, u32 cap, const u8 **out, int owner)
{
    unsigned int f;
    struct link_handle *e;
    i32 rc;
    u32 n, chunk;

    f = LINK_IRQ_SAVE();
    e = link_slot(h, owner);
    if (!e) { LINK_IRQ_RESTORE(f); return OS32_ERR_INVAL; }
    if (e->state == LINK_H_STALE) { LINK_IRQ_RESTORE(f); return OS32_ERR_STALE; }
    if (e->state == LINK_H_SENT) { LINK_IRQ_RESTORE(f); return OS32_ERR_AGAIN; }
    if (e->length == 0 || e->read_bytes >= e->length) {
        LINK_IRQ_RESTORE(f);
        return 0;                                   /* 完了 */
    }
    if (link_ring_owner < 0) { link_ring_owner = (int)h; e->ring_owner = 1; }
    if (link_ring_owner != (int)h) { LINK_IRQ_RESTORE(f); return OS32_ERR_AGAIN; }

    n = cap;
    if (n > LINK_MAX_PAYLOAD) n = LINK_MAX_PAYLOAD;
    chunk = LINK_STREAM_BUF - link_stream_head;     /* リングの連続可用 */
    if (chunk > link_stream_count) chunk = link_stream_count;
    if (n > chunk) n = chunk;
    if (n == 0) { LINK_IRQ_RESTORE(f); return OS32_ERR_AGAIN; }

    kmemcpy(e->relay, link_stream_buf + link_stream_head, n);
    link_stream_head = (link_stream_head + n) % LINK_STREAM_BUF;
    link_stream_count -= n;
    e->read_bytes += n;
    link_l2_read += n;
    if (e->read_bytes >= e->length) e->state = LINK_H_DONE;
    *out = e->relay;
    rc = (i32)n;
    LINK_IRQ_RESTORE(f);
    return rc;
}

i32 link_host_write(i32 h, const void *buf, u32 len, int owner)
{
    unsigned int f;
    struct link_handle *e;
    u32 n, remain;
    u16 gen;

    if (buf == 0 || len == 0) return OS32_ERR_INVAL;

    f = LINK_IRQ_SAVE();
    e = link_slot(h, owner);
    if (!e) { LINK_IRQ_RESTORE(f); return OS32_ERR_INVAL; }
    if (e->state == LINK_H_STALE) { LINK_IRQ_RESTORE(f); return OS32_ERR_STALE; }
    if (e->decl_len == 0) { LINK_IRQ_RESTORE(f); return OS32_ERR_INVAL; }
    if (e->state != LINK_H_SENT) { LINK_IRQ_RESTORE(f); return OS32_ERR_INVAL; }
    remain = e->decl_len - e->wsent_total;
    if (len > remain) { LINK_IRQ_RESTORE(f); return OS32_ERR_INVAL; }
    if (!e->req_acked) { LINK_IRQ_RESTORE(f); return OS32_ERR_AGAIN; }
    if (e->wlen || !e->wacked) { LINK_IRQ_RESTORE(f); return OS32_ERR_AGAIN; }
    gen = e->gen;
    n = (len > LINK_MAX_PAYLOAD) ? LINK_MAX_PAYLOAD : len;
    LINK_IRQ_RESTORE(f);

    /* 未 ACK の WDATA が無い間 wcopy は link_tick から読まれない。 */
    kmemcpy(link_h[h].wcopy, buf, n);

    f = LINK_IRQ_SAVE();
    e = link_slot(h, owner);
    if (!e || e->gen != gen) { LINK_IRQ_RESTORE(f); return OS32_ERR_INVAL; }
    if (e->state == LINK_H_STALE) { LINK_IRQ_RESTORE(f); return OS32_ERR_STALE; }
    if (e->state != LINK_H_SENT || e->wlen || !e->wacked || !e->req_acked) {
        LINK_IRQ_RESTORE(f);
        return OS32_ERR_AGAIN;
    }
    e->wlen = (u16)n;
    e->wacked = 0;
    e->w_due = 1;
    e->retries = 0;
    e->wsent_total += n;
    e->last_tx_tick = tick_count;   /* 未送信は RTO 対象外。w_due=1 が即送信を担保 */
    LINK_IRQ_RESTORE(f);
    return (i32)n;
}

static void link_free_handle(struct link_handle *e, int idx)
{
    if (e->state != LINK_H_STALE && e->state != LINK_H_FREE) {
        /* 専用スロットの RELEASE (キューの容量に依らず必ず積める) */
        e->rel_pending = 1;
        e->rel_due = 1;
        e->rel_rid = e->rid;
        e->rel_epoch = e->epoch;
        e->rel_tick = tick_count;   /* 未送信は RTO 対象外。rel_due=1 が即送信を担保 */
        e->rel_tries = 0;
    }
    if (e->ring_owner) {
        e->ring_owner = 0;
        if (link_ring_owner == idx) { link_ring_owner = -1; link_stream_reset(); }
    }
    e->state = LINK_H_FREE;
    e->owner = -1;
    e->gen++;
    e->req_due = e->w_due = 0;
    e->wlen = 0;
    e->want_ack = e->want_window = e->want_status = 0;
    e->recv_bytes = e->read_bytes = e->length = e->status = 0;
    e->decl_len = e->wsent_total = 0;
    e->ack_seq = 0;
}

i32 link_host_close(i32 h, int owner)
{
    unsigned int f;
    struct link_handle *e;

    f = LINK_IRQ_SAVE();
    e = link_slot(h, owner);
    if (!e) { LINK_IRQ_RESTORE(f); return OS32_ERR_INVAL; }
    link_free_handle(e, (int)h);
    LINK_IRQ_RESTORE(f);
    return 0;
}

void link_host_owner_exit(int owner)
{
    unsigned int f;
    int i;
    f = LINK_IRQ_SAVE();
    for (i = 0; i < LINK_HANDLES; i++) {
        if (link_h[i].state != LINK_H_FREE && link_h[i].owner == owner)
            link_free_handle(&link_h[i], i);
    }
    LINK_IRQ_RESTORE(f);
}

/* ======================================================================== */
/*  自己試験 (LGY98_FLAG_LINKTEST)                                          */
/*                                                                          */
/*  KAPI と同じ非同期 API の上に組み、待つのは **IF=1 の hlt** だけ。        */
/*  状態機械を進めるのは link_tick (タイマ) で、ここからは呼ばない           */
/*  (TASK_N0 §1a の最終行、往復 2 の R8)。                                   */
/* ======================================================================== */
#define LINK_SELF_OWNER  0          /* CPL=0 の自己試験が使う owner */

static unsigned int cstr(const char *s, char *dst, unsigned int cap)
{
    unsigned int n = 0;
    while (s[n] && n < cap) { dst[n] = s[n]; n++; }
    return n;
}

static int link_wait_up(u32 ticks)
{
    u32 deadline = tick_count + ticks;
    while (link_state != LINK_S_UP && (i32)(deadline - tick_count) > 0) LINK_IDLE();
    return link_state == LINK_S_UP;
}

/* open を AGAIN / FULL の間だけ待つ。戻り値 = ハンドル / 負。 */
static i32 link_wait_open(const char *req, u32 len, u32 ticks)
{
    u32 deadline = tick_count + ticks;
    for (;;) {
        i32 h = link_host_open(req, len, LINK_SELF_OWNER);
        if (h >= 0 || (h != OS32_ERR_AGAIN && h != OS32_ERR_FULL)) return h;
        if ((i32)(deadline - tick_count) <= 0) return OS32_ERR_AGAIN;
        LINK_IDLE();
    }
}

static i32 link_wait_status(i32 h, u32 *st, u32 *ln, u32 ticks)
{
    u32 deadline = tick_count + ticks;
    for (;;) {
        i32 rc = link_host_status(h, st, ln, LINK_SELF_OWNER);
        if (rc != OS32_ERR_AGAIN) return rc;
        if ((i32)(deadline - tick_count) <= 0) return OS32_ERR_AGAIN;
        LINK_IDLE();
    }
}

/* 本文を全部消費する。verify なら offset の下位 8bit と照合する。 */
static u32 link_wait_read_all(i32 h, u32 ticks, int verify)
{
    u32 deadline = tick_count + ticks;
    u32 off = 0;
    for (;;) {
        const u8 *src = 0;
        i32 n = link_host_read_stage(h, LINK_MAX_PAYLOAD, &src, LINK_SELF_OWNER);
        if (n == 0) return off;
        if (n < 0) {
            if (n != OS32_ERR_AGAIN) return off;
            if ((i32)(deadline - tick_count) <= 0) return off;
            LINK_IDLE();
            continue;
        }
        if (verify) {
            u32 k;
            for (k = 0; k < (u32)n; k++)
                if (src[k] != (u8)(off + k)) link_l2_bad++;
        }
        off += (u32)n;
    }
}

static void link_counters_reset(void)
{
    /* 往復計数は自己試験の区間ごとに打ち直す (F2: L0〜L3 で累積させない)。*/
    link_rt_ok = link_rt_fail = 0;
    link_l1_recv = link_l1_bytes = link_l1_ooo = link_l1_windows = 0;
    link_l1_max_credit = link_l1_min_credit = link_l1_done = link_l1_meas_pages = 0;
    link_l2_bytes = link_l2_read = link_l2_gaps = link_l2_bad = 0;
    link_l2_eof = link_l2_overflow = 0;
}

void link_selftest(int rounds)
{
    char req[32];
    unsigned int n;
    int i;

    if (!link_ready) return;
    if (!link_wait_up(LINK_PROBE_TICKS * 5)) {
        kprintf(0xC1, "[link] HELLO failed (no Host Agent?) — L0 selftest skipped\n");
        return;
    }
    kprintf(0x07, "[link] session %d epoch %d agent %d, peer "
                  "%02x:%02x:%02x:%02x:%02x:%02x\n",
            (int)link_sess, (int)link_epoch, (int)link_agent_gen,
            link_peer_mac[0], link_peer_mac[1], link_peer_mac[2],
            link_peer_mac[3], link_peer_mac[4], link_peer_mac[5]);

    link_counters_reset();              /* L0 区間を隔離 (F2) */
    n = cstr("PING", req, sizeof(req));
    for (i = 0; i < rounds; i++) {
        u32 st = 0, ln = 0;
        i32 h = link_wait_open(req, n, LINK_PROBE_TICKS * 2);
        if (h < 0) { link_rt_fail++; continue; }
        (void)link_wait_status(h, &st, &ln, LINK_PROBE_TICKS * 5);
        link_host_close(h, LINK_SELF_OWNER);
    }
    /* L0 の結果を専用スナップショットへ (後続の L1〜L3 が link_rt_ok を打ち直すので、
     * net_l0_test.py の最終読み出しは link_l0_ok / link_l0_fail を見る)。*/
    link_l0_ok = link_rt_ok;
    link_l0_fail = link_rt_fail;
    kprintf(link_rt_fail ? 0xC1 : 0x07,
            "[link] L0 selftest: %d/%d round trips ok, %d retransmit\n",
            (int)link_rt_ok, rounds, (int)link_retransmits);
}

void link_l1_bulk(unsigned int count, unsigned int payload)
{
    char req[48];
    unsigned int i;
    u32 st = 0, ln = 0, got;
    u32 rxf0, pages0;
    struct ne2k_stats sn;
    i32 h;

    if (!link_ready || link_state != LINK_S_UP) {
        kprintf(0xC1, "[link] L1 skipped (no peer)\n");
        return;
    }
    link_counters_reset();
    ne2k_get_stats(&sn);
    rxf0 = sn.rx_frames;
    pages0 = sn.rx_pages_total;

    i = 0;
    i += cstr("BULK ", req + i, (unsigned int)(sizeof(req) - i));
    i += (unsigned int)kutoa_dec(count, req + i, (int)(sizeof(req) - i));
    req[i++] = ' ';
    i += (unsigned int)kutoa_dec(payload, req + i, (int)(sizeof(req) - i));

    h = link_wait_open(req, i, LINK_PROBE_TICKS * 2);
    if (h < 0) { kprintf(0xC1, "[link] L1 open failed %d\n", (int)h); return; }
    (void)link_wait_status(h, &st, &ln, LINK_PROBE_TICKS * 10);
    got = link_wait_read_all(h, LINK_PROBE_TICKS * 15, 0);
    link_host_close(h, LINK_SELF_OWNER);

    ne2k_get_stats(&sn);
    {
        u32 df = sn.rx_frames - rxf0;
        u32 dp = sn.rx_pages_total - pages0;
        if (df) link_l1_meas_pages = (dp * 100) / df;
    }
    kprintf((link_l1_recv == count && link_l1_ooo == 0) ? 0x07 : 0xC1,
            "[link] L1 bulk: %d/%d frames, %dB, ooo %d, windows %d, "
            "credit %d..%d pages, meas %d.%02d pages/frame\n",
            (int)link_l1_recv, (int)count, (int)got, (int)link_l1_ooo,
            (int)link_l1_windows, (int)link_l1_min_credit, (int)link_l1_max_credit,
            (int)(link_l1_meas_pages / 100), (int)(link_l1_meas_pages % 100));
}

void link_l2_stream(unsigned int total, unsigned int plen, unsigned int dropseq)
{
    char req[64];
    unsigned int i;
    u32 st = 0, ln = 0;
    i32 h;

    if (!link_ready || link_state != LINK_S_UP) {
        kprintf(0xC1, "[link] L2 skipped (no peer)\n");
        return;
    }
    link_counters_reset();
    i = 0;
    i += cstr("STREAM ", req + i, (unsigned int)(sizeof(req) - i));
    i += (unsigned int)kutoa_dec(total, req + i, (int)(sizeof(req) - i));
    req[i++] = ' ';
    i += (unsigned int)kutoa_dec(plen, req + i, (int)(sizeof(req) - i));
    req[i++] = ' ';
    i += (unsigned int)kutoa_dec(dropseq, req + i, (int)(sizeof(req) - i));

    h = link_wait_open(req, i, LINK_PROBE_TICKS * 2);
    if (h < 0) { kprintf(0xC1, "[link] L2 open failed %d\n", (int)h); return; }
    (void)link_wait_status(h, &st, &ln, LINK_PROBE_TICKS * 10);
    (void)link_wait_read_all(h, LINK_PROBE_TICKS * 30, 1);
    link_host_close(h, LINK_SELF_OWNER);

    kprintf((link_l2_read == total && link_l2_bad == 0) ? 0x07 : 0xC1,
            "[link] L2 stream: read %d/%d bytes, gaps %d, bad %d, overflow %d%s\n",
            (int)link_l2_read, (int)total, (int)link_l2_gaps, (int)link_l2_bad,
            (int)link_l2_overflow, link_l2_eof ? " (EOF)" : " (no EOF)");
}

/* 1 本の要求を最後まで通す小道具 (L3 自己試験用)。 */
static void link_l3_one(const char *text, u32 *status, u32 *len, u32 *read,
                        int verify)
{
    char req[64];
    unsigned int n = cstr(text, req, sizeof(req));
    u32 st = 0, ln = 0;
    i32 h;

    *status = *len = *read = 0;
    h = link_wait_open(req, n, LINK_PROBE_TICKS * 2);
    if (h < 0) return;
    if (link_wait_status(h, &st, &ln, LINK_PROBE_TICKS * 15) == 0) {
        *status = st;
        *len = ln;
        if (ln) *read = link_wait_read_all(h, LINK_PROBE_TICKS * 30, verify);
    }
    link_host_close(h, LINK_SELF_OWNER);
}

void link_l3_service(void)
{
    u32 st, ln, rd;

    if (!link_ready || link_state != LINK_S_UP) {
        kprintf(0xC1, "[link] L3 skipped (no peer)\n");
        return;
    }
    link_counters_reset();
    link_l3_one("GET /pattern/65536", &st, &ln, &rd, 1);
    link_l3_get_status = st;
    link_l3_get_len = ln;
    link_l3_get_read = rd;
    link_l3_get_bad = link_l2_bad;

    link_l3_one("GET /notfound", &st, &ln, &rd, 0);
    link_l3_404 = st;

    link_l3_one("GET http://example.com/", &st, &ln, &rd, 0);
    link_l3_http_status = st;
    link_l3_http_read = rd;

    link_l3_one("TIME", &st, &ln, &rd, 0);
    link_l3_time_len = ln;

    kprintf((link_l3_get_status == 200 && link_l3_get_read == 65536 &&
             link_l3_get_bad == 0) ? 0x07 : 0xC1,
            "[link] L3: GET /pattern -> %d %d/%dB bad %d; /notfound -> %d; "
            "http -> %d %dB; TIME -> %dB\n",
            (int)link_l3_get_status, (int)link_l3_get_read, (int)link_l3_get_len,
            (int)link_l3_get_bad, (int)link_l3_404, (int)link_l3_http_status,
            (int)link_l3_http_read, (int)link_l3_time_len);
}
