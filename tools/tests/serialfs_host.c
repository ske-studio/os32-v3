/* ======================================================================== */
/*  SERIALFS_HOST.C — SerialFS のゲスト側のホスト試験 (票 TASK_SERIAL_HOSTFS) */
/*                                                                          */
/*  実物の fs/sfs_proto.c・fs/sfs_client.c・fs/serialfs.c・lib/crc32.c と    */
/*  userland/shell/serial_watchdog.c・userland/system/hsync_bootold.inc を   */
/*  #include し、線と時計と相手 (ホスト) だけを偽物にする。                 */
/*                                                                          */
/*  偽の線: 仮想の tick。ゲストが idle するたびに 1 tick 進む。ホスト →      */
/*  ゲストのバイトは「届く tick」を持つので、遅延を注入できる。             */
/*  偽のホスト: 要求を C で解いて小さなメモリ上の FS で答える。障害の注入    */
/*  (応答の喪失・遅延・CRC 破損・番号違い・セッション違い・停止・ごみの      */
/*  連続・ERR・契約違反の応答) は案件ごとに旗で立てる。                      */
/*                                                                          */
/*  モード:                                                                 */
/*    serialfs-host <case>   … 1 案件 (終了コード 0 = 通過)                  */
/*    serialfs-host vectors  … C が組んだフレームを 16 進で出す (Python が読む)*/
/*    serialfs-host decode   … 標準入力の 16 進 1 行を C の受信器で読む       */
/*    serialfs-host pipe     … 標準入出力を線にして実物の Python ホストと話す */
/*                             (実時間。test_serialfs.py の結合試験)          */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
/* glibc の <sys/stat.h> は st_mtime などを st_mtim.tv_sec へのマクロにする。
 * OS32_Stat の欄名と衝突するので消す (ここでは POSIX の stat は使わない)。 */
#undef st_atime
#undef st_mtime
#undef st_ctime

#include "types.h"
#include "os32_kapi_shared.h"

#include "crc32.c"
#include "sfs_proto.c"
#include "sfs_client.c"
#include "serialfs.c"
#include "serial_watchdog.c"

/* hsync_bootold.inc は crc32_core.inc の関数を使う (crc32.c が取り込み済み) */
#include "hsync_bootold.inc"

/* serialfs_init が呼ぶ */
static VfsOps *g_registered;
void vfs_register_fs(VfsOps *ops) { g_registered = ops; }

static int failed;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

/* ======================================================================== */
/*  偽の線と時計                                                            */
/* ======================================================================== */
#define Q_MAX 262144
static u32 g_now;                      /* 仮想 tick */
static u8  g_g2h[Q_MAX];               /* ゲスト → ホスト */
static u32 g_g2h_n;                    /* 溜まった数 (ホストが読むと 0 に) */
static struct { u32 at; u8 b; } g_h2g[Q_MAX];
static u32 g_h2g_head, g_h2g_tail;
static int g_can_wait = 1;
static u32 g_put_frames;               /* ゲストが送ったフレームの数 */
static u8  g_last_req[SFS_MAX_FRAME];  /* 直前にゲストが送ったフレーム */
static u16 g_last_req_len;
static u8  g_prev_req[SFS_MAX_FRAME];  /* その 1 つ前 */
static u16 g_prev_req_len;
static u32 g_put_limit_ticks;          /* 暴走止め */

static void host_pump(void);

static void h2g_push(u32 at, const u8 *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        g_h2g[g_h2g_tail].at = at;
        g_h2g[g_h2g_tail].b = b[i];
        g_h2g_tail = (g_h2g_tail + 1) % Q_MAX;
    }
}

static int f_put(void *ctx, const u8 *buf, u32 n)
{
    (void)ctx;
    if (g_g2h_n + n > Q_MAX) return -1;
    memcpy(g_g2h + g_g2h_n, buf, n);
    g_g2h_n += n;
    if (n <= SFS_MAX_FRAME && buf[0] == SFS_ENQ) {
        memcpy(g_prev_req, g_last_req, g_last_req_len);
        g_prev_req_len = g_last_req_len;
        memcpy(g_last_req, buf, n);
        g_last_req_len = (u16)n;
        g_put_frames++;
    }
    host_pump();
    return 0;
}

/* 受け取ったバイトで時計も進める (115200 で 1 tick に約 115 バイト)。
 * ごみが途切れずに来ると idle が呼ばれないので、これが無いと時が止まる。 */
static u32 g_rx_bytes;
static int f_get(void *ctx)
{
    (void)ctx;
    host_pump();
    if (g_h2g_head == g_h2g_tail) return -1;
    /* 届く tick に達していないものは見えない (FIFO なので先頭だけ見る) */
    if (g_h2g[g_h2g_head].at > g_now) return -1;
    {
        u8 b = g_h2g[g_h2g_head].b;
        g_h2g_head = (g_h2g_head + 1) % Q_MAX;
        if (++g_rx_bytes % 100 == 0) g_now++;
        return b;
    }
}

static u32 f_now(void *ctx) { (void)ctx; return g_now; }

static void f_idle(void *ctx)
{
    (void)ctx;
    g_now++;
    if (g_put_limit_ticks && g_now > g_put_limit_ticks) {
        fprintf(stderr, "RUNAWAY: virtual clock passed %lu ticks\n",
                (unsigned long)g_put_limit_ticks);
        exit(3);
    }
}

static int f_can_wait(void *ctx) { (void)ctx; return g_can_wait; }

static const SfsIo g_fio = { f_put, f_get, f_now, f_idle, f_can_wait };

/* ======================================================================== */
/*  偽のホスト                                                              */
/* ======================================================================== */
#define HOST_SID 0x1234ABCDUL

/* 障害の旗 (案件ごと) */
static int  inj_drop;          /* 次の n 個の応答を送らない */
static int  inj_corrupt;       /* 次の n 個の応答の CRC を壊す */
static u32  inj_delay;         /* 次の 1 個の応答をこの tick だけ遅らせる */
static int  inj_stale_first;   /* 次の応答の前に、番号を 1 つ戻した偽の応答 */
static int  inj_wrong_sid;     /* 次の応答の前に、セッション違いの応答 */
static int  inj_dead;          /* 何も答えない */
static int  inj_err;           /* SFS_T_ERR (OS32_ERR_STALE) で答える */
static int  inj_garbage;       /* 答えずに、ごみ (0x55) を流し続ける */
static int  inj_read_extra;    /* READ で要求より 1 バイト多く返す */
static int  inj_list_bad;      /* LIST の名前に '/' を混ぜる */
static int  inj_list_stuck;    /* LIST の next を進めない */
static int  inj_hello_nonce;   /* HELLO の nonce を違えて返す */
static int  inj_weird_status;  /* 知らない status (-99) で答える */
static int  inj_stray_flood;   /* 答えずに、番号違いの応答を 50 tick ごとに流し続ける */
static u32  g_flood_last;

/* 実行した操作の数 (種別ごと) */
static u32 g_exec[256];

static SfsDec g_hdec;
static u32 g_sid;

/* メモリ上の FS: ファイル 3 つとディレクトリ 1 つ (子は 100 件) */
#define A_LEN 1300
static u8  g_a[A_LEN];
static u8  g_w[8192];
static u32 g_w_len;
static int g_w_exists;
#define DIR_N 100

static void resp_send(u8 type, u32 sid, u16 seq, const u8 *pl, u16 len)
{
    u8 f[SFS_MAX_FRAME];
    u16 n = sfs_encode(f, type, sid, seq, pl, len);
    u32 at = g_now + 1;

    if (inj_drop > 0) { inj_drop--; return; }
    if (inj_corrupt > 0) { inj_corrupt--; f[n - 1] ^= 0x5A; }
    if (inj_delay) { at += inj_delay; inj_delay = 0; }
    if (inj_stale_first) {
        u8 g[SFS_MAX_FRAME];
        u16 m = sfs_encode(g, type, sid, (u16)(seq - 1), pl, len);
        inj_stale_first = 0;
        h2g_push(at, g, m);
    }
    if (inj_wrong_sid) {
        u8 g[SFS_MAX_FRAME];
        u16 m = sfs_encode(g, type, sid ^ 1, seq, pl, len);
        inj_wrong_sid = 0;
        h2g_push(at, g, m);
    }
    h2g_push(at, f, n);
}

