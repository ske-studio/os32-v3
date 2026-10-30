/* ======================================================================== */
/*  SERIALFS_SESSION.C — `sfs run` のセッション (KAPI sfs_begin / sfs_end)  */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md §1-v3 と ユーザー決裁        */
/*  (2026-09-24)。流れ (常駐シェルの `sfs run` が begin → 子 → end と呼ぶ): */
/*                                                                          */
/*   begin: (1) /host が空いているか → (2) ゲートを上げる → (3) HELLO       */
/*          → (4) /host に SerialFS をマウント                              */
/*   end  : (7) BYE → アンマウント (線が死んでいても手元は解放)             */
/*          → (8) 隔離: 受信を捨てて 500ms 静まるのを待つ。上限 5 秒で      */
/*            打ち切り、警告を出して進む (決裁 1B — 保留状態は設けない)     */
/*          → (10)(11) 溜めた出力と `sfs: exit=N` を**長さ付きのフレーム**  */
/*            (SFS_T_LOG / SFS_T_EXIT) で送る (決裁 2A — 生バイトで流さない) */
/*          → (9) ゲートを下ろす (受信リングも空にする)                     */
/*                                                                          */
/*  ログのフレームは**ゲートを下ろす前に**送る — 下ろした後は console の    */
/*  複写が同じ線に出るので、フレームの間に文字が挟まりうる。票の番号        */
/*  (9) → (10) の順とは入れ替えている (報告済み)。                          */
/*                                                                          */
/*  HELLO に答えが無ければセッションは始まっていない。フレームは送らない   */
/*  が、**相手がセッションを始めている可能性を前提に** (HELLO の応答が     */
/*  遅れて届く・化けた)、(8) の隔離を済ませてからゲートを下ろし、溜めた     */
/*  文字をそのまま流す (レビュー往復 1、決定 11。ホストは `sfs run` の行の  */
/*  EOT までのフレームでない部分を出力として出す)。                        */
/* ======================================================================== */
#include "serialfs.h"
#include "serial.h"
#include "serial_plan.h"      /* SER_TX_OK */
#include "io.h"
#include "kprintf.h"
#include "kstring.h"
#include "appslot.h"            /* APP_ID_SHELL */
#include "os32_kapi_shared.h"
#include "sfs_lan.h"
#include "sys.h"
#include "lan_contract.h"
#include "lan_module.h"
#include "sysconfig.h"
#include "config.h"

/* SFS_LAN=1 selects the foreground 82557 transport at every begin. It shares
 * the client/mount lifecycle below, but bypasses UART gate/hold/LOG/EXIT and
 * reports start, counters and exit directly through the serial console. */

extern volatile u32 tick_count;
extern int res_owner_get(void);

/* 保留ログを生出力する際の排出バッファ容量。 */
#define SFS_HOLD_FLUSH_BUF_SIZE 64

/* セッション nonce の分散係数。 */
#define SFS_NONCE_MIX 0x9E3779B9UL

#define SFS_LAN_READY_MS 100

static SfsClient g_cli;
static SfsLan g_lan;
static int g_use_lan;
static const SfsLanNic g_nic = { e82557_send, e82557_recv, e82557_get_mac };
static int g_active;            /* begin の準備から finish の解放まで */
static u32 g_nonce_ctr;
static u32 g_lan_rxfull;

/* ---- 線への出入り (ゲートの口) ---- */
static int sio_put(void *ctx, const u8 *buf, u32 n)
{
    (void)ctx;
    return serial_gate_put(buf, n) == SER_TX_OK ? 0 : -1;
}

static int sio_get(void *ctx)
{
    (void)ctx;
    return serial_gate_get();
}

static u32 sio_now(void *ctx)
{
    (void)ctx;
    return tick_count;
}

static void sio_idle(void *ctx)
{
    (void)ctx;
    /* can_wait (IF=1) を確かめてからしか呼ばれない。IF=0 の hlt は起きない */
    _halt();
}

