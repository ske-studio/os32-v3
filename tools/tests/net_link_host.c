/* =========================================================================
 *  NET_LINK_HOST.C — リンク層 v2 と KAPI v51 を **実物のソースで** 確かめる
 *
 *  対象票: docs/archive/network/TASK_N1.md 段 4 / 契約は TASK_N0.md §1a・§1b・§2
 *  実行:   python3 -B tools/tests/test_net_link.py [ケース名 ...]
 *  記録:   tools/tests/n1_tdd.md
 *
 *  `net/link.c` と `kapi/kapi_host.c` を 1 行も写さずそのまま #include する
 *  (模型ではない)。贋物にするのは、ホストに持ち込めない 4 つだけ:
 *    - NIC   : RX キュー、**TX は 1 tick 1 フレームしか受けない**、tick
 *    - cli/sti: IF=0 中のタイマ延期と、復元直後の tick
 *    - 100Hz タイマ: host_tick() が唯一の駆動元 (link_tick を呼ぶのはここだけ)
 *    - ディスパッチャ: kapi_argptr の早期検査と ring3_ptr_ok の帯
 *  対向は **実 Agent** (tools/host_agent.py を --unix でサブプロセス起動) か、
 *  フレームを手で組む台本 (欠落・重複・遅延・墓標の注入)。
 *
 *  ホスト 64bit + libc で走らせる (tools/tests/kapi_db_v50_host.c と同じ様式)。
 *  u32 / i32 は 32bit 幅の贋 types.h に差し替える (カーネルでは 32bit ILP32)。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

/* ---- 贋 types.h (ホストの unsigned long は 64bit なので置き換える) ------ */
#define TYPES_H
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    i8;
typedef signed short   i16;
typedef signed int     i32;
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef __cdecl
#define __cdecl
#endif

/* ---- 贋 os32_kapi_shared.h ([C4] 共有定数のうちこの票が使う分だけ) ------ */
#define OS32_KAPI_SHARED_H
#define OS32_ERR_IO        -1
#define OS32_ERR_INVAL     -9
#define OS32_ERR_NOSYS     -10
#define OS32_ERR_STALE     -11
#define OS32_ERR_FULL      -13
#define OS32_ERR_AGAIN     -14