static void resp_status(u8 type, const SfsFrame *q, i32 st, const u8 *body,
                        u16 blen)
{
    u8 pl[SFS_MAX_PAYLOAD];
    sfs_put32(pl, (u32)st);
    if (blen) memcpy(pl + 4, body, blen);
    resp_send((u8)(type | SFS_T_RESP), q->sid, q->seq, pl, (u16)(4 + blen));
}

static int path_is(const u8 *p, u16 at, const char *want, u16 *next)
{
    u8 n = p[at];
    if (next) *next = (u16)(at + 1 + n);
    return strlen(want) == n && memcmp(p + at + 1, want, n) == 0;
}

static void dir_name(int i, char *out)
{
    /* 長めの名前で頁を分けさせる (100 件 x 約 40 バイト = 数頁) */
    sprintf(out, "entry-%03d-with-a-rather-long-file-name", i);
}

static void host_handle(const SfsFrame *q)
{
    u8 body[SFS_MAX_PAYLOAD];
    const u8 *p = q->payload;
    u16 at;

    g_exec[q->type]++;
    if (q->type == SFS_T_HELLO) {
        u8 pl[12];
        g_sid = HOST_SID;
        sfs_put32(pl, 0);
        sfs_put16(pl + 4, SFS_VERSION);
        sfs_put16(pl + 6, SFS_MAX_PAYLOAD);
        sfs_put32(pl + 8, sfs_get32(p + 4) ^ (u32)(inj_hello_nonce ? 1 : 0));
        resp_send(SFS_T_HELLO | SFS_T_RESP, g_sid, 0, pl, 12);
        return;
    }
    if (q->type == SFS_T_BYE || q->type == SFS_T_LOG || q->type == SFS_T_EXIT)
        return;
    if (inj_weird_status) {
        resp_status(q->type, q, -99, 0, 0);
        return;
    }
    if (inj_err) {
        u8 pl[4];
        sfs_put32(pl, (u32)OS32_ERR_STALE);
        resp_send(SFS_T_ERR, q->sid, q->seq, pl, 4);
        return;
    }
    switch (q->type) {
    case SFS_T_STAT:
        if (path_is(p, 0, "/a.txt", 0)) {
            body[0] = SFS_KIND_FILE; sfs_put32(body + 1, A_LEN);
            sfs_put32(body + 5, 1700000000UL);
            resp_status(q->type, q, 0, body, 9);
        } else if (path_is(p, 0, "/dir", 0) || path_is(p, 0, "/", 0)) {
            body[0] = SFS_KIND_DIR; sfs_put32(body + 1, 0);
            sfs_put32(body + 5, 1700000001UL);
            resp_status(q->type, q, 0, body, 9);
        } else if (path_is(p, 0, "/w.bin", 0) && g_w_exists) {
            body[0] = SFS_KIND_FILE; sfs_put32(body + 1, g_w_len);
            sfs_put32(body + 5, 1700000002UL);
            resp_status(q->type, q, 0, body, 9);
        } else {
            resp_status(q->type, q, OS32_ERR_NOTFOUND, 0, 0);
        }
        return;
    case SFS_T_LIST: {
        u32 cookie = sfs_get32(p);
        u16 n = 4;
        int i;
        if (path_is(p, 4, "/a.txt", 0)) {
            resp_status(q->type, q, OS32_ERR_NOTDIR, 0, 0);
            return;
        }
        if (!path_is(p, 4, "/dir", 0)) {
            resp_status(q->type, q, OS32_ERR_NOTFOUND, 0, 0);
            return;
        }
        for (i = (int)cookie; i < DIR_N; i++) {
            char nm[64];
            u16 nl;
            dir_name(i, nm);
            if (inj_list_bad && i == 3) nm[2] = '/';
            nl = (u16)strlen(nm);
            if (n + 6 + nl > SFS_MAX_PAYLOAD - 4) break;
            body[n] = (u8)((i % 10 == 0) ? SFS_KIND_DIR : SFS_KIND_FILE);
            sfs_put32(body + n + 1, (u32)i);
            body[n + 5] = (u8)nl;
            memcpy(body + n + 6, nm, nl);
            n = (u16)(n + 6 + nl);
        }
        sfs_put32(body, (i < DIR_N) ? ((inj_list_stuck && cookie > 0) ? cookie : (u32)i) : 0);
        resp_status(q->type, q, 0, body, n);
        return;
    }
    case SFS_T_READ: {
        u32 off = sfs_get32(p);
        u16 cnt = sfs_get16(p + 4);
        u32 n;
        if (!path_is(p, 6, "/a.txt", 0)) {
            resp_status(q->type, q, OS32_ERR_NOTFOUND, 0, 0);
            return;
        }
        n = (off >= A_LEN) ? 0 : (A_LEN - off);
        if (n > cnt) n = cnt;
        if (inj_read_extra && n > 0 && n < SFS_READ_MAX) n++;
        resp_status(q->type, q, (i32)n, g_a + off, (u16)n);
        return;
    }
    case SFS_T_WRITE: {
        u32 off = sfs_get32(p);
        u8 flags = p[4];
        u16 dlen;
        if (!path_is(p, 5, "/w.bin", &at)) {
            resp_status(q->type, q, OS32_ERR_NOTFOUND, 0, 0);
            return;
        }
        dlen = (u16)(q->len - at);
        if (flags & SFS_WF_TRUNC) {           /* 作り直す = 中身も消える */
            memset(g_w, 0, sizeof(g_w));
            g_w_len = 0;
        }
        memcpy(g_w + off, p + at, dlen);
        if (off + dlen > g_w_len) g_w_len = off + dlen;
        g_w_exists = 1;
        resp_status(q->type, q, (i32)dlen, 0, 0);
        return;
    }
    case SFS_T_MKDIR: case SFS_T_RMDIR: case SFS_T_UNLINK:
        resp_status(q->type, q, path_is(p, 0, "/nope", 0) ? OS32_ERR_NOTFOUND : 0,
                    0, 0);
        return;
    case SFS_T_RENAME:
        resp_status(q->type, q, 0, 0, 0);
        return;
    default:
        resp_status(q->type, q, OS32_ERR_INVAL, 0, 0);
        return;
    }
}

static void host_pump(void)
{
    u32 i;
    SfsFrame q;

    if (inj_garbage) {
        /* 途切れずにごみを流す (ENQ を含まない) */
        u8 g = 0x55;
        while ((g_h2g_tail + 1) % Q_MAX != g_h2g_head &&
               (g_h2g_tail - g_h2g_head + Q_MAX) % Q_MAX < 64)
            h2g_push(g_now, &g, 1);
    }
    if (inj_stray_flood && g_now - g_flood_last >= 50) {
        /* 番号違い (0) の応答。受け取られないが、期限を延ばしてはいけない */
        u8 f[SFS_MAX_FRAME], pl[4] = { 0, 0, 0, 0 };
        u16 m = sfs_encode(f, SFS_T_STAT | SFS_T_RESP, HOST_SID, 0, pl, 4);
        g_flood_last = g_now;
        h2g_push(g_now, f, m);
    }
    for (i = 0; i < g_g2h_n; i++) {
        if (sfs_dec_feed(&g_hdec, g_g2h[i], &q) == 1 && !inj_dead &&
            !inj_garbage && !inj_stray_flood)
            host_handle(&q);
    }
    g_g2h_n = 0;
}

