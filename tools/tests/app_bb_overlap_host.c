/* Real private image/heap/stack allocation and teardown, with supervisor BB
 * aliases (PC98, PEGC and Cirrus). Repeated exits must return every AS page;
 * exec's actual launch mapping block must never expose low Unicode or BB.
 * Run through test_app_bb_overlap.py, GNU11 ILP32. */
#include "types.h"
#include "ring3_str.h"
#include "redir_access.h"
static u32 test_tramp;
u32 exec_tramp_page_addr(void) { return test_tramp; }
static u32 host_cr3;
/* (e): paging.c が返す物理を数える口。pgalloc.h の宣言も同じ名に変わるので、
 * paging.c の呼び出しは全部この関数へ来る (本物へ転送する)。 */
#define pgalloc_free_n_owner host_free_hook
#include "paging_host_source.c"
#undef pgalloc_free_n_owner
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *text, u32 len)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len) : "memory");
}
#define SAY(s) report(s "\n", sizeof(s "\n") - 1)
#define CHECK(x) do { if (!(x)) { SAY("FAIL: " #x); die(1); } } while (0)
#include "pgalloc_host_source.c"
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

/* 試験が決める「機械の姿」: 割り当ててよい物理の上限と、いまの BB。 */
static u32 t_usable_end;
static u32 t_bb_base, t_bb_size;
static u32 bb_free_attempts;     /* (e) BB の物理を返そうとした回数 */

u32 sys_usable_mem_end(void);
u32 sys_usable_mem_end(void) { return t_usable_end; }
void gfx_bb_phys_range(u32 *base, u32 *size);
void gfx_bb_phys_range(u32 *base, u32 *size)
{
    if (base) *base = t_bb_base;
    if (size) *size = t_bb_size;
}
int host_free_hook(u32 owner, u32 pfn, int n)
{
    u32 phys = pfn * PAGE_SIZE;
    if (t_bb_size && phys < t_bb_base + t_bb_size &&
        phys + (u32)n * PAGE_SIZE > t_bb_base)
        bb_free_attempts++;
    return pgalloc_free_n_owner(owner, pfn, n);
}

/* exec_teardown_app が引く。この試験は shlib を載せない。 */
#include "appslot.h"
void serial_puts_polled(const char *s) { (void)s; }
void shlib_addrspace_detach(struct addrspace *as);
void shlib_addrspace_detach(struct addrspace *as) { (void)as; }

#include "os32_kapi_shared.h"
#include "kmalloc.h"
#include "lease.h"
int lease_revoke_all(struct addrspace *as) { (void)as; return 0; }
void *kmemset(void *dst, int c, u32 n) { u8 *p = dst; while (n--) *p++ = (u8)c; return dst; }
void *kmemcpy(void *dst, const void *src, u32 n) { u8 *p = dst; const u8 *q = src; while (n--) *p++ = *q++; return dst; }
static u32 app_fail_at, app_alloc_calls;
static u32 app_fail_alloc(u32 owner, int n) {
    if (app_fail_at) {
        if (n != 1) return 0; /* exercise the fragmented fallback */
        if (++app_alloc_calls >= app_fail_at) return 0;
    }
    return pgalloc_alloc_phys(owner, n);
}
static AppSlot *g_cur_app;
static int host_shlib_loaded;
int shlib_loaded(void) { return host_shlib_loaded; }
/* This legacy image/BB fixture never supplies lease pointers. d5 added a
 * caller dependency to the real early classifier; trap unexpected use here.
 * The actual lease/caller path is exercised in db_caller_host.c. */