/* ---- 贋 kstring.h / kprintf.h ----------------------------------------- */
#define __KSTRING_H
#define __KPRINTF_H
static void *kmemcpy(void *d, const void *s, u32 n) { return memcpy(d, s, n); }
static void *kmemset(void *d, int v, u32 n) { return memset(d, v, n); }
static int kprintf_quiet = 1;
static void kprintf(u8 color, const char *fmt, ...)
{
    (void)color; (void)fmt;
    if (!kprintf_quiet) { /* 試験では黙らせる */ }
}
static int kutoa_dec(u32 val, char *buf, int bufsz)
{
    char tmp[12];
    int n = 0, i;
    if (val == 0) { if (bufsz > 0) buf[0] = '0'; return 1; }
    while (val && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (val % 10)); val /= 10; }
    for (i = 0; i < n && i < bufsz; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

/* ---- 贋 ne2000.h ------------------------------------------------------- */
#define NE2000_H
#define NE2K_OK             0
#define NE2K_ERR_BUSY      -4
#define NE2K_ERR_AGAIN     -5
#define NE2K_STATE_OFF      0
#define NE2K_STATE_RUNNING  1
struct ne2k_stats { u32 rx_frames; u32 rx_pages_total; };
static int  ne2k_send(const void *frame, unsigned int length);
static int  ne2k_recv(void *frame, unsigned int capacity, unsigned int *length);
static int  ne2k_state(void);
static int  ne2k_is_busy(void);
static unsigned int ne2k_rx_ring_free_pages(void);
static unsigned int ne2k_rx_queue_free(void);
static void ne2k_get_stats(struct ne2k_stats *s);

/* ======================================================================== */
/*  贋 NIC                                                                  */
/* ======================================================================== */
#define NIC_Q       128
#define FRAME_CAP   1600
static u8  nic_rx[NIC_Q][FRAME_CAP];
static unsigned int nic_rxlen[NIC_Q];
static int nic_rxh, nic_rxt;
static int nic_up = 1;
static int nic_busy = 0;
static int nic_tx_allow = 1;            /* **1 tick 1 フレーム** */
static u32 nic_ring_pages = 120;
static u32 nic_queue_free = 30;
static struct ne2k_stats nic_stats;

/* TX 記録 (試験が中身を見る) */
#define TXLOG 512
static u8  txlog[TXLOG][FRAME_CAP];
static unsigned int txloglen[TXLOG];
static int txn;

/* TX のたびに呼ぶ口 (実 Agent へ渡すか、台本が見るだけか) */
static void (*tx_hook)(const u8 *f, unsigned int n);

/* リング更新 / TX 構築の途中に「贋 IRQ5」が積むフレーム (注入試験) */
static int  inject_on_send = 0;
static u8   inject_frame[FRAME_CAP];
static unsigned int inject_len;

static void nic_push_rx(const u8 *f, unsigned int n)
{
    int next = (nic_rxt + 1) % NIC_Q;
    if (next == nic_rxh) return;               /* 満杯 = 取りこぼし */
    if (n > FRAME_CAP) n = FRAME_CAP;
    memcpy(nic_rx[nic_rxt], f, n);
    nic_rxlen[nic_rxt] = n;
    nic_rxt = next;
}

static int ne2k_send(const void *frame, unsigned int length)
{
    if (!nic_up) return NE2K_ERR_BUSY;
    if (nic_tx_allow <= 0) return NE2K_ERR_BUSY;
    nic_tx_allow--;
    if (txn < TXLOG) {
        memcpy(txlog[txn], frame, length > FRAME_CAP ? FRAME_CAP : length);
        txloglen[txn] = length;
        txn++;
    }
    if (inject_on_send && inject_len) {
        /* TX を組んでいる最中に IRQ5 が来てリングへ積んだ、という筋 */
        nic_push_rx(inject_frame, inject_len);
        inject_on_send = 0;
    }
    if (tx_hook) tx_hook((const u8 *)frame, length);
    return NE2K_OK;
}

static int ne2k_recv(void *frame, unsigned int capacity, unsigned int *length)
{
    unsigned int n;
    if (nic_rxh == nic_rxt) return NE2K_ERR_AGAIN;
    n = nic_rxlen[nic_rxh];
    if (n > capacity) n = capacity;
    memcpy(frame, nic_rx[nic_rxh], n);
    *length = n;
    nic_rxh = (nic_rxh + 1) % NIC_Q;
    nic_stats.rx_frames++;
    nic_stats.rx_pages_total += 3;
    return NE2K_OK;
}

static int ne2k_state(void) { return nic_up ? NE2K_STATE_RUNNING : NE2K_STATE_OFF; }
static int ne2k_is_busy(void) { return nic_busy; }
static unsigned int ne2k_rx_ring_free_pages(void) { return nic_ring_pages; }
static unsigned int ne2k_rx_queue_free(void) { return nic_queue_free; }
static void ne2k_get_stats(struct ne2k_stats *s) { *s = nic_stats; }

/* ======================================================================== */
/*  贋タイマ + 贋 cli / sti                                                  */
/* ======================================================================== */
volatile u32 tick_count = 0;
static int  irq_depth = 0;              /* IF=0 のネスト段数 */
static u32  timer_deferred = 0;         /* IF=0 中に延期した tick 数 */
static u32  timer_ran = 0;
static void (*wire_pump_fn)(void);

/* 前方宣言。link.c を include した後に実体を結ぶ。 */
static void link_tick_fwd(void);

static void run_link_tick(void)
{
    timer_ran++;
    link_tick_fwd();
}

unsigned int link_test_irq_save(void)
{
    irq_depth++;
    return 0;
}

void link_test_irq_restore(unsigned int flags)
{
    (void)flags;
    irq_depth--;
    if (irq_depth == 0 && timer_deferred) {
        /* **IF 復元直後の tick が延期分を処理する** (TASK_N0 §2a) */
        timer_deferred--;
        run_link_tick();
    }
}

/* 100Hz タイマ 1 回。試験が時間を進める唯一の口。 */
static void host_tick(void)
{
    tick_count++;
    nic_tx_allow = 1;
    if (wire_pump_fn) wire_pump_fn();
    if (irq_depth > 0) { timer_deferred++; return; }   /* IF=0 中は延期 */
    run_link_tick();
    if (wire_pump_fn) wire_pump_fn();
}

/* link.c の hlt 待ちの代わり (IF=1 で次のタイマまで寝る)。 */
void link_test_idle(void) { host_tick(); }

static void ticks(int n) { int i; for (i = 0; i < n; i++) host_tick(); }

/* ======================================================================== */
/*  実物                                                                    */
/* ======================================================================== */
#define LINK_HOST_TEST 1
#include "../../net/link.c"

static void link_tick_fwd(void) { link_tick(); }

/* ---- 贋 exec.h (KAPI ラッパーが唯一使う口) ---------------------------- */
#define __EXEC_H
static const u8 *user_lo, *user_hi;
static int host_cpl3 = 0;
static u32 fault_kills = 0;
static int host_owner = 3;
int res_owner_get(void) { return host_owner; }

static const u8 *user_resolve(u32 p)
{
    const u8 *q;
    for (q = user_lo; q < user_hi; q++)
        if ((u32)(unsigned long)q == p) return q;
    return (const u8 *)0;
}

int ring3_ptr_ok(u32 p)
{
    if (p == 0) return 1;
    if (!host_cpl3) return 1;
    return user_resolve(p) != (const u8 *)0;
}

int ring3_user_range_ok(u32 p, u32 len)
{
    const u8 *q;
    if (!host_cpl3) return 1;                 /* CPL=0 の直呼びは素通し */
    if (p == 0) return 0;
    if (len == 0) return 1;
    if (p + len < p) return 0;
    q = user_resolve(p);
    if (!q) return 0;
    return (u32)(user_hi - q) >= len;
}

#include "../../kapi/kapi_host.c"

/* ディスパッチャの早期検査 (exec/exec.c の ring3_syscall_dispatch と同じ規則)。
 * 先頭番地が帯外なら **ラッパーに入る前に** kill する ([往復 3 の B8])。 */
static int dispatch_early(u16 ptrmask, const u32 *args, int nfixed)
{
    int k;
    for (k = 0; k < nfixed && k < 16; k++) {
        if ((ptrmask & (u16)(1u << k)) && !ring3_ptr_ok(args[k])) {
            fault_kills++;
            return 0;                          /* kill: 戻らない = 呼ばない */
        }
    }
    return 1;
}

/* ======================================================================== */
/*  報告                                                                    */
/* ======================================================================== */
static int failures;
static const char *cur_case = "";

static void check(int cond, const char *what)
{
    if (!cond) { printf("    FAIL %s: %s\n", cur_case, what); failures++; }
}

/* ======================================================================== */
/*  フレームの組み立て / 読み取り (試験側。link.c の直列化を借りない)        */
/* ======================================================================== */
static const u8 my_mac[6]    = { 0x02, 0x00, 0x5e, 0x00, 0x00, 0x02 };
static const u8 agent_mac[6] = { 0x02, 0x00, 0x5e, 0x00, 0x00, 0x01 };

static unsigned int mkframe(u8 *out, u8 op, u8 flags, u16 epoch, u32 seq, u32 ack,
                            u32 rid, u16 sess, const void *pl, unsigned int plen)
{
    u8 *h = out + 14;
    memcpy(out, my_mac, 6);
    memcpy(out + 6, agent_mac, 6);
    out[12] = (u8)(LINK_ETHERTYPE >> 8);
    out[13] = (u8)(LINK_ETHERTYPE & 0xFF);
    h[0] = op; h[1] = flags;
    h[2] = (u8)epoch; h[3] = (u8)(epoch >> 8);
    h[4] = (u8)seq; h[5] = (u8)(seq >> 8); h[6] = (u8)(seq >> 16); h[7] = (u8)(seq >> 24);
    h[8] = (u8)ack; h[9] = (u8)(ack >> 8); h[10] = (u8)(ack >> 16); h[11] = (u8)(ack >> 24);
    h[12] = (u8)plen; h[13] = (u8)(plen >> 8);
    h[14] = (u8)rid; h[15] = (u8)(rid >> 8); h[16] = (u8)(rid >> 16); h[17] = (u8)(rid >> 24);
    h[18] = (u8)sess; h[19] = (u8)(sess >> 8);
    if (plen) memcpy(h + 20, pl, plen);
    return 14 + 20 + plen;
}

static void inject(u8 op, u8 flags, u16 epoch, u32 seq, u32 ack, u32 rid, u16 sess,
                   const void *pl, unsigned int plen)
{
    u8 f[FRAME_CAP];
    unsigned int n = mkframe(f, op, flags, epoch, seq, ack, rid, sess, pl, plen);
    nic_push_rx(f, n);
}

/* TX 記録の読み取り */
static u8  tx_op(int i)    { return txlog[i][14 + LINK_OFF_OP]; }
static u8  tx_flags(int i) { return txlog[i][14 + LINK_OFF_FLAGS]; }
static u16 tx_epoch(int i) { return rd16(txlog[i] + 14 + LINK_OFF_EPOCH); }
static u32 tx_seq(int i)   { return rd32(txlog[i] + 14 + LINK_OFF_SEQ); }
static u32 tx_ack(int i)   { return rd32(txlog[i] + 14 + LINK_OFF_ACK); }
static u32 tx_rid(int i)   { return rd32(txlog[i] + 14 + LINK_OFF_RID); }
static u16 tx_sess(int i)  { return rd16(txlog[i] + 14 + LINK_OFF_SESS); }

static int tx_next(int from, u8 op)
{
    int i;
    for (i = from; i < txn; i++) if (tx_op(i) == op) return i;
    return -1;
}

static int tx_count(u8 op)
{
    int i, n = 0;
    for (i = 0; i < txn; i++) if (tx_op(i) == op) n++;
    return n;
}

static void tx_clear(void) { txn = 0; }

/* ======================================================================== */
/*  台本の Agent (フレームを手で返す)                                        */
/* ======================================================================== */
#define SCRIPT_SESS   7
#define SCRIPT_AGENT  0x1234
#define SCRIPT_NONCE  0xA5A5A5A5u

static u16 script_epoch;

/* SYN を待ち、SYN-ACK → CONFIRM → ESTABLISHED まで通す。 */
static void script_handshake(u16 sess, u16 want_epoch)
{
    u8 pl[6];
    int i, syn;
    u32 nonce;

    for (i = 0; i < 40 && (syn = tx_next(0, LINK_OP_HELLO)) < 0; i++) host_tick();
    syn = tx_next(0, LINK_OP_HELLO);
    check(syn >= 0 && tx_flags(syn) == LINK_HS_SYN, "SYN が出ない");
    if (syn < 0) return;
    nonce = tx_seq(syn);
    script_epoch = want_epoch ? want_epoch : tx_epoch(syn);
    wr16(pl, SCRIPT_AGENT);
    wr16(pl + 2, tx_sess(syn));          /* req_sess の写し */
    wr16(pl + 4, tx_epoch(syn));         /* req_epoch の写し */
    inject(LINK_OP_HELLO, LINK_HS_SYNACK, script_epoch, nonce, SCRIPT_NONCE,
           0, sess, pl, 6);
    host_tick();
    i = tx_next(syn + 1, LINK_OP_HELLO);
    check(i >= 0 && tx_flags(i) == LINK_HS_CONFIRM, "CONFIRM が出ない");
    if (i < 0) return;
    check(tx_ack(i) == SCRIPT_NONCE, "CONFIRM に Agent nonce の写しが無い");
    wr16(pl, SCRIPT_AGENT);
    inject(LINK_OP_HELLO, LINK_HS_ESTAB, script_epoch, SCRIPT_NONCE, nonce,
           0, sess, pl, 2);
    host_tick();
    check(link_hello_ok == 1, "ESTABLISHED で確立しない");
}

static void reset_all(const char *name)
{
    cur_case = name;
    memset(nic_rx, 0, sizeof(nic_rx));
    nic_rxh = nic_rxt = 0;
    nic_up = 1; nic_busy = 0; nic_tx_allow = 1;
    nic_ring_pages = 120; nic_queue_free = 30;
    memset(&nic_stats, 0, sizeof(nic_stats));
    txn = 0;
    tx_hook = 0;
    wire_pump_fn = 0;
    inject_on_send = 0; inject_len = 0;
    irq_depth = 0; timer_deferred = 0; timer_ran = 0;
    tick_count = 1000;
    host_cpl3 = 0; fault_kills = 0; host_owner = 3;
    link_hello_ok = link_rt_ok = link_rt_fail = link_retransmits = 0;
    link_rx_frames = link_rx_dropped = 0;
    link_resyncs = link_tombstones = link_no_slots = link_processing = 0;
    link_tx_deferred = 0;
    link_l2_read = link_l2_bytes = link_l2_gaps = link_l2_bad = 0;
    link_l2_eof = link_l2_overflow = 0;
    link_init(my_mac);
}

/* 業務 RESPONSE / 制御 RESPONSE の payload を組む */
static void resp_pl(u8 *pl, u16 status, u32 len)
{
    wr16(pl, status);
    wr32(pl + 2, len);
}

/* ======================================================================== */
/*  実 Agent (tools/host_agent.py を --unix でサブプロセス起動)              */
/* ======================================================================== */
static pid_t agent_pid = -1;
static int   agent_fd = -1;
static char  agent_sock[128];
static char  agent_state[128];
static u8    wire_in[1 << 16];
static size_t wire_inlen;

static void agent_tx(const u8 *f, unsigned int n)
{
    u8 hdr[4];
    hdr[0] = (u8)(n >> 24); hdr[1] = (u8)(n >> 16);
    hdr[2] = (u8)(n >> 8);  hdr[3] = (u8)n;
    if (agent_fd < 0) return;
    if (write(agent_fd, hdr, 4) != 4) return;
    if (write(agent_fd, f, n) != (ssize_t)n) return;
}

static void agent_pump(void)
{
    struct pollfd pf;
    int rounds;
    if (agent_fd < 0) return;
    for (rounds = 0; rounds < 8; rounds++) {
        ssize_t k;
        pf.fd = agent_fd; pf.events = POLLIN; pf.revents = 0;
        if (poll(&pf, 1, 3) <= 0) break;
        k = read(agent_fd, wire_in + wire_inlen, sizeof(wire_in) - wire_inlen);
        if (k <= 0) break;
        wire_inlen += (size_t)k;
    }
    while (wire_inlen >= 4) {
        size_t n = ((size_t)wire_in[0] << 24) | ((size_t)wire_in[1] << 16) |
                   ((size_t)wire_in[2] << 8) | (size_t)wire_in[3];
        if (wire_inlen < 4 + n) break;
        nic_push_rx(wire_in + 4, (unsigned int)n);
        memmove(wire_in, wire_in + 4 + n, wire_inlen - 4 - n);
        wire_inlen -= 4 + n;
    }
}

static int agent_start(void)
{
    struct sockaddr_un sa;
    int i, fd;
    snprintf(agent_sock, sizeof(agent_sock), "/tmp/os32-n1-%d.sock", (int)getpid());
    snprintf(agent_state, sizeof(agent_state), "/tmp/os32-n1-%d.state", (int)getpid());
    unlink(agent_sock);
    if (mkdir(agent_state, 0700) != 0 && errno != EEXIST) return -1;
    agent_pid = fork();
    if (agent_pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); }
        execlp("python3", "python3", "-B", "tools/host_agent.py",
               "--unix", agent_sock, "--state-dir", agent_state,
               "--agent-gen", "4660", "--quiet", "--offline", (char *)NULL);
        _exit(127);
    }
    if (agent_pid < 0) return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, agent_sock, sizeof(sa.sun_path) - 1);
    for (i = 0; i < 200; i++) {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
            agent_fd = fd;
            wire_inlen = 0;
            tx_hook = agent_tx;
            wire_pump_fn = agent_pump;
            return 0;
        }
        if (fd >= 0) close(fd);
        usleep(20000);
    }
    return -1;
}

