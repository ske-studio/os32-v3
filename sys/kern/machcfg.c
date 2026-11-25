#include "machcfg_contract.h"
#include "kprintf.h"

static struct machcfg_seed machine_seed;
static struct machcfg current;
static int finished;
static u8 file_result;
static u8 file_keys;
static u8 reported;

void machcfg_begin(struct machcfg_seed seed)
{
    machine_seed = seed;
    current = seed.defaults;
    finished = 0;
    file_result = MACHCFG_FILE_NOFS;
    file_keys = 0;
    reported = 0;
}

void machcfg_finish(const struct machcfg *values)
{
    if (finished)
        return;
    struct machcfg next = values ? *values : machine_seed.defaults;
    if (!machine_seed.accepts_nap7450)
        next.nap7450 = machine_seed.defaults.nap7450;
    current = next;
    finished = 1;
}

const struct machcfg *machcfg_get(void)
{
    return &current;
}

void machcfg_note_file(u8 result, u8 keys)
{
    file_result = result;
    file_keys = keys;
}

void machcfg_report(const char *machine, u32 cpu_id, int seed_from_cpu)
{
    static const char *const files[] = {
        [MACHCFG_FILE_NOFS] = "nofs", [MACHCFG_FILE_NONE] = "none",
        [MACHCFG_FILE_BIG] = "big", [MACHCFG_FILE_OK] = "ok"
    };
    if (!finished || reported)
        return;
    reported = 1;
    const char *perf = file_keys & MACHCFG_KEY_PERF ? "file" : "def";
    const char *nap = file_keys & MACHCFG_KEY_NAP ? "file" : "def";
    if (machine_seed.accepts_nap7450 == 0)
        nap = "n-a";
    kprintf(KLOG_ATTR, "MACHCFG %s cpu=0x%x seed=%s file=%s perf=%u/%s nap=%u/%s\n",
            machine, cpu_id, seed_from_cpu ? "cpu" : "plat", (file_result < (unsigned)(sizeof(files) / sizeof(files[0])) ? files[file_result] : "?"),
            (u32)current.perf_request, perf, (u32)current.nap7450, nap);
}
