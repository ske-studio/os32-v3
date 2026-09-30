/* ======================================================================== */
/*  SERIALFS_SESSION_HOST.C — fs/serialfs_session.c のホスト試験             */
/*                                                                          */
/*  票 TASK_SERIAL_HOSTFS §1-v3 のセッション (sfs_begin / sfs_end) を、      */
/*  実物の fs/serialfs_session.c・fs/sfs_client.c・fs/sfs_proto.c・         */
/*  drivers/serial.c を #include して回す。偽物はポート I/O (8251 の送信を  */
/*  記録、受信は「届く tick」つきの待ち行列)、tick (hlt のたびに 1 進む)、  */
/*  VFS (mount / umount / fstype)、kprintf (実物の console と同じく          */
/*  serial_putchar へ複写)、常駐シェルの持ち主。                            */
/*                                                                          */
/*  見ること (レビュー往復 1):                                              */
/*   - 決定 11: HELLO に答えが無くても、相手がセッションを始めている前提で  */
/*     **隔離 (500ms 静まるまで、上限 5 秒) を済ませてから**ゲートを下ろす。 */
/*     隔離のあいだに届いたごみは rshell に 1 バイトも渡らない。溜めた      */
/*     文字 (`sfs: no answer to HELLO`) は下ろした後に生で流れる            */
/*   - Fable m6: 2 回目の sfs_send_log から EXIT までに保留へ入った文字は    */
/*     LOG フレームで、EXIT からゲートを下ろすまでに入った文字は生で流れる  */
/*     (どちらも捨てない)                                                   */
/*   - 流れ: HELLO → mount → BYE → LOG → EXIT (code / flags)、断る条件      */
/*   - 隔離の上限に達したら EXIT の flags に NOT_QUIET                       */
/* ======================================================================== */
#define _GNU_SOURCE 1           /* memmem */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define TX_MAX 65536
static unsigned char g_tx[TX_MAX];
static int g_tx_n;

/* ホスト → ゲスト: 届く tick つきの待ち行列 */
#define RXQ_MAX 65536
static struct { unsigned long at; unsigned char b; } g_rxq[RXQ_MAX];
static int g_rxq_head, g_rxq_n;
static int g_cmd_writes;
static int g_if = 1;                    /* _irq_enabled() の答え */

void fake_outp(unsigned port, unsigned val);
unsigned fake_inp(unsigned port);
void fake_halt(void);
int  fake_irq_enabled(void) { return g_if; }

unsigned long sysclk_hz(void) { return 2457600UL; }
int sysclk_is_8mhz(void) { return 0; }
void irq_enable(unsigned int irq) { (void)irq; }
void irq_disable(unsigned int irq) { (void)irq; }

#include "serial.c"
#include "../../drivers/serial_plan.c"
#include "crc32.c"
#include "sfs_proto.c"
#include "sfs_client.c"

void cpu_delay_us(u32 us) { (void)us; }
volatile u32 tick_count;

/* ---- 常駐シェル・VFS・kstring の偽物 ---- */
static int g_owner = 1;                 /* APP_ID_SHELL */
int res_owner_get(void) { return g_owner; }

static char g_fstype[16];               /* /host に付いているもの ("" = 空き) */
static int g_mount_calls, g_umount_calls, g_permit_now, g_permit_at_mount;
static char g_mount_args[3][16];
static SfsClient *g_permit_cli;

#include "serialfs.h"
int vfs_mount(const char *prefix, const char *dev_name, const char *fstype)
{
    g_mount_calls++;
    strncpy(g_mount_args[0], prefix, 15);
    strncpy(g_mount_args[1], dev_name, 15);
    strncpy(g_mount_args[2], fstype, 15);
    g_permit_at_mount = g_permit_now;
    strncpy(g_fstype, fstype, 15);
    return VFS_OK;
}
void vfs_umount(const char *prefix) { (void)prefix; g_umount_calls++; g_fstype[0] = '\0'; }
const char *vfs_fstype(const char *prefix) { (void)prefix; return g_fstype; }
void serialfs_permit(SfsClient *c) { g_permit_cli = c; g_permit_now = 1; }
void serialfs_forbid(void) { g_permit_now = 0; }
int  serialfs_is_mounted(void) { return g_fstype[0] != '\0'; }
int  kstrcmp(const char *a, const char *b) { return strcmp(a, b); }