static void agent_stop(void)
{
    if (agent_fd >= 0) { close(agent_fd); agent_fd = -1; }
    if (agent_pid > 0) { kill(agent_pid, SIGTERM); waitpid(agent_pid, 0, 0); agent_pid = -1; }
    unlink(agent_sock);
    if (agent_state[0]) {
        char f[160];
        snprintf(f, sizeof(f), "%s/sess.txt", agent_state);
        unlink(f);
        rmdir(agent_state);
        agent_state[0] = '\0';
    }
    tx_hook = 0;
    wire_pump_fn = 0;
}

/* 実 Agent と HELLO を通す (link_tick が SYN を出し、Agent が答える)。 */
static int agent_up(int max_ticks)
{
    int i;
    for (i = 0; i < max_ticks && !link_hello_ok; i++) host_tick();
    return link_hello_ok != 0;
}

/* AGAIN の間タイマを回す小道具 (自己試験と同じ待ち方 = hlt 相当)。 */
static i32 wait_status(i32 h, u32 *st, u32 *ln, int max_ticks)
{
    int i;
    i32 rc = OS32_ERR_AGAIN;
    for (i = 0; i < max_ticks; i++) {
        rc = link_host_status(h, st, ln, host_owner);
        if (rc != OS32_ERR_AGAIN) return rc;
        host_tick();
    }
    return rc;
}

static u32 read_all(i32 h, u8 *out, u32 cap, int max_ticks)
{
    u32 got = 0;
    int i;
    for (i = 0; i < max_ticks; i++) {
        const u8 *src = 0;
        i32 n = link_host_read_stage(h, cap - got, &src, host_owner);
        if (n == 0) return got;
        if (n > 0) { memcpy(out + got, src, (size_t)n); got += (u32)n; continue; }
        if (n != OS32_ERR_AGAIN) return got;
        host_tick();
    }
    return got;
}

/* =========================================================================
 *  ケース
 * ========================================================================= */

/* ---- 往復 2 --------------------------------------------------------- */

/* R1: 非ゼロの agent 世代での HELLO → REQUEST → RESPONSE → 本文完了 (実 Agent) */
static void r2_R1_agent_request_response_body(void)
{
    u8 buf[4096];
    u32 st = 0, ln = 0, got;
    i32 h;
    u32 k;

    if (agent_start() != 0) { check(0, "実 Agent を起動できない"); return; }
    check(agent_up(200), "実 Agent と HELLO が確立しない");
    check(link_agent_gen == 4660, "agent 世代 (非ゼロ) が控えられていない");
    check(link_sess != 0, "sess が Agent から採番されていない");

    h = link_host_open("GET /pattern/2048", 17, host_owner);
    check(h >= 0, "host_open が失敗した");
    if (h < 0) { agent_stop(); return; }
    check(wait_status(h, &st, &ln, 300) == 0, "RESPONSE が届かない");
    check(st == 200 && ln == 2048, "status / length が違う");
    got = read_all(h, buf, sizeof(buf), 600);
    check(got == 2048, "本文の長さが違う");
    for (k = 0; k < got && k < 2048; k++)
        if (buf[k] != (u8)k) { check(0, "本文の内容が違う"); break; }
    check(link_host_close(h, host_owner) == 0, "close が失敗した");
    agent_stop();
}

/* R8: 自己試験 (非同期 API + hlt 待ち) の実行中もタイマだけが状態を進める */
static void r2_R8_selftest_is_driven_only_by_the_timer(void)
{
    u32 ran0;
    i32 h;
    u32 st = 0, ln = 0;

    if (agent_start() != 0) { check(0, "実 Agent を起動できない"); return; }
    check(agent_up(200), "HELLO が確立しない");
    ran0 = timer_ran;
    h = link_host_open("PING", 4, host_owner);
    check(h >= 0, "open が失敗した");
    /* KAPI を何度呼んでも tick は増えない = KAPI は link_tick を呼ばない */
    check(link_host_status(h, &st, &ln, host_owner) == OS32_ERR_AGAIN, "即答した");
    check(link_host_status(h, &st, &ln, host_owner) == OS32_ERR_AGAIN, "即答した");
    check(timer_ran == ran0, "KAPI が link_tick を呼んでいる");
    check(wait_status(h, &st, &ln, 300) == 0, "タイマで進まない");
    check(st == 200, "PING の status が 200 でない");
    link_host_close(h, host_owner);
    agent_stop();
}

/* R3: 転送 ACK 後の RESPONSE 消失 → T_probe の STATUS で再提示 (0 長も) */
static void r2_R3_status_probe_repeats_lost_response(void)
{
    u8 pl[8];
    i32 h;
    u32 st = 99, ln = 99;
    int s;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("PING", 4, host_owner);
    check(h >= 0, "open が失敗した");
    ticks(2);
    check(tx_next(0, LINK_OP_REQUEST) >= 0, "REQUEST が出ない");
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    /* RESPONSE は消えたことにして、T_probe を過ぎるまで回す */
    tx_clear();
    ticks(LINK_PROBE_TICKS + 3);
    s = tx_next(0, LINK_OP_STATUS);
    check(s >= 0, "T_probe を過ぎても STATUS が出ない");
    check(s < 0 || tx_rid(s) == 1, "STATUS の rid が違う");
    /* 0 長の成功応答を再提示 */
    resp_pl(pl, 200, 0);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    check(link_host_status(h, &st, &ln, host_owner) == 0, "再提示を受け取れない");
    check(st == 200 && ln == 0, "0 長の成功応答が拾えない");
    link_host_close(h, host_owner);
}

/* R4: A / B の close 順に関わらず RELEASE が正しい rid を載せる */
static void r2_R4_release_carries_the_right_rid(void)
{
    i32 a, b;
    int i, seen_a = 0, seen_b = 0;

    script_handshake(SCRIPT_SESS, 0);
    a = link_host_open("PING", 4, host_owner);
    b = link_host_open("TIME", 4, host_owner);
    check(a >= 0 && b >= 0, "2 本開けない");
    ticks(4);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, 0, 0);
    ticks(2);
    tx_clear();
    link_host_close(b, host_owner);        /* B を先に閉じる */
    link_host_close(a, host_owner);
    ticks(6);
    for (i = 0; i < txn; i++) {
        if (tx_op(i) != LINK_OP_RELEASE) continue;
        if (tx_rid(i) == 1) seen_a = 1;
        if (tx_rid(i) == 2) seen_b = 1;
    }
    check(seen_a && seen_b, "閉じた 2 本ぶんの RELEASE が出ない");
}

