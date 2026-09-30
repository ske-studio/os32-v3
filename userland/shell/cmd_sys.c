#include "shell.h"
#include "config.h"   /* SYS_GSHELL_BIN, SYS_SYSTEM_CFG (K4: os32gui) */
#include "kbd_watch.h" /* kbdstat -w の行 (票 TASK_KBD_NAV §3) */

/* ======================================================================== */
/*  システム操作モジュール (cmd_sys.c)                                      */
/* ======================================================================== */

/* I-1: drivers/ide.h の IdeInfo と**同じ並び**でなければならない。
 * ide_identify はカーネル側の定義 (96B) で書くので、ここが 92B のままだと
 * phys_sector_size の 2 バイトが呼び手のスタックを踏む (`ide 0` で発現)。
 * drivers/ide.h はカーネル内部ヘッダで外部プログラムからは引けないため、
 * 写しをここに置く — ide.h を変えたら必ず一緒に直すこと。 */
typedef struct {
    u32 total_sectors;
    u16 cylinders;
    u16 heads;
    u16 sectors;
    u32 size_mb;
    char model[41];
    char serial[21];
    char firmware[9];
    int  lba_supported;
    u16  phys_sector_size;   /* 物理セクタサイズ (SASI=256, IDE=512) */
} IdeInfo;


static int cmd_mem(int argc, char **argv)
{
    u32 pmem_kb;
    u32 ram_kb;
    (void)argc; (void)argv;
    pmem_kb = g_api->sys_get_mem_kb();
    /* sys_get_mem_kb は RAM 上端アドレス / 1024 なので、PC-98 の 15-16MB
     * システム空間を RAM として数えてしまう (15MB 機が 17408 KB を名乗る)。
     * sys_ram_kb (KAPI v46) は起動時に実際に登録した span の合計。RAM が
     * 15MB より下で止まる機械では両者が一致する。 */
    ram_kb = g_api->sys_ram_kb();
    g_api->kprintf(ATTR_CYAN, "%s", "Memory Info:\n");
    g_api->kprintf(ATTR_WHITE, "  Physical : %u KB (%u MB)\n", pmem_kb, pmem_kb / 1024);
    g_api->kprintf(ATTR_WHITE,
                   "  RAM      : %u KB (%u MB)  usable (15-16MB system space excluded)\n",
                   ram_kb, ram_kb / 1024);
    g_api->kprintf(ATTR_WHITE, "  Paging   : %s\n",
                   g_api->paging_enabled() ? "ENABLED" : "DISABLED");
    g_api->kprintf(ATTR_WHITE, "  Heap Tot : %u B, Used: %u B, Free: %u B\n",
                   g_api->kmalloc_total(), g_api->kmalloc_used(), g_api->kmalloc_free());
    g_api->kprintf(ATTR_CYAN, "%s", "Memory Map:\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x00000 - 0x00FFF  NP (NULL guard)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x01000 - 0x9FFFF  Font/Unicode/GFX (V86 guest window)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0xA0000 - 0xEFFFF  VRAM\n");
    /* 番地の正典は include/memmap.h (docs/02_memory.md §2-1 が生成)。ここは
     * 2026-09-17 の kstack 移設と 2026-09-23 の DMA プールを反映した写し。 */
    g_api->kprintf(ATTR_WHITE, "%s", "  0x100000-0x1FFFFF  Kernel Band (code+heap+KAPI+SHM)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x200000-0x2FFFFF  SQLite Band (code+alt stack, DMA pool 0x2E8000-0x2F7FFF)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x2FB000-0x2FBFFF  NP (kernel stack guard)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x2FC000-0x2FFFFF  Kernel Stack (16KB)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x300000-0x3FFFFF  Shell Band (1MB)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x400000-0x4FFFFF  Shared Library Band (1MB)\n");
    g_api->kprintf(ATTR_WHITE, "%s", "  0x500000-          Program Space\n");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_api->sys_reboot();
    return 0;
}

