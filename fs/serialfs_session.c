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

extern volatile u32 tick_count;
extern int res_owner_get(void);

/* kprintf の属性 (PC/AT 流。kprintf が入口で PC-98 流へ直す) */
#define SFS_ATTR_INFO  0x0B
#define SFS_ATTR_WARN  0x0E
#define SFS_ATTR_ERR   0x0C

static SfsClient g_cli;
static int g_active;            /* ゲートを上げてから下ろすまで */
static u32 g_nonce_ctr;

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
    u8 buf[64];
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
    u8 pl[12];

    if (g_cli.sid != 0) (void)sfs_client_oneway(&g_cli, SFS_T_BYE, 0, 0);
    /* 子が /host を付け替えていたら、それは外さない */
    if (serialfs_is_mounted()) {
        if (kstrcmp(vfs_fstype(SERIALFS_PREFIX), SERIALFS_NAME) == 0)
            vfs_umount(SERIALFS_PREFIX);
    }

    quiet = sfs_client_quiesce(&g_cli, SFS_QUIET_MS, SFS_QUIET_MAX_MS);
    if (!quiet) {
        flags |= SFS_XF_NOT_QUIET;
        kprintf(SFS_ATTR_WARN,
                "sfs: line not quiet after %ums; closing the session anyway\n",
                (u32)SFS_QUIET_MAX_MS);
    }
    if (g_cli.dead != SFS_DEAD_NONE) {
        flags |= SFS_XF_DEAD;
        kprintf(SFS_ATTR_ERR, "sfs: line declared dead (%s)\n",
                sfs_dead_name(g_cli.dead));
    }
    if (g_cli.sid != 0) {
        kprintf(SFS_ATTR_INFO,
                "sfs: requests=%u resent=%u timeouts=%u stray=%u bad_crc=%u bad_len=%u\n",
                g_cli.calls, g_cli.resends, g_cli.timeouts, g_cli.stray,
                g_cli.dec.bad_crc, g_cli.dec.bad_len);
        sfs_send_log();
        dropped = serial_hold_dropped();
        if (dropped)
            kprintf(SFS_ATTR_WARN, "sfs: log dropped %u bytes (oldest)\n", dropped);
        kprintf(SFS_ATTR_INFO, "sfs: exit=%d\n", code);
        sfs_send_log();
        sfs_put32(pl, (u32)code);
        sfs_put32(pl + 4, dropped);
        sfs_put32(pl + 8, flags);
        /* ここから下ろすまでに保留へ入った文字 (ISR の kprintf) は、下ろした
         * 後に sfs_flush_hold_raw が生で流す (もう一度汲んでも窓は残る) */
        (void)sfs_client_oneway(&g_cli, SFS_T_EXIT, pl, 12);
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

    (void)serial_get_status(&mode, &baud, &fifo);
    sfs_client_init(&g_cli, &g_sio, (void *)0, baud);
    g_active = 1;

    /* (2) ゲート → (3) HELLO */
    serial_gate_set(1);
    g_nonce_ctr++;
    rc = sfs_client_hello(&g_cli, tick_count ^ (g_nonce_ctr * 0x9E3779B9UL));
    if (rc != 0) {
        kprintf(SFS_ATTR_ERR,
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
        kprintf(SFS_ATTR_ERR, "sfs: mount %s failed (%d)\n",
                SERIALFS_PREFIX, rc);
        (void)sfs_finish(rc);
        return rc;
    }
    return 0;
}

int serialfs_session_end(int exit_code)
{
    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    if (!g_active) return OS32_ERR_INVAL;
    return sfs_finish(exit_code);
}