/* R9: credit 0 の A と B の並行。NIC が 1 tick 1 フレームでも制御と通常が
 *     交互に出て、どちらも飢えない。 */
static void r2_R9_alternating_tx_one_frame_per_tick(void)
{
    u8 pl[8];
    i32 a, b;
    int i, ctrl = 0, norm = 0, over = 0;
    u32 t_prev = 0;

    script_handshake(SCRIPT_SESS, 0);
    a = link_host_open("GET /pattern/8192", 17, host_owner);
    b = link_host_open("ECHO 4096", 9, host_owner);
    check(a >= 0 && b >= 0, "2 本開けない");
    ticks(4);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, 8192);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(3);
    /* A はリングを取って WINDOW を出し続ける (credit は出るが DATA は来ない)。
     * B は WDATA を積む。両方が出せる状態にして 1 tick 1 フレームを確かめる。 */
    {
        const u8 *src = 0;
        (void)link_host_read_stage(a, 1400, &src, host_owner);
    }
    check(link_host_write(b, "abcd", 4, host_owner) == 4, "host_write が受け付けない");
    tx_clear();
    ticks(12);
    for (i = 0; i < txn; i++) {
        u8 op = tx_op(i);
        if (op == LINK_OP_WINDOW || op == LINK_OP_ACK || op == LINK_OP_STATUS ||
            op == LINK_OP_RELEASE) ctrl++;
        else norm++;
    }
    check(ctrl > 0, "制御フレームが 1 本も出ない (飢餓)");
    check(norm > 0, "通常フレームが 1 本も出ない (飢餓)");
    check(txn <= 12, "1 tick に 2 本以上 NIC へ渡した");
    check(link_tx_deferred > 0, "NIC が受けなかった周回が記録されていない");
    (void)t_prev; (void)over;
}

/* R10: 反射モードでは link_init を呼ばないので link_tick が何もしない */
static void r2_R10_reflect_mode_does_not_run_link_tick(void)
{
    link_ready = 0;                        /* 反射モード = link_init されていない */
    txn = 0;
    ticks(50);
    check(txn == 0, "link_init 前に link_tick がフレームを出した");
    check(nic_rxh == nic_rxt, "link_tick が RX キューを消費した");
}

/* ---- 往復 3 --------------------------------------------------------- */

/* B1: RELEASE 消失 + 旧 REQUEST の ACK (flags 0) 到着でも RELEASE の再送は
 *     止まらない (完了は flags bit0 の ACK だけ)。 */
