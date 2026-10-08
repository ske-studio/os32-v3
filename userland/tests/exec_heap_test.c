/* f9/f10: one deployed executable also serves as its own nested child. */
#include "os32api.h"
#include "rt/testresult.h"

#define HEAP_INITIAL_BASE 0x88000000UL
#define HEAP_INITIAL_BYTES 65536UL
#define HEAP_PROBE_BYTES 16384UL
#define HEAP_PROBE_COUNT 6
#define HEAP_CHILD_COMMAND "/usr/bin/exec_heap_test.bin --child"

int main(int argc, char **argv, KernelAPI *api)
{
    u8 *blocks[HEAP_PROBE_COUNT], *edge[3];
    const u32 sizes[3] = {65535, 65536, 65537};
    int passed = 0, total = 0, extended = 0;
    int child = argc > 1 && argv[1][0] == '-' && argv[1][2] == 'c';
#define CHECK(c) do { total++; if (c) passed++; else goto done; } while (0)
    for (u32 i = 0; i < HEAP_PROBE_COUNT; i++) {
        blocks[i] = api->mem_alloc(HEAP_PROBE_BYTES);
        CHECK(blocks[i] != 0);
        if ((u32)blocks[i] < HEAP_INITIAL_BASE ||
            (u32)blocks[i] >= HEAP_INITIAL_BASE + HEAP_INITIAL_BYTES) extended = 1;
        for (u32 j = 0; j < HEAP_PROBE_BYTES; j++) blocks[i][j] = (u8)(i ^ j ^ 0xa5);
    }
    CHECK(extended);
    for (u32 i = 0; i < 3; i++) {
        edge[i] = api->mem_alloc(sizes[i]);
        CHECK(edge[i] != 0);
        CHECK((u32)edge[i] % 4096 == (i ? 0 : 8));
        for (u32 j = 0; j < sizes[i]; j++) {
            if (i) CHECK(edge[i][j] == 0);
            edge[i][j] = (u8)(i ^ j ^ 0x69);
        }
    }
    if (!child) {
        int kind, code;
        CHECK(api->exec_run(HEAP_CHILD_COMMAND) == 0);
        CHECK(api->exec_last_result(&kind, &code) == 0 && kind == EXEC_KIND_EXITED && code == 0);
    }
    for (u32 i = 0; i < HEAP_PROBE_COUNT; i++) {
        for (u32 j = 0; j < HEAP_PROBE_BYTES; j++) CHECK(blocks[i][j] == (u8)(i ^ j ^ 0xa5));
        api->mem_free(blocks[i]);
    }
    for (u32 i = 0; i < 3; i++) {
        for (u32 j = 0; j < sizes[i]; j++) CHECK(edge[i][j] == (u8)(i ^ j ^ 0x69));
    }
    api->mem_free(edge[1]);
    u8 *again = api->mem_alloc(sizes[1]);
    CHECK(again == edge[1]);
    for (u32 j = 0; j < sizes[1]; j++) CHECK(again[j] == 0);
    api->mem_free(again); api->mem_free(edge[0]); api->mem_free(edge[2]);
    blocks[0] = api->mem_alloc(HEAP_PROBE_BYTES);
    CHECK(blocks[0] != 0);
    /* BlkHdr.size is USER writable; next free must neither crash nor modify it. */
    volatile u32 *header = (volatile u32 *)blocks[0] - 2;
    u32 magic = header[1];
    header[0] = 0xfffffff8UL;
    api->mem_free(blocks[0]);
    CHECK(header[0] == 0xfffffff8UL && header[1] == magic);
    CHECK(api->mem_alloc(16) == 0);
done:
    /* Corrupt arena teardown belongs to the kernel, without retrying free. */
    return os32_test_summary(api, "exec_heap_test", passed, total);
#undef CHECK
}
