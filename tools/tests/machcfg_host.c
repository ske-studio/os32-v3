/* Expected seeds and overrides: MACHCFG_DESIGN §3, §7, P-R7. */
#include "machcfg_contract.h"
#include "kprintf.h"
#include "config.h"
#include "kstring.h"
#include "sysconfig.h"
#include "os32_snd2.h"
#include "vfs.h"

#ifdef TEST_POWERMAC
#include "platform_machcfg.h"
static u32 host_pvr;
u32 ppc_read_pvr(void) { return host_pvr; }

static int check_powermac_seeds(void)
{
    static const struct { u32 pvr; int matched, accepts; } cases[] = {
        {0x80000201, 1, 1}, {0x80010201, 1, 0}, {0x00088000, 0, 0}
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        struct machcfg_seed row;
        host_pvr = cases[i].pvr;
        if (pm_machcfg_seed(host_pvr, &row) != cases[i].matched)
            return 0;
        struct machcfg_seed seed = platform_machcfg_seed();
        if (row.accepts_nap7450 != cases[i].accepts || row.defaults.perf_request ||
            row.defaults.nap7450 || seed.accepts_nap7450 != cases[i].accepts ||
            seed.defaults.perf_request || seed.defaults.nap7450)
            return 0;
    }
    return 1;
}
#endif

_Static_assert(MACHCFG_FILE_MAX == TEST_SYSCONFIG_MAX - 1, "machine.cfg limit");

static char line[160];
static unsigned used;
static int bad;

u8 kprintf_attr_to_pc98(u8 attr) { return attr; }
void console_write(const char *s, u32 n, u8 attr)
{
    if (attr != KLOG_ATTR || n >= sizeof(line) - used) {
        bad = 1;
        return;
    }
    while (n--)
        line[used++] = *s++;
    line[used] = 0;
}
void shell_print(const char *s, u8 attr)
{
    while (*s)
        console_write(s++, 1, attr);
}
void shell_print_utf8(const char *s, u8 attr) { shell_print(s, attr); }

static int reports(void)
{
    static const struct {
        const char *machine, *prefix, *nap_def, *nap_file;
        u32 cpu;
        int accepts;
    } machines[] = {
        {"pc98", "MACHCFG pc98 cpu=0x0 seed=plat file=", "0/n-a", "0/n-a", 0, 0},
        {"powermac", "MACHCFG powermac cpu=0x80000201 seed=cpu file=", "0/def", "1/file", 0x80000201, 1},
        {"powermac", "MACHCFG powermac cpu=0x80010201 seed=cpu file=", "0/n-a", "0/n-a", 0x80010201, 0}
    };
    static const struct {
        int result, keys;
        struct machcfg values;
        const char *body;
    } cases[] = {
        {MACHCFG_FILE_OK, 3, {2, 1, 0}, "ok perf=2/file nap="},
        {MACHCFG_FILE_NOFS, 0, {0, 0, 0}, "nofs perf=0/def nap="},
        {MACHCFG_FILE_NONE, 0, {0, 0, 0}, "none perf=0/def nap="},
        {MACHCFG_FILE_BIG, 0, {0, 0, 0}, "big perf=0/def nap="},
        {MACHCFG_FILE_OK, MACHCFG_KEY_PERF, {0, 0, 0}, "ok perf=0/file nap="},
        {MACHCFG_FILE_OK, MACHCFG_KEY_NAP, {0, 1, 0}, "ok perf=0/def nap="}
    };
    for (unsigned m = 0; m < sizeof(machines)/sizeof(machines[0]); ++m) {
        const typeof(*machines) *mch = &machines[m];
#ifdef TEST_POWERMAC
        if (!mch->cpu) continue;
        host_pvr = mch->cpu;
        struct machcfg_seed seed = platform_machcfg_seed();
#else
        struct machcfg_seed seed = mch->cpu ? (struct machcfg_seed){{0, 0, 0}, mch->accepts}
                                            : platform_machcfg_seed();
#endif
        for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
            const typeof(*cases) *c = &cases[i];
            used = 0;
            machcfg_begin(seed);
            machcfg_report(mch->machine, mch->cpu, mch->cpu != 0);
            if (used) return 8;
            if (c->result != MACHCFG_FILE_NOFS)
                machcfg_note_file(c->result, c->keys);
            machcfg_finish(&c->values);
            machcfg_report(mch->machine, mch->cpu, mch->cpu != 0);
            machcfg_report(mch->machine, mch->cpu, mch->cpu != 0);
            const char *parts[] = {mch->prefix, c->body,
                c->values.nap7450 ? mch->nap_file : mch->nap_def, "\n"};
            const char *actual = line;
            for (unsigned j = 0; j < 4; ++j)
                for (const char *s = parts[j]; *s; ++s)
                    if (*actual++ != *s) return 9;
            if (*actual || bad) return 10;
        }
    }
    return 0;
}