static void r3_B1_release_resends_until_bit0_ack(void)
{
    i32 h;
    int before, after;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("PING", 4, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    link_host_close(h, host_owner);
    ticks(2);
    tx_clear();
    /* 旧 REQUEST の ACK (flags 0) が遅れて届く = RELEASE の ACK ではない */
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(LINK_RTO_TICKS + 3);
    before = tx_count(LINK_OP_RELEASE);
    check(before >= 1, "flags 0 の ACK で RELEASE の再送が止まった");
    /* bit0 の ACK を返すと止まる */
    inject(LINK_OP_ACK, LINK_F_RELACK, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    tx_clear();
    ticks(LINK_RTO_TICKS * 3);
    after = tx_count(LINK_OP_RELEASE);
    check(after == 0, "bit0 の ACK の後も RELEASE を送り続けた");
}

/* B6: STALE のハンドルの close は RELEASE を送らない (新 epoch の rid を
 *     解放してしまわない) */
static void r3_B6_stale_close_sends_no_release(void)
{
    i32 h;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("PING", 4, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    link_resync();                          /* 再同期 = 生存ハンドルは STALE */
    check(link_host_status(h, 0, 0, host_owner) == OS32_ERR_STALE, "STALE にならない");
    tx_clear();
    check(link_host_close(h, host_owner) == 0, "STALE の close が失敗した");
    ticks(LINK_RTO_TICKS * 2);
    check(tx_count(LINK_OP_RELEASE) == 0, "STALE の close が RELEASE を送った");
    check(link_host_close(h, host_owner) == OS32_ERR_INVAL, "二重 close が通った");
}

/* B7: 業務の 503 / 本文付き 410 が呼び手に届き、制御 (NO_SLOT / TOMBSTONE)
 *     と混ざらない。 */
static void r3_B7_business_status_vs_control(void)
{
    u8 pl[8];
    i32 h;
    u32 st = 0, ln = 0;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("GET /x", 6, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    /* 制御 NO_SLOT (status = 3) は業務結果にならない */
    resp_pl(pl, LINK_CTL_NO_SLOT, 0);
    inject(LINK_OP_RESPONSE, LINK_F_CTRL, script_epoch, 0, 0, 1, SCRIPT_SESS,
           pl, LINK_RESP_LEN);
    ticks(2);
    check(link_host_status(h, &st, &ln, host_owner) == OS32_ERR_AGAIN,
          "制御 NO_SLOT を業務結果として拾った");
    check(link_no_slots == 1, "NO_SLOT が数えられていない");
    /* 業務の 503 */
    resp_pl(pl, 503, 0);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    check(link_host_status(h, &st, &ln, host_owner) == 0, "業務 503 が届かない");
    check(st == 503 && ln == 0, "503 の中身が違う");
    link_host_close(h, host_owner);
    ticks(2);

    /* 本文付きの業務 410 */
    h = link_host_open("GET /y", 6, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 410, 9);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    st = 0; ln = 0;
    check(link_host_status(h, &st, &ln, host_owner) == 0, "業務 410 が届かない");
    check(st == 410 && ln == 9, "本文付き 410 の中身が違う");
    link_host_close(h, host_owner);
}

/* 制御 TOMBSTONE を受けたハンドルは STALE になる (呼び手は close するだけ) */
static void r3_tombstone_makes_the_handle_stale(void)
{
    u8 pl[8];
    i32 h;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("PING", 4, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    resp_pl(pl, LINK_CTL_TOMBSTONE, 0);
    inject(LINK_OP_RESPONSE, LINK_F_CTRL, script_epoch, 0, 0, 1, SCRIPT_SESS,
           pl, LINK_RESP_LEN);
    ticks(2);
    check(link_tombstones == 1, "TOMBSTONE が数えられていない");
    check(link_host_status(h, 0, 0, host_owner) == OS32_ERR_STALE, "STALE にならない");
    {
        const u8 *src = 0;
        check(link_host_read_stage(h, 16, &src, host_owner) == OS32_ERR_STALE,
              "STALE のハンドルで read が通った");
    }
    check(link_host_close(h, host_owner) == 0, "STALE の close が通らない");
}

/* 制御 NO_SLOT は RELEASE → REQUEST の順で再送させる */
static void r3_no_slot_resends_release_then_request(void)
{
    u8 pl[8];
    i32 h, h2;
    int rel, req;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("PING", 4, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    link_host_close(h, host_owner);          /* RELEASE が未 ACK のまま残る */
    ticks(2);
    h2 = link_host_open("TIME", 4, host_owner);
    check(h2 >= 0, "もう 1 本が開けない");
    ticks(3);
    tx_clear();
    resp_pl(pl, LINK_CTL_NO_SLOT, 0);
    inject(LINK_OP_RESPONSE, LINK_F_CTRL, script_epoch, 0, 0, 2, SCRIPT_SESS,
           pl, LINK_RESP_LEN);
    ticks(6);
    rel = tx_next(0, LINK_OP_RELEASE);
    req = tx_next(0, LINK_OP_REQUEST);
    check(rel >= 0, "NO_SLOT の後に RELEASE を再送しない");
    check(req >= 0, "NO_SLOT の後に REQUEST を再送しない");
    check(rel < 0 || req < 0 || rel < req, "REQUEST が RELEASE より先に出た");
}

/* B8: 先頭帯外ポインタはディスパッチャが kill、先頭帯内 + 長さ超過は
 *     ラッパーが INVAL (どちらもラッパー経由で踏む) */
static void r3_B8_dispatcher_kill_and_wrapper_inval(void)
{
    static u8 band[4096];
    u32 args[3];
    i32 rc;

    script_handshake(SCRIPT_SESS, 0);
    user_lo = band;
    user_hi = band + sizeof(band);
    host_cpl3 = 1;
    memcpy(band, "PING", 4);

    /* (1) 先頭が帯外 → ディスパッチャが kill (ラッパーへ入らない) */
    args[0] = 0xDEADBEEFu; args[1] = 4;
    check(!dispatch_early((u16)ARGPTR_HOST_OPEN, args, 2), "帯外ポインタが通った");
    check(fault_kills == 1, "kill が数えられていない");

    /* (2) 先頭は帯内、長さが帯を越える → ラッパーが INVAL */
    args[0] = (u32)(unsigned long)band; args[1] = (u32)sizeof(band) + 16;
    check(dispatch_early((u16)ARGPTR_HOST_OPEN, args, 2), "帯内の先頭で kill された");
    rc = kapi_host_open((const char *)band, (u32)sizeof(band) + 16);
    check(rc == OS32_ERR_INVAL, "帯を越える長さが INVAL にならない");
    check(kapi_host_open((const char *)band, LINK_MAX_PAYLOAD + 1) == OS32_ERR_INVAL,
          "1400B 超が INVAL にならない");
    /* 帯の末尾 1B から 16B 読もうとする = 範囲外 */
    check(kapi_host_open((const char *)(band + sizeof(band) - 1), 16) == OS32_ERR_INVAL,
          "長さが帯を越えたのに通った");

    /* (3) host_status の出力ポインタは **全部検証してから書く** */
    {
        u32 good = 0xAAAAAAAAu;
        u32 *bad = (u32 *)(void *)(band + sizeof(band) - 2);   /* 4B 入らない */
        i32 h = link_host_open("PING", 4, host_owner);
        u8 pl[8];
        ticks(3);
        inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
        resp_pl(pl, 200, 0);
        inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
        ticks(2);
        memcpy(band, &good, 4);
        check(kapi_host_status(h, (u32 *)(void *)band, bad) == OS32_ERR_INVAL,
              "2 本目の出力ポインタが帯外でも INVAL にならない");
        check(memcmp(band, &good, 4) == 0, "失敗したのに 1 本目の出力を書いた");
        link_host_close(h, host_owner);
    }
    host_cpl3 = 0;
}

/* ---- 往復 4 --------------------------------------------------------- */

/* R4: ユーザーコピー中に再同期が起きても、戻り値と buf の内容が一致する
 *     (成功確定点は最初の cli 区間)。次の呼び出しが STALE。 */
static void r4_R4_resync_during_user_copy_keeps_the_return_value(void)
{
    u8 pl[8];
    u8 body[600];
    u8 out[600];
    const u8 *src = 0;
    i32 h, n;
    unsigned int i;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("GET /x", 6, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, sizeof(body));
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    for (i = 0; i < sizeof(body); i++) body[i] = (u8)i;
    /* リングを取る (最初の read は AGAIN) */
    check(link_host_read_stage(h, 512, &src, host_owner) == OS32_ERR_AGAIN, "");
    ticks(2);
    inject(LINK_OP_DATA, 0, script_epoch, 1, 0, 1, SCRIPT_SESS, body, sizeof(body));
    ticks(2);
    n = link_host_read_stage(h, sizeof(out), &src, host_owner);
    check(n == (i32)sizeof(body), "本文が 1 回で取れない");
    /* ここで (= ユーザー領域への写しの前に) 再同期が起きる */
    link_resync();
    memcpy(out, src, (size_t)n);
    for (i = 0; i < (unsigned int)n; i++)
        if (out[i] != (u8)i) { check(0, "確定した中身が壊れた"); break; }
    check(link_host_read_stage(h, 16, &src, host_owner) == OS32_ERR_STALE,
          "次の呼び出しが STALE にならない");
    link_host_close(h, host_owner);
}

/* HELLO 各段階の消失: SYN / SYN-ACK / CONFIRM / ESTABLISHED */
static void r4_hello_stage_losses(void)
{
    u8 pl[6];
    int syn, i, c1;
    u32 nonce;

    /* SYN-ACK が落ちた → RTO ごとに SYN を出し直す (nonce は +1) */
    ticks(2);
    syn = tx_next(0, LINK_OP_HELLO);
    check(syn >= 0, "SYN が出ない");
    nonce = tx_seq(syn);
    ticks(LINK_RTO_TICKS + 2);
    i = tx_next(syn + 1, LINK_OP_HELLO);
    check(i >= 0 && tx_flags(i) == LINK_HS_SYN, "SYN を再送しない");
    check(i < 0 || tx_seq(i) != nonce, "SYN 再送で nonce が進んでいない");

    /* 新しい nonce に SYN-ACK を返す → CONFIRM */
    nonce = tx_seq(i);
    wr16(pl, SCRIPT_AGENT);
    wr16(pl + 2, tx_sess(i));
    wr16(pl + 4, tx_epoch(i));
    inject(LINK_OP_HELLO, LINK_HS_SYNACK, tx_epoch(i), nonce, SCRIPT_NONCE, 0,
           SCRIPT_SESS, pl, 6);
    host_tick();
    c1 = tx_next(i + 1, LINK_OP_HELLO);
    check(c1 >= 0 && tx_flags(c1) == LINK_HS_CONFIRM, "CONFIRM が出ない");
    check(link_hello_ok == 0, "ESTABLISHED 前に確立した");

    /* ESTABLISHED が落ちた → CONFIRM を再送する */
    tx_clear();
    ticks(LINK_RTO_TICKS + 2);
    check(tx_count(LINK_OP_HELLO) >= 1, "ESTABLISHED 待ちで CONFIRM を再送しない");
    check(tx_flags(tx_next(0, LINK_OP_HELLO)) == LINK_HS_CONFIRM, "再送が CONFIRM でない");

    /* 遅延した SYN-ACK (古い nonce) は採用しない */
    wr16(pl, SCRIPT_AGENT);
    wr16(pl + 2, 0);
    wr16(pl + 4, 1);
    inject(LINK_OP_HELLO, LINK_HS_SYNACK, 9, nonce - 1, 0xBBBBBBBBu, 0, 999, pl, 6);
    host_tick();
    check(link_sess == SCRIPT_SESS, "古い SYN-ACK で sess が動いた");

    /* 正しい ESTABLISHED で確立 */
    wr16(pl, SCRIPT_AGENT);
    inject(LINK_OP_HELLO, LINK_HS_ESTAB, link_epoch, SCRIPT_NONCE, nonce, 0,
           SCRIPT_SESS, pl, 2);
    host_tick();
    check(link_hello_ok == 1, "ESTABLISHED で確立しない");
}

/* R6: 2 ハンドル ACK 済みで Agent が黙る → STATUS 無応答 → 再同期 → STALE */
static void r6_agent_silence_resyncs_and_stales_handles(void)
{
    i32 a, b;

    script_handshake(SCRIPT_SESS, 0);
    a = link_host_open("PING", 4, host_owner);
    b = link_host_open("TIME", 4, host_owner);
    ticks(4);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, 0, 0);
    ticks(3);
    check(link_resyncs == 0, "まだ再同期してはいけない");
    /* Agent が黙ったまま T_probe × (k+1) 回 */
    ticks(LINK_PROBE_TICKS * (LINK_PROBE_MAX + 2));
    check(link_resyncs >= 1, "無応答でも再同期しない");
    check(link_host_status(a, 0, 0, host_owner) == OS32_ERR_STALE, "A が STALE でない");
    check(link_host_status(b, 0, 0, host_owner) == OS32_ERR_STALE, "B が STALE でない");
    check(tx_count(LINK_OP_STATUS) >= LINK_PROBE_MAX, "STATUS を出していない");
    check(link_host_close(a, host_owner) == 0 && link_host_close(b, host_owner) == 0,
          "STALE の close が通らない");
}

/* CPL=0 経路 (ディスパッチャを通らない直呼び) の open / close */
static void r4_cpl0_open_close(void)
{
    i32 h;
    host_cpl3 = 0;                            /* CPL=0 = 範囲検証は素通し */
    script_handshake(SCRIPT_SESS, 0);
    h = kapi_host_open("PING", 4);
    check(h >= 0, "CPL=0 の open が失敗した");
    check(kapi_host_close(h) == 0, "CPL=0 の close が失敗した");
    check(kapi_host_close(h) == OS32_ERR_INVAL, "二重 close が通った");
}

/* ---- その他 ---------------------------------------------------------- */

/* op ごとの payload 長と一致しないフレームは捨てる */
static void wire_paylen_mismatch_is_dropped(void)
{
    u8 pl[8];
    u32 dropped0;

    script_handshake(SCRIPT_SESS, 0);
    dropped0 = link_rx_dropped;
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, 2);       /* 0B のはず */
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, 4);  /* 6B のはず */
    inject(LINK_OP_WINDOW, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, 1);    /* 2B のはず */
    inject(LINK_OP_EOF, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, 1);       /* 0B のはず */
    ticks(2);
    check(link_rx_dropped - dropped0 == 4, "payload 長が違うフレームを通した");
}

/* 別 sess / 別 epoch のフレームは捨てる */
static void wire_stale_session_frames_are_dropped(void)
{
    u8 pl[8];
    i32 h;
    u32 st = 0, ln = 0;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("PING", 4, host_owner);
    ticks(3);
    resp_pl(pl, 200, 0);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, (u16)(SCRIPT_SESS + 1),
           pl, LINK_RESP_LEN);
    inject(LINK_OP_RESPONSE, 0, (u16)(script_epoch + 3), 0, 0, 1, SCRIPT_SESS,
           pl, LINK_RESP_LEN);
    ticks(2);
    check(link_host_status(h, &st, &ln, host_owner) == OS32_ERR_AGAIN,
          "旧 sess / 旧 epoch の RESPONSE を採用した");
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    check(link_host_status(h, &st, &ln, host_owner) == 0, "現行の RESPONSE まで捨てた");
    link_host_close(h, host_owner);
}

/* IF=0 中のタイマは延期され、復元直後の tick が処理する */
static void irq_if0_defers_the_timer_then_runs_it(void)
{
    unsigned int f;
    u32 ran0;

    script_handshake(SCRIPT_SESS, 0);
    ran0 = timer_ran;
    f = link_test_irq_save();
    host_tick();
    host_tick();
    check(timer_ran == ran0, "IF=0 中にタイマが走った");
    check(timer_deferred == 2, "延期が数えられていない");
    link_test_irq_restore(f);
    check(timer_ran == ran0 + 1, "復元直後の tick が走らない");
}

/* リング更新 / TX 構築の途中に贋 IRQ5 がフレームを積んでも壊れない */
static void irq_injection_during_tx_build(void)
{
    u8 pl[8];
    u8 body[300];
    const u8 *src = 0;
    i32 h, n;
    unsigned int i;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("GET /x", 6, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, sizeof(body));
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    (void)link_host_read_stage(h, 512, &src, host_owner);      /* リングを取る */
    for (i = 0; i < sizeof(body); i++) body[i] = (u8)(i + 1);
    /* 次に link_tick が TX を組む最中に、IRQ5 が DATA を積む */
    inject_len = mkframe(inject_frame, LINK_OP_DATA, 0, script_epoch, 1, 0, 1,
                         SCRIPT_SESS, body, sizeof(body));
    inject_on_send = 1;
    ticks(4);
    n = link_host_read_stage(h, sizeof(body), &src, host_owner);
    check(n == (i32)sizeof(body), "割込み注入で DATA が落ちた");
    for (i = 0; i < (unsigned int)n && i < sizeof(body); i++)
        if (src[i] != (u8)(i + 1)) { check(0, "注入した DATA の中身が違う"); break; }
    link_host_close(h, host_owner);
}

/* 最後のバイトを写した呼び出しは正の長さ、**その次**が 0 */
static void read_last_byte_then_zero(void)
{
    u8 pl[8];
    u8 body[64];
    const u8 *src = 0;
    i32 h, n;
    unsigned int i;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("GET /x", 6, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, sizeof(body));
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    (void)link_host_read_stage(h, 64, &src, host_owner);
    for (i = 0; i < sizeof(body); i++) body[i] = (u8)i;
    inject(LINK_OP_DATA, 0, script_epoch, 1, 0, 1, SCRIPT_SESS, body, sizeof(body));
    ticks(2);
    n = link_host_read_stage(h, sizeof(body), &src, host_owner);
    check(n == (i32)sizeof(body), "最後のバイトを含む呼び出しが正の長さを返さない");
    check(link_host_read_stage(h, 16, &src, host_owner) == 0, "次の呼び出しが 0 でない");
    link_host_close(h, host_owner);
}

/* リングの所有は最初に読んだ側が close まで持つ (他方は AGAIN) */
static void ring_owner_is_exclusive_until_close(void)
{
    u8 pl[8];
    const u8 *src = 0;
    i32 a, b;

    script_handshake(SCRIPT_SESS, 0);
    a = link_host_open("GET /a", 6, host_owner);
    b = link_host_open("GET /b", 6, host_owner);
    ticks(4);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, 100);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(3);
    check(link_host_read_stage(a, 64, &src, host_owner) == OS32_ERR_AGAIN, "");
    check(link_host_read_stage(b, 64, &src, host_owner) == OS32_ERR_AGAIN,
          "2 本目がリングを奪った");
    link_host_close(a, host_owner);
    ticks(2);
    check(link_host_read_stage(b, 64, &src, host_owner) == OS32_ERR_AGAIN,
          "close の後も 2 本目がリングを取れない");
}

/* host_write の契約: 宣言長が無い / 残りを超える / ACK 前 / 1 本ずつ */
static void write_contract_declared_length(void)
{
    i32 h, g;

    script_handshake(SCRIPT_SESS, 0);
    /* 宣言長の無い要求 */
    h = link_host_open("PING", 4, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    check(link_host_write(h, "x", 1, host_owner) == OS32_ERR_INVAL,
          "宣言長の無い要求に書けた");
    link_host_close(h, host_owner);
    ticks(2);

    /* 宣言長のある要求 */
    g = link_host_open("ECHO 6", 6, host_owner);
    check(g >= 0, "ECHO が開けない");
    check(link_host_write(g, "abc", 3, host_owner) == OS32_ERR_AGAIN,
          "転送 ACK の前に書けた");
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, 0, 0);
    ticks(2);
    check(link_host_write(g, "abcdefgh", 8, host_owner) == OS32_ERR_INVAL,
          "残り宣言長を超えたのに受け付けた");
    check(link_host_write(g, "abc", 3, host_owner) == 3, "1 本目が受け付けられない");
    check(link_host_write(g, "def", 3, host_owner) == OS32_ERR_AGAIN,
          "未 ACK の WDATA があるのに 2 本目を受け付けた");
    ticks(3);
    check(tx_count(LINK_OP_WDATA) >= 1, "WDATA が出ない");
    inject(LINK_OP_ACK, 0, script_epoch, 0, 1, 2, SCRIPT_SESS, 0, 0);
    ticks(2);
    check(link_host_write(g, "def", 3, host_owner) == 3, "ACK 後も 2 本目が通らない");
    check(link_host_write(g, "x", 1, host_owner) == OS32_ERR_INVAL,
          "宣言長を使い切ったのに受け付けた");
    link_host_close(g, host_owner);
}

/* 宣言長の読み取り (ECHO / CLIP PUT / PRINT DATA / PUT) */
static void decl_len_parsing(void)
{
    check(link_decl_len("ECHO 5", 6) == 5, "ECHO");
    check(link_decl_len("ECHO 0", 6) == 0, "ECHO 0");
    check(link_decl_len("GET /pattern/65536", 18) == 0, "GET に宣言長が付いた");
    check(link_decl_len("CLIP PUT 4096", 13) == 4096, "CLIP PUT");
    check(link_decl_len("CLIP GET", 8) == 0, "CLIP GET に宣言長が付いた");
    check(link_decl_len("PRINT DATA 7 4096", 17) == 4096, "PRINT DATA");
    check(link_decl_len("PRINT OPEN rep text", 19) == 0, "PRINT OPEN に宣言長が付いた");
    check(link_decl_len("PUT /file/x 12", 14) == 12, "PUT");
    check(link_decl_len("ECHO abc", 8) == 0, "数字でない宣言長を受けた");
    check(link_decl_len("ECHO 99999999", 13) == 0, "64KB 超の宣言長を受けた");
}

/* owner 回収 (親へ戻った状態) */
static void owner_exit_reclaims_handles(void)
{
    i32 a, b;

    script_handshake(SCRIPT_SESS, 0);
    host_owner = 4;
    a = link_host_open("PING", 4, host_owner);
    host_owner = 5;
    b = link_host_open("TIME", 4, host_owner);
    check(a >= 0 && b >= 0, "2 本開けない");
    ticks(4);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 2, SCRIPT_SESS, 0, 0);
    ticks(2);
    tx_clear();
    link_host_owner_exit(4);
    ticks(4);
    check(tx_count(LINK_OP_RELEASE) == 1, "回収で RELEASE が 1 本出ない");
    check(tx_rid(tx_next(0, LINK_OP_RELEASE)) == 1, "回収した rid が違う");
    host_owner = 5;
    check(link_host_status(b, 0, 0, host_owner) == OS32_ERR_AGAIN,
          "別 owner のハンドルまで回収した");
    host_owner = 4;
    check(link_host_status(a, 0, 0, host_owner) == OS32_ERR_INVAL,
          "回収したハンドルがまだ生きている");
}

