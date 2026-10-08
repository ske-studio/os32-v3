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
#define TEST_LARGE 65536u
extern void *sbrk(int incr);
/* SDK-internal test entry, deliberately absent from public application headers. */
extern size_t os32_nano_trim(void);

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned char *p = NULL, *q;
    void *block = NULL, *reused = NULL;
    uintptr_t brk, secondary, primary_limit;
    size_t secondary_bytes = (TEST_REQUEST + 2u * TEST_PAGE - 1u + 16u) & ~(TEST_PAGE - 1u);
    size_t trim_bytes = 0;
    void *trim_map = NULL;
    int passed = 0, total = 0;
    (void)argc; (void)argv;
#define CHECK(c) do { total++; if (c) passed++; else goto done; } while (0)
    p = malloc(32);
    CHECK(p != NULL);
    memset(p, TEST_PATTERN, 32);
    /* f12: keep の先に 3 page を明示的に map して trim の前提を作る。
     * break は戻し、nano の free list に未使用の大塊を追加しない。 */
    brk = (uintptr_t)sbrk(0);
    primary_limit = ((brk + TEST_PAGE - 1u) & ~(TEST_PAGE - 1u)) + 3u * TEST_PAGE;
    CHECK(brk <= primary_limit && primary_limit - brk <= INT_MAX &&
          (uintptr_t)sbrk((int)(primary_limit - brk)) == brk &&
          (uintptr_t)sbrk(-(int)(primary_limit - brk)) == primary_limit);
    block = api->mem_map(TEST_PAGE, (void *)primary_limit, OS32_MEM_MAP_EXACT);
    CHECK(block == (void *)primary_limit);
    brk = (uintptr_t)sbrk(0);
    /* Reserve the unused primary tail, without overwriting its live chunk.
     * malloc and direct sbrk intentionally share one primary break. */
    CHECK((uintptr_t)sbrk((int)(primary_limit - brk)) == brk);
    q = realloc(p, TEST_REQUEST);
    CHECK(q != NULL);
    p = q;
    CHECK((uintptr_t)p > primary_limit + TEST_PAGE);
    for (unsigned i = 0; i < 32; i++) CHECK(p[i] == TEST_PATTERN);
    memset(p, TEST_PATTERN, TEST_REQUEST);
    CHECK(p[TEST_REQUEST - 1u] == TEST_PATTERN);
    secondary = ((uintptr_t)p & ~(TEST_PAGE - 1u)) - TEST_PAGE;
    free(p); p = NULL;
    reused = api->mem_map(secondary_bytes, (void *)secondary, OS32_MEM_MAP_EXACT);
    CHECK(reused == (void *)secondary);
    CHECK(api->mem_unmap(reused, secondary_bytes) == 0);
    reused = NULL;
    /* f8: requested size selects nano vs TOPDOWN; free returns all pages. */
    for (unsigned request=TEST_LARGE-1u;request<=TEST_LARGE+1u;request++) {
        p=calloc(1,request);
        CHECK(p != NULL && !((uintptr_t)p & 7u));
        for (unsigned i=0;i<request;i++) CHECK(p[i] == 0);
        if (request >= TEST_LARGE) {
            /* Direct-map prefix fits in the first page; infer its page base. */
            uintptr_t base=(uintptr_t)p & ~(TEST_PAGE-1u);
            size_t bytes=((uintptr_t)p-base+request+TEST_PAGE-1u) & ~(TEST_PAGE-1u);
            p[request-1u]=TEST_PATTERN;
            free(p); p=NULL;
            void *exact=api->mem_map(bytes,(void*)base,OS32_MEM_MAP_EXACT);
            CHECK(exact == (void*)base);
            CHECK(api->mem_unmap(exact,bytes) == 0);
        } else { free(p); p=NULL; }
    }
    /* f11: restore the break to the old primary free chunk's end. The range
     * ends at primary_limit (the adjacent EXACT block's start), so this is
     * an extent tail removal, not a middle split requiring another slot. */
    CHECK((uintptr_t)sbrk(-(int)(primary_limit - brk)) == primary_limit);
    size_t pages=os32_nano_trim();
    CHECK(pages >= 1 && pages <= (primary_limit-(brk & ~(TEST_PAGE-1u)))/TEST_PAGE);
    trim_bytes=pages*TEST_PAGE;
    uintptr_t trim_base=primary_limit-trim_bytes;
    trim_map=api->mem_map(trim_bytes,(void *)trim_base,OS32_MEM_MAP_EXACT);
    CHECK(trim_map == (void *)trim_base);
    CHECK(api->mem_unmap(trim_map,trim_bytes) == 0);
    trim_map=NULL;
    p=malloc(TEST_REQUEST);
    CHECK(p != NULL && (uintptr_t)p < primary_limit);
    memset(p,TEST_PATTERN,TEST_REQUEST);
    CHECK(p[0] == TEST_PATTERN && p[TEST_REQUEST-1u] == TEST_PATTERN);
    free(p); p=NULL;
    CHECK(printf("malloc_arena_test: printf %d\n", TEST_PATTERN) > 0);
    CHECK(fflush(stdout) == 0);
done:
    free(p);
    if (trim_map) api->mem_unmap(trim_map,trim_bytes);
    if (reused) api->mem_unmap(reused, secondary_bytes);
    if (block) api->mem_unmap(block, TEST_PAGE);
    return os32_test_summary(api, "malloc_arena_test", passed, total);
#undef CHECK
}
