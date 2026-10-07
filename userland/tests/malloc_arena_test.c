/* f7 USER nano arena acceptance; PM checks teardown counters after exit. */
#include "os32api.h"
#include "rt/testresult.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define TEST_PAGE 4096u
#define TEST_REQUEST (2u * TEST_PAGE)
#define TEST_PATTERN 0x6d
extern void *sbrk(int incr);

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned char *p = NULL, *q;
    void *block = NULL, *reused = NULL;
    uintptr_t brk, secondary;
    size_t secondary_bytes = (TEST_REQUEST + 2u * TEST_PAGE - 1u + 16u) & ~(TEST_PAGE - 1u);
    int passed = 0, total = 0;
    (void)argc; (void)argv;
#define CHECK(c) do { total++; if (c) passed++; else goto done; } while (0)
    p = malloc(32);
    CHECK(p != NULL);
    memset(p, TEST_PATTERN, 32);
    block = api->mem_map(TEST_PAGE, (void *)api->sbrk_heap_limit, OS32_MEM_MAP_EXACT);
    CHECK(block == (void *)api->sbrk_heap_limit);
    brk = (uintptr_t)sbrk(0);
    CHECK(brk <= api->sbrk_heap_limit && api->sbrk_heap_limit - brk <= INT_MAX);
    /* Reserve the unused primary tail, without overwriting its live chunk.
     * malloc and direct sbrk intentionally share one primary break. */
    CHECK((uintptr_t)sbrk((int)(api->sbrk_heap_limit - brk)) == brk);
    q = realloc(p, TEST_REQUEST);
    CHECK(q != NULL);
    p = q;
    CHECK((uintptr_t)p > api->sbrk_heap_limit + TEST_PAGE);
    for (unsigned i = 0; i < 32; i++) CHECK(p[i] == TEST_PATTERN);
    memset(p, TEST_PATTERN, TEST_REQUEST);
    CHECK(p[TEST_REQUEST - 1u] == TEST_PATTERN);
    secondary = ((uintptr_t)p & ~(TEST_PAGE - 1u)) - TEST_PAGE;
    free(p); p = NULL;
    reused = api->mem_map(secondary_bytes, (void *)secondary, OS32_MEM_MAP_EXACT);
    CHECK(reused == (void *)secondary);
    CHECK(api->mem_unmap(reused, secondary_bytes) == 0);
    reused = NULL;
    CHECK(printf("malloc_arena_test: printf %d\n", TEST_PATTERN) > 0);
    CHECK(fflush(stdout) == 0);
done:
    free(p);
    if (reused) api->mem_unmap(reused, secondary_bytes);
    if (block) api->mem_unmap(block, TEST_PAGE);
    return os32_test_summary(api, "malloc_arena_test", passed, total);
#undef CHECK
}
