/* Real CRT included to inspect its arena without a production reset API. */
#include <limits.h>
#include "sdk/crt/syscalls.c"
#include "sdk/allocator/nano_adapter.c"

void *memset(void *p, int c, size_t n) { unsigned char *q=p; while(n--) *q++=c; return p; }
void *memcpy(void *p, const void *s, size_t n) { unsigned char *q=p; const unsigned char *r=s; while(n--) *q++=*r++; return p; }
static struct _reent host_reent;
struct _reent *_impure_ptr = &host_reent;
int *__errno(void) { return &host_reent._errno; }
KernelAPI *kapi;
char _end[4] __attribute__((aligned(4)));
static KernelAPI api;
static unsigned calls, map_bytes;
static uintptr_t map_base;
static int mode;

static void report(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(c, label) do { if (!(c)) { report("FAIL: " label "\n"); return 1; } } while (0)

static void *__cdecl fake_map(u32 bytes, void *hint, u32 flags)
{
    calls++;
    map_bytes = bytes;
    map_base = (uintptr_t)hint;
    if (flags != OS32_MEM_MAP_EXACT) return NULL;
    if (mode == 1) return NULL;
    if (mode == 2) return (void *)(map_base + OS32_NANO_PAGE);
    return hint;
}

static void reset(uintptr_t initial, uintptr_t mapped, uintptr_t limit)
{
    primary_arena.initial = initial;
    primary_arena.brk = initial;
    primary_arena.mapped_end = mapped;
    primary_arena.limit = limit;
    primary_initialized = 1;
    calls = 0;
    mode = 0;
    errno = 0;
}

static int run(void)
{
    uintptr_t initial = ((uintptr_t)_end + 3u) & ~3u;
    uintptr_t mapped = (initial + 2u * OS32_NANO_PAGE) & ~(OS32_NANO_PAGE - 1u);
    api.sbrk_heap_limit = mapped;
    api.mem_map = fake_map; /* Even if populated, resident must never use it. */
    kapi = &api;
    CHECK((uintptr_t)sbrk(0) == initial, "aligned initial");
    CHECK(primary_arena.mapped_end == mapped, "initial mapped end");
    CHECK(sbrk(-1) == (void *)-1 && errno == ENOMEM && (uintptr_t)sbrk(0) == initial,
          "negative lower bound");
    CHECK(sbrk(INT_MIN) == (void *)-1 && errno == ENOMEM && primary_arena.brk == initial,
          "INT_MIN lower bound");
    CHECK((uintptr_t)sbrk((int)(mapped - initial)) == initial && calls == 0 && primary_arena.brk == mapped,
          "inclusive mapped boundary");
#ifdef OS32_CRT_RESIDENT
    CHECK(sbrk(1) == (void *)-1 && errno == ENOMEM && calls == 0 && primary_arena.brk == mapped,
          "resident no map");
    reset(initial, mapped, UINTPTR_MAX);
    CHECK(sbrk(1 + (int)(mapped - initial)) == (void *)-1 && calls == 0 && primary_arena.brk == initial,
          "resident no map beyond backing");
#else
    CHECK((uintptr_t)sbrk(1) == mapped && calls == 1 && map_base == mapped && map_bytes == OS32_NANO_PAGE,
          "EXACT tail only");
    CHECK(primary_arena.brk == mapped + 1u && primary_arena.mapped_end == mapped + OS32_NANO_PAGE,
          "break and mapped end");
    CHECK((uintptr_t)sbrk(-1) == mapped + 1u && primary_arena.brk == mapped && calls == 1 &&
          primary_arena.mapped_end == mapped + OS32_NANO_PAGE, "shrink retains mapping");
    CHECK((uintptr_t)sbrk(OS32_NANO_PAGE) == mapped && calls == 1, "reuse mapping");
    mode = 1;
    CHECK(sbrk(1) == (void *)-1 && errno == ENOMEM && calls == 2 &&
          primary_arena.brk == mapped + OS32_NANO_PAGE && primary_arena.mapped_end == mapped + OS32_NANO_PAGE,
          "failed map unchanged");
    mode = 2;
    CHECK(sbrk(1) == (void *)-1 && errno == ENOMEM && calls == 3 &&
          primary_arena.brk == mapped + OS32_NANO_PAGE && primary_arena.mapped_end == mapped + OS32_NANO_PAGE,
          "noncontiguous map refused");
    mode = 0;
    CHECK((uintptr_t)sbrk(OS32_NANO_PAGE + 1) == mapped + OS32_NANO_PAGE && calls == 4 &&
          map_base == mapped + OS32_NANO_PAGE && map_bytes == 2u * OS32_NANO_PAGE,
          "multi page tail");
    /* No reset occurs when the kernel restores the original KAPI field. */
    api.sbrk_heap_limit = mapped;
    CHECK((uintptr_t)sbrk(0) == mapped + 2u * OS32_NANO_PAGE + 1u && calls == 4,
          "parent arena retained");
#endif
    reset(0xfffffff0u, UINTPTR_MAX, UINTPTR_MAX);
    CHECK(sbrk(32) == (void *)-1 && errno == ENOMEM && primary_arena.brk == 0xfffffff0u && calls == 0,
          "addition overflow");
    reset(0xffffe000u, 0xfffff000u, UINTPTR_MAX);
    CHECK(sbrk(0x1001) == (void *)-1 && errno == ENOMEM && primary_arena.brk == 0xffffe000u && calls == 0,
          "page rounding overflow");
    reset(initial, mapped, UINTPTR_MAX);
#ifndef OS32_CRT_RESIDENT
    api.mem_map = NULL;
    CHECK(sbrk((int)(mapped - initial + 1)) == (void *)-1 && errno == ENOMEM && calls == 0 && primary_arena.brk == initial,
          "missing map refused");
#endif
    /* USER already has arenas: configure rejects reinit, masking a missing handoff check. */
    primary_initialized = 0;
    api.sbrk_heap_limit = initial - 1u;
    CHECK(sbrk(0) == (void *)-1 && errno == ENOMEM && primary_initialized == -1,
          "invalid handoff rejected");
    api.sbrk_heap_limit = mapped;
    CHECK(sbrk(0) == (void *)-1 && sbrk(1) == (void *)-1 && sbrk(-1) == (void *)-1,
          "invalid handoff stays ENOMEM");
    report("PASS CRT morecore\n");
    return 0;
}

void fixture_start(void)
{
    int rc = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
__asm__(".global _start\n_start:\n call fixture_start\n");