static char config[MACHCFG_FILE_MAX + 1];
static int config_size;
static unsigned reads;

int vfs_read(const char *path, void *buf, u32 max)
{
    reads++;
    if (!kstrcmp(path, SYS_SYSTEM_CFG)) {
        static const char system_cfg[] = "GUI=1 ;x";
        if (max < sizeof(system_cfg) - 1) return -1;
        kmemcpy(buf, system_cfg, sizeof(system_cfg) - 1);
        return sizeof(system_cfg) - 1;
    }
    if (kstrcmp(path, SYS_MACHINE_CFG) || config_size < 0) return -1;
    if ((u32)config_size > max) return -1;
    for (int i = 0; i < config_size; i++) ((char *)buf)[i] = config[i];
    return config_size;
}

static int line_endings(void)
{
    const char *path = SYS_MACHINE_CFG;
    char value[2];
    config[0] = 'K';
    config[1] = '=';
    config[2] = '2';
    for (int i = 3; i < MACHCFG_LINE_MAX; i++) config[i] = ' ';
    config[MACHCFG_LINE_MAX] = '\r';
    config[MACHCFG_LINE_MAX + 1] = '\n';
    config_size = MACHCFG_LINE_MAX + 2;
    sysconfig_temp_begin(path);
    int valid = sysconfig_get_int(path, "K", -1) == 2;
    sysconfig_temp_end();
    config_size--;
    sysconfig_temp_begin(path);
    valid &= sysconfig_get_str(path, "K", value, sizeof(value)) == -1;
    sysconfig_temp_end();
    config[3] = '\r';
    config_size = 4;
    sysconfig_temp_begin(path);
    valid &= sysconfig_get_int(path, "K", -1) == -1;
    sysconfig_temp_end();
    return valid;
}

static int root_loads(void)
{
    static const struct {
        const char *text;
        int pad_to, result, perf, nap;
        const char *perf_field, *nap_field;
    } cases[] = {
        {0, 0, MACHCFG_FILE_NONE, 0, 0, "none perf=0/def", "0/def"},
        {"", 0, MACHCFG_FILE_NONE, 0, 0, "none perf=0/def", "0/def"},
        {"perf.profile=2\nppc7450.nap=1\n", MACHCFG_FILE_MAX, MACHCFG_FILE_OK,
         2, 1, "ok perf=2/file", "1/file"},
        {"perf.profile=2\nppc7450.nap=1\n", MACHCFG_FILE_MAX + 1, MACHCFG_FILE_BIG,
         0, 0, "big perf=0/def", "0/def"},
        {"perf.profile=3\nppc7450.nap=2", 0, MACHCFG_FILE_OK, 0, 0, "ok perf=0/def", "0/def"},
        {"perf.profile=0\nperf.profile=2\nppc7450.nap=0", 0, MACHCFG_FILE_OK,
         0, 0, "ok perf=0/file", "0/file"},
        {"perf.profile=x\nppc7450.nap=1", 0, MACHCFG_FILE_OK, 0, 1, "ok perf=0/def", "1/file"},
        {"perf.profile=4294967296", 0, MACHCFG_FILE_OK, 0, 0, "ok perf=0/def", "0/def"},
    };
    static const struct {
        u32 cpu;
        int accepts;
        const char *prefix;
    } machines[] = {
        {0, 0, "MACHCFG pc98 cpu=0x0 seed=plat file="},
        {0x80000201, 1, "MACHCFG powermac cpu=0x80000201 seed=cpu file="},
        {0x80010201, 0, "MACHCFG powermac cpu=0x80010201 seed=cpu file="},
    };
    for (unsigned m = 0; m < sizeof(machines)/sizeof(machines[0]); m++) {
        const typeof(*machines) *machine = &machines[m];
#ifdef TEST_POWERMAC
        if (!machine->cpu) continue;
        host_pvr = machine->cpu;
        struct machcfg_seed seed = platform_machcfg_seed();
#else
        struct machcfg_seed seed = machine->cpu ? (struct machcfg_seed){{0, 0, 0}, machine->accepts}
                                            : platform_machcfg_seed();
#endif
        for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
            const typeof(*cases) *c = &cases[i];
            config_size = -1;
            if (c->text) {
                config_size = kstrlen(c->text);
                kmemcpy(config, c->text, config_size);
                while (config_size < c->pad_to) config[config_size++] = ' ';
            }
            used = reads = 0;
            machcfg_begin(seed);
            if (machcfg_load_root() != c->result || reads != 1 ||
                machcfg_get()->perf_request != c->perf ||
                machcfg_get()->nap7450 != (machine->accepts ? c->nap : 0)) return 0;
            machcfg_report(machine->cpu ? "powermac" : "pc98", machine->cpu, machine->cpu != 0);
            const char *parts[] = {machine->prefix, c->perf_field, " nap=",
                                   machine->accepts ? c->nap_field : "0/n-a", "\n"};
            const char *actual = line;
            for (unsigned j = 0; j < sizeof(parts)/sizeof(parts[0]); j++) {
                unsigned len = kstrlen(parts[j]);
                if (kstrncmp(actual, parts[j], len)) return 0;
                actual += len;
            }
            if (bad || *actual) return 0;
            sysconfig_get_int(SYS_MACHINE_CFG, "perf.profile", -1);
            if (reads != 2 || sysconfig_get_int(SYS_SYSTEM_CFG, "GUI", -1) != 1) return 0;
        }
    }
    return 1;
}