/* 他人のハンドルは触れない */
static void handles_are_owner_private(void)
{
    i32 h;
    script_handshake(SCRIPT_SESS, 0);
    host_owner = 4;
    h = link_host_open("PING", 4, host_owner);
    check(h >= 0, "open できない");
    host_owner = 5;
    check(link_host_status(h, 0, 0, host_owner) == OS32_ERR_INVAL, "他人が status を読めた");
    check(link_host_close(h, host_owner) == OS32_ERR_INVAL, "他人が close できた");
    host_owner = 4;
    check(link_host_close(h, host_owner) == 0, "持ち主が close できない");
}

/* 空き無し = FULL、NIC 無し = NOSYS、未確立 = STALE */
static void open_error_codes(void)
{
    i32 a, b;

    /* 未確立 (HELLO 前) */
    check(link_host_open("PING", 4, host_owner) == OS32_ERR_STALE,
          "HELLO 前の open が STALE でない");
    script_handshake(SCRIPT_SESS, 0);
    check(link_host_open("", 0, host_owner) == OS32_ERR_INVAL, "0 長が INVAL でない");
    check(link_host_open("x", LINK_MAX_PAYLOAD + 1, host_owner) == OS32_ERR_INVAL,
          "1400B 超が INVAL でない");
    a = link_host_open("PING", 4, host_owner);
    b = link_host_open("TIME", 4, host_owner);
    check(a >= 0 && b >= 0, "2 本開けない");
    check(link_host_open("PING", 4, host_owner) == OS32_ERR_FULL, "3 本目が FULL でない");
    nic_up = 0;
    check(link_host_open("PING", 4, host_owner) == OS32_ERR_NOSYS, "NIC 無しが NOSYS でない");
    nic_up = 1;
    /* RELEASE が未 ACK の間、そのハンドルは AGAIN */
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    link_host_close(a, host_owner);
    check(link_host_open("PING", 4, host_owner) == OS32_ERR_AGAIN,
          "RELEASE 未 ACK のスロットが AGAIN でない");
    inject(LINK_OP_ACK, LINK_F_RELACK, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    check(link_host_open("PING", 4, host_owner) >= 0, "ACK 後も開けない");
}

/* 最終 DATA / EOF が落ちても、STATUS の再提示と WINDOW で埋まる */
static void last_data_and_eof_loss(void)
{
    u8 pl[8];
    u8 body[200];
    const u8 *src = 0;
    i32 h;
    u32 got = 0;
    unsigned int i;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("GET /x", 6, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, 400);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    (void)link_host_read_stage(h, 64, &src, host_owner);
    for (i = 0; i < sizeof(body); i++) body[i] = (u8)i;
    inject(LINK_OP_DATA, 0, script_epoch, 1, 0, 1, SCRIPT_SESS, body, sizeof(body));
    ticks(2);
    /* seq 2 (最終 DATA) が落ちた → 完了しない。EOF だけ来ても完了扱いにしない */
    inject(LINK_OP_EOF, 0, script_epoch, 3, 0, 1, SCRIPT_SESS, 0, 0);
    ticks(2);
    got += (u32)link_host_read_stage(h, sizeof(body), &src, host_owner);
    check(got == sizeof(body), "先に届いた分が読めない");
    check(link_host_read_stage(h, 64, &src, host_owner) == OS32_ERR_AGAIN,
          "EOF だけで完了にした");
    /* 再送された最終 DATA が届けば完了する */
    inject(LINK_OP_DATA, 0, script_epoch, 2, 0, 1, SCRIPT_SESS, body, sizeof(body));
    ticks(2);
    got += (u32)link_host_read_stage(h, sizeof(body), &src, host_owner);
    check(got == 400, "再送で埋まらない");
    check(link_host_read_stage(h, 64, &src, host_owner) == 0, "完了にならない");
    link_host_close(h, host_owner);
}

/* 先行 DATA (gap) は捨てて累積 ACK を止める (Go-Back-N) */
static void data_gap_is_dropped_and_reacked(void)
{
    u8 pl[8];
    u8 body[100];
    const u8 *src = 0;
    i32 h;
    int a1;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("GET /x", 6, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, 300);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    (void)link_host_read_stage(h, 64, &src, host_owner);
    memset(body, 0x5A, sizeof(body));
    tx_clear();
    inject(LINK_OP_DATA, 0, script_epoch, 2, 0, 1, SCRIPT_SESS, body, sizeof(body));
    ticks(3);
    check(link_l2_gaps == 1, "先行 DATA を gap として数えない");
    a1 = tx_next(0, LINK_OP_ACK);
    check(a1 >= 0 && tx_ack(a1) == 0, "gap のときに累積 ACK が進んだ");
    inject(LINK_OP_DATA, 0, script_epoch, 1, 0, 1, SCRIPT_SESS, body, sizeof(body));
    ticks(3);
    a1 = tx_next(0, LINK_OP_ACK);
    while (a1 >= 0 && tx_ack(a1) == 0) a1 = tx_next(a1 + 1, LINK_OP_ACK);
    check(a1 >= 0 && tx_ack(a1) == 1, "欠落が埋まっても ACK が進まない");
    link_host_close(h, host_owner);
}

/* ---- N2: カウンタ契約 --------------------------------------------------- */

/* (a) 無ドロップの open→REQUEST→RESPONSE→read→close で再送計上が無い (F4)。
 * 修正前は open で 16・close で 16 の未送信フレームに RTO を課して
 * link_retransmits=32 になっていた (前倒し last_tx_tick / rel_tick + !due 欠落)。*/
static void n2_no_drop_roundtrip_has_zero_retransmits(void)
{
    u8 buf[4096];
    u32 st = 0, ln = 0, got;
    i32 h;

    if (agent_start() != 0) { check(0, "実 Agent を起動できない"); return; }
    check(agent_up(200), "HELLO が確立しない");
    link_retransmits = 0;                 /* HELLO 分は除く。業務往復だけを見る */
    h = link_host_open("GET /pattern/2048", 17, host_owner);
    check(h >= 0, "open が失敗した");
    if (h < 0) { agent_stop(); return; }
    check(wait_status(h, &st, &ln, 300) == 0, "RESPONSE が届かない");
    got = read_all(h, buf, sizeof(buf), 600);
    check(got == 2048, "本文の長さが違う");
    check(link_host_close(h, host_owner) == 0, "close が失敗した");
    ticks(10);                            /* RELEASE 送出 + bit0 ACK (RTO 未満) */
    check(link_retransmits == 0,
          "無ドロップ往復で再送が計上された (F4: 未送信に RTO)");
    agent_stop();
}

/* (b) L0 相当 → L1 相当を連続で回すと link_rt_ok が区間ごとに打ち直される (F2)。
 * L0 の結果は link_l0_ok に隔離され、L1 実行後も保たれる。*/
static void n2_rt_ok_resets_between_selftest_sections(void)
{
    if (agent_start() != 0) { check(0, "実 Agent を起動できない"); return; }
    check(agent_up(200), "HELLO が確立しない");

    link_selftest(3);                     /* L0 相当: PING ×3 */
    check(link_rt_ok == 3, "L0 区間の link_rt_ok が 3 でない");
    check(link_l0_ok == 3, "L0 の往復数が link_l0_ok に隔離されていない");
    check(link_l0_fail == 0, "L0 で失敗往復が出た");

    link_l1_bulk(4, 512);                 /* L1 相当: 入口で link_counters_reset */
    check(link_rt_ok == 1,
          "L1 区間で link_rt_ok が打ち直されず累積している (F2)");
    check(link_l0_ok == 3, "L1 実行後も L0 スナップショットは保たれる");
    check(link_l1_recv == 4, "L1 の受信フレーム数が違う");
    agent_stop();
}

/* F6 [N1-fix2]: 64KB を超えるストリームを実 Agent から完走する。
 * 修正前は要求 total によらず 65536 直後 (ゲストで 66114 B) で静かに停止した。
 * GET /pattern は N3 wget と同じ L3 ストリーム経路 (host_read_stage) を通る。 */
static void n1fix2_stream_over_64k_completes(void)
{
    static u8 buf[200000];
    u32 st = 0, ln = 0, got;
    i32 h;
    u32 k;

    if (agent_start() != 0) { check(0, "実 Agent を起動できない"); return; }
    check(agent_up(200), "HELLO が確立しない");
    h = link_host_open("GET /pattern/200000", 19, host_owner);
    check(h >= 0, "host_open が失敗した");
    if (h < 0) { agent_stop(); return; }
    check(wait_status(h, &st, &ln, 500) == 0, "RESPONSE が届かない");
    check(st == 200 && ln == 200000, "宣言 length が 200000 でない");
    got = read_all(h, buf, sizeof(buf), 400000);
    if (got != 200000) printf("    got=%u (expected 200000)\n", got);
    check(got == 200000, "64KB 超の本文が完走しない (F6)");
    for (k = 0; k < got; k++)
        if (buf[k] != (u8)k) { check(0, "本文の内容が違う"); break; }
    check(link_host_close(h, host_owner) == 0, "close が失敗した");
    agent_stop();
}

/* F6 [N1-fix2]: 実 Agent で L2 STREAM を >64KB 流して完走する (ゲスト検査
 * check-net-l2 = link_l2_stream(131072,512,100) と同じ経路、seq100 で 1 回欠落)。 */
static void n1fix2_l2_stream_over_64k_completes(void)
{
    if (agent_start() != 0) { check(0, "実 Agent を起動できない"); return; }
    check(agent_up(200), "HELLO が確立しない");
    link_l2_stream(200000, 512, 100);
    if (link_l2_read != 200000)
        printf("    L2: read=%u/200000 gaps=%u bad=%u overflow=%u eof=%u recv=%u\n",
               (unsigned)link_l2_read, (unsigned)link_l2_gaps,
               (unsigned)link_l2_bad, (unsigned)link_l2_overflow,
               (unsigned)link_l2_eof, (unsigned)link_l1_recv);
    check(link_l2_read == 200000, "L2 >64KB が完走しない (F6)");
    check(link_l2_bad == 0, "L2 >64KB の内容が壊れた");
    agent_stop();
}

/* F6 [N1-fix2]: 実 Agent で L1 BULK を >64KB 流して完走する (ゲスト検査
 * check-net-l1 = link_l1_bulk(200,512) の 2 倍 = 204800 B)。 */
static void n1fix2_l1_bulk_over_64k_completes(void)
{
    if (agent_start() != 0) { check(0, "実 Agent を起動できない"); return; }
    check(agent_up(200), "HELLO が確立しない");
    link_l1_bulk(400, 512);
    if (link_l1_recv != 400)
        printf("    L1: recv=%u/400 bytes=%u ooo=%u windows=%u\n",
               (unsigned)link_l1_recv, (unsigned)link_l1_bytes,
               (unsigned)link_l1_ooo, (unsigned)link_l1_windows);
    check(link_l1_recv == 400, "L1 >64KB が完走しない (F6)");
    agent_stop();
}

/* F6 [N1-fix2]: 台本で >64KB の DATA を順序どおり注ぎ、OS32 の受理 / length /
 * 完了判定に 65536 の境界が無いことを Agent もタイミングも介さず決定的に見る。
 * length は RESPONSE の宣言値を保ち (u16 に化けない)、EOF ではなく
 * read_bytes==length で完了する (N0 §2c)。 */
static void n1fix2_scripted_inorder_over_64k(void)
{
    u8 pl[8];
    static u8 body[512];
    const u8 *src = 0;
    i32 h;
    u32 total = 70000, got = 0, off = 0;
    unsigned int seq, nframes;
    u32 st = 0, ln = 0;
    int bad = 0;

    script_handshake(SCRIPT_SESS, 0);
    h = link_host_open("GET /big", 8, host_owner);
    ticks(3);
    inject(LINK_OP_ACK, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, 0, 0);
    resp_pl(pl, 200, total);
    inject(LINK_OP_RESPONSE, 0, script_epoch, 0, 0, 1, SCRIPT_SESS, pl, LINK_RESP_LEN);
    ticks(2);
    check(link_host_status(h, &st, &ln, host_owner) == 0 && ln == total,
          "宣言 length が >64KB のまま保たれない");
    /* 最初の read で ring owner を確保する (DATA は ring_owner のハンドルにだけ届く) */
    (void)link_host_read_stage(h, sizeof(body), &src, host_owner);

    nframes = (total + 512 - 1) / 512;
    for (seq = 1; seq <= nframes; seq++) {
        unsigned int k, chunk = (total - off < 512) ? (total - off) : 512;
        for (k = 0; k < chunk; k++) body[k] = (u8)(off + k);
        inject(LINK_OP_DATA, 0, script_epoch, seq, 0, 1, SCRIPT_SESS, body, chunk);
        ticks(1);
        for (;;) {
            i32 n = link_host_read_stage(h, sizeof(body), &src, host_owner);
            u32 j;
            if (n <= 0) break;
            for (j = 0; j < (u32)n; j++) if (src[j] != (u8)(got + j)) bad = 1;
            got += (u32)n;
        }
        off += chunk;
    }
    if (got != total) printf("    scripted: got=%u/%u\n", (unsigned)got, (unsigned)total);
    check(got == total, "順序どおり >64KB を読み切れない (F6: 65536 境界)");
    check(bad == 0, ">64KB の本文パターンが壊れた");
    check(link_host_read_stage(h, 64, &src, host_owner) == 0,
          "read_bytes==length で完了にならない");
    link_host_close(h, host_owner);
}

/* ======================================================================== */
/*  実行                                                                    */
/* ======================================================================== */
struct testcase { const char *name; void (*fn)(void); };

static struct testcase cases[] = {
    { "r2_R1_agent_request_response_body",          r2_R1_agent_request_response_body },
    { "r2_R8_selftest_is_driven_only_by_the_timer", r2_R8_selftest_is_driven_only_by_the_timer },
    { "r2_R3_status_probe_repeats_lost_response",   r2_R3_status_probe_repeats_lost_response },
    { "r2_R4_release_carries_the_right_rid",        r2_R4_release_carries_the_right_rid },
    { "r2_R9_alternating_tx_one_frame_per_tick",    r2_R9_alternating_tx_one_frame_per_tick },
    { "r2_R10_reflect_mode_does_not_run_link_tick", r2_R10_reflect_mode_does_not_run_link_tick },
    { "r3_B1_release_resends_until_bit0_ack",       r3_B1_release_resends_until_bit0_ack },
    { "r3_B6_stale_close_sends_no_release",         r3_B6_stale_close_sends_no_release },
    { "r3_B7_business_status_vs_control",           r3_B7_business_status_vs_control },
    { "r3_tombstone_makes_the_handle_stale",        r3_tombstone_makes_the_handle_stale },
    { "r3_no_slot_resends_release_then_request",    r3_no_slot_resends_release_then_request },
    { "r3_B8_dispatcher_kill_and_wrapper_inval",    r3_B8_dispatcher_kill_and_wrapper_inval },
    { "r4_R4_resync_during_user_copy_keeps_the_return_value",
      r4_R4_resync_during_user_copy_keeps_the_return_value },
    { "r4_hello_stage_losses",                      r4_hello_stage_losses },
    { "r4_cpl0_open_close",                         r4_cpl0_open_close },
    { "r6_agent_silence_resyncs_and_stales_handles", r6_agent_silence_resyncs_and_stales_handles },
    { "wire_paylen_mismatch_is_dropped",            wire_paylen_mismatch_is_dropped },
    { "wire_stale_session_frames_are_dropped",      wire_stale_session_frames_are_dropped },
    { "irq_if0_defers_the_timer_then_runs_it",      irq_if0_defers_the_timer_then_runs_it },
    { "irq_injection_during_tx_build",              irq_injection_during_tx_build },
    { "read_last_byte_then_zero",                   read_last_byte_then_zero },
    { "ring_owner_is_exclusive_until_close",        ring_owner_is_exclusive_until_close },
    { "write_contract_declared_length",             write_contract_declared_length },
    { "decl_len_parsing",                           decl_len_parsing },
    { "owner_exit_reclaims_handles",                owner_exit_reclaims_handles },
    { "handles_are_owner_private",                  handles_are_owner_private },
    { "open_error_codes",                           open_error_codes },
    { "last_data_and_eof_loss",                     last_data_and_eof_loss },
    { "data_gap_is_dropped_and_reacked",            data_gap_is_dropped_and_reacked },
    { "n2_no_drop_roundtrip_has_zero_retransmits",  n2_no_drop_roundtrip_has_zero_retransmits },
    { "n2_rt_ok_resets_between_selftest_sections",  n2_rt_ok_resets_between_selftest_sections },
    { "n1fix2_stream_over_64k_completes",           n1fix2_stream_over_64k_completes },
    { "n1fix2_l2_stream_over_64k_completes",        n1fix2_l2_stream_over_64k_completes },
    { "n1fix2_l1_bulk_over_64k_completes",          n1fix2_l1_bulk_over_64k_completes },
    { "n1fix2_scripted_inorder_over_64k",           n1fix2_scripted_inorder_over_64k }
};

int main(int argc, char **argv)
{
    unsigned int i;
    int ran = 0, bad = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int want = (argc <= 1);
        int k;
        for (k = 1; k < argc; k++) if (strcmp(argv[k], cases[i].name) == 0) want = 1;
        if (!want) continue;
        failures = 0;
        reset_all(cases[i].name);
        cases[i].fn();
        agent_stop();
        ran++;
        if (failures) { bad++; printf("  FAIL %s (%d)\n", cases[i].name, failures); }
        else printf("  ok   %s\n", cases[i].name);
    }
    printf("SUMMARY %d/%d PASS\n", ran - bad, ran);
    return bad ? 1 : 0;
}