static int cmd_dev(int argc, char **argv)
{
    int i, n = g_api->dev_count();
    (void)argc; (void)argv;
    g_api->kprintf(ATTR_CYAN, "%s", "Devices:\n");
    for (i=0; i<n; i++) {
        char name[32]; int type; u32 sects;
        if (g_api->dev_get_info(i, name, 32, &type, &sects) == 0) {
            if (type == 1) g_api->kprintf(ATTR_WHITE, "  %s: block %u sects\n", name, sects);
            else g_api->kprintf(ATTR_WHITE, "  %s: char\n", name);
        }
    }
    return 0;
}

static int cmd_ide(int argc, char **argv)
{
    int drv = 0, i;
    IdeInfo info;
    if (argc > 1 && argv[1][0] >= '0' && argv[1][0] <= '3') {
        drv = argv[1][0] - '0';
    }
    if (!g_api->ide_drive_present(drv)) {
        g_api->kprintf(ATTR_RED, "IDE drive %d not present.\n", drv);
        return SH_STATUS_ERROR;
    }
    if (g_api->ide_identify(drv, &info) == 0) {
        char model[41];
        for (i = 0; i < 40; i++) model[i] = info.model[i];
        model[40] = '\0';
        g_api->kprintf(ATTR_WHITE, "IDE %d: %s\n  C/H/S: %u/%u/%u\n  LBA Segs: %u\n",
                       drv, model, info.cylinders, info.heads, info.sectors, info.total_sectors);
    } else {
        g_api->kprintf(ATTR_RED, "IDE %d: Identify fail\n", drv);
        return SH_STATUS_ERROR;
    }
    return 0;
}

static int cmd_format(int argc, char **argv)
{
    int drv = 0;
    u32 sects = 2880;
    int ret;
    const char *p;

    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    drv = argv[1][0] - '0';
    if (argc > 2) {
        p = argv[2];
        sects = 0;
        while (*p >= '0' && *p <= '9') sects = sects * 10 + (*p++ - '0');
    }
    g_api->kprintf(ATTR_YELLOW, "Formatting drive %d (%u sectors)...\n", drv, sects);
    ret = g_api->ext2_format(drv, sects);
    if (ret != 0) {
        g_api->kprintf(ATTR_RED, "Format failed: %d\n", ret);
        return SH_STATUS_ERROR;
    }
    g_api->kprintf(ATTR_GREEN, "%s", "Format complete.\n");
    return 0;
}