static char g_kout[8192];               /* kprintf の全文 (画面) */
static int  g_kout_n;
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n, i;
    (void)attr;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    for (i = 0; i < n; i++) {
        if (g_kout_n < (int)sizeof(g_kout) - 1) g_kout[g_kout_n++] = buf[i];
        /* 実物の console.c は rshell 中に serial_putchar で複写する */
        serial_putchar(buf[i]);
    }
    g_kout[g_kout_n] = '\0';
}

#include "serialfs_session.c"

/* ======================================================================== */
/*  偽のホスト                                                              */
/* ======================================================================== */
#define HOST_SID 0x1234ABCDUL
static SfsDec g_hdec;                   /* ゲストの送信を読む */
static int g_answer_hello = 1;
static int g_hello_n;                   /* ゲストが送った HELLO の数 */
static unsigned long g_last_hello_tick;
static unsigned long g_garbage_until;   /* この tick までごみを 1 バイト / tick */
static int g_garbage_forever;
static int g_inject_y;                  /* `sfs: exit=` の LOG が出た瞬間に 'Y' を保留へ */
static int g_inject_z;                  /* EXIT が出た瞬間に 'Z' を保留へ */
static int g_frames_n;
static struct { u8 type; u32 sid; u16 len; u8 payload[SFS_MAX_PAYLOAD]; int end; } g_frames[64];

static void rx_at(unsigned long at, const unsigned char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (g_rxq_n >= RXQ_MAX) return;
        g_rxq[(g_rxq_head + g_rxq_n) % RXQ_MAX].at = at;
        g_rxq[(g_rxq_head + g_rxq_n) % RXQ_MAX].b = b[i];
        g_rxq_n++;
    }
}

static void host_on_frame(const SfsFrame *f)
{
    if (g_frames_n < 64) {
        g_frames[g_frames_n].type = f->type;
        g_frames[g_frames_n].sid = f->sid;
        g_frames[g_frames_n].len = f->len;
        memcpy(g_frames[g_frames_n].payload, f->payload, f->len);
        g_frames[g_frames_n].end = g_tx_n;
        g_frames_n++;
    }
    if (f->type == SFS_T_HELLO) {
        g_hello_n++;
        g_last_hello_tick = tick_count;
        if (g_garbage_until && !g_garbage_forever) {
            /* 最後の試行が切れて隔離に入ってから 100 tick は、まだごみが来る */
            g_garbage_until = tick_count + sfs_trial_ticks(9600) + 100;
        }
        if (g_answer_hello) {
            u8 pl[12], out[SFS_MAX_FRAME];
            u16 n;
            sfs_put32(pl, 0);
            sfs_put16(pl + 4, SFS_VERSION);
            sfs_put16(pl + 6, SFS_MAX_PAYLOAD);
            sfs_put32(pl + 8, sfs_get32(f->payload + 4));
            n = sfs_encode(out, SFS_T_HELLO | SFS_T_RESP, HOST_SID, 0, pl, 12);
            rx_at(tick_count + 2, out, n);
        }
        return;
    }
    if (f->type == SFS_T_LOG && g_inject_y &&
        f->len >= 10 && memmem(f->payload, f->len, "sfs: exit=", 10)) {
        /* 2 回目の sfs_send_log の送信中に ISR の kprintf が入った形 */
        g_inject_y = 0;
        serial_putchar('Y');
    }
    if (f->type == SFS_T_EXIT && g_inject_z) {
        /* EXIT を送ってからゲートを下ろすまでの間に入った形 */
        g_inject_z = 0;
        serial_putchar('Z');
    }
}

