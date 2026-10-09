/* USER observation: errors preserve the whole buffer; no invalid pointer kills. */
#include "os32api.h"

#define TEST_ATTR 0x07
#define CHECK(test) do { total++; if (test) passed++; } while (0)

int main(int argc, char **argv, KernelAPI *api)
{
    MemStat stat;
    union { MemStat align; u8 bytes[sizeof(MemStat) + 16]; } out;
    int passed = 0, total = 0, self, others[3], n = 0;
    int bad[] = {1, 99, -2};
    u32 sizes[] = {0, MEMSTAT_MIN - 1, MEMSTAT_MIN, 120, sizeof(MemStat), sizeof(out)};
    (void)argc; (void)argv;
    int rc = api->mem_stat(-1, &stat, sizeof(stat));
    CHECK(rc == sizeof(stat) && stat.size == sizeof(stat) &&
          (stat.flags & MEMSTAT_HAS_AS) && stat.app_id >= 2 && stat.app_id <= 5);
    if (passed != total) goto done;
    self = stat.app_id;
    CHECK(api->mem_stat(self, &stat, sizeof(stat)) == sizeof(stat) && stat.app_id == self);
    CHECK(api->mem_stat(0, &stat, sizeof(stat)) == sizeof(stat) &&
          stat.app_id == 0 && !(stat.flags & MEMSTAT_HAS_AS));
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
        u32 size = sizes[j], written = size < MEMSTAT_MIN ? 0 :
            (size < sizeof(stat) ? size : sizeof(stat));
        for (u32 i = 0; i < sizeof(out); i++) out.bytes[i] = 0xa5;
        rc = api->mem_stat(-1, &out, size);
        int unchanged = 1;
        for (u32 i = written; i < sizeof(out); i++) if (out.bytes[i] != 0xa5) unchanged = 0;
        CHECK(unchanged && (written ? rc == (int)written && out.align.size == written : rc == OS32_ERR_INVAL));
    }
    if (passed == total)
        api->kprintf(TEST_ATTR, "mem_stat_test PASS self=%d others=%d,%d,%d\n", self, others[0], others[1], others[2]);
done:
    api->kprintf(TEST_ATTR, "mem_stat_test: %s %d/%d\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