static void reset_all(void)
{
    u32 i;
    g_now = 0;
    g_g2h_n = 0;
    g_h2g_head = g_h2g_tail = 0;
    g_can_wait = 1;
    g_put_frames = 0;
    g_last_req_len = g_prev_req_len = 0;
    g_put_limit_ticks = 200000;
    g_rx_bytes = 0;
    inj_drop = inj_corrupt = inj_stale_first = inj_wrong_sid = 0;
    inj_dead = inj_err = inj_garbage = inj_read_extra = 0;
    inj_list_bad = inj_list_stuck = inj_hello_nonce = 0;
    inj_weird_status = inj_stray_flood = 0;
    g_flood_last = 0;
    inj_delay = 0;
    memset(g_exec, 0, sizeof(g_exec));
    sfs_dec_reset(&g_hdec);
    g_sid = 0;
    for (i = 0; i < A_LEN; i++) g_a[i] = (u8)(i * 7 + 3);
    g_w_len = 0;
    g_w_exists = 0;
    serialfs_forbid();
    g_mounted = 0;
}

static SfsClient g_c;

/* HELLO してマウントまで済ませた受け手を返す */
static void *setup_session(u32 baud)
{
    void *ctx;
    reset_all();
    sfs_client_init(&g_c, &g_fio, 0, baud);
    CHECK(sfs_client_hello(&g_c, 0xC0FFEEUL) == 0);
    CHECK(g_c.sid == HOST_SID);
    serialfs_permit(&g_c);
    ctx = serialfs_get_ops()->mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_SERIAL, 1));
    serialfs_forbid();
    CHECK(ctx == &g_c);
    return ctx;
}

/* ======================================================================== */
/*  案件: フレーム                                                          */
/* ======================================================================== */
static void crc_and_frames(void)
{
    u8 f[SFS_MAX_FRAME + 8], pl[SFS_MAX_PAYLOAD + 1];
    SfsDec d;
    SfsFrame o;
    u16 n, i;
    int r = 0, got = 0;

    CHECK(sfs_crc32((const u8 *)"123456789", 9) == 0xCBF43926UL);
    for (i = 0; i < sizeof(pl); i++) pl[i] = (u8)i;
    /* 本文に ENQ / EOT / ESC を含めても往復する */
    pl[0] = SFS_ENQ; pl[1] = 0x04; pl[2] = 0x1B; pl[3] = 'S'; pl[4] = 'F';

    /* 上限ちょうどは組める、1 つ越えは組まない */
    CHECK(sfs_encode(f, SFS_T_READ, 7, 9, pl, SFS_MAX_PAYLOAD) == SFS_MAX_FRAME);
    CHECK(sfs_encode(f, SFS_T_READ, 7, 9, pl, SFS_MAX_PAYLOAD + 1) == 0);
    CHECK(SFS_MAX_FRAME == 528);

    n = sfs_encode(f, SFS_T_READ | SFS_T_RESP, 0xA1B2C3D4UL, 0xBEEF, pl, 300);
    CHECK(n == 12 + 300 + 4);
    CHECK(f[0] == 0x05 && f[1] == 'S' && f[2] == 'F');
    CHECK(sfs_get32(f + n - 4) == sfs_crc32(f + 3, 9 + 300));
    sfs_dec_reset(&d);
    /* 前にごみ・偽の ENQ・ENQ ENQ を置いても揃う */
    sfs_dec_feed(&d, 0x41, &o);
    sfs_dec_feed(&d, SFS_ENQ, &o);
    sfs_dec_feed(&d, 'X', &o);
    sfs_dec_feed(&d, SFS_ENQ, &o);
    for (i = 0; i < n; i++) {
        r = sfs_dec_feed(&d, f[i], &o);
        if (r == 1) got++;
    }
    CHECK(got == 1 && r == 1);
    CHECK(o.type == (SFS_T_READ | SFS_T_RESP) && o.sid == 0xA1B2C3D4UL &&
          o.seq == 0xBEEF && o.len == 300 && memcmp(o.payload, pl, 300) == 0);

    /* CRC を壊すと捨てる (-1) → 次の正しいフレームは読める */
    f[n - 1] ^= 1;
    got = 0;
    for (i = 0; i < n; i++) if (sfs_dec_feed(&d, f[i], &o) < 0) got--;
    CHECK(got == -1 && d.bad_crc == 1);
    f[n - 1] ^= 1;
    got = 0;
    for (i = 0; i < n; i++) if (sfs_dec_feed(&d, f[i], &o) == 1) got++;
    CHECK(got == 1);

    /* 長さ欄 513 はヘッダで捨てる (本体を待たない) → 次は読める */
    n = sfs_encode(f, SFS_T_STAT, 1, 2, pl, 10);
    {
        u8 h[12];
        memcpy(h, f, 12);
        sfs_put16(h + 10, SFS_MAX_PAYLOAD + 1);
        got = 0;
        for (i = 0; i < 12; i++) if (sfs_dec_feed(&d, h[i], &o) < 0) got--;
        CHECK(got == -1 && d.bad_len == 1);
        CHECK(!sfs_dec_busy(&d));
    }
    got = 0;
    for (i = 0; i < n; i++) if (sfs_dec_feed(&d, f[i], &o) == 1) got++;
    CHECK(got == 1 && o.len == 10);

    /* 長さ 0 のフレーム */
    n = sfs_encode(f, SFS_T_BYE, 5, 0, 0, 0);
    CHECK(n == 16);
    got = 0;
    for (i = 0; i < n; i++) if (sfs_dec_feed(&d, f[i], &o) == 1) got++;
    CHECK(got == 1 && o.len == 0 && o.type == SFS_T_BYE);

    /* path の上限 255 */
    {
        char p[300];
        memset(p, 'a', sizeof(p));
        p[255] = 0;
        CHECK(sfs_put_path(pl, 512, p) == 256 && pl[0] == 255);
        p[255] = 'a'; p[256] = 0;
        CHECK(sfs_put_path(pl, 512, p) == -1);
        p[10] = 0;
        CHECK(sfs_put_path(pl, 10, p) == -1);       /* 11 バイト要る */
        CHECK(sfs_put_path(pl, 11, p) == 11);
    }
}

static void timing_and_seq(void)
{
    u16 s = 0;
    CHECK(sfs_byte_us(9600) == 1042);
    CHECK(sfs_byte_us(115200) == 87);
    CHECK(sfs_byte_us(0) == 1042);
    CHECK(sfs_ms_ticks(100) == 11);
    CHECK(sfs_ms_ticks(0) == 1);
    /* 2000 + ceil(528*87*2/1000)=92 + 500 = 2592ms → 260 + 1 */
    CHECK(sfs_trial_ticks(115200) == 261);
    /* 2000 + ceil(528*1042*2/1000)=1101 + 500 = 3601ms → 361 + 1 */
    CHECK(sfs_trial_ticks(9600) == 362);
    /* 遅い速度ほど長い */
    CHECK(sfs_trial_ticks(1200) > sfs_trial_ticks(9600));
    CHECK(sfs_seq_next(&s) == 0 && s == 1);
    s = 0xFFFD;
    CHECK(sfs_seq_next(&s) == 0 && s == 0xFFFE);
    CHECK(sfs_seq_next(&s) == -1 && s == 0xFFFE);   /* 周回させない */
}

/* ======================================================================== */
/*  案件: セッションと VFS の契約                                           */
/* ======================================================================== */
static int g_list_n;
static int g_list_dirs;
static char g_list_first[64], g_list_last[64];
static int g_list_nested;           /* コールバックの中で FS に触る */
static void list_cb(const VfsDirEntry *e, void *u)
{
    (void)u;
    if (g_list_n == 0) strcpy(g_list_first, e->name);
    strcpy(g_list_last, e->name);
    if (e->type == VFS_TYPE_DIR) g_list_dirs++;
    g_list_n++;
    if (g_list_nested) {
        /* §4-26: コールバックの中の要求で受信器が上書きされても頁は崩れない */
        OS32_Stat st;
        (void)serialfs_get_ops()->stat(&g_c, "/a.txt", &st);
    }
}