void fake_outp(unsigned port, unsigned val)
{
    val &= 0xFF;
    if (port == 0x30 || port == 0x130) {
        SfsFrame f;
        if (g_tx_n < TX_MAX) g_tx[g_tx_n++] = (unsigned char)val;
        if (sfs_dec_feed(&g_hdec, (u8)val, &f) == 1) host_on_frame(&f);
    } else if (port == 0x32) {
        g_cmd_writes++;
    }
}

static int rx_ready(void)
{
    return g_rxq_n > 0 && g_rxq[g_rxq_head].at <= tick_count;
}

unsigned fake_inp(unsigned port)
{
    unsigned r = 0xFF;
    if (port == 0x32) {
        r = 0x01 | (rx_ready() ? 0x02 : 0);
    } else if (port == 0x30) {
        if (rx_ready()) {
            r = g_rxq[g_rxq_head].b;
            g_rxq_head = (g_rxq_head + 1) % RXQ_MAX;
            g_rxq_n--;
        } else {
            r = 0;
        }
    } else if (port == 0x136) {
        r = 0xFF;                      /* FIFO 非搭載 */
    } else if (port == 0x35) {
        r = 0xF8;
    }
    return r;
}

void fake_halt(void)
{
    tick_count++;
    if (g_garbage_forever || tick_count < g_garbage_until) {
        unsigned char g = 'g';
        rx_at(tick_count, &g, 1);
    }
    if (tick_count > 200000UL) {
        fprintf(stderr, "RUNAWAY: virtual clock passed 200000 ticks\n");
        exit(3);
    }
}

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)
static int failed;

static void reset(void)
{
    g_tx_n = 0;
    g_rxq_head = g_rxq_n = 0;
    g_cmd_writes = 0;
    g_if = 1;
    g_owner = 1;
    g_fstype[0] = '\0';
    g_mount_calls = g_umount_calls = g_permit_now = g_permit_at_mount = 0;
    g_kout_n = 0; g_kout[0] = '\0';
    sfs_dec_reset(&g_hdec);
    g_answer_hello = 1;
    g_hello_n = 0;
    g_garbage_until = 0;
    g_garbage_forever = 0;
    g_inject_y = g_inject_z = 0;
    g_frames_n = 0;
    tick_count = 1000;
    serial_gate_set(0);
    serial_init(9600UL);
    g_tx_n = 0;
    g_cmd_writes = 0;
}

/* g_tx の最後のフレームの後ろ (生の文字) */
static const char *raw_after_last_frame(int *len)
{
    int end = g_frames_n ? g_frames[g_frames_n - 1].end : 0;
    *len = g_tx_n - end;
    return (const char *)g_tx + end;
}

static int raw_has(const char *s)
{
    int n;
    const char *p = raw_after_last_frame(&n);
    return memmem(p, (size_t)n, s, strlen(s)) != NULL;
}

/* 種別 t のフレームのうち、payload に s を含むものの番号 (-1 = 無い) */
static int frame_with(u8 t, const char *s)
{
    int i;
    for (i = 0; i < g_frames_n; i++) {
        if (g_frames[i].type != t) continue;
        if (!s || memmem(g_frames[i].payload, g_frames[i].len, s, strlen(s)))
            return i;
    }
    return -1;
}

/* ISR を回して rshell の読み口に何か渡るか */
static int rshell_sees_byte(void)
{
    int k;
    for (k = 0; k < 8; k++) serial_irq_handler();
    return serial_trygetchar();
}