static int sio_can_wait(void *ctx)
{
    (void)ctx;
    return _irq_enabled();
}

static const SfsIo g_sio = { sio_put, sio_get, sio_now, sio_idle, sio_can_wait };

/* 保留リングを LOG フレームで送る */
static void sfs_send_log(void)
{
    u8 buf[SFS_MAX_PAYLOAD];
    u32 n;

    for (;;) {
        n = serial_hold_take(buf, (u32)SFS_MAX_PAYLOAD);
        if (n == 0) break;
        if (sfs_client_oneway(&g_cli, SFS_T_LOG, buf, (u16)n) != 0) break;
    }
}

/* ゲートを下ろした後に、溜めた文字をそのまま流す (HELLO が通らなかった回) */
static void sfs_flush_hold_raw(void)
{
    u8 buf[SFS_HOLD_FLUSH_BUF_SIZE];
    u32 n, i;

    for (;;) {
        n = serial_hold_take(buf, (u32)sizeof(buf));
        if (n == 0) break;
        for (i = 0; i < n; i++) (void)serial_putchar((char)buf[i]);
    }
}

static const char *sfs_dead_name(u8 d)
{
    switch (d) {
    case SFS_DEAD_FAILS: return "no answer";
    case SFS_DEAD_STALE: return "host does not know this session";
    case SFS_DEAD_SEQ:   return "sequence numbers exhausted";
    default:             return "";
    }
}

/* (7)〜(11)。戻り 0 = 静まった / 1 = 静まらなかった (警告済み) */
static int sfs_finish(int code)
{
    int quiet;
    u32 flags = 0;
    u32 dropped;
    u8 pl[SFS_EXIT_LEN];

    if (g_cli.sid != 0) (void)sfs_client_oneway(&g_cli, SFS_T_BYE, 0, 0);
    /* 子が /host を付け替えていたら、それは外さない */
    if (serialfs_is_mounted()) {
        if (kstrcmp(vfs_fstype(SERIALFS_PREFIX), SERIALFS_NAME) == 0)
            vfs_umount(SERIALFS_PREFIX);
    }

    if (g_use_lan) {
        sfs_lan_reset(&g_lan);
        g_cli.io = 0;
        g_cli.ctx = 0;
        g_lan.nic = 0;
        g_lan.clock = 0;
        g_lan.clock_ctx = 0;
        lan_module_session_release();
        if (g_cli.dead != SFS_DEAD_NONE)
            kprintf(KLOG_LEGACY_LIGHT_RED, "sfs: line declared dead (%s)\n",
                    sfs_dead_name(g_cli.dead));
        kprintf(KLOG_LEGACY_CYAN,
                "sfs: requests=%u resent=%u timeouts=%u stray=%u bad_crc=%u bad_len=%u rxfull=%u avg_us=%u max_us=%u\n",
                g_cli.calls, g_cli.resends, g_cli.timeouts, g_cli.stray,
                g_cli.dec.bad_crc, g_cli.dec.bad_len,
                g_lan_rxfull,
                g_cli.rtt_samples ? (u32)(g_cli.rtt_total_us / g_cli.rtt_samples) : 0,
                g_cli.rtt_max_us);
        kprintf(KLOG_LEGACY_CYAN, "sfs: exit=%d\n", code);
        sfs_dec_reset(&g_cli.dec);
        g_cli.sid = 0;
        g_active = g_use_lan = 0;
        return 0;
    }

    quiet = sfs_client_quiesce(&g_cli, SFS_QUIET_MS, SFS_QUIET_MAX_MS);
    if (!quiet) {
        flags |= SFS_XF_NOT_QUIET;
        kprintf(KLOG_LEGACY_YELLOW,
                "sfs: line not quiet after %ums; closing the session anyway\n",
                (u32)SFS_QUIET_MAX_MS);
    }
    if (g_cli.dead != SFS_DEAD_NONE) {
        flags |= SFS_XF_DEAD;
        kprintf(KLOG_LEGACY_LIGHT_RED, "sfs: line declared dead (%s)\n",
                sfs_dead_name(g_cli.dead));
    }
    if (g_cli.sid != 0) {
        kprintf(KLOG_LEGACY_CYAN,
                "sfs: requests=%u resent=%u timeouts=%u stray=%u bad_crc=%u bad_len=%u avg_us=%u max_us=%u\n",
                g_cli.calls, g_cli.resends, g_cli.timeouts, g_cli.stray,
                g_cli.dec.bad_crc, g_cli.dec.bad_len,
                g_cli.rtt_samples ? (u32)(g_cli.rtt_total_us / g_cli.rtt_samples) : 0,
                g_cli.rtt_max_us);
        sfs_send_log();
        dropped = serial_hold_dropped();
        if (dropped)
            kprintf(KLOG_LEGACY_YELLOW, "sfs: log dropped %u bytes (oldest)\n", dropped);
        kprintf(KLOG_LEGACY_CYAN, "sfs: exit=%d\n", code);
        sfs_send_log();
        sfs_put32(pl, (u32)code);
        sfs_put32(pl + SFS_EXIT_DROPPED_OFF, dropped);
        sfs_put32(pl + SFS_EXIT_FLAGS_OFF, flags);
        /* ここから下ろすまでに保留へ入った文字 (ISR の kprintf) は、下ろした
         * 後に sfs_flush_hold_raw が生で流す (もう一度汲んでも窓は残る) */
        (void)sfs_client_oneway(&g_cli, SFS_T_EXIT, pl, SFS_EXIT_LEN);
    }

    serial_gate_set(0);
    /* EXIT を送ってから下ろすまでに保留へ入った文字も捨てない (レビュー往復 1、
     * Fable m6)。HELLO が通らなかった回はここで全部を生で流す。どちらも
     * ホストは `sfs run` の行の EOT までの「フレームでない部分」として出す。
     * HELLO が通らなかった回も、ホストがセッションを始めている前提で、
     * 下ろす前に上の隔離 (500ms、上限 5 秒) を済ませている (決定 11)。 */
    sfs_flush_hold_raw();
    g_cli.sid = 0;
    g_active = 0;
    return quiet ? 0 : 1;
}