static int sound_modes(void)
{
    static const struct { const char *text; u8 mode; } cases[] = {
        {"snd.fm=hw", SND2_DEV_FM_HW}, {"snd.fm=soft", SND2_DEV_FM_SOFT},
        {"snd.fm=none", SND2_DEV_FM_NONE}, {"snd.fm=auto", 0}
    };
    struct machcfg_seed seed = platform_machcfg_seed();
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        config_size = kstrlen(cases[i].text);
        kmemcpy(config, cases[i].text, config_size);
        machcfg_begin(seed);
        if (machcfg_load_root() != MACHCFG_FILE_OK ||
            machcfg_get()->snd_flags != (seed.defaults.snd_flags | cases[i].mode)) return 0;
    }
    return 1;
}

static int run(void)
{
#ifdef TEST_POWERMAC
    if (!check_powermac_seeds()) return 22;
#endif
    if (!sound_modes()) return 23;
    if (!root_loads()) return 21;
    if (!line_endings()) return 20;
    const struct machcfg_seed seeds[] = {
        {{0, 0, 0}, 1}, {{0, 0, 0}, 0}, platform_machcfg_seed()
    };
    struct machcfg values = {2, 1, 0};
    unsigned int i;
    for (i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        machcfg_begin(seeds[i]);
        if (machcfg_get()->perf_request != 0 || machcfg_get()->nap7450 != 0)
            return 1;
        machcfg_finish(&values);
        if (machcfg_get()->perf_request != 2 ||
            machcfg_get()->nap7450 != (i == 0 ? 1 : 0))
            return 2;
        machcfg_finish(0);
        if (machcfg_get()->perf_request != 2 ||
            machcfg_get()->nap7450 != (i == 0 ? 1 : 0))
            return 3;
        machcfg_begin(seeds[i]);
        machcfg_finish(0);
        machcfg_finish(&values);
        if (machcfg_get()->perf_request != 0 || machcfg_get()->nap7450 != 0)
            return 4;
    }
    {
        const struct machcfg_seed seed = {{1, 1, 0}, 0};
        const struct machcfg candidate = {2, 0, 0};
        machcfg_begin(seed);
        if (machcfg_get()->perf_request != 1 || machcfg_get()->nap7450 != 1)
            return 5;
        machcfg_finish(0);
        if (machcfg_get()->perf_request != 1 || machcfg_get()->nap7450 != 1)
            return 6;
        machcfg_begin(seed);
        machcfg_finish(&candidate);
        if (machcfg_get()->perf_request != 2 || machcfg_get()->nap7450 != 1)
            return 7;
    }
    return 0;
}

void _start(void)
{
    int status = run();
    if (!status) status = reports();
    __asm__ volatile ("int $0x80" : : "a" (1), "b" (status));
    __builtin_unreachable();
}