/* ------------------------------------------------------------------------ */
static void flow(void)
{
    int rc, i_bye, i_exit;

    reset();
    rc = serialfs_session_begin();
    CHECK(rc == 0);
    CHECK(serial_gate_active() == 1);
    CHECK(g_hello_n == 1);
    CHECK(g_mount_calls == 1 && g_permit_at_mount == 1 && g_permit_now == 0);
    CHECK(strcmp(g_mount_args[0], "/host") == 0 && strcmp(g_mount_args[1], "COM1") == 0
          && strcmp(g_mount_args[2], "serialfs") == 0);
    CHECK(g_permit_cli != 0 && g_permit_cli->sid == HOST_SID);
    /* セッション中はもう一度始められない */
    CHECK(serialfs_session_begin() == OS32_ERR_BUSY);
    /* 子の出力は線に出ず保留へ */
    kprintf(0x07, "child out %d\n", 42);
    CHECK(memmem(g_tx, (size_t)g_tx_n, "child out", 9) == NULL);

    rc = serialfs_session_end(7);
    CHECK(rc == 0);
    CHECK(serial_gate_active() == 0);
    CHECK(g_umount_calls == 1);
    CHECK(g_cli.sid == 0 && g_active == 0);
    i_bye = frame_with(SFS_T_BYE, 0);
    i_exit = frame_with(SFS_T_EXIT, 0);
    CHECK(i_bye == 1 && i_exit == g_frames_n - 1 && i_exit > i_bye);
    CHECK(frame_with(SFS_T_LOG, "child out 42") > i_bye);
    CHECK(frame_with(SFS_T_LOG, "sfs: exit=7") > i_bye);
    CHECK(frame_with(SFS_T_LOG, "requests=") > i_bye);
    CHECK(g_frames[i_exit].sid == HOST_SID && g_frames[i_exit].len == 12);
    CHECK(sfs_get32(g_frames[i_exit].payload) == 7);
    CHECK(sfs_get32(g_frames[i_exit].payload + 4) == 0);
    CHECK(sfs_get32(g_frames[i_exit].payload + 8) == 0);
    /* 終わった後は何も残っていない */
    {
        int n;
        raw_after_last_frame(&n);
        CHECK(n == 0);
    }
    CHECK(rshell_sees_byte() == -1);
    CHECK(serialfs_session_end(0) == OS32_ERR_INVAL);

    /* 断る条件 */
    reset();
    g_owner = 2;
    CHECK(serialfs_session_begin() == OS32_ERR_INVAL && g_hello_n == 0);
    reset();
    g_if = 0;
    CHECK(serialfs_session_begin() == OS32_ERR_INVAL && g_hello_n == 0);
    reset();
    strcpy(g_fstype, "hostdrv");
    CHECK(serialfs_session_begin() == OS32_ERR_BUSY && g_hello_n == 0);
    CHECK(serial_gate_active() == 0);
}

/* 決定 11: HELLO に答えが無い。ごみが最後の試行の期限の後も 100 tick 続く。
 * 隔離が 500ms の静けさを待ってから下ろすので、ごみは rshell に渡らない。 */
static void hello_fail_quiet(void)
{
    int rc;
    unsigned long trial = sfs_trial_ticks(9600);

    reset();
    g_answer_hello = 0;
    g_garbage_until = 1;                /* 有効化 (HELLO ごとに延ばす) */
    rc = serialfs_session_begin();
    CHECK(rc == OS32_ERR_IO);
    CHECK(g_hello_n == 1 + SFS_RETRIES);
    CHECK(g_mount_calls == 0);
    CHECK(serial_gate_active() == 0);
    /* 隔離: 最後のごみ (tick until-1) から 500ms 静まってから下ろした */
    CHECK(tick_count >= (g_garbage_until - 1) + sfs_ms_ticks(SFS_QUIET_MS));
    CHECK(tick_count < g_garbage_until + sfs_ms_ticks(SFS_QUIET_MS) + 20);
    /* 上限 (5 秒) には達していない = 静まった */
    CHECK(tick_count < g_last_hello_tick + trial + sfs_ms_ticks(SFS_QUIET_MAX_MS));
    CHECK(strstr(g_kout, "line not quiet") == NULL);
    /* 溜めた文字は下ろした後に生で流れる (フレームではなく) */
    CHECK(raw_has("no answer to HELLO"));
    CHECK(frame_with(SFS_T_BYE, 0) < 0 && frame_with(SFS_T_EXIT, 0) < 0);
    /* ごみは 1 バイトも rshell に渡らない (時計を進めて ISR を回しても) */
    tick_count += 10000;
    CHECK(rshell_sees_byte() == -1);
    CHECK(g_rxq_n == 0);
}

