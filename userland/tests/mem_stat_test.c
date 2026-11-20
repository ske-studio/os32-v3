/* USER observation: errors preserve the whole buffer; no invalid pointer kills. */
#include "os32api.h"
#include <stdio.h>
#include <string.h>

#define TEST_ATTR 0x07
#define CHECK(test) do { total++; if (test) passed++; } while (0)

int main(int argc, char **argv, KernelAPI *api)
{
    MemStat stat;
    union { MemStat align; u8 bytes[sizeof(MemStat) + 16]; } out;
    int passed = 0, total = 0, self, others[3], n = 0;
    int bad[] = {1, 99, -2};
    u32 sizes[] = {0, MEMSTAT_MIN - 1, MEMSTAT_MIN, 120, 132, sizeof(MemStat), sizeof(out)};
    (void)argc; (void)argv;
    memset(&stat, 0xa5, sizeof(stat));
    int rc = api->mem_stat(-1, &stat, sizeof(stat));
    CHECK(rc == sizeof(stat) && stat.size == sizeof(stat) &&
          (stat.flags & MEMSTAT_HAS_AS) && stat.app_id >= 2 && stat.app_id <= 5);
    if (passed != total) goto done;
    CHECK(sizeof(MemStat) == 220 && stat.phys_total_pages > 0 && stat.owner_pages > 0);
    self = stat.app_id;
    memset(&stat, 0xa5, sizeof(stat));
    CHECK(api->mem_stat(self, &stat, sizeof(stat)) == sizeof(stat) && stat.app_id == self &&
          (stat.flags & MEMSTAT_HAS_AS));
    memset(&stat, 0xa5, sizeof(stat));
    rc = api->mem_stat(0, &stat, sizeof(stat));
    CHECK(rc == sizeof(stat) && stat.size == sizeof(stat) &&
          stat.app_id == 0 && !(stat.flags & MEMSTAT_HAS_AS));
    int accounting_ok = stat.owner_pages == 0 && stat.phys_total_pages > 0 &&
        stat.kheap_peak >= stat.kheap_used && stat.resident_heap_peak >= stat.resident_heap_used;
#ifdef __PPC__
    if (argc == 2 && strcmp(argv[1], "kheap-used") == 0) {
        char value[16];
        if (passed != total || !stat.kheap_used || stat.kheap_used == 0xa5a5a5a5U ||
            stat.kheap_used > stat.kheap_total) return 1;
        snprintf(value, sizeof(value), "%u\n", stat.kheap_used);
        for (char *c = value; *c; c++) api->shell_putchar(*c, TEST_ATTR);
        return 0;
    }
    int diag_ok = !stat.kstack_high_water && !stat.audit_runs && !stat.audit_fail &&
        !stat.kselftest_pass && !stat.kselftest_fail && !stat.kdebug_state &&
        !stat.kdebug_pair_crc && !stat.kdebug_pages && !stat.kdebug_owner_pages &&
        !stat.kdebug_open_count;
#else
    int diag_ok = stat.kstack_high_water > 0 && (stat.kdebug_state == 1 ?
        (stat.kselftest_pass >= 295 && stat.kselftest_fail == 0) :
        (stat.kselftest_pass == 0 && stat.kselftest_fail == 0));
#endif
    CHECK(accounting_ok && diag_ok);
    /* Rejection is a failure, distinct from an intentional OFF boot. */
    CHECK(stat.kdebug_state < 2);
    CHECK(stat.kdebug_state == 1 ? stat.kdebug_owner_pages > 0 : stat.kdebug_owner_pages == 0);
    for (int id = 2; id <= 5; id++) {
        if (id == self) continue;
        others[n++] = id;
        for (u32 i = 0; i < sizeof(out); i++) out.bytes[i] = 0xa5;
        rc = api->mem_stat(id, &out, sizeof(out));
        int unchanged = 1;
        for (u32 i = 0; i < sizeof(out); i++) if (out.bytes[i] != 0xa5) unchanged = 0;
        CHECK(rc == OS32_ERR_INVAL && unchanged);
    }
    for (u32 j = 0; j < sizeof(bad) / sizeof(bad[0]); j++) {
        for (u32 i = 0; i < sizeof(out); i++) out.bytes[i] = 0xa5;
        rc = api->mem_stat(bad[j], &out, sizeof(out));
        int unchanged = 1;
        for (u32 i = 0; i < sizeof(out); i++) if (out.bytes[i] != 0xa5) unchanged = 0;
        CHECK(rc == OS32_ERR_INVAL && unchanged);
    }
    for (u32 j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
        int ok = 1;
        u32 last = sizes[j] + (sizes[j] == 132 || sizes[j] == sizeof(stat));
        for (u32 size = sizes[j]; size <= last; size++) {
            u32 written = size < MEMSTAT_MIN ? 0 :
                (size < sizeof(stat) ? size : sizeof(stat));
            for (u32 i = 0; i < sizeof(out); i++) out.bytes[i] = 0xa5;
            if (size == 133) {
                if (api->mem_stat(self, &stat, sizeof(stat)) != sizeof(stat)) ok = 0;
                stat.size = size;
                for (u32 i = 0; i < size; i++) out.bytes[i] = ~((u8 *)&stat)[i];
            }
            rc = api->mem_stat(-1, &out, size);
            if (size == 133 && memcmp(out.bytes, &stat, size) != 0) ok = 0;
            int unchanged = 1;
            for (u32 i = written; i < sizeof(out); i++) if (out.bytes[i] != 0xa5) unchanged = 0;
            if (!unchanged || (written ? rc != (int)written || out.align.size != written : rc != OS32_ERR_INVAL)) ok = 0;
        }
        CHECK(ok);
    }
#ifndef __PPC__
    if (passed == total)
        api->kprintf(TEST_ATTR, "mem_stat_test PASS self=%d others=%d,%d,%d\n", self, others[0], others[1], others[2]);
done:
    api->kprintf(TEST_ATTR, "mem_stat_test: %s %d/%d\n", passed == total ? "PASS" : "FAIL", passed, total);
    printf("mem_stat_test: %s %d/%d\n", passed == total ? "PASS" : "FAIL", passed, total);
#else
done: ;
    const char *message = passed == total ? "MEMSTAT OK\n" : "MEMSTAT FAIL\n";
    while (*message)
        api->shell_putchar(*message++, TEST_ATTR);
#endif
    return passed == total ? 0 : 1;
}
