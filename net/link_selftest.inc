/* リンク自己試験の待機処理と L0〜L3 の実行。link.c から取り込む。 */

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
        kprintf(ATTR_YELLOW, "[link] HELLO failed (no Host Agent?) — L0 selftest skipped\n");
        return;
    }
    kprintf(KLOG_ATTR, "[link] session %d epoch %d agent %d, peer "
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
        /* STALE は再同期 / TOMBSTONE 側で計上済み。待機期限切れだけを足す。 */
        if (link_wait_status(h, &st, &ln, LINK_PROBE_TICKS * 5) == OS32_ERR_AGAIN)
            link_rt_fail++;
        link_host_close(h, LINK_SELF_OWNER);
    }
    /* L0 の結果を専用スナップショットへ (後続の L1〜L3 が link_rt_ok を打ち直すので、
     * net_l0_test.py の最終読み出しは link_l0_ok / link_l0_fail を見る)。*/
    link_l0_ok = link_rt_ok;
    link_l0_fail = link_rt_fail;
    kprintf(link_rt_fail ? ATTR_YELLOW : KLOG_ATTR,
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
        kprintf(ATTR_YELLOW, "[link] L1 skipped (no peer)\n");
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
    if (h < 0) { kprintf(ATTR_YELLOW, "[link] L1 open failed %d\n", (int)h); return; }
    (void)link_wait_status(h, &st, &ln, LINK_PROBE_TICKS * 10);
    got = link_wait_read_all(h, LINK_PROBE_TICKS * 15, 0);
    link_host_close(h, LINK_SELF_OWNER);

    ne2k_get_stats(&sn);
    {
        u32 df = sn.rx_frames - rxf0;
        u32 dp = sn.rx_pages_total - pages0;
        if (df) link_l1_meas_pages = (dp * 100) / df;
    }
    kprintf((link_l1_recv == count && link_l1_ooo == 0) ? KLOG_ATTR : ATTR_YELLOW,
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
        kprintf(ATTR_YELLOW, "[link] L2 skipped (no peer)\n");
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
    if (h < 0) { kprintf(ATTR_YELLOW, "[link] L2 open failed %d\n", (int)h); return; }
    (void)link_wait_status(h, &st, &ln, LINK_PROBE_TICKS * 10);
    (void)link_wait_read_all(h, LINK_PROBE_TICKS * 30, 1);
    link_host_close(h, LINK_SELF_OWNER);

    kprintf((link_l2_read == total && link_l2_bad == 0) ? KLOG_ATTR : ATTR_YELLOW,
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
        kprintf(ATTR_YELLOW, "[link] L3 skipped (no peer)\n");
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
             link_l3_get_bad == 0) ? KLOG_ATTR : ATTR_YELLOW,
            "[link] L3: GET /pattern -> %d %d/%dB bad %d; /notfound -> %d; "
            "http -> %d %dB; TIME -> %dB\n",
            (int)link_l3_get_status, (int)link_l3_get_read, (int)link_l3_get_len,
            (int)link_l3_get_bad, (int)link_l3_404, (int)link_l3_http_status,
            (int)link_l3_http_read, (int)link_l3_time_len);
}
