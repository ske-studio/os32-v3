/* T3c2: run under both resident shells. STOP is driven by the PM, never timed.
 * --once is the nested parent; --leaf leaves allocations for exit reclamation.
 * Registration and the GUI/CUI procedure: tools/accept/t3c2.md. */
#include "os32api.h"
#include "rt/testresult.h"
#include <stdlib.h>
#include <string.h>

#define PROBE_BYTES 4096u
#define CYCLE_COUNT 20
#define SELF "/usr/bin/layout_cycle_test.bin"
static volatile u8 bss_probe[PROBE_BYTES];
static int run_cycles(KernelAPI *api, const char *mode, int rounds, int nested);

int main(int argc, char **argv, KernelAPI *api)
{
    const char *mode = argc > 1 ? argv[1] : "exit";
    int rounds = CYCLE_COUNT, nested = 0;
    if (!strcmp(mode, "--leaf")) {
        u8 *a = malloc(PROBE_BYTES), *b = api->mem_alloc(PROBE_BYTES);
        if (!a || !b) return 1;
        memset(a, 0x5a, PROBE_BYTES);
        memset(b, 0x69, PROBE_BYTES);
        return 0; /* both heaps are deliberately reclaimed by exit */
    }
    if (!strcmp(mode, "--once") && argc == 3) {
        rounds = 1;
        mode = argv[2];
    }
    if (!strncmp(mode, "nested-", 7)) { nested = 1; mode += 7; }
    if (strcmp(mode, "exit") && strcmp(mode, "fault") && strcmp(mode, "stop")) {
        api->kprintf(0x07, "layout_cycle_test: FAIL usage: exit|fault|stop|nested-exit|nested-fault|nested-stop\n");
        return 1;
    }
    return run_cycles(api, mode, rounds, nested);
}

static int same_usage(const MemStat *a, const MemStat *b)
{
    return a->phys_free_pages == b->phys_free_pages &&
        a->owner_pages == b->owner_pages && a->exec_heap_used == b->exec_heap_used &&
        a->extents_total == b->extents_total &&
        a->resident_heap_used == b->resident_heap_used && a->kheap_used == b->kheap_used &&
        a->leftover_pages == b->leftover_pages && a->lease_active == b->lease_active;
}

static int run_cycles(KernelAPI *api, const char *mode, int rounds, int nested)
{
    volatile u8 stack_probe[PROBE_BYTES];
    u8 *libc_probe = malloc(PROBE_BYTES), *exec_probe = api->mem_alloc(PROBE_BYTES);
    MemStat before, after;
    int passed = 0, total = 0;
    const char *command = SELF " --leaf";
    int expected = EXEC_KIND_EXITED, expected_code = 0;
    if (nested) {
        command = !strcmp(mode, "exit") ? SELF " --once exit" :
            !strcmp(mode, "fault") ? SELF " --once fault" : SELF " --once stop";
    } else if (strcmp(mode, "exit")) {
        command = !strcmp(mode, "fault") ? "/usr/bin/faulttest.bin ud" : "/usr/bin/faulttest.bin loop";
        expected = !strcmp(mode, "fault") ? EXEC_KIND_FAULT : EXEC_KIND_ABORTED;
        expected_code = EXEC_ERR_FAULT;
    }
#define CHECK(c) do { total++; if (c) passed++; else { \
    api->kprintf(0x0c, "layout_cycle_test: FAIL %s\n", #c); goto done; } } while (0)
    CHECK(libc_probe && exec_probe);
    for (u32 j = 0; j < PROBE_BYTES; j++) {
        bss_probe[j] = stack_probe[j] = (u8)(j ^ 0xa5);
        libc_probe[j] = (u8)(j ^ 0x3c);
        exec_probe[j] = (u8)(j ^ 0xc3);
    }
    api->kprintf(0x07, "layout_cycle_test: begin mode=%s nested=%d rounds=%d\n", mode, nested, rounds);
    CHECK(api->mem_stat(-1, &before, sizeof(before)) == (int)sizeof(before));
    for (int i = 0; i < rounds; i++) {
        int kind = EXEC_KIND_NONE, code = 0;
        int rc = api->exec_run(command);
        CHECK(api->exec_last_result(&kind, &code) == 0);
        CHECK(kind == expected && code == expected_code && rc == expected_code);
        for (u32 j = 0; j < PROBE_BYTES; j++) {
            CHECK(bss_probe[j] == (u8)(j ^ 0xa5) && stack_probe[j] == (u8)(j ^ 0xa5) &&
                  libc_probe[j] == (u8)(j ^ 0x3c) && exec_probe[j] == (u8)(j ^ 0xc3));
        }
        CHECK(api->mem_stat(-1, &after, sizeof(after)) == (int)sizeof(after));
        api->kprintf(0x07, "layout_cycle_test: usage cycle=%d pool=%u owner=%u exec=%u resident=%u kheap=%u\n",
            i + 1, after.phys_free_pages, after.owner_pages, after.exec_heap_used,
            after.resident_heap_used, after.kheap_used);
        CHECK(same_usage(&before, &after));
        api->kprintf(0x07, "layout_cycle_test: cycle=%d mode=%s nested=%d sentinel=PASS usage=PASS\n",
            i + 1, mode, nested);
    }
done:
    if (libc_probe) free(libc_probe);
    if (exec_probe) api->mem_free(exec_probe);
    return os32_test_summary(api, "layout_cycle_test", passed, total);
#undef CHECK
}