static void vfs_contract(void)
{
    VfsOps *ops = serialfs_get_ops();
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u8 buf[4096];
    u32 sz = 0;
    int rc, i;

    CHECK(ops->is_mounted(ctx) == 1);
    /* stat: ファイル・ディレクトリ・無い */
    CHECK(ops->stat(ctx, "/a.txt", &st) == 0);
    CHECK((st.st_mode & OS_S_IFMT) == OS_S_IFREG && st.st_size == A_LEN &&
          st.st_mtime == 1700000000UL && st.st_nlink == 1);
    CHECK(ops->stat(ctx, "/dir", &st) == 0 && (st.st_mode & OS_S_IFMT) == OS_S_IFDIR);
    CHECK(ops->stat(ctx, "/nope", &st) == OS32_ERR_NOTFOUND);
    /* get_file_size: ディレクトリは ISDIR */
    CHECK(ops->get_file_size(ctx, "/a.txt", &sz) == 0 && sz == A_LEN);
    CHECK(ops->get_file_size(ctx, "/dir", &sz) == OS32_ERR_ISDIR);
    /* read_stream: 分割して全部、要求ちょうど、EOF で短い */
    memset(buf, 0, sizeof(buf));
    CHECK(ops->read_stream(ctx, "/a.txt", buf, A_LEN, 0) == A_LEN);
    CHECK(memcmp(buf, g_a, A_LEN) == 0);
    CHECK(ops->read_stream(ctx, "/a.txt", buf, 100, 1250) == 50);
    CHECK(memcmp(buf, g_a + 1250, 50) == 0);
    CHECK(ops->read_stream(ctx, "/a.txt", buf, 10, A_LEN) == 0);   /* EOF */
    CHECK(ops->read_stream(ctx, "/a.txt", buf, SFS_READ_MAX, 0) == SFS_READ_MAX);
    CHECK(ops->read_file(ctx, "/a.txt", buf, sizeof(buf)) == A_LEN);
    CHECK(ops->read_stream(ctx, "/nope", buf, 10, 0) == OS32_ERR_NOTFOUND);
    /* list_dir: 頁をまたいで 100 件、ファイルには NOTDIR */
    g_list_n = 0; g_list_dirs = 0;
    CHECK(ops->list_dir(ctx, "/dir", list_cb, 0) == 0);
    CHECK(g_list_n == DIR_N && g_list_dirs == 10);
    CHECK(strcmp(g_list_first, "entry-000-with-a-rather-long-file-name") == 0);
    CHECK(strcmp(g_list_last, "entry-099-with-a-rather-long-file-name") == 0);
    CHECK(g_exec[SFS_T_LIST] >= 5);                  /* 頁は複数 */
    g_list_n = 0; g_list_nested = 1;
    CHECK(ops->list_dir(ctx, "/dir", list_cb, 0) == 0);
    g_list_nested = 0;
    CHECK(g_list_n == DIR_N &&
          strcmp(g_list_last, "entry-099-with-a-rather-long-file-name") == 0);
    CHECK(ops->list_dir(ctx, "/a.txt", list_cb, 0) == OS32_ERR_NOTDIR);
    /* write_file: 複数の要求に分けて、1 回目だけ TRUNC */
    for (i = 0; i < 1500; i++) buf[i] = (u8)(i ^ 0xA5);
    g_w_len = 9999 % sizeof(g_w);
    CHECK(ops->write_file(ctx, "/w.bin", buf, 1500) == 1500);
    CHECK(g_w_len == 1500 && memcmp(g_w, buf, 1500) == 0);
    CHECK(g_exec[SFS_T_WRITE] >= 3);
    /* write_stream: 途中から */
    CHECK(ops->write_stream(ctx, "/w.bin", buf, 10, 1500) == 10);
    CHECK(g_w_len == 1510);
    /* write_file 0 バイトは作り直す (空)、write_stream 0 バイトは何もしない */
    rc = (int)g_exec[SFS_T_WRITE];
    CHECK(ops->write_stream(ctx, "/w.bin", buf, 0, 0) == 0);
    CHECK((int)g_exec[SFS_T_WRITE] == rc);
    CHECK(ops->write_file(ctx, "/w.bin", buf, 0) == 0);
    CHECK(g_w_len == 0 && (int)g_exec[SFS_T_WRITE] == rc + 1);
    /* 変更系 */
    CHECK(ops->mkdir(ctx, "/d2") == 0);
    CHECK(ops->unlink(ctx, "/nope") == OS32_ERR_NOTFOUND);
    CHECK(ops->rename(ctx, "/w.bin", "/w2.bin") == 0);
    CHECK(ops->set_mtime == 0 && ops->create_excl == 0 && ops->ino == 0);
    /* 長すぎる path は線に出さない */
    {
        char p[300];
        u32 before = g_put_frames;
        memset(p, 'x', sizeof(p)); p[0] = '/'; p[299] = 0;
        CHECK(ops->stat(ctx, p, &st) == OS32_ERR_NAMETOOLONG);
        CHECK(g_put_frames == before);
    }
    ops->umount(ctx);
    CHECK(ops->is_mounted(ctx) == 0);
}

static void mount_permit(void)
{
    VfsOps *ops = serialfs_get_ops();
    reset_all();
    sfs_client_init(&g_c, &g_fio, 0, 9600);
    /* セッションの外 (口が閉じている) からは付かない */
    CHECK(ops->mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_SERIAL, 1)) == 0);
    serialfs_permit(&g_c);
    /* 種別違い (hd1) も付かない */
    CHECK(ops->mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 1)) == 0);
    CHECK(ops->mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_SERIAL, 1)) == &g_c);
    /* 二重には付かない */
    CHECK(ops->mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_SERIAL, 1)) == 0);
    serialfs_forbid();
    ops->umount(&g_c);
    CHECK(serialfs_is_mounted() == 0);
    serialfs_init();
    CHECK(g_registered == ops && strcmp(ops->name, "serialfs") == 0);
}

/* ======================================================================== */
/*  案件: 障害の注入                                                        */
/* ======================================================================== */
static void fault_drop(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u32 t0;

    inj_drop = 1;
    t0 = g_now;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == 0 && st.st_size == A_LEN);
    CHECK(g_c.resends == 1 && g_c.timeouts == 1);
    /* **再送は同じ番号・同じバイト列** (ホストのキャッシュが効く条件) */
    CHECK(g_last_req_len == g_prev_req_len &&
          memcmp(g_last_req, g_prev_req, g_last_req_len) == 0);
    /* 期限 (1 試行) を待ってから再送している */
    CHECK(g_now - t0 >= sfs_trial_ticks(115200));
    /* 3 回まで落ちても通る (試行は 1 + 3) */
    inj_drop = 3;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == 0);
    CHECK(g_c.fails == 0 && g_c.dead == SFS_DEAD_NONE);
    /* 4 回落ちると IO (1 回の失敗。まだ死んではいない) */
    inj_drop = 4;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_c.fails == 1 && g_c.dead == SFS_DEAD_NONE);
    /* 次が通れば連続失敗は数え直し */
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == 0 && g_c.fails == 0);
}

static void fault_corrupt(void)
{
    void *ctx = setup_session(115200);
    u8 buf[64];

    inj_corrupt = 1;
    CHECK(serialfs_get_ops()->read_stream(ctx, "/a.txt", buf, 64, 0) == 64);
    CHECK(memcmp(buf, g_a, 64) == 0);
    CHECK(g_c.dec.bad_crc == 1 && g_c.resends == 1);
}

static void fault_stray(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;

    /* 番号を 1 つ戻した偽の応答が先に来る → 数えずに捨て、本物を取る */
    inj_stale_first = 1;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == 0 && st.st_size == A_LEN);
    CHECK(g_c.stray == 1 && g_c.resends == 0);
    /* セッション違いの応答も同じ */
    inj_wrong_sid = 1;
    CHECK(serialfs_get_ops()->stat(ctx, "/dir", &st) == 0 &&
          (st.st_mode & OS_S_IFMT) == OS_S_IFDIR);
    CHECK(g_c.stray == 2 && g_c.resends == 0);
}

