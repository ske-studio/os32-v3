#include "machcfg_contract.h"
#include "kprintf.h"

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
        {MACHCFG_FILE_OK, 3, {2, 1}, "ok perf=2/file nap="},
        {MACHCFG_FILE_NOFS, 0, {0, 0}, "nofs perf=0/def nap="},
        {MACHCFG_FILE_NONE, 0, {0, 0}, "none perf=0/def nap="},
        {MACHCFG_FILE_BIG, 0, {0, 0}, "big perf=0/def nap="},
        {MACHCFG_FILE_OK, MACHCFG_KEY_PERF, {0, 0}, "ok perf=0/file nap="},
        {MACHCFG_FILE_OK, MACHCFG_KEY_NAP, {0, 1}, "ok perf=0/def nap="}
    };
    for (unsigned m = 0; m < sizeof(machines)/sizeof(machines[0]); ++m) {
        const typeof(*machines) *mch = &machines[m];
        struct machcfg_seed seed = mch->cpu ? (struct machcfg_seed){{0, 0}, mch->accepts}
                                           : platform_machcfg_seed();
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

static int run(void)
{
    const struct machcfg_seed seeds[] = {
        {{0, 0}, 1}, {{0, 0}, 0}, platform_machcfg_seed()
    };
    struct machcfg values = {2, 1};
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
        const struct machcfg_seed seed = {{1, 1}, 0};
        const struct machcfg candidate = {2, 0};
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
