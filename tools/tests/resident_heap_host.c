/* T3ab F / T3c1: real resident CRT, handoff, restore and KHeap bytes (ILP32).
 * Linux mmap supplies RAM; page-table/entry operations are boundary stubs. */
#include "exec.h"
#include "appslot.h"
#include "exec_heap.h"
#include "kmalloc.h"
#define OS32_CRT_RESIDENT
#define _end resident_image_end
#include "nano_adapter.c"
#undef _end
KernelAPI api, *kapi = &api;
volatile u32 kctx_irq_depth, kctx_exc_depth;
volatile int exec_nest_level;
static AppSlot parent, *g_cur_app;
static u32 ring3_tramp_page[1024];
AppSlot *appslot_at(int id) { return id == APP_ID_SHELL ? &parent : 0; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 p) { (void)p; }
int paging_set_not_present(u32 a, u32 b) { (void)a; (void)b; return 0; }
static void ring3_band_set(int n) { (void)n; }
static void die(int n) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(n) : "memory");
    for (;;) {}
}
static void report(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK_NAMED(x, label) do { if (!(x)) { report("FAIL: " label "\n"); die(1); } } while (0)
#define CHECK(x) CHECK_NAMED(x, #x)
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; die(251); }
#include "resident_exec.inc"
static u8 child[4096] __attribute__((aligned(8)));
static u8 saved_band[MEM_SHELL_BAND_END + 1 - MEM_SHELL_LOAD_ADDR];
static void snapshot(void) {
    const u8 *p = (const u8 *)MEM_SHELL_LOAD_ADDR;
    for (u32 i = 0; i < sizeof(saved_band); i++) saved_band[i] = p[i];
}
static int unchanged(void) {
    const u8 *p = (const u8 *)MEM_SHELL_LOAD_ADDR;
    for (u32 i = 0; i < sizeof(saved_band); i++)
        if (saved_band[i] != p[i]) return 0;
    return 1;
}
void _start(void) {
    u32 args[] = {MEM_SHELL_LOAD_ADDR, MEM_SHELL_BAND_END + 1 - MEM_SHELL_LOAD_ADDR,
                  3, 0x32, (u32)-1, 0};
    u32 mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "0"(90), "b"(args) : "memory");
    CHECK(mapped == MEM_SHELL_LOAD_ADDR);
    CHECK(exec_shell_layout(PAGE_SIZE + 3, 0, 0, &parent.sbrk_heap_limit,
                           &parent.exec_heap_base, &parent.exec_heap_size) == 0);
    resident_handoff(&parent, parent.sbrk_heap_limit,
                     parent.exec_heap_base, parent.exec_heap_size);
    struct _reent reent = {0};
    uintptr_t initial = MEM_SHELL_LOAD_ADDR + PAGE_SIZE + 4;
    CHECK(os32_nano_crt_sbrk(&reent, 0) == (void *)initial);
    CHECK_NAMED(primary_arena.grow_exact == 0 &&
                primary_arena.limit == parent.exec_heap_base &&
                primary_arena.mapped_end == parent.exec_heap_base,
                "resident handoff limit");
    exec_heap_init_at(parent.exec_heap_base, parent.exec_heap_size);
    u32 *sentinel = exec_heap_alloc(32);
    CHECK(sentinel != 0);
    *sentinel = 0x12345678;
    exec_heap_save_state(&parent.exec_heap_used);
    CHECK(os32_nano_crt_sbrk(&reent, 32) == (void *)initial);
    *(u32 *)initial = 0x87654321;
    snapshot();
    uintptr_t old = primary_arena.brk;
    CHECK(os32_nano_crt_sbrk(&reent, parent.sbrk_heap_limit - old + 1) == (void *)-1);
    CHECK_NAMED(primary_arena.brk == old && reent._errno == ENOMEM && unchanged(),
                "upper failure unchanged");
    reent._errno = 0;
    CHECK(os32_nano_crt_sbrk(&reent, -33) == (void *)-1 && primary_arena.brk == old);
    CHECK(reent._errno == ENOMEM && unchanged());
    reent._errno = 0;
    CHECK(os32_nano_crt_sbrk(&reent, (ptrdiff_t)0x80000000U) == (void *)-1 && primary_arena.brk == old);
    CHECK(reent._errno == ENOMEM && unchanged());
    /* Overflow near UINTPTR_MAX, using the same product morecore. */
    struct os32_nano_arena wrap = {0};
    wrap.initial = wrap.brk = 0xfffffff0U; wrap.limit = wrap.mapped_end = UINTPTR_MAX;
    reent._errno = 0;
    CHECK(os32_nano_morecore(&wrap, &reent, 32) == (void *)-1 && wrap.brk == 0xfffffff0U);
    CHECK(reent._errno == ENOMEM && primary_arena.brk == old && unchanged());
    CHECK(os32_nano_crt_sbrk(&reent, parent.sbrk_heap_limit - old) == (void *)old);
    CHECK(os32_nano_crt_sbrk(&reent, 1) == (void *)-1 && primary_arena.brk == parent.sbrk_heap_limit);
    CHECK(os32_nano_crt_sbrk(&reent, -(ptrdiff_t)(parent.sbrk_heap_limit - initial)) == (void *)parent.sbrk_heap_limit);
    CHECK(primary_arena.brk == initial);
    CHECK(os32_nano_crt_sbrk(&reent, -1) == (void *)-1 && primary_arena.brk == initial);
    CHECK(os32_nano_crt_sbrk(&reent, 32) == (void *)initial);
    /* Repeated CPL0 child heaps restore actual headers, contents and used. */
    for (int i = 0; i < 3; i++) {
        exec_heap_init_at((u32)child, sizeof(child));
        CHECK(exec_heap_alloc(64));
        api.sbrk_heap_limit = 0;
        exec_restore_context(APP_ID_SHELL);
        CHECK(api.sbrk_heap_limit == parent.sbrk_heap_limit);
        CHECK(primary_arena.brk == initial + 32);
        CHECK(exec_heap_used() == parent.exec_heap_used && *sentinel == 0x12345678);
        CHECK(*(u32 *)initial == 0x87654321);
        CHECK(unchanged());
    }
    /* Fill the actual remaining block, then refuse even the smallest request.
     * Keep both the parent's libc bytes and exec allocation alive throughout. */
    u32 remaining = exec_heap_total() - exec_heap_used() - BLK_HDR_SIZE;
    u8 *tail = exec_heap_alloc(remaining);
    CHECK(tail && exec_heap_used() == exec_heap_total());
    for (u32 i = 0; i < remaining; i++) tail[i] = (u8)(i ^ 0xa5);
    snapshot();
    u32 used = exec_heap_used();
    old = primary_arena.brk;
    CHECK(exec_heap_alloc(1) == 0);
    CHECK_NAMED(exec_heap_used() == used && primary_arena.brk == old && unchanged(),
                "exhaustion unchanged");
    exec_heap_free(tail);
    CHECK(exec_heap_alloc(remaining) == tail); /* Failed allocation leaves a reusable heap. */
    exec_heap_free(tail);
    exec_heap_free(sentinel);
    CHECK(exec_heap_used() == 0);
    /* Simulate a fresh shell launch: heap_sz=1 is accepted, but cannot hold
     * a block. Its later child return must restore the empty heap unchanged. */
    CHECK(exec_shell_layout(PAGE_SIZE, 0, 1, &parent.sbrk_heap_limit,
                           &parent.exec_heap_base, &parent.exec_heap_size) == 0);
    resident_handoff(&parent, parent.sbrk_heap_limit,
                     parent.exec_heap_base, parent.exec_heap_size);
    primary_initialized = 0;
    CHECK(os32_nano_crt_sbrk(&reent, 0) == (void *)initial);
    CHECK_NAMED(primary_arena.limit == parent.exec_heap_base &&
                api.sbrk_heap_limit == parent.exec_heap_base && parent.exec_heap_size == 1,
                "tiny heap tail reserved");
    exec_heap_init_at(parent.exec_heap_base, parent.exec_heap_size);
    CHECK(exec_heap_alloc(1) == 0);
    exec_heap_save_state(&parent.exec_heap_used);
    exec_heap_init_at((u32)child, sizeof(child));
    exec_restore_context(APP_ID_SHELL);
    CHECK(exec_heap_alloc(1) == 0 && exec_heap_total() == 0);
    /* Allocator alignment can trim a valid request without exposing its tail. */
    CHECK(exec_shell_layout(PAGE_SIZE, 0, 33, &parent.sbrk_heap_limit,
                           &parent.exec_heap_base, &parent.exec_heap_size) == 0);
    exec_heap_init_at(parent.exec_heap_base, parent.exec_heap_size);
    sentinel = exec_heap_alloc(8); CHECK(sentinel); *sentinel = 0x11223344;
    exec_heap_save_state(&parent.exec_heap_used);
    exec_heap_init_at((u32)child, sizeof(child));
    exec_restore_context(APP_ID_SHELL);
    CHECK(exec_heap_total() == 32 && *sentinel == 0x11223344);
    exec_heap_free(sentinel); CHECK(exec_heap_used() == 0);
    report("PASS resident handoff, byte-exact failure/restore, exhaustion, tiny heap\n");
    die(0);
}