static void fault_delay(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u8 buf[16];
    u32 trial = sfs_trial_ticks(115200);

    /* A の 1 回目の応答が期限を大きく過ぎて届く (番号のずれ)。A は再送で
     * 通る。遅れた A の応答が B の最中に届いても、B は取り違えない。 */
    inj_delay = trial * 3;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == 0);
    CHECK(g_c.resends >= 1);
    CHECK(serialfs_get_ops()->read_stream(ctx, "/a.txt", buf, 16, 100) == 16);
    CHECK(memcmp(buf, g_a + 100, 16) == 0);
    /* B の最中に届くよう時計を進めてから C */
    while (g_now < trial * 4) g_now++;
    CHECK(serialfs_get_ops()->stat(ctx, "/dir", &st) == 0 &&
          (st.st_mode & OS_S_IFMT) == OS_S_IFDIR);
    CHECK(g_c.stray >= 1);       /* 遅れた A の応答は数えずに捨てた */
}

static void fault_dead(void)
{
    void *ctx = setup_session(9600);
    OS32_Stat st;
    u32 frames, t0;
    int i;

    inj_dead = 1;
    for (i = 0; i < SFS_DEAD_AFTER; i++)
        CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_c.dead == SFS_DEAD_FAILS);
    /* 死んだ後は線に何も出さず、待たずに IO */
    frames = g_put_frames;
    t0 = g_now;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    {
        u8 b4[4];
        CHECK(serialfs_get_ops()->read_stream(ctx, "/a.txt", b4, 4, 0) == OS32_ERR_IO);
    }
    CHECK(g_put_frames == frames && g_now == t0);
    /* 1 要求の失敗は (1 + 3) 試行: 期限の合計が上限を越えない */
    CHECK(g_c.timeouts == (u32)SFS_DEAD_AFTER * (1 + SFS_RETRIES));
}

static void fault_err_stale(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u32 frames;

    inj_err = 1;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_c.dead == SFS_DEAD_STALE && g_c.resends == 0);
    frames = g_put_frames;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_put_frames == frames);
}

static void fault_garbage(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u32 t0 = g_now;
    u32 trial = sfs_trial_ticks(115200);
    u32 resync = sfs_ms_ticks(SFS_RESYNC_MAX_MS);

    /* ごみが途切れずに流れても、期限は毎周見るので抜ける */
    inj_garbage = 1;
    g_put_limit_ticks = 0;
    /* 時計は受け取ったバイトでも進む (f_get) — 実機で時が進むのと同じ */
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_now - t0 <= (1 + SFS_RETRIES) * (trial + resync + 2));
    CHECK(g_c.timeouts == 1 + SFS_RETRIES);
    /* 静まるのを待つ口は上限で 0 を返す */
    CHECK(sfs_client_quiesce(&g_c, 200, 1000) == 0);
    inj_garbage = 0;
    g_h2g_head = g_h2g_tail;
    CHECK(sfs_client_quiesce(&g_c, 200, 1000) == 1);
}

static void cannot_wait(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u32 frames = g_put_frames, t0 = g_now;

    g_can_wait = 0;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_put_frames == frames && g_now == t0);
    CHECK(sfs_client_hello(&g_c, 1) == OS32_ERR_IO);
    CHECK(sfs_client_quiesce(&g_c, 200, 1000) == 0);
}

static void seq_exhausted(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u32 frames;

    g_c.seq = (u16)(SFS_SEQ_LAST - 1);
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == 0);   /* 最後の番号 */
    frames = g_put_frames;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_c.dead == SFS_DEAD_SEQ && g_put_frames == frames);
    CHECK(g_c.seq == SFS_SEQ_LAST);
}

static void hello_cases(void)
{
    reset_all();
    sfs_client_init(&g_c, &g_fio, 0, 115200);
    inj_dead = 1;
    CHECK(sfs_client_hello(&g_c, 7) == OS32_ERR_IO);
    CHECK(g_c.sid == 0 && g_put_frames == 1 + SFS_RETRIES);
    /* nonce の違う応答 (前のセッションの遅れた HELLO) は取らない */
    reset_all();
    sfs_client_init(&g_c, &g_fio, 0, 115200);
    inj_hello_nonce = 1;
    CHECK(sfs_client_hello(&g_c, 7) == OS32_ERR_IO);
    CHECK(g_c.sid == 0 && g_c.stray == 1 + SFS_RETRIES);
    /* HELLO の前の要求は出さない */
    {
        const u8 *r; u16 rl;
        u32 frames = g_put_frames;
        CHECK(sfs_client_call(&g_c, SFS_T_STAT, (const u8 *)"\x01/", 2, &r, &rl)
              == OS32_ERR_IO);
        CHECK(g_put_frames == frames);
    }
}

static void bad_host_replies(void)
{
    void *ctx = setup_session(115200);
    u8 buf[600];

    /* 要求より多く返すホスト → IO */
    inj_read_extra = 1;
    CHECK(serialfs_get_ops()->read_stream(ctx, "/a.txt", buf, 10, 0) == OS32_ERR_IO);
    inj_read_extra = 0;
    /* 名前に '/' を含む列挙 → IO (途中まで流して OK にしない) */
    inj_list_bad = 1;
    g_list_n = 0;
    CHECK(serialfs_get_ops()->list_dir(ctx, "/dir", list_cb, 0) == OS32_ERR_IO);
    CHECK(g_list_n == 0);
    inj_list_bad = 0;
    /* cookie が進まない → IO (回り続けない) */
    inj_list_stuck = 1;
    CHECK(serialfs_get_ops()->list_dir(ctx, "/dir", list_cb, 0) == OS32_ERR_IO);
    CHECK(g_exec[SFS_T_LIST] <= 3);
    inj_list_stuck = 0;
    /* 知らない status は IO に畳む (ホストの番号をそのまま信じない) */
    inj_weird_status = 1;
    {
        OS32_Stat st;
        CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
        CHECK(serialfs_get_ops()->mkdir(ctx, "/d") == OS32_ERR_IO);
    }
}

static void fault_stray_flood(void)
{
    void *ctx = setup_session(115200);
    OS32_Stat st;
    u32 t0 = g_now;
    u32 bound = (1 + SFS_RETRIES) *
                (sfs_trial_ticks(115200) + sfs_ms_ticks(SFS_RESYNC_MAX_MS) + 2);

    /* 番号違いの応答が流れ続けても、期限は送り終えた時点から固定 */
    inj_stray_flood = 1;
    g_put_limit_ticks = t0 + bound * 2;
    CHECK(serialfs_get_ops()->stat(ctx, "/a.txt", &st) == OS32_ERR_IO);
    CHECK(g_now - t0 <= bound);
    CHECK(g_c.stray >= 4);
}

