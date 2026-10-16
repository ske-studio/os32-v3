/* T3ab F: real resident CRT, split, context restore and KHeap bytes (ILP32).
 * Linux mmap supplies RAM; page-table/entry operations are boundary stubs. */
#include "exec.h"
#include "appslot.h"
#include "exec_heap.h"
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
#define CHECK(x) do { if (!(x)) die(__LINE__ % 250 + 1); } while (0)
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; die(251); }
#include "resident_exec.inc"
static u8 child[4096] __attribute__((aligned(8)));
static u32 digest(u32 base, u32 size) {
    u8 *p = (u8 *)base; u32 h = 0;
    while (size--) h = h * 33 + *p++;
    return h;
}
void _start(void) {
    u32 args[] = {MEM_SHELL_LOAD_ADDR, MEM_SHELL_BAND_END + 1 - MEM_SHELL_LOAD_ADDR,
                  3, 0x32, (u32)-1, 0};
    u32 mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "0"(90), "b"(args) : "memory");
    CHECK(mapped == MEM_SHELL_LOAD_ADDR);
    CHECK(exec_shell_layout(PAGE_SIZE + 3, 0, 0, &parent.sbrk_heap_limit,
                           &parent.exec_heap_base, &parent.exec_heap_size) == 0);
    api.sbrk_heap_limit = parent.sbrk_heap_limit;
    struct _reent reent = {0};
    uintptr_t initial = MEM_SHELL_LOAD_ADDR + PAGE_SIZE + 4;
    CHECK(os32_nano_crt_sbrk(&reent, 0) == (void *)initial);
    CHECK(primary_arena.grow_exact == 0 && primary_arena.limit == parent.sbrk_heap_limit);
    exec_heap_init_at(parent.exec_heap_base, parent.exec_heap_size);
    u32 *sentinel = exec_heap_alloc(32);
    CHECK(sentinel != 0);
    *sentinel = 0x12345678;
    exec_heap_save_state(&parent.exec_heap_used);
    CHECK(os32_nano_crt_sbrk(&reent, 32) == (void *)initial);
    *(u32 *)initial = 0x87654321;
    u32 first = digest(MEM_SHELL_LOAD_ADDR, MEM_SHELL_BAND_END + 1 - MEM_SHELL_LOAD_ADDR);
    uintptr_t old = primary_arena.brk;
    CHECK(os32_nano_crt_sbrk(&reent, parent.sbrk_heap_limit - old + 1) == (void *)-1);
    CHECK(primary_arena.brk == old && reent._errno == ENOMEM);
    CHECK(os32_nano_crt_sbrk(&reent, -33) == (void *)-1 && primary_arena.brk == old);
    CHECK(os32_nano_crt_sbrk(&reent, (ptrdiff_t)0x80000000U) == (void *)-1 && primary_arena.brk == old);
    /* Overflow near UINTPTR_MAX, using the same product morecore. */
    struct os32_nano_arena wrap = {0};
    wrap.initial = wrap.brk = 0xfffffff0U; wrap.limit = wrap.mapped_end = UINTPTR_MAX;
    CHECK(os32_nano_morecore(&wrap, &reent, 32) == (void *)-1 && wrap.brk == 0xfffffff0U);
    CHECK(digest(MEM_SHELL_LOAD_ADDR, MEM_SHELL_BAND_END + 1 - MEM_SHELL_LOAD_ADDR) == first);
    CHECK(os32_nano_crt_sbrk(&reent, parent.sbrk_heap_limit - old) == (void *)old);
    CHECK(os32_nano_crt_sbrk(&reent, 1) == (void *)-1 && primary_arena.brk == parent.sbrk_heap_limit);
    CHECK(os32_nano_crt_sbrk(&reent, -(ptrdiff_t)(parent.sbrk_heap_limit - initial)) == (void *)parent.sbrk_heap_limit);
    CHECK(primary_arena.brk == initial);
    CHECK(os32_nano_crt_sbrk(&reent, -1) == (void *)-1 && primary_arena.brk == initial);
    /* Repeated CPL0 child heaps restore actual headers, contents and used. */
    for (int i = 0; i < 3; i++) {
        exec_heap_init_at((u32)child, sizeof(child));
        CHECK(exec_heap_alloc(64));
        api.sbrk_heap_limit = 0;
        exec_restore_context(APP_ID_SHELL);
        CHECK(api.sbrk_heap_limit == parent.sbrk_heap_limit);
        CHECK(exec_heap_used() == parent.exec_heap_used && *sentinel == 0x12345678);
        CHECK(*(u32 *)initial == 0x87654321);
        CHECK(digest(MEM_SHELL_LOAD_ADDR, MEM_SHELL_BAND_END + 1 - MEM_SHELL_LOAD_ADDR) == first);
    }
    exec_heap_free(sentinel);
    CHECK(exec_heap_used() == 0);
    /* heap_sz=1 is accepted, but cannot hold a block; restore stays empty. */
    CHECK(exec_shell_layout(PAGE_SIZE, 0, 1, &parent.sbrk_heap_limit,
                           &parent.exec_heap_base, &parent.exec_heap_size) == 0);
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
    die(0);
}
