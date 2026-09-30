#include "shell.h"
#include "config.h"
#include "os32/help.h"

/* ======================================================================== */
/*  基本コマンドモジュール (cmd_base.c)                                     */
/* ======================================================================== */

static int cmd_help(int argc, char **argv)
{
    int count, i;
    const ShellCmd *cmds;
    
    if (argc > 1) {
        /* manページを参照 */
        if (os32_help_show(argv[1]) != 0) {
            g_api->kprintf(ATTR_RED, "No manual entry for %s\n", argv[1]);
            g_api->kprintf(ATTR_WHITE, "%s", "  Use 'help' to list available commands.\n");
            return SH_STATUS_ERROR;
        }
        return 0;
    }

    g_api->kprintf(ATTR_CYAN, "%s", "OS32 Shell Commands:\n");
    cmds = shell_get_cmds(&count);
    
    for (i = 0; i < count; i++) {
        char pad[12];
        int len = str_len(cmds[i].name);
        int p = 0;
        while (len < 10 && p < 11) { pad[p++] = ' '; len++; }
        pad[p] = '\0';
        g_api->kprintf(ATTR_WHITE, "  %s%s", cmds[i].name, pad);
        /* manページ有無チェック */
        if (os32_help_exists(cmds[i].name)) {
            g_api->kprintf(ATTR_GREEN, "%s", "[man]\n");
        } else {
            g_api->kprintf(ATTR_WHITE, "%s", "\n");
        }
    }
    g_api->kprintf(ATTR_WHITE, "%s", "\n  Use 'help <cmd>' or 'man <cmd>' for details.\n");
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_api->tvram_clear();
    return 0;
}

static int cmd_tick(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_api->kprintf(ATTR_WHITE, "Timer ticks: %u (%u sec)\n", g_api->get_tick(), g_api->get_tick()/100);
    return 0;
}

static int cmd_ver(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_api->kprintf(ATTR_GREEN, "PC-9801 OS32 v%s (Ring3 Native)\n",
                   SYS_VERSION);
    g_api->kprintf(ATTR_CYAN, "%s", "  CPU: Intel 386+ (Protected Mode + Paging)\n");
    g_api->kprintf(ATTR_CYAN, "%s", "  PIC: 8259A x2 (remapped to INT 20h+)\n");
    g_api->kprintf(ATTR_CYAN, "%s", "  PIT: 8254 @ 100Hz\n");
    g_api->kprintf(ATTR_CYAN, "%s", "  KBD: uPD8251A (IRQ1)\n");
    g_api->kprintf(ATTR_CYAN, "%s", "  SER: uPD8251A RS-232C (IRQ4)\n");
    g_api->kprintf(ATTR_CYAN, "%s", "  SND: YM2203 (OPN) FM3+SSG3\n");
    g_api->kprintf(ATTR_CYAN, "%s", "  GFX: 640x400x16 CPU direct\n");
    g_api->kprintf(ATTR_WHITE, "  API: v%u\n", g_api->version);
    /* Build はカーネルを組んだ日時 (以前はシェル自身の __DATE__ だった)。
     * Commit / Image CRC は票 TASK_SERIAL_HOSTFS A-4 (KAPI v65)。
     * 更新の証拠は Image CRC (ローダが検査して起動した vmkernel.lz4)。 */
    {
        char build[32];
        BootImageInfo bi;

        g_api->sys_get_build_info(build, (int)sizeof(build));
        g_api->kprintf(ATTR_WHITE, "  Build: %s\n", build);
        if (g_api->version >= 65 && g_api->boot_image_info(&bi) == 0) {
            g_api->kprintf(ATTR_WHITE, "  Commit: %s\n", bi.commit);
            if (bi.crc_valid)
                g_api->kprintf(ATTR_WHITE, "  Image CRC: %08x (%u bytes, %s loader)\n",
                               bi.image_crc, bi.image_size,
                               bi.source == 2 ? "HDD" : bi.source == 1 ? "FD" : "?");
            else
                g_api->kprintf(ATTR_WHITE, "%s", "  Image CRC: none (loader did not record)\n");
        }
    }
    return 0;
}

static const char *wday_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static int cmd_date(int argc, char **argv)
{
    RTC_Time_Ext t;
    const char *w;
    (void)argc; (void)argv;
    g_api->rtc_read(&t);
    w = (t.wday < 7) ? wday_names[t.wday] : "???";
    g_api->kprintf(ATTR_WHITE, "20%02u-%02u-%02u %02u:%02u:%02u (%s)\n",
                   (u32)t.year, (u32)t.month, (u32)t.day,
                   (u32)t.hour, (u32)t.min, (u32)t.sec,
                   w);
    return 0;
}