/* ======================================================================== */
/*  案件: rshell の ESC と `sfs run` の行 (userland/shell/serial_watchdog.c) */
/* ======================================================================== */
static void rshell_rules(void)
{
    /* 本体の ESC はどこでも閉じる */
    CHECK(rsh_esc_classify(0x1B, 0, 0, 1) == RSH_ESC_EXIT);
    CHECK(rsh_esc_classify(0x1B, 0, 1, 0) == RSH_ESC_EXIT);
    /* シリアルの ESC は行頭の単独だけ */
    CHECK(rsh_esc_classify(0x1B, 1, 1, 0) == RSH_ESC_EXIT);
    CHECK(rsh_esc_classify(0x1B, 1, 1, 1) == RSH_ESC_JUNK);
    CHECK(rsh_esc_classify(0x1B, 1, 0, 0) == RSH_ESC_JUNK);
    CHECK(rsh_esc_classify(0x1B, 1, 0, 1) == RSH_ESC_JUNK);
    CHECK(rsh_esc_classify('a', 1, 1, 0) == RSH_ESC_NONE);
    CHECK(rsh_esc_classify(0x05, 1, 1, 0) == RSH_ESC_NONE);
    /* `sfs run` の行 */
    CHECK(strcmp(rsh_sfs_child("sfs run hsync boot") ? rsh_sfs_child("sfs run hsync boot") : "",
                 "hsync boot") == 0);
    CHECK(rsh_sfs_child("  sfs \t run   ls /host") != 0 &&
          strcmp(rsh_sfs_child("  sfs \t run   ls /host"), "ls /host") == 0);
    CHECK(rsh_sfs_child("sfs run") == 0);
    CHECK(rsh_sfs_child("sfs run   ") == 0);
    CHECK(rsh_sfs_child("sfsrun ls") == 0);
    CHECK(rsh_sfs_child("sfs runx ls") == 0);
    CHECK(rsh_sfs_child("xsfs run ls") == 0);
    CHECK(rsh_sfs_child("echo a && sfs run ls") == 0);
    CHECK(rsh_sfs_child(0) == 0);

    /* 行の組み立て (レビュー往復 1、Codex 8): 拒否は本当の行末まで持つ */
    {
        char b[8];
        struct rsh_line l;
        rsh_line_begin(&l, b, (int)sizeof(b));
        CHECK(rsh_line_feed(&l, 0x1B, 1, 1, 1) == RSH_LINE_MORE && l.junk);
        CHECK(rsh_line_feed(&l, 'x', 1, 0, 1) == RSH_LINE_MORE);
        {
            int k;
            /* 沈黙が何度 (何秒) 続いても解けない (往復 2、Codex 3) */
            for (k = 0; k < 100000; k++)
                if (rsh_line_idle(&l) != RSH_LINE_MORE) break;
            CHECK(k == 100000);
        }
        CHECK(rsh_line_feed(&l, 'l', 1, 0, 1) == RSH_LINE_MORE);
        CHECK(l.junk && l.pos == 2 && strcmp(b, "xl") == 0);
        /* 沈黙の後に来た同じ行の続きも拒否のまま (実行されない) */
        CHECK(rsh_line_idle(&l) == RSH_LINE_MORE);
        CHECK(rsh_line_feed(&l, 's', 1, 0, 1) == RSH_LINE_MORE && l.junk);
        /* 閉じるのは行末だけ (ホストの改行) */
        CHECK(rsh_line_feed(&l, '\n', 1, 0, 1) == RSH_LINE_DONE && l.junk);
        /* 本体キーボードの Enter でも閉じる (拒否のまま) — 回復の口 */
        rsh_line_begin(&l, b, (int)sizeof(b));
        CHECK(rsh_line_feed(&l, 0x1B, 1, 1, 1) == RSH_LINE_MORE && l.junk);
        CHECK(rsh_line_idle(&l) == RSH_LINE_MORE);
        CHECK(rsh_line_feed(&l, '\r', 0, 0, 1) == RSH_LINE_DONE && l.junk);
        /* 本体の ESC は rshell を閉じる (拒否した行の途中でも) */
        rsh_line_begin(&l, b, (int)sizeof(b));
        CHECK(rsh_line_feed(&l, 0x1B, 1, 1, 1) == RSH_LINE_MORE && l.junk);
        CHECK(rsh_line_feed(&l, 0x1B, 0, 0, 1) == RSH_LINE_EXIT);
        /* 普通の行は短い空回りで終わる (従来どおり) */
        rsh_line_begin(&l, b, (int)sizeof(b));
        CHECK(rsh_line_feed(&l, 'l', 1, 1, 0) == RSH_LINE_MORE);
        CHECK(rsh_line_idle(&l) == RSH_LINE_DONE && !l.junk && l.bytes == 1);
        /* 行の途中のシリアルの ESC は拒否 (閉じない)、本体の ESC は閉じる */
        rsh_line_begin(&l, b, (int)sizeof(b));
        CHECK(rsh_line_feed(&l, 'a', 1, 1, 0) == RSH_LINE_MORE);
        CHECK(rsh_line_feed(&l, 0x1B, 1, 0, 1) == RSH_LINE_MORE && l.junk);
        CHECK(rsh_line_feed(&l, 0x1B, 0, 0, 1) == RSH_LINE_EXIT && l.bytes == 2);
        /* 行頭の単独の ESC は閉じ、EOT の数え (bytes) に入れない */
        rsh_line_begin(&l, b, (int)sizeof(b));
        CHECK(rsh_line_feed(&l, 0x1B, 1, 1, 0) == RSH_LINE_EXIT && l.bytes == 0);
        rsh_line_begin(&l, b, (int)sizeof(b));
        CHECK(rsh_line_feed(&l, 0x1B, 0, 1, 0) == RSH_LINE_EXIT && l.bytes == 0);
        /* 溢れ: 読み続けて印だけ */
        rsh_line_begin(&l, b, 4);
        CHECK(rsh_line_feed(&l, 'a', 1, 1, 0) == RSH_LINE_MORE);
        CHECK(rsh_line_feed(&l, 'b', 1, 0, 1) == RSH_LINE_MORE);
        CHECK(rsh_line_feed(&l, 'c', 1, 0, 1) == RSH_LINE_MORE && l.overflow);
        CHECK(strcmp(b, "ab") == 0 && l.bytes == 3);
        /* 本体のバイトの印 */
        rsh_line_begin(&l, b, (int)sizeof(b));
        (void)rsh_line_feed(&l, 's', 1, 1, 0);
        (void)rsh_line_feed(&l, 'f', 0, 0, 1);
        CHECK(l.local == 1);
    }
}

/* rsh_line_rest の偽の線: 台本 (tick ごとに 1 バイト) と仮想の時計 */
struct rio_fake {
    unsigned long now;          /* 仮想 tick (idle で 1 進む) */
    const int *script;          /* tick ごとに届くバイト (-1 = 無し) */
    const int *script_fs;       /* そのバイトがシリアル由来か */
    unsigned long script_n;
    unsigned long taken_at;     /* 最後に渡した tick + 1 (同じ tick で 2 度渡さない) */
    unsigned long idles;
    unsigned long limit;        /* 暴走止め (これを越えたら本体の Enter を出す) */
    int limit_hit;
};
static int rio_getch(void *ctx, int *from_serial)
{
    struct rio_fake *f = (struct rio_fake *)ctx;
    if (f->idles >= f->limit) {             /* 暴走止め: 本体の Enter */
        f->limit_hit = 1;
        *from_serial = 0;
        return '\r';
    }
    if (f->now < f->script_n && f->taken_at != f->now + 1 &&
        f->script[f->now] >= 0) {
        f->taken_at = f->now + 1;
        *from_serial = f->script_fs ? f->script_fs[f->now] : 1;
        return f->script[f->now];
    }
    *from_serial = 0;
    return -1;
}
static unsigned long rio_tick(void *ctx)
{
    return ((struct rio_fake *)ctx)->now;
}
static void rio_idle(void *ctx)
{
    struct rio_fake *f = (struct rio_fake *)ctx;
    f->now++;
    f->idles++;
}