int caller_access_get_user(struct caller_access *out)
{
    (void)out;
    CHECK(0);
    return 0;
}
int caller_access_page(const struct caller_access *c, u32 va, int write, u32 *pa)
{
    (void)c; (void)va; (void)write; (void)pa;
    CHECK(0);
    return 0;
}
static u32 launch_map_attempts;
static int __attribute__((unused)) launch_map_attempt(struct addrspace *as, u32 start, u32 end, u32 flags)
{
    launch_map_attempts++;
    return paging_addrspace_map_user_range(as, start, end, flags);
}
#include "exec_bb_overlap.inc"
static u8 heap[192 * 1024];
static u32 pte(struct addrspace *as, u32 va) {
    u32 pt = as->app_pt_phys[(va >> 22) - as->app_pde];
    return pt ? ((u32 *)P2V(pt))[(va >> 12) & 1023] : 0;
}
void _start(void) {
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, before, round, i, owner;
    AppSlot a;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);
    host_map_fixed_paging(); paging_init(8192); host_pool_boot(8192);
    kmalloc_init(heap, sizeof(heap)); before = used_pages;
    {
        u32 bytes;
        CHECK(!exec_stack_bytes(0, 0, &bytes) && bytes == MEM_EXEC_STACK_SIZE);
        CHECK(!exec_stack_bytes(1, 0, &bytes) && bytes == MEM_APP_STACK_MIN);
        CHECK(!exec_stack_bytes(65537, 0, &bytes) && bytes == 69632);
        CHECK(exec_stack_bytes(0x80000000UL, 0, &bytes) == -1);
        CHECK(exec_stack_bytes(0xFFFFFFFFUL, 0, &bytes) == -1);
        CHECK(exec_stack_bytes(0, MEM_EXEC_STACK_SIZE, &bytes) == -1);
    }
    {
        struct addrspace *controls[4];
        for (i = 0; i < 4; i++) { controls[i] = kmalloc(sizeof(*controls[i])); CHECK(controls[i]); }
        CHECK(kmalloc_used() == 4 * (((sizeof(struct addrspace) + 7) & ~7UL) + 8));
        for (i = 0; i < 4; i++) kfree(controls[i]);
        CHECK(!kmalloc_used());
    }
    t_bb_base = 0x7B5000; t_bb_size = 0x4B000;
    CHECK(ledger_claim_fixed(LEDGER_OWNER_BOOT, t_bb_base / PAGE_SIZE, 0x800000 / PAGE_SIZE));
    before = used_pages;
    for (round = 0; round < 10; round++) {
        kmemset(&a, 0, sizeof(a));
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "app", &owner));
        a.as = kmalloc(sizeof(*a.as)); CHECK(a.as);
        CHECK(!paging_addrspace_create_lease(a.as, owner)); a.cpl3 = 1;
        a.load_addr = MEM_EXEC_LOAD_ADDR; a.sbrk_heap_limit = a.load_addr + 2 * PAGE_SIZE;
        a.exec_heap_base = MEM_EXEC_HEAP_BASE; a.exec_heap_size = 16 * PAGE_SIZE;
        a.stack_size = round % 2 ? 0x80000 : MEM_EXEC_STACK_SIZE;
        a.stack_top = MEM_APP_STACK_TOP; a.stack_base = a.stack_top - a.stack_size;
        CHECK(!app_map_region(a.as, a.load_addr, a.sbrk_heap_limit));
        CHECK(!app_map_region(a.as, a.exec_heap_base, a.exec_heap_base + a.exec_heap_size));
        CHECK(!app_map_region(a.as, a.stack_base, a.stack_top));
        launch_shared_maps(&a); CHECK(!launch_map_attempts);
        for (i = MEM_UNICODE_TABLE_BASE; i < MEM_GFX_BB_BASE + MEM_GFX_BB_SIZE; i += PAGE_SIZE)
            CHECK(!(page_tables[0][i / PAGE_SIZE] & PTE_USER));
        for (i = 0; i < t_bb_size; i += PAGE_SIZE)
            CHECK(page_tables[(t_bb_base + i) >> 22][((t_bb_base + i) >> 12) & 1023] == ((t_bb_base + i) | PAGE_RW));
        CHECK(!(pte(a.as, a.stack_base - PAGE_SIZE) & PTE_PRESENT));
        CHECK(!(pte(a.as, a.exec_heap_base - PAGE_SIZE) & PTE_PRESENT));
        CHECK(!page_directory[MEM_EXEC_LOAD_ADDR >> 22]);
        CHECK(!page_directory[MEM_EXEC_HEAP_BASE >> 22]);
        CHECK(!page_directory[(MEM_APP_STACK_TOP - 1) >> 22]);
        CHECK(*(u32 *)P2V(pte(a.as, a.load_addr) & ~0xFFFUL) == 0);
        *(u32 *)P2V(pte(a.as, a.load_addr) & ~0xFFFUL) = 0xBAD;
        exec_teardown_app(&a);
        CHECK(!a.as && !a.cpl3); CHECK(kmalloc_used() == 0);
        CHECK(used_pages == before); CHECK(!exec_as_leftover_pages);
        CHECK(!ledger_bad_free && !bb_free_attempts && !ledger_owners[owner].kind);
    }
    {
        u32 pa;
        kmemset(&a, 0, sizeof(a));
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "copy", &owner));
        a.as = kmalloc(sizeof(*a.as)); CHECK(a.as);
        CHECK(!paging_addrspace_create_lease(a.as, owner)); a.cpl3 = 1;
        a.load_addr = MEM_EXEC_LOAD_ADDR; a.sbrk_heap_limit = a.load_addr + 2 * PAGE_SIZE;
        CHECK(!app_map_region(a.as, a.load_addr, a.sbrk_heap_limit));
        CHECK(!app_store(&a, a.load_addr + PAGE_SIZE - 2, "abcd", 4));
        int read_failed = 0;
        ((u32 *)P2V((((u32 *)P2V(a.as->pd_phys))[a.load_addr >> 22] & ~0xFFFUL)))[(a.load_addr >> 12) & 1023] &= ~PTE_RW;
        ((u32 *)P2V((((u32 *)P2V(a.as->pd_phys))[a.load_addr >> 22] & ~0xFFFUL)))[((a.load_addr + PAGE_SIZE) >> 12) & 1023] &= ~PTE_RW;
        CHECK(launch_read_byte(a.as->pd_phys, (const char *)(a.load_addr + PAGE_SIZE), &read_failed) == 'c');
        CHECK(!read_failed);
        CHECK(launch_read_byte(a.as->pd_phys, (const char *)a.sbrk_heap_limit, &read_failed) == 0);
        CHECK(read_failed);
        ((u32 *)P2V((((u32 *)P2V(a.as->pd_phys))[a.load_addr >> 22] & ~0xFFFUL)))[(a.load_addr >> 12) & 1023] |= PTE_RW;
        ((u32 *)P2V((((u32 *)P2V(a.as->pd_phys))[a.load_addr >> 22] & ~0xFFFUL)))[((a.load_addr + PAGE_SIZE) >> 12) & 1023] |= PTE_RW;
        CHECK(!as_va_to_pa(a.as->pd_phys, a.load_addr, &pa));
        CHECK(*(u8 *)P2V(pa + PAGE_SIZE - 2) == 'a');
        CHECK(app_store(&a, a.sbrk_heap_limit, "x", 1) == -1);
        CHECK(paging_current_cr3() == paging_kernel_pd_phys());
        exec_teardown_app(&a); CHECK(used_pages == before && !kmalloc_used());
    }
    {
        OS32Header h = {0};
        h.load_addr = MEM_EXEC_LOAD_ADDR; h.text_size = PAGE_SIZE;
        CHECK(!exec_image_reject_reason(&h, 0, MEM_EXEC_LOAD_ADDR, 0x100000));
        h.shlib_protocol = OS32_SHLIB_PROTOCOL;
        CHECK(exec_image_reject_reason(&h, 0, MEM_EXEC_LOAD_ADDR, 0x100000));
        host_shlib_loaded = 1;
        CHECK(!exec_image_reject_reason(&h, 0, MEM_EXEC_LOAD_ADDR, 0x100000));
        h.load_addr = MEM_SHELL_LOAD_ADDR;
        CHECK(exec_image_reject_reason(&h, 1, MEM_SHELL_LOAD_ADDR, 0x100000));
        h.shlib_protocol = 0;
        CHECK(!exec_image_reject_reason(&h, 1, MEM_SHELL_LOAD_ADDR, 0x100000));
        h.flags = OS32X_FLAG_SHLIB;
        CHECK(exec_image_reject_reason(&h, 1, MEM_SHELL_LOAD_ADDR, 0x100000));
        h.flags = 0; h.entry_offset = h.text_size;
        CHECK(exec_image_reject_reason(&h, 1, MEM_SHELL_LOAD_ADDR, 0x100000));
        h.entry_offset = 0; h.bss_size = 0x100000;
        CHECK(exec_image_reject_reason(&h, 1, MEM_SHELL_LOAD_ADDR, 0x100000));
        g_cur_app = &a; a.stack_base = MEM_APP_STACK_TOP - 512UL * 1024;
        test_tramp = 0x100000UL + PAGE_SIZE;
        CHECK(ring3_ptr_ok(test_tramp + RING3_USTR_OFF));
        CHECK(ring3_ptr_ok(test_tramp + RING3_USTR_OFF + RING3_USTR_CAP - 1));
        CHECK(!ring3_ptr_ok(test_tramp + RING3_USTR_OFF - 1));
        CHECK(!ring3_ptr_ok(test_tramp + RING3_USTR_OFF + RING3_USTR_CAP));
        test_tramp = 0;
        CHECK(!ring3_ptr_ok(a.stack_base - PAGE_SIZE));
        CHECK(!ring3_ptr_ok(a.stack_base - 1));
        CHECK(ring3_ptr_ok(a.stack_base));
        CHECK(ring3_ptr_ok(a.stack_base - PAGE_SIZE - 1));
        g_cur_app = 0;
    }
    /* Every fragmented image/heap/stack allocation failure unwinds normally. */
    for (round = 1; round <= 2 + 16 + MEM_EXEC_STACK_SIZE / PAGE_SIZE; round++) {
        int rc;
        kmemset(&a, 0, sizeof(a));
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "fail", &owner));
        a.as = kmalloc(sizeof(*a.as)); CHECK(a.as);
        CHECK(!paging_addrspace_create_lease(a.as, owner)); a.cpl3 = 1;
        a.load_addr = MEM_EXEC_LOAD_ADDR; a.sbrk_heap_limit = a.load_addr + 2 * PAGE_SIZE;
        a.exec_heap_base = MEM_EXEC_HEAP_BASE; a.exec_heap_size = 16 * PAGE_SIZE;
        a.stack_top = MEM_APP_STACK_TOP; a.stack_base = a.stack_top - MEM_EXEC_STACK_SIZE;
        app_fail_at = round; app_alloc_calls = 0;
        rc = app_map_region(a.as, a.load_addr, a.sbrk_heap_limit);
        if (!rc) rc = app_map_region(a.as, a.exec_heap_base, a.exec_heap_base + a.exec_heap_size);
        if (!rc) rc = app_map_region(a.as, a.stack_base, a.stack_top);
        CHECK(rc == -1); app_fail_at = 0;
        exec_teardown_app(&a);
        CHECK(used_pages == before && !kmalloc_used() && !exec_as_leftover_pages);
        CHECK(!ledger_bad_free && !ledger_owners[owner].kind);
    }
    t_bb_base = MEM_DEVICE_APERTURE_BASE + 0x100000; t_bb_size = PAGE_SIZE;
    page_tables[t_bb_base >> 22][(t_bb_base >> 12) & 1023] = t_bb_base | PAGE_RW | PTE_PCD;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "PCD", &owner));
    a.as = kmalloc(sizeof(*a.as)); CHECK(a.as); CHECK(!paging_addrspace_create_lease(a.as, owner));
    a.cpl3 = 1; a.load_addr = a.sbrk_heap_limit = 0; a.exec_heap_size = 0; a.stack_base = a.stack_top = 0;
    launch_shared_maps(&a); CHECK(!launch_map_attempts);
    CHECK(!(page_tables[t_bb_base >> 22][(t_bb_base >> 12) & 1023] & PTE_USER));
    CHECK(page_tables[t_bb_base >> 22][(t_bb_base >> 12) & 1023] & PTE_PCD);
    exec_teardown_app(&a); CHECK(!kmalloc_used() && used_pages == before);
    SAY("app_bb_overlap: PASS high private image/heap/variable stack, supervisor BB aliases, KHEAP and owner0"); die(0);
}

/* Paging-only fixture has no exec slots; abort delivery is appmem_map_host. */
void exec_addrspace_abort(struct addrspace *as) { (void)as; }