static int cmd_beep(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_api->fm_startup_sound();
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    u32 s = g_api->get_tick() / 100;
    (void)argc; (void)argv;
    g_api->kprintf(ATTR_WHITE, "up %u min %u sec\n", s / 60, s % 60);
    return 0;
}

static int cmd_np2(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (g_api->np2_detect()) {
        char buf[64];
        g_api->kprintf(ATTR_GREEN, "%s", "NP21/W detected!\n");
        g_api->np2_get_version(buf, sizeof(buf));
        g_api->kprintf(ATTR_WHITE, "  Ver: %s\n", buf);
    } else {
        g_api->kprintf(ATTR_RED, "%s", "Not NP21/W.\n");
    }
    return 0;
}

static int cmd_time(int argc, char **argv)
{
    u32 start, end, elapsed_ms;
    char cmd_buf[TIME_CMD_MAX];
    int i, bp;
    int inner;

    if (argc < 2) {
        g_api->kprintf(ATTR_RED, "%s", "Usage: time COMMAND [ARGS...]\n");
        return SH_STATUS_USAGE;
    }

    /* T3: 溢れた引数を落として計測すると、意図と違う行が走る。組み立てる
     * 前に数えて断る。`time` はクォートを付け直さないので、数えるのは本体と
     * 区切りの空白だけ ([C4] 上限は TIME_CMD_MAX から出す)。 */
    {
        int need = 0;
        for (i = 1; i < argc; i++) {
            const char *s = argv[i];
            if (i > 1) need++;                 /* 区切りの空白 */
            while (*s++) need++;
        }
        if (need > TIME_CMD_MAX - 2) {
            sh_refuse("time: command line", TIME_CMD_MAX - 2);
            return SH_STATUS_USAGE;
        }
    }

    /* argv[1..] からコマンドライン文字列を再構築 */
    bp = 0;
    for (i = 1; i < argc && bp < TIME_CMD_MAX - 2; i++) {
        const char *s = argv[i];
        if (i > 1 && bp < TIME_CMD_MAX - 2) cmd_buf[bp++] = ' ';
        while (*s && bp < TIME_CMD_MAX - 2) cmd_buf[bp++] = *s++;
    }
    cmd_buf[bp] = '\0';

    start = g_api->get_tick();
    /* 票 §2-3: `time` の値は **内側の値**。 */
    inner = execute_command(cmd_buf);
    end = g_api->get_tick();

    elapsed_ms = (end - start) * 10; /* 1ティック = 10ms */
    g_api->kprintf(ATTR_CYAN, "\nreal  %u.%03us\n",
                   elapsed_ms / 1000, elapsed_ms % 1000);
    return inner;
}

#ifdef SHELL_AS_APP
/* exit [N] — sh.bin (端末の子) を終わらせる (D2(d))。常駐シェルには登録
 * しない (表は先勝ちなので、常駐側は cmd_script.c が別の `exit` を登録する。
 * 票 §2-5)。N は sh_exit_code と main の戻り値に載る。
 *
 * 往復 5 の注意 1: **値を書いてから要求を立てる**。`exit 3 | echo tail` で
 * 段ループがこの要求を見て抜けたあとも、その値が行の `$?` として残る。 */
static int cmd_exit(int argc, char **argv)
{
    int code = 0;

    if (sh_exit_arg(argc, argv, &code) < 0) {
        /* 不正な引数では **終わらせない** (票 §2-5-1)。 */
        return SH_STATUS_USAGE;
    }
    sh_status_set(code);        /* 先に値 */
    sh_exit_code = code;
    sh_exit_flag = 1;           /* そのあと要求 */
    return code;
}
#endif

/* 登録用テーブル */
static const ShellCmd base_cmds[] = {
    { "help",   cmd_help,   "[cmd]", "Show help" },
    { "?",      cmd_help,   "[cmd]", "Alias for help" },
    { "clear",  cmd_clear,  "",      "Clear the screen" },
    { "cls",    cmd_clear,  "",      "Alias for clear" },
    { "tick",   cmd_tick,   "",      "Show system uptime ticks" },
    { "ver",    cmd_ver,    "",      "Show OS version" },
    { "uname",  cmd_ver,    "",      "Alias for ver" },
    { "date",   cmd_date,   "",      "Show RTC date/time" },
    { "beep",   cmd_beep,   "",      "Play startup beep" },
    { "uptime", cmd_uptime, "",      "Show sys uptime" },
    { "np2",    cmd_np2,    "",      "Detect NP21/W emulator" },
    { "time",   cmd_time,   "CMD",   "Measure command time" },
#ifdef SHELL_AS_APP
    { "exit",   cmd_exit,   "[N]",   "Leave this shell" },
#endif
    { (const char *)0, 0, 0, 0 }
};

void shell_cmd_base_init(void)
{
    shell_register_cmds(base_cmds);
}

