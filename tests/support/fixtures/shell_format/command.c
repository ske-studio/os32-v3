#include "userland/shell/cmd_sys.c"

static unsigned calls, helps;
static int drive, denied, permission_calls, permission_drive, format_rc;
static const char *target, *reason;
static int shown_rc, complete, denied_message;
static u32 sectors;

static int record_format(int drv, u32 count)
{
    ++calls;
    drive = drv;
    sectors = count;
    return format_rc;
}

static int same(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void record_print(u8 attr, const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    if (same(fmt, "Formatting %s...\n"))
        target = __builtin_va_arg(args, const char *);
    else if (same(fmt, "Format failed: %s (%d)\n")) {
        reason = __builtin_va_arg(args, const char *);
        shown_rc = __builtin_va_arg(args, int);
    } else if (same(fmt, "%s")) {
        const char *msg = __builtin_va_arg(args, const char *);
        complete += same(msg, "Format complete.\n");
        denied_message += same(msg, "Format failed: disk write not allowed (-1)\n");
    }
    __builtin_va_end(args);
}

static int permission(int drv, u32 lba, u32 count, const void *buf)
{
    ++permission_calls;
    permission_drive = drv;
    return lba || count || buf || denied ? -1 : 0;
}
void shell_print_help(const char *name) { ++helps; }
static KernelAPI api = {.ide_write_sectors = permission, .kprintf = record_print, .ext2_format = record_format};
KernelAPI *g_api = &api;

int probe(void)
{
    static const struct {
        char *name, *count;
        int drv;
        u32 sectors;
    } cases[] = {
        {"0", 0, 0, 2880}, {"1", "001", 1, 1}, {"2", 0, 2, 2880},
        {"3", "4294967295", 3, 0xffffffffu},
        {"lo0", 0, 0x600, 2880}, {"lo1", 0, 0x601, 2880},
        {"lo2", 0, 0x602, 2880}, {"lo3", "128", 0x603, 128},
        {0, 0, -1}, {"", 0, -1}, {"10", 0, -1}, {"4", 0, -1},
        {"lo", 0, -1}, {"lo4", 0, -1}, {"lo01", 0, -1}, {"0x", 0, -1},
        {"0", "", -1}, {"0", "0", -1}, {"0", "-1", -1},
        {"0", "12x", -1}, {"0", "4294967296", -1},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        char *argv[] = {"format", cases[i].name, cases[i].count, 0};
        calls = helps = permission_calls = complete = 0;
        target = 0;
        int rc = cmd_format(1 + !!argv[1] + !!argv[2], argv);
        if (cases[i].drv < 0) {
            if (rc != SH_STATUS_USAGE || calls || permission_calls || helps != 1) return 1;
        } else if (rc || calls != 1 || helps || drive != cases[i].drv ||
                   sectors != cases[i].sectors || complete != 1 || !same(target, argv[1]) ||
                   permission_calls != (drive < 4) || (drive < 4 && permission_drive != drive)) return 2;
    }
    char *hd[] = {"format", "0", 0};
    denied = 1;
    calls = 0;
    target = 0;
    if (cmd_format(2, hd) != SH_STATUS_ERROR || calls || target || denied_message != 1) return 3;
    denied = 0;
    static const struct { int rc; const char *message; } errors[] = {
        {-1, "I/O error"}, {-5, "insufficient space"}, {-10, "invalid partition or length"},
        {-13, "no approved partition"}, {-17, "device busy or reservation unavailable"},
    };
    for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; ++i) {
        format_rc = errors[i].rc;
        reason = 0;
        complete = calls = 0;
        if (cmd_format(2, hd) != SH_STATUS_ERROR || calls != 1 || complete ||
            shown_rc != format_rc || !same(reason, errors[i].message)) return 4;
    }
    char *extra[] = {"format", "0", "128", "extra", 0};
    calls = helps = 0;
    return cmd_format(4, extra) != SH_STATUS_USAGE || calls || helps != 1;
}