/* 番犬と拒否した行 (Codex P2): 行末を待つ間も期限で旧速度へ戻す */
static void rshell_watchdog_junk(void)
{
    char b[16];
    struct rsh_line l;
    struct serial_watchdog w;
    struct rio_fake f;
    struct rsh_io io;
    int r;
    static int garbage[2000];
    unsigned long i;

    io.getch = rio_getch; io.tick = rio_tick; io.idle = rio_idle; io.ctx = &f;

    /* 1) 番犬が動いている間に「ESC + 続き (改行なし)」→ 沈黙。
     *    期限で REVERT、行は捨てられ (実行も EOT もしない)、番犬は下りる。 */
    memset(&w, 0, sizeof(w));
    serial_watchdog_arm(&w, 0, 1, 9600);
    memset(&f, 0, sizeof(f));
    f.limit = 5000;
    rsh_line_begin(&l, b, (int)sizeof(b));
    r = rsh_line_feed(&l, 0x1B, 1, 1, 1);
    CHECK(r == RSH_LINE_MORE && l.junk);
    r = rsh_line_rest(&l, r, 'x', 1, &w, &io);
    CHECK(r == RSH_LINE_REVERT);
    CHECK(!f.limit_hit);
    CHECK(f.now >= SER_SWITCH_WATCHDOG_TICKS &&
          f.now <= SER_SWITCH_WATCHDOG_TICKS + 2);
    CHECK(l.pos == 0 && !l.junk && l.bytes == 0 && b[0] == '\0');
    CHECK(!w.armed);
    CHECK(serial_watchdog_poll(&w, f.now + 1000) == SER_WD_WAIT);

    /* 2) 化けたバイトが改行なしで来続けても、期限で戻す (毎周見る) */
    for (i = 0; i < sizeof(garbage) / sizeof(garbage[0]); i++)
        garbage[i] = 'a' + (int)(i % 26);
    serial_watchdog_arm(&w, 0, 1, 9600);
    memset(&f, 0, sizeof(f));
    f.limit = 5000;
    f.script = garbage;
    f.script_n = sizeof(garbage) / sizeof(garbage[0]);
    rsh_line_begin(&l, b, (int)sizeof(b));
    r = rsh_line_feed(&l, 0x1B, 1, 1, 1);
    r = rsh_line_rest(&l, r, -1, 1, &w, &io);
    CHECK(r == RSH_LINE_REVERT && !f.limit_hit);
    CHECK(f.now <= SER_SWITCH_WATCHDOG_TICKS + 2);
    CHECK(l.pos == 0 && !l.junk && !l.overflow);

    /* 3) 番犬が居なければ従来どおり: 沈黙では解けず、行末で拒否のまま閉じる */
    {
        static int late_nl[1200];
        for (i = 0; i < 1200; i++) late_nl[i] = -1;
        late_nl[1100] = '\n';
        memset(&w, 0, sizeof(w));
        memset(&f, 0, sizeof(f));
        f.limit = 5000;
        f.script = late_nl;
        f.script_n = 1200;
        rsh_line_begin(&l, b, (int)sizeof(b));
        r = rsh_line_feed(&l, 0x1B, 1, 1, 1);
        r = rsh_line_rest(&l, r, 'x', 1, &w, &io);
        CHECK(r == RSH_LINE_DONE && l.junk && !f.limit_hit && f.now == 1100);
        /* NULL の番犬も同じ */
        memset(&f, 0, sizeof(f));
        f.limit = 5000;
        f.script = late_nl;
        f.script_n = 1200;
        rsh_line_begin(&l, b, (int)sizeof(b));
        r = rsh_line_feed(&l, 0x1B, 1, 1, 1);
        r = rsh_line_rest(&l, r, 'x', 1, (struct serial_watchdog *)0, &io);
        CHECK(r == RSH_LINE_DONE && l.junk && f.now == 1100);
    }

    /* 4) 期限の前なら本体の Enter / ESC で回復できる (番犬は仕掛かったまま) */
    {
        static int kb[50];
        static int kb_fs[50];
        for (i = 0; i < 50; i++) { kb[i] = -1; kb_fs[i] = 1; }
        kb[40] = '\r'; kb_fs[40] = 0;
        serial_watchdog_arm(&w, 0, 1, 9600);
        memset(&f, 0, sizeof(f));
        f.limit = 5000;
        f.script = kb; f.script_fs = kb_fs; f.script_n = 50;
        rsh_line_begin(&l, b, (int)sizeof(b));
        r = rsh_line_feed(&l, 0x1B, 1, 1, 1);
        r = rsh_line_rest(&l, r, 'x', 1, &w, &io);
        CHECK(r == RSH_LINE_DONE && l.junk && w.armed);
        kb[40] = 0x1B;
        serial_watchdog_arm(&w, 0, 1, 9600);
        memset(&f, 0, sizeof(f));
        f.limit = 5000;
        f.script = kb; f.script_fs = kb_fs; f.script_n = 50;
        rsh_line_begin(&l, b, (int)sizeof(b));
        r = rsh_line_feed(&l, 0x1B, 1, 1, 1);
        r = rsh_line_rest(&l, r, 'x', 1, &w, &io);
        CHECK(r == RSH_LINE_EXIT && w.armed);
    }

    /* 5) 拒否していない行は番犬に触らない (ack の行を途中で切らない)。
     *    期限を過ぎていても、行は短い空回りで終わって実行に回る。 */
    serial_watchdog_arm(&w, 0, 1, 9600);
    memset(&f, 0, sizeof(f));
    f.now = SER_SWITCH_WATCHDOG_TICKS + 10;
    f.limit = 5000;
    rsh_line_begin(&l, b, (int)sizeof(b));
    r = rsh_line_feed(&l, 's', 1, 1, 0);
    r = rsh_line_rest(&l, r, -1, 1, &w, &io);
    CHECK(r == RSH_LINE_DONE && l.pos == 1 && w.armed);
}

/* ======================================================================== */
/*  案件: vmkernel.old の判定 (userland/system/hsync_bootold.inc)           */
/* ======================================================================== */
static void bootold_rules(void)
{
    u8 img[200];
    u32 off = 0, i, s1, s2, want;

    memset(img, 0, sizeof(img));
    sfs_put32(img, 0x32334B56UL);         /* 'VK32' */
    sfs_put32(img + 4, 16 + 20 * 2 + 8);
    sfs_put32(img + 8, 2);
    sfs_put32(img + 12, 2);
    for (i = 64; i < sizeof(img); i++) img[i] = (u8)(i * 13);
    CHECK(hbo_crc_offset(img, sizeof(img), &off) == 0 && off == 16 + 40 + 4);
    sfs_put32(img + off, 0xDEADBEEFUL);   /* 欄に何が入っていても */
    /* 期待値: 欄を 0 にした全体の CRC */
    {
        u8 z[200];
        memcpy(z, img, sizeof(z));
        memset(z + off, 0, 4);
        want = crc32_calc(z, sizeof(z));
    }
    s1 = crc32_core_final(hbo_crc_update(CRC32_INIT, img, sizeof(img), 0, off));
    CHECK(s1 == want);
    /* 欄をまたぐ分割でも同じ (1 バイトずつ) */
    s2 = CRC32_INIT;
    for (i = 0; i < sizeof(img); i++) s2 = hbo_crc_update(s2, img + i, 1, i, off);
    CHECK(crc32_core_final(s2) == want);
    s2 = CRC32_INIT;
    s2 = hbo_crc_update(s2, img, off + 2, 0, off);
    s2 = hbo_crc_update(s2, img + off + 2, sizeof(img) - off - 2, off + 2, off);
    CHECK(crc32_core_final(s2) == want);
    /* 読めないヘッダ */
    CHECK(hbo_crc_offset(img, 15, &off) == -1);
    img[0] ^= 1;
    CHECK(hbo_crc_offset(img, sizeof(img), &off) == -1);
    img[0] ^= 1;
    sfs_put32(img + 8, 1);
    CHECK(hbo_crc_offset(img, sizeof(img), &off) == -1);   /* v1 は断る */
    sfs_put32(img + 8, 2);
    sfs_put32(img + 12, 0);
    CHECK(hbo_crc_offset(img, sizeof(img), &off) == -1);
    sfs_put32(img + 12, 5);
    CHECK(hbo_crc_offset(img, sizeof(img), &off) == -1);
    /* 判定 */
    CHECK(hbo_decide(0, 1, 0x1234, 1, 0x1234, 0x1234) == HBO_MAKE);
    CHECK(hbo_decide(0, 1, 0x1234, 1, 0x1235, 0x1235) == HBO_REFUSE_DIFF);
    CHECK(hbo_decide(0, 0, 0, 1, 0x1234, 0x1234) == HBO_REFUSE_INFO);
    CHECK(hbo_decide(0, 1, 0x1234, 0, 0, 0) == HBO_REFUSE_DISK);
    /* 中身は起動した版と同じで、欄だけが壊れている → 断る (起動しない .old) */
    CHECK(hbo_decide(0, 1, 0x1234, 1, 0x1234, 0x9999) == HBO_REFUSE_CORRUPT);
    CHECK(hbo_decide(1, 0, 0, 0, 0, 0) == HBO_NO_BACKUP);
    CHECK(hbo_decide(1, 1, 1, 1, 2, 3) == HBO_NO_BACKUP);
}

