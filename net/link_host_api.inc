/* 要求行の解析と非同期ホスト API・所有者回収。link.c から取り込む。 */

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
    if (link_next_rid >= U32_MAX) {          /* rid 枯渇 = 新セッションへ */
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

