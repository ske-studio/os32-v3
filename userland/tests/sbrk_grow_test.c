/* f6 USER morecore acceptance; PM observes teardown counters after exit.
 * f7 shares primary with nano. No libc allocation/stdio is called before or
 * during these direct sbrk probes; CRT's normal main entry does not allocate. */
#include "os32api.h"
#include "rt/testresult.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#define TEST_PAGE 4096u
#define TEST_ATTR 0x07
#define TEST_BYTE 0x6f
extern void *sbrk(int incr);

int main(int argc, char **argv, KernelAPI *api)
{
    uintptr_t initial, target, tail;
    volatile unsigned char *p;
    void *blocked;
    int passed = 0, total = 0;
    (void)argc; (void)argv;
#define CHECK(c) do { total++; if (c) passed++; else goto done; } while (0)
    initial = (uintptr_t)sbrk(0);
    CHECK(initial != UINTPTR_MAX &&
          api->sbrk_heap_limit == ((initial + TEST_PAGE - 1u) & ~(TEST_PAGE - 1u)) + TEST_PAGE);
    errno = 0;
    CHECK(sbrk(-1) == (void *)-1 && errno == ENOMEM && (uintptr_t)sbrk(0) == initial);
    errno = 0;
    CHECK(sbrk(INT_MIN) == (void *)-1 && errno == ENOMEM && (uintptr_t)sbrk(0) == initial);
    target = api->sbrk_heap_limit + TEST_PAGE;
    CHECK(target > initial && target - initial <= INT_MAX);
    CHECK((uintptr_t)sbrk((int)(target - initial)) == initial && (uintptr_t)sbrk(0) == target);
    p = (volatile unsigned char *)initial;
    for (uintptr_t i = 0; i < target - initial; i += TEST_PAGE) p[i] = TEST_BYTE;
    p[target - initial - 1u] = TEST_BYTE;
    for (uintptr_t i = 0; i < target - initial; i += TEST_PAGE) CHECK(p[i] == TEST_BYTE);
    CHECK(p[target - initial - 1u] == TEST_BYTE);
    /* Shrink changes break alone: bytes beyond the break remain writable. */
    CHECK((uintptr_t)sbrk(-1) == target && (uintptr_t)sbrk(0) == target - 1u);
    p[target - initial - 1u] = TEST_BYTE + 1;
    CHECK((uintptr_t)sbrk(1) == target - 1u && p[target - initial - 1u] == TEST_BYTE + 1);
    tail = target;
    blocked = api->mem_map(TEST_PAGE, (void *)tail, OS32_MEM_MAP_EXACT);
    CHECK(blocked == (void *)tail);
    errno = 0;
    CHECK(sbrk(1) == (void *)-1 && errno == ENOMEM && (uintptr_t)sbrk(0) == target);
    CHECK(api->mem_unmap(blocked, TEST_PAGE) == 0);
    CHECK((uintptr_t)sbrk(1) == target && (uintptr_t)sbrk(0) == target + 1u);
    *(volatile unsigned char *)target = TEST_BYTE;
    CHECK(*(volatile unsigned char *)target == TEST_BYTE);
done:
    return os32_test_summary(api, "sbrk_grow_test", passed, total);
#undef CHECK
}
