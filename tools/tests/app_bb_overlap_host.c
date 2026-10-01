/* ========================================================================
 *  app_bb_overlap_host.c — 共有 BB と CPL=3 アプリの私有領域が重ならないこと
 *
 *  実行: python3 -B tools/tests/test_app_bb_overlap.py [--mutate]
 *
 *  2026-09-30 の後退 (8MB + PEGC、PM が NP21/W で観測): CPL=3 アプリを 1 本
 *  起動して終了するたびに pgalloc の used_pages が 74 ずつ増え、4 本目で
 *  NOMEM・v86 -t は 159 ページの連続が取れずに失敗した。原因は exec が私有
 *  領域 (スタック / exec_heap) を帯の上端 0x800000 まで張ってから、PEGC の
 *  BB [0x7B5000, 0x800000) を恒等で重ねて写していたこと — 私有ページ 74 枚
 *  の PTE が BB の物理で上書きされ、teardown で戻らない。
 *
 *  修正 (exec/exec.c ring3_band_set): 私有領域の上端を帯の上端と
 *  sys_usable_mem_end() の低い方にする。BB は従来どおり恒等のまま丸ごと写す
 *  (gfx_get_framebuffer / pegc_init が同じ番地を使う) ので、カーネル・アプリ
 *  の BB ポインタは変わらない。
 *
 *  実物の kernel/paging.c + kernel/pgalloc.c + kernel/physmem.c を ILP32 で
 *  そのまま組み、exec/exec.c から切り出した本物の ring3_band_set /
 *  app_map_region / exec_bb_overlaps_user / exec_map_shared_bb /
 *  exec_teardown_app (test_app_bb_overlap.py が生成する exec_bb_overlap.inc)
 *  で「起動 → 終了」をまわす。
 *
 *  見るもの:
 *    (a) 8MB + PEGC: 起動・終了 10 回で used_pages が毎回戻り、V86 の backing
 *        (V86_BACKING_PAGES = 159 の連続) が取れる
 *    (b) 私有 PTE (スタック / exec_heap) が BB の物理を指さない・恒等でない
 *    (c) BB の仮想番地の PTE が BB の物理を恒等 + USER で指す (カーネル・
 *        アプリの BB ポインタが BB に届く)
 *    (d) 私有領域の上端 (band_top = RING3_USTACK_TOP) が BB の下にあり、
 *        PDE の所有範囲 (帯 1 枚) は変わらない
 *    (e) teardown が BB の物理を pgalloc へ返そうとしない (paging.c の
 *        pgalloc_free_n_owner 呼び出しを数える)。T1b 以後は teardown の最後の
 *        ledger_reclaim_owner(AS) が取り残しを回収するので、used_pages が戻る
 *        だけでは漏れが無い証拠にならない — exec_as_leftover_pages (回収で
 *        返った取り残し) と ledger_bad_free (他 owner のページ = BB を返そうと
 *        して断られた回数) が増えないことも見る
 *    (f) BB が私有領域と重なる形 (上端が下がっていない) は起動を断り、
 *        私有 PTE を触らない
 *    (g) 17MB (BB が帯の上) / 12MB (帯の上、PDE 2) / 9801 planar (0x6A000、
 *        帯の下) / Cirrus (デバイス窓、PCD 付き) では私有領域の上端が従来の
 *        0x800000 のままで、BB は共有 PT に USER で写り、PCD を保つ
 *
 *  C89 ([C1])。libc は使わない (-nostdlib で直接走る)。
 * ======================================================================== */
#include "types.h"
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
        CHECK(!exec_map_shared_bb(a.as, MEM_APP_STACK_TOP));
        for (i = 0; i < t_bb_size; i += PAGE_SIZE)
            CHECK(page_tables[(t_bb_base + i) >> 22][((t_bb_base + i) >> 12) & 1023] == ((t_bb_base + i) | PAGE_RW | PTE_USER));
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
        CHECK(launch_read_byte(a.as->pd_phys, (const char *)(a.load_addr + PAGE_SIZE)) == 'c');
        CHECK(!as_va_to_pa(a.as->pd_phys, a.load_addr, &pa));
        CHECK(*(u8 *)P2V(pa + PAGE_SIZE - 2) == 'a');
        CHECK(app_store(&a, a.sbrk_heap_limit, "x", 1) == -1);
        CHECK(paging_current_cr3() == paging_kernel_pd_phys());
        exec_teardown_app(&a); CHECK(used_pages == before && !kmalloc_used());
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
    CHECK(exec_bb_overlaps_user(MEM_EXEC_LOAD_ADDR, PAGE_SIZE, MEM_APP_STACK_TOP));
    t_bb_base = MEM_EXEC_LOAD_ADDR; t_bb_size = PAGE_SIZE;
    CHECK(exec_map_shared_bb(0, MEM_APP_STACK_TOP) == -1);
    t_bb_base = MEM_DEVICE_APERTURE_BASE + 0x100000; t_bb_size = PAGE_SIZE;
    page_tables[t_bb_base >> 22][(t_bb_base >> 12) & 1023] = t_bb_base | PAGE_RW | PTE_PCD;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "PCD", &owner));
    a.as = kmalloc(sizeof(*a.as)); CHECK(a.as); CHECK(!paging_addrspace_create_lease(a.as, owner));
    a.cpl3 = 1; a.load_addr = a.sbrk_heap_limit = 0; a.exec_heap_size = 0; a.stack_base = a.stack_top = 0;
    CHECK(!exec_map_shared_bb(a.as, MEM_APP_STACK_TOP));
    CHECK(page_tables[t_bb_base >> 22][(t_bb_base >> 12) & 1023] & PTE_PCD);
    exec_teardown_app(&a); CHECK(!kmalloc_used() && used_pages == before);
    SAY("app_bb_overlap: PASS high private image/heap/variable stack, shared low BB, KHEAP and owner0"); die(0);
}