/* ======================================================================== */
/*  vectors / decode / pipe (Python との相互照合)                           */
/* ======================================================================== */
static void print_hex(const u8 *b, u16 n)
{
    u16 i;
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

static int mode_vectors(void)
{
    u8 f[SFS_MAX_FRAME], pl[SFS_MAX_PAYLOAD];
    u16 i, n;
    for (i = 0; i < SFS_MAX_PAYLOAD; i++) pl[i] = (u8)(i * 31 + 7);
    n = sfs_encode(f, SFS_T_HELLO, 0, 0, pl, 8);            print_hex(f, n);
    n = sfs_encode(f, SFS_T_READ, 0xDEADBEEFUL, 1, pl, 0);  print_hex(f, n);
    n = sfs_encode(f, SFS_T_WRITE | SFS_T_RESP, 0x01020304UL, 0xFFFE, pl,
                   SFS_MAX_PAYLOAD);                         print_hex(f, n);
    n = sfs_encode(f, SFS_T_ERR, 0x80000001UL, 0x8000, pl, 4); print_hex(f, n);
    return 0;
}

static int mode_decode(void)
{
    char line[4096];
    SfsDec d;
    SfsFrame o;
    u32 i, n;
    int got = 0;

    sfs_dec_reset(&d);
    if (!fgets(line, sizeof(line), stdin)) return 2;
    n = (u32)strlen(line) / 2;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(line + 2 * i, "%2x", &v) != 1) break;
        if (sfs_dec_feed(&d, (u8)v, &o) == 1) {
            u16 k;
            printf("%02x %08lx %04x %u ", o.type, (unsigned long)o.sid, o.seq,
                   (unsigned)o.len);
            for (k = 0; k < o.len; k++) printf("%02x", o.payload[k]);
            printf("\n");
            got++;
        }
    }
    printf("bad_crc=%lu bad_len=%lu frames=%d\n", (unsigned long)d.bad_crc,
           (unsigned long)d.bad_len, got);
    return 0;
}

/* ---- pipe: 実時間で標準入出力を線にする ---- */
static u32 p_now(void *ctx)
{
    struct timespec ts;
    (void)ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)(ts.tv_sec * 100 + ts.tv_nsec / 10000000);
}
static int p_put(void *ctx, const u8 *b, u32 n)
{
    (void)ctx;
    while (n) {
        ssize_t w = write(1, b, n);
        if (w <= 0) return -1;
        b += w; n -= (u32)w;
    }
    return 0;
}
static int p_get(void *ctx)
{
    u8 b;
    (void)ctx;
    if (read(0, &b, 1) == 1) return b;
    return -1;
}
static void p_idle(void *ctx) { (void)ctx; usleep(1000); }
static int p_can_wait(void *ctx) { (void)ctx; return 1; }
static const SfsIo g_pio = { p_put, p_get, p_now, p_idle, p_can_wait };

static int mode_pipe(void)
{
    VfsOps *ops = serialfs_get_ops();
    void *ctx;
    OS32_Stat st;
    static u8 buf[70000];
    u32 i;
    int rc;

    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    sfs_client_init(&g_c, &g_pio, 0, 115200);
    if (sfs_client_hello(&g_c, 0x5151UL) != 0) {
        fprintf(stderr, "PIPE hello failed\n");
        return 1;
    }
    serialfs_permit(&g_c);
    ctx = ops->mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_SERIAL, 1));
    serialfs_forbid();
    rc = ops->stat(ctx, "/hello.txt", &st);
    fprintf(stderr, "stat rc=%d size=%lu mtime=%lu dir=%d\n", rc,
            (unsigned long)st.st_size, (unsigned long)st.st_mtime,
            (st.st_mode & OS_S_IFMT) == OS_S_IFDIR);
    rc = ops->read_stream(ctx, "/hello.txt", buf, sizeof(buf), 0);
    fprintf(stderr, "read rc=%d crc=%08lx\n", rc,
            (unsigned long)(rc > 0 ? crc32_calc(buf, (u32)rc) : 0));
    g_list_n = 0;
    rc = ops->list_dir(ctx, "/many", list_cb, 0);
    fprintf(stderr, "list rc=%d n=%d last=%s\n", rc, g_list_n, g_list_last);
    for (i = 0; i < 3000; i++) buf[i] = (u8)(i * 3);
    rc = ops->write_file(ctx, "/out.bin", buf, 3000);
    fprintf(stderr, "write rc=%d\n", rc);
    rc = ops->rename(ctx, "/out.bin", "/out2.bin");
    fprintf(stderr, "rename rc=%d\n", rc);
    rc = ops->mkdir(ctx, "/newdir");
    fprintf(stderr, "mkdir rc=%d\n", rc);
    rc = ops->unlink(ctx, "/gone.txt");
    fprintf(stderr, "unlink rc=%d\n", rc);
    rc = ops->stat(ctx, "/../etc/passwd", &st);
    fprintf(stderr, "escape rc=%d\n", rc);
    rc = ops->stat(ctx, "/nope", &st);
    fprintf(stderr, "nope rc=%d\n", rc);
    ops->umount(ctx);
    (void)sfs_client_oneway(&g_c, SFS_T_BYE, 0, 0);
    fprintf(stderr, "stats calls=%lu resends=%lu timeouts=%lu stray=%lu bad_crc=%lu\n",
            (unsigned long)g_c.calls, (unsigned long)g_c.resends,
            (unsigned long)g_c.timeouts, (unsigned long)g_c.stray,
            (unsigned long)g_c.dec.bad_crc);
    return 0;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "vectors")) return mode_vectors();
    if (!strcmp(c, "decode")) return mode_decode();
    if (!strcmp(c, "pipe")) return mode_pipe();
    if (!strcmp(c, "crc_and_frames")) crc_and_frames();
    else if (!strcmp(c, "timing_and_seq")) timing_and_seq();
    else if (!strcmp(c, "vfs_contract")) vfs_contract();
    else if (!strcmp(c, "mount_permit")) mount_permit();
    else if (!strcmp(c, "fault_drop")) fault_drop();
    else if (!strcmp(c, "fault_corrupt")) fault_corrupt();
    else if (!strcmp(c, "fault_stray")) fault_stray();
    else if (!strcmp(c, "fault_delay")) fault_delay();
    else if (!strcmp(c, "fault_dead")) fault_dead();
    else if (!strcmp(c, "fault_err_stale")) fault_err_stale();
    else if (!strcmp(c, "fault_garbage")) fault_garbage();
    else if (!strcmp(c, "fault_stray_flood")) fault_stray_flood();
    else if (!strcmp(c, "cannot_wait")) cannot_wait();
    else if (!strcmp(c, "seq_exhausted")) seq_exhausted();
    else if (!strcmp(c, "hello_cases")) hello_cases();
    else if (!strcmp(c, "bad_host_replies")) bad_host_replies();
    else if (!strcmp(c, "rshell_rules")) rshell_rules();
    else if (!strcmp(c, "rshell_watchdog_junk")) rshell_watchdog_junk();
    else if (!strcmp(c, "bootold_rules")) bootold_rules();
    else { fprintf(stderr, "unknown case %s\n", c); return 2; }
    return failed ? 1 : 0;
}