int serialfs_session_begin(void)
{
    u32 mode = 0, baud = 0, fifo = 0;
    int rc;

    /* 常駐シェルの `sfs run` だけ (sh.bin・外部プログラムは断る) */
    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    if (g_active) return OS32_ERR_BUSY;
    if (!serial_is_initialized()) return OS32_ERR_INVAL;
    /* 待てない (IF=0) まま始めない。tick が進まず期限が来ない */
    if (!_irq_enabled()) return OS32_ERR_INVAL;
    /* (1) /host が空いているか (HostDrv や既存のマウント) */
    if (vfs_fstype(SERIALFS_PREFIX)[0] != '\0') return OS32_ERR_BUSY;

    g_use_lan = sysconfig_get_int(SYS_SYSTEM_CFG, "SFS_LAN", 0) == 1;
    if (g_use_lan) {
        if (!lan_module_session_acquire()) {
            kprintf(KLOG_LEGACY_LIGHT_RED, "sfs: lan.mod unavailable (sfs lan off for serial)\n");
            g_use_lan = 0;
            return OS32_ERR_IO;
        }
        /* These driver entry points belong exclusively to the foreground. */
        int state = e82557_state();
        char peer[32]; /* room to detect a too-long configured MAC */
        u32 start;
        g_lan_rxfull = state == E82557_RX_WAIT;
        sfs_lan_init(&g_lan, &g_nic, &g_sio, 0);
        sfs_client_init(&g_cli, &sfs_lan_io, &g_lan, SFS_LAN_BAUD);
        g_cli.max_payload = SFS_LAN_MAX_PAYLOAD;
        g_active = 1;
        /* Free stale frames before waiting for the existing tick recovery.
         * Reset clears peer configuration, so apply SFS_LAN_PEER afterwards. */
        if (state == E82557_RUNNING || state == E82557_RX_WAIT)
            sfs_lan_reset(&g_lan);
        (void)sysconfig_get_str(SYS_SYSTEM_CFG, "SFS_LAN_PEER", peer, sizeof(peer));
        if ((state != E82557_RUNNING && state != E82557_RX_WAIT) ||
            sfs_lan_set_peer(&g_lan, peer) != 0) {
            kprintf(KLOG_LEGACY_LIGHT_RED, "sfs: LAN unavailable or invalid SFS_LAN_PEER\n");
            (void)sfs_finish(OS32_ERR_IO);
            return OS32_ERR_IO;
        }
        start = tick_count;
        while (!e82557_rx_ready()) {
            if ((u32)(tick_count - start) >= sfs_ms_ticks(SFS_LAN_READY_MS)) {
                kprintf(KLOG_LEGACY_YELLOW,
                        "sfs: LAN RX not ready after %ums; trying HELLO anyway\n",
                        (u32)SFS_LAN_READY_MS);
                break;
            }
            sio_idle(0);
        }
    } else {
        (void)serial_get_status(&mode, &baud, &fifo);
        sfs_client_init(&g_cli, &g_sio, (void *)0, baud);
    }
    g_active = 1;

    g_cli.time_us = sys_time_now;

    /* (2) ゲート → (3) HELLO */
    if (!g_use_lan) serial_gate_set(1);
    g_nonce_ctr++;
    rc = sfs_client_hello(&g_cli, tick_count ^ (g_nonce_ctr * SFS_NONCE_MIX));
    if (rc != 0) {
        if (g_use_lan)
            kprintf(KLOG_LEGACY_LIGHT_RED, "sfs: no answer to LAN HELLO\n");
        else kprintf(KLOG_LEGACY_LIGHT_RED,
                "sfs: no answer to HELLO at %ubps (host must run "
                "rshell_serial.py --serve-host)\n", baud);
        (void)sfs_finish(rc);
        return OS32_ERR_IO;
    }

    /* (4) マウント。口はこの呼び出しのあいだだけ開ける */
    serialfs_permit(&g_cli);
    rc = vfs_mount(SERIALFS_PREFIX, SERIALFS_DEVNAME, SERIALFS_NAME);
    serialfs_forbid();
    if (rc != VFS_OK) {
        kprintf(KLOG_LEGACY_LIGHT_RED, "sfs: mount %s failed (%d)\n",
                SERIALFS_PREFIX, rc);
        (void)sfs_finish(rc);
        return rc;
    }
    if (g_use_lan)
        kprintf(KLOG_LEGACY_CYAN,
                "sfs: transport=lan nic=82557 peer=%02x:%02x:%02x:%02x:%02x:%02x sid=%u\n",
                (u32)g_lan.peer[0], (u32)g_lan.peer[1], (u32)g_lan.peer[2],
                (u32)g_lan.peer[3], (u32)g_lan.peer[4], (u32)g_lan.peer[5], g_cli.sid);
    return 0;
}

/* kdebug の peek -l (TASK_KPEEK §2-8) の宛先: いまのペアの MAC。
 * 0 = out に写した / -1 = ペアが無い (未接続かつ SFS_LAN_PEER 無し)。 */
int sfs_lan_peer_mac(u8 *out)
{
    u32 i;
    if (!out || !(g_lan.linked || g_lan.peer_fixed)) return -1;
    for (i = 0; i < SFS_LAN_MAC_LEN; i++) out[i] = g_lan.peer[i];
    return 0;
}

int serialfs_session_end(int exit_code)
{
    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    if (!g_active) return OS32_ERR_INVAL;
    return sfs_finish(exit_code);
}