static int cmd_play(int argc, char **argv)
{
    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    g_api->fm_play_mml(argv[1]);
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  os32gui — CUI ⇄ GUI シェル切替 (契約 T9 / TASK_K4 作業 0・3)             */
/* ------------------------------------------------------------------------ */

/* system.cfg 書き換え時に KEY=VALUE 1 行分として確保しておく余白 (バイト)。
 * キーも値も 16 バイト未満の短い識別子しか使わない。 */
#define CFG_LINE_RESERVE 64
/* /etc/system.cfg の読み書きバッファ幅 ([C4])。読みは実効 CFG_READ_MAX - 1。 */
#define CFG_READ_MAX     1024
#define CFG_OUT_MAX      1152

/* 行 [p, p+len) が "KEY=" (前後空白許容) の代入行か判定する。 */
static int cfg_line_is_key(const char *p, int len, const char *key)
{
    int i = 0;
    int k = 0;
    while (i < len && (p[i] == ' ' || p[i] == '\t')) i++;
    while (key[k]) {
        if (i >= len || p[i] != key[k]) return 0;
        i++; k++;
    }
    while (i < len && (p[i] == ' ' || p[i] == '\t')) i++;
    return (i < len && p[i] == '=');
}

/* /etc/system.cfg の KEY= 行を val に差し替える (無ければ追記)。
 * 既存の他キー行は保存する。成功で 0、失敗で -1。
 * K4 の os32gui on/off と H2b の gfxmode が共用する。 */
static int cfg_set_key(const char *key, const char *val)
{
    char buf[CFG_READ_MAX];
    char out[CFG_OUT_MAX];
    int fd, n, i, o;

    /* 既存内容を読む (無ければ空から作る)。
     * T22: **読み切れなければ書き戻さない**。以前は 1023 バイトで切って
     * そのまま O_TRUNC で書き戻していたので、1KB を超える設定が消えた。 */
    n = 0;
    fd = g_api->sys_open(SYS_SYSTEM_CFG, KAPI_O_RDONLY);
    if (fd >= 0) {
        int r = g_api->sys_read(fd, buf, (int)sizeof(buf) - 1);
        int more = 0;
        if (r == (int)sizeof(buf) - 1) {
            char probe;
            if (g_api->sys_read(fd, &probe, 1) > 0) more = 1;
        }
        g_api->sys_close(fd);
        if (more) {
            sh_refuse("system.cfg", (int)sizeof(buf) - 1);
            return -1;
        }
        if (r > 0) n = r;
    }

    /* 行ごとにコピー。旧 KEY= 行は捨てる。 */
    o = 0;
    i = 0;
    while (i < n) {
        int ls = i;
        int len;
        while (i < n && buf[i] != '\n') i++;
        len = i - ls;          /* 改行を含まない行長 */
        if (i < n) i++;        /* 改行を飛ばす */
        if (cfg_line_is_key(&buf[ls], len, key)) continue;
        {
            int k;
            /* T22: 収まらない行を切って書き戻すと、その行の設定が変わる。 */
            if (o + len >= (int)sizeof(out) - CFG_LINE_RESERVE) {
                sh_refuse("system.cfg", (int)sizeof(out) - CFG_LINE_RESERVE);
                return -1;
            }
            for (k = 0; k < len; k++) out[o++] = buf[ls + k];
            out[o++] = '\n';
        }
    }

    /* 新しい KEY=VALUE 行を追記 */
    {
        int k;
        int need = 2;                    /* '=' と '\n' */
        for (k = 0; key[k]; k++) need++;
        for (k = 0; val[k]; k++) need++;
        if (o + need > (int)sizeof(out)) {
            sh_refuse("system.cfg", (int)sizeof(out));
            return -1;
        }
        for (k = 0; key[k]; k++) out[o++] = key[k];
        out[o++] = '=';
        for (k = 0; val[k]; k++) out[o++] = val[k];
        out[o++] = '\n';
    }

    fd = g_api->sys_open(SYS_SYSTEM_CFG, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) return -1;
    {
        int w = g_api->sys_write(fd, out, o);
        g_api->sys_close(fd);
        if (w != o) return -1;
    }
    return 0;
}

static int cmd_os32gui(int argc, char **argv)
{
    if (argc >= 2) {
        /* on|off: system.cfg の GUI= を書き換える (次回起動から有効) */
        if (str_eq(argv[1], "on")) {
            if (cfg_set_key("GUI", "1") == 0)
                g_api->kprintf(ATTR_GREEN, "%s", "GUI enabled at next boot (system.cfg GUI=1)\n");
            else
                g_api->kprintf(ATTR_RED, "%s", "os32gui: failed to write /etc/system.cfg\n");
        } else if (str_eq(argv[1], "off")) {
            if (cfg_set_key("GUI", "0") == 0)
                g_api->kprintf(ATTR_GREEN, "%s", "GUI disabled at next boot (system.cfg GUI=0)\n");
            else
                g_api->kprintf(ATTR_RED, "%s", "os32gui: failed to write /etc/system.cfg\n");
        } else {
            shell_print_help(argv[0]);
            return SH_STATUS_USAGE;
        }
        return 0;
    }

    /* 引数なし: 今すぐ gshell へ切替。カーネルに次シェルを記録して自分は
     * 終了する → 起動ループが gshell を 0x300000 に載せる (契約 T9)。 */
    {
        int rc = g_api->sys_switch_shell(SYS_GSHELL_BIN);
        if (rc < 0) {
            g_api->kprintf(ATTR_RED, "os32gui: switch not permitted (rc=%d)\n", rc);
            return SH_STATUS_ERROR;
        }
        g_api->kprintf(ATTR_CYAN, "%s", "Switching to GUI shell...\n");
        g_api->sys_exit(0);
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  gfxmode — グラフィクスバックエンドの強制指定 (票 H2b、契約 G5)           */
/*                                                                          */
/*  /etc/system.cfg の GFX= を書く。カーネルが起動時に読み、最初の           */
/*  gfx_init より前に HAL へ渡すので **次回起動から** 有効。                  */
/*    pc98   9801 プレーン 16 色を強制 (NP21/W は常に PEGC 相当なので、       */
/*           プレーン経路の回帰試験にはこれが要る)                           */
/*    pegc   PEGC 256 色を強制 (probe が通らなければ 9801 へ落ちる)          */
/*    cirrus Cirrus GD54xx アクセラレータを強制 (票 H3。NP21/W では ini の    */
/*           USEGD5430 / GD5430TYPE で有効化していないと probe が落ちる)     */
/*    auto   既定。probe 順 (Cirrus → PEGC → 9801)                           */
/* ------------------------------------------------------------------------ */
static int cmd_gfxmode(int argc, char **argv)
{
    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    if (!str_eq(argv[1], "pc98") && !str_eq(argv[1], "pegc") &&
        !str_eq(argv[1], "cirrus") && !str_eq(argv[1], "auto")) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    if (cfg_set_key("GFX", argv[1]) == 0)
        g_api->kprintf(ATTR_GREEN,
                       "Graphics backend = %s at next boot (system.cfg GFX=%s)\n",
                       argv[1], argv[1]);
    else {
        g_api->kprintf(ATTR_RED, "%s", "gfxmode: failed to write /etc/system.cfg\n");
        return SH_STATUS_ERROR;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  kbdstat — キーボード 8251 の診断カウンタ (KAPI v62 kbd_diag)              */
/*                                                                          */
/*  実機で本体キーボードの打鍵が届かないときの切り分け用。rshell (シリアル) */
/*  から打って読む前提なので 1 行で出す。読み方は docs/POLICY_DEBUG.md §4-57:*/
/*    irq=0 かつ now の RxRDY (bit1) = 1 → 8251 は受けている = PIC / IRQ1 側  */
/*    irq=0 かつ RxRDY = 0              → キーボードが送っていない           */
/*    irq>0 なのに文字が出ない          → 配送側 (リング / GUI / rshell)      */
/*    同じ code で irq が暴走            → 再送ストーム                       */
/*                                                                          */
/*  kbdstat -w — 受信 1 バイトごとの行 (KAPI v67 kbd_diag_log、票           */
/*  docs/tasks/gui/TASK_KBD_NAV.md §3)。カナ / CAPS が「ロックで make、解除で */
/*  break」か「押すたびに make だけ」かを見る。毎 tick リングの新しい分を読み、 */
/*  seq が飛んだら LOST 行 (取りこぼし — その区間は判定不能) を出す。         */
/*  ESC (本体でもシリアルでも) か KBDW_TIMEOUT_SEC 秒で終わる。本体の ESC は  */
/*  それ自身も make の行として出る (観測の邪魔にならないよう、終える前に     */
/*  リングをもう一度読む)。出力は kprintf なので rshell ではシリアルにも出る。 */
/* ------------------------------------------------------------------------ */
static int kbdstat_watch(void)
{
    KbdDiagLogEnt ents[KBD_DLOG_CAP];
    KbdDiag d0, d1;
    char line[KBDW_LINE_MAX];
    u32 last = 0;
    u32 shown = 0;
    u32 lost = 0;
    u32 lost_now;
    u32 t_end;
    int have_d0;
    int esc = 0;
    int n, i, k;

    have_d0 = (g_api->kbd_diag(&d0) == 0);
    /* 基準: 始める前に溜まっている分は出さず、最後の seq だけ覚える */
    n = g_api->kbd_diag_log(0, ents, KBD_DLOG_CAP);
    if (n < 0) {
        g_api->kprintf(ATTR_RED, "kbdstat: kbd_diag_log failed (rc=%d)\n", n);
        return SH_STATUS_ERROR;
    }
    if (n > 0) last = ents[n - 1].seq;
    (void)kbdw_fmt_mods(line, (int)sizeof(line), g_api->kbd_get_modifiers());
    g_api->kprintf(ATTR_CYAN, "kbdstat -w: ESC or %us to stop. start seq=%u mods=%s\n",
                   (u32)KBDW_TIMEOUT_SEC, last, line);

    t_end = g_api->get_tick() + (u32)(KBDW_TIMEOUT_SEC * KBDW_TICK_HZ);
    for (;;) {
        /* 先にキーを読む。IRQ1 は cooked リングへ積んだ同じ割り込みの中で
         * 記録リングへも積むので、ここで見えた ESC は下の読みで必ず行になる。
         * ESC 以外のキーは捨てる (終わった後のシェルに残さない)。 */
        while ((k = g_api->kbd_trygetkey()) >= 0) {
            if ((k & 0xFF) == KBDW_KEY_ESC) esc = 1;
        }
        n = g_api->kbd_diag_log(last, ents, KBD_DLOG_CAP);
        if (n > 0) {
            lost_now = kbdw_lost(last, ents[0].seq);
            if (lost_now != 0) {
                (void)kbdw_fmt_lost(line, (int)sizeof(line), last, ents[0].seq);
                g_api->kprintf(ATTR_YELLOW, "%s\n", line);
                lost += lost_now;
            }
            for (i = 0; i < n; i++) {
                (void)kbdw_fmt_ent(line, (int)sizeof(line), &ents[i]);
                g_api->kprintf(ATTR_WHITE, "%s\n", line);
            }
            last = ents[n - 1].seq;
            shown += (u32)n;
        }
        if (esc) break;
        if ((int)(g_api->get_tick() - t_end) >= 0) break;
        {   /* 1 tick 待つ (rshell の待ちと同じ形) */
            u32 w = g_api->get_tick() + 1;
            while (g_api->get_tick() < w) g_api->sys_halt();
        }
    }

    (void)kbdw_fmt_mods(line, (int)sizeof(line), g_api->kbd_get_modifiers());
    g_api->kprintf(ATTR_CYAN, "kbdstat -w: end (%s) shown=%u lost=%u mods=%s",
                   esc ? "ESC" : "timeout", shown, lost, line);
    /* EMPTY / ERROR で捨てたバイトはリングに積まれないので、増えた数を添える */
    if (have_d0 && g_api->kbd_diag(&d1) == 0) {
        g_api->kprintf(ATTR_CYAN, " empty+%u err+%u ovr+%u",
                       d1.empty_count - d0.empty_count, d1.err_count - d0.err_count,
                       (u32)d1.overrun_count - (u32)d0.overrun_count);
    }
    g_api->kprintf(ATTR_CYAN, "%s", "\n");
    return 0;
}

static int cmd_kbdstat(int argc, char **argv)
{
    KbdDiag d;
    int rc;
    if (argc >= 2 && str_eq(argv[1], "-w")) {
        return kbdstat_watch();
    }
    if (argc >= 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    rc = g_api->kbd_diag(&d);
    if (rc < 0) {
        g_api->kprintf(ATTR_RED, "kbdstat: kbd_diag failed (rc=%d)\n", rc);
        return SH_STATUS_ERROR;
    }
    g_api->kprintf(ATTR_WHITE,
                   "kbd irq=%u empty=%u err=%u ovr=%u flushed=%u init=%02x->%02x "
                   "cmd=%02x st=%02x code=%02x now=%02x\n",
                   d.irq_count, d.empty_count, d.err_count,
                   (u32)d.overrun_count, d.flushed,
                   (u32)d.init_st_before, (u32)d.init_st_after, (u32)d.cmd,
                   (u32)d.last_st, (u32)d.last_code, (u32)d.now_st);
    return 0;
}

/* 登録用テーブル */
static const ShellCmd sys_cmds[] = {
    { "mem",    cmd_mem,    "",              "Show memory statistics" },
    { "heap",   cmd_mem,    "",              "Alias for mem" },
    { "reboot", cmd_reboot, "",              "Reboot the system" },
    { "dev",    cmd_dev,    "",              "List block/char devices" },
    { "df",     cmd_dev,    "",              "Alias for dev" },
    { "ide",    cmd_ide,    "[0-3]",         "Show IDE drive geometry" },
    { "format", cmd_format, "[0-3] [sects]", "Format a drive to ext2" },
    { "play",   cmd_play,   "MML",           "Play MML via FM synth" },
    { "os32gui",cmd_os32gui,"[on|off]",      "Switch to GUI shell now, or set GUI at boot" },
    { "gfxmode",cmd_gfxmode,"pc98|pegc|cirrus|auto","Force the graphics backend at next boot" },
    { "kbdstat",cmd_kbdstat,"[-w]",          "Show keyboard 8251 counters (-w: log each byte, ESC/30s)" },
    { (const char *)0, 0, 0, 0 }
};

void shell_cmd_sys_init(void)
{
    shell_register_cmds(sys_cmds);
}
