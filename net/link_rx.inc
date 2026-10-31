/* 受信フレームの検査・応答処理と RX ポンプ。link.c から取り込む。 */

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
        if (le_read16(pl + LINK_HELLO_REQ_SESS_OFF) != link_syn_sess) return;      /* req_sess の写し */
        if (le_read16(pl + LINK_HELLO_REQ_EPOCH_OFF) != link_syn_epoch) return;     /* req_epoch の写し */
        link_agent_gen = le_read16(pl);
        link_agent_nonce = ack;
        kmemcpy(link_peer_mac, src, ETH_ALEN);
        if (sess != link_sess) link_next_rid = 1;       /* 新セッション */
        link_sess = sess;
        link_epoch = epoch;                             /* CONFIRM 送出で切替 */
        link_state = LINK_S_CONFIRM;
        link_hs_tries = 0;
        link_hs_due = LINK_HS_CONFIRM;
        return;
    }
    if (flags == LINK_HS_ESTAB) {
        if (link_state != LINK_S_CONFIRM) return;
        if (sess != link_sess || epoch != link_epoch) return;
        if (ack != link_nonce) return;
        if (!mac_eq(src, link_peer_mac)) return;
        link_agent_gen = le_read16(pl);
        link_state = LINK_S_UP;
        link_hs_due = 0;
        link_hello_ok = 1;
        return;
    }
    /* SYN / CONFIRM は OS32 発。受けても何もしない。 */
}

static void link_on_response(struct link_handle *e, u8 flags, const u8 *pl)
{
    u16 st = le_read16(pl);
    u32 ln = le_read32(pl + LINK_RESPONSE_LENGTH_OFF);

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
    if ((((u16)f[ETH_TYPE_OFF] << 8) | f[ETH_TYPE_OFF + 1]) != LINK_ETHERTYPE) { link_rx_dropped++; return; }
    if (!mac_eq(f, link_my_mac) && !mac_eq(f, link_bcast)) { link_rx_dropped++; return; }

    h = f + LINK_ETH_HDR_LEN;
    op = h[LINK_OFF_OP];
    flags = h[LINK_OFF_FLAGS];
    epoch = le_read16(h + LINK_OFF_EPOCH);
    seq = le_read32(h + LINK_OFF_SEQ);
    ack = le_read32(h + LINK_OFF_ACK);
    plen = le_read16(h + LINK_OFF_LENGTH);
    rid = le_read32(h + LINK_OFF_RID);
    sess = le_read16(h + LINK_OFF_SESS);
    if ((unsigned int)(LINK_ETH_HDR_LEN + LINK_HDR_LEN + plen) > flen) {
        link_rx_dropped++; return;
    }
    if (!link_paylen_ok(op, flags, plen)) { link_rx_dropped++; return; }
    pl = h + LINK_HDR_LEN;
    link_rx_frames++;

    if (op == LINK_OP_HELLO) {
        link_on_hello(f + ETH_ALEN, flags, epoch, seq, ack, sess, pl);
        return;
    }
    /* HELLO 以外は sess / epoch が控えと一致し、相手が peer のものだけ。 */
    if (link_state != LINK_S_UP || sess != link_sess || epoch != link_epoch ||
        !mac_eq(f + ETH_ALEN, link_peer_mac)) {
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