/* 相手が送り続ける: 隔離は上限 5 秒で打ち切り、警告を出して下ろす (決裁 1B) */
static void hello_fail_flood(void)
{
    int rc;
    unsigned long trial = sfs_trial_ticks(9600);
    unsigned long start_max;

    reset();
    g_answer_hello = 0;
    g_garbage_forever = 1;
    rc = serialfs_session_begin();
    CHECK(rc == OS32_ERR_IO);
    CHECK(serial_gate_active() == 0);
    start_max = g_last_hello_tick + trial;
    CHECK(tick_count >= start_max + sfs_ms_ticks(SFS_QUIET_MAX_MS) - 2);
    CHECK(tick_count <= start_max + sfs_ms_ticks(SFS_QUIET_MAX_MS) + 60);
    CHECK(strstr(g_kout, "line not quiet") != NULL);
    CHECK(raw_has("line not quiet"));
    CHECK(raw_has("no answer to HELLO"));
}

/* Fable m6: 2 回目の sfs_send_log の送信中に入った 'Y' は同じ汲み出しの LOG で、
 * EXIT の後 (ゲートを下ろすまで) に入った 'Z' は下ろした後に生で流れる。
 * どちらも捨てない。 */
static void end_late_hold(void)
{
    int rc, i_exit, i_y;

    reset();
    CHECK(serialfs_session_begin() == 0);
    kprintf(0x07, "child out\n");
    g_inject_y = 1;
    g_inject_z = 1;
    rc = serialfs_session_end(0);
    CHECK(rc == 0);
    CHECK(g_inject_y == 0 && g_inject_z == 0);      /* 両方の注入が起きた */
    i_exit = frame_with(SFS_T_EXIT, 0);
    i_y = frame_with(SFS_T_LOG, "Y");
    CHECK(i_exit == g_frames_n - 1);
    CHECK(i_y >= 0 && i_y < i_exit);                /* 'Y' は EXIT の前の LOG */
    CHECK(frame_with(SFS_T_LOG, "child out") >= 0);
    CHECK(raw_has("Z"));                            /* 'Z' は EXIT の後に生で */
    {
        int n;
        const char *p = raw_after_last_frame(&n);
        CHECK(n == 1 && p[0] == 'Z');
    }
    CHECK(memmem(g_tx, (size_t)g_frames[i_exit].end, "Z", 1) == NULL);
    CHECK(serial_hold_take((u8 *)g_kout, 1) == 0);  /* 保留は空 */
}

/* セッションの終わりに相手が送り続ける: EXIT の flags に NOT_QUIET */
static void end_not_quiet(void)
{
    int rc, i_exit;

    reset();
    CHECK(serialfs_session_begin() == 0);
    g_garbage_forever = 1;
    rc = serialfs_session_end(3);
    CHECK(rc == 1);
    i_exit = frame_with(SFS_T_EXIT, 0);
    CHECK(i_exit >= 0);
    CHECK(sfs_get32(g_frames[i_exit].payload) == 3);
    CHECK((sfs_get32(g_frames[i_exit].payload + 8) & SFS_XF_NOT_QUIET) != 0);
    CHECK(strstr(g_kout, "line not quiet") != NULL);
    CHECK(serial_gate_active() == 0);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "flow")) flow();
    else if (!strcmp(c, "hello_fail_quiet")) hello_fail_quiet();
    else if (!strcmp(c, "hello_fail_flood")) hello_fail_flood();
    else if (!strcmp(c, "end_late_hold")) end_late_hold();
    else if (!strcmp(c, "end_not_quiet")) end_not_quiet();
    else {
        fprintf(stderr, "usage: serialfs-session-host <case>\n");
        return 2;
    }
    printf("%s: %s\n", c, failed ? "FAIL" : "ok");
    return failed ? 1 : 0;
}
