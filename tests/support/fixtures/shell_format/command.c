#include "userland/shell/cmd_sys.c"

static unsigned calls, helps;
static int drive;
static u32 sectors;

static int record_format(int drv, u32 count)
{
    ++calls;
    drive = drv;
    sectors = count;
    return 0;
}

static void quiet(u8 attr, const char *fmt, ...) {}
void shell_print_help(const char *name) { ++helps; }
static KernelAPI api = {.kprintf = quiet, .ext2_format = record_format};
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
        calls = helps = 0;
        int rc = cmd_format(1 + !!argv[1] + !!argv[2], argv);
        if (cases[i].drv < 0) {
            if (rc != SH_STATUS_USAGE || calls || helps != 1) return 1;
        } else if (rc || calls != 1 || helps || drive != cases[i].drv ||
                   sectors != cases[i].sectors) return 2;
    }
    char *extra[] = {"format", "0", "128", "extra", 0};
    calls = helps = 0;
    return cmd_format(4, extra) != SH_STATUS_USAGE || calls || helps != 1;
}
