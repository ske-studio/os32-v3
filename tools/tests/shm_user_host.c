#include "types.h"
#include "v86.h"
#include "idt.h"
static u32 host_cr3, host_flushes, host_flushed_cr3;
#include "paging.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
#include "pgalloc_host_source.c"
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"
#include "shm.c"
static int setup_map_fail, setup_alloc_fail;
static int checked_setup_map(u32 base, u32 end, u32 phys, u32 flags) {
    if (setup_map_fail && --setup_map_fail == 0) return -1;
    return paging_v86_map_range(base, end, phys, flags);
}
static u32 checked_setup_alloc(u32 owner_id, int n) {
    return setup_alloc_fail ? 0 : pgalloc_alloc_phys(owner_id, n);
}
#define paging_v86_map_range checked_setup_map
#define pgalloc_alloc_phys checked_setup_alloc
static int checked_backing_free(u32 owner_id, u32 pfn, int n);
static int checked_restore(struct v86_session_state *s);
#define paging_v86_restore checked_restore
#define pgalloc_free_n_owner checked_backing_free
#include "v86_mem.c"
#undef paging_v86_restore
#undef pgalloc_free_n_owner
#undef pgalloc_alloc_phys
#undef paging_v86_map_range
static u8 host_page0[PAGE_SIZE];
static int owner = 2;
int res_owner_get(void) { return owner; }
int ring3_call_from_user(void) { return 1; }
void *kmemset(void *p, int c, u32 n)
{
    u8 *d = p;
    while (n--) *d++ = (u8)c;
    return p;
}
void *kmemcpy(void *dest, const void *src, u32 n) {
    u8 *d = dest; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dest;
}
#define real_saved v86_session.real_saved
#define real_lowmem v86_session.real_lowmem
#include "v86_bios_slice.inc"
void v86_bios_setup(void) {}
void v86_io_apply_policy(void) {}
void v86_io_reset_policy(void) {}

void v86_bios_detach_disk(void);
V86Gcap *v86_gcap_rec;
static u32 release_calls, return_calls, audit_calls;
void v86_gcap_release(void);
void gfx_v86_return(void);
void kselftest_audit_v86_return(void);
static void exec_stop_mark(void) {}
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static void say(const char *s, u32 n)
{
    u32 call = 4;
    __asm__ volatile("int $0x80" : "+a"(call) : "b"(1), "c"(s), "d"(n) : "memory");
}
#define SAY(s) say(s "\n", sizeof(s "\n") - 1)
static u32 checks;
#define CHECK(x, s) do { checks++; if (!(x)) { SAY("FAIL " s); die(1); } } while (0)
void v86_gcap_release(void) {
    CHECK(!kctx_irq_depth && !kctx_exc_depth, "gcap release normal context");
    release_calls++;
}
void gfx_v86_return(void) {
    CHECK(!kctx_irq_depth && !kctx_exc_depth && !backing_phys, "gfx return after release");
    return_calls++;
}
void kselftest_audit_v86_return(void) {
    CHECK(!kctx_irq_depth && !kctx_exc_depth && return_calls == audit_calls + 1,
          "audit after gfx return");
    audit_calls++;
}
#define V86_TEST_BB MEM_GFX_BB_BASE
static int checked_restore(struct v86_session_state *s) {
    u32 flushes = host_flushes;
    int rc = paging_v86_restore(s);
    CHECK(host_flushes == flushes + 1, "V86 active TLB");
    return rc;
}
void v86_bios_detach_disk(void) {
    CHECK(!kctx_irq_depth && !kctx_exc_depth, "backing release trusted context");
    CHECK(_irq_enabled(), "release IF enabled");
}
static int checked_backing_free(u32 owner_id, u32 pfn, int n)
{
    CHECK(!kctx_irq_depth && !kctx_exc_depth, "backing release trusted context");
    CHECK(_irq_enabled(), "release IF enabled");
    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++)
        CHECK(page_tables[0][i] == v86_session.low_pte[i], "unmap before backing free");
    return pgalloc_free_n_owner(owner_id, pfn, n);
}
/* Production exception transfer/runtime unwind, with only machine edges stubbed. */
typedef struct { int state; u32 jmpbuf[KSETJMP_BUF_LEN]; } AppSlot;
static AppSlot fault_app;
static int g_pending_id, g_pending_kind, g_longjmp_reason, g_longjmp_id;
#define EXEC_LJ_EXIT 5
#define EXEC_ERR_FAULT -99
#define APP_STATE_ABORT_PENDING 2
#define APP_STATE_FAULT_PENDING 3
#define EXEC_LJ_PENDING 4
static int appslot_cur(void) { return 2; }
static AppSlot *appslot_get(int id) { (void)id; return &fault_app; }
static void ring3_context_clear(void) {}
static u32 irq_disabled, restored_esp0, reenter_end, end_entries;
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN];
static int fault_kill_count, ring3_wm_depth, ring3_wm_fault_count;
void irq_disable(unsigned int irq) { irq_disabled |= 1U << irq; }
static void tss_set_esp0(u32 esp0) {
    restored_esp0 = esp0;
    if (reenter_end) {
        CHECK(++end_entries == 1, "end recursion");
        v86_session_end();
    }
}
void exec_longjmp(u32 *buf) {
    if (buf == g_exit_jmpbuf) {
        CHECK(!v86_session.open && !v86_session.active && !backing_phys &&
              !paging_v86_session_open(), "direct kill closes session");
        return;
    }
    CHECK(!v86_session.open && !v86_session.active && v86_session.release_pending,
          "kill ends session before app jump");
    CHECK(kctx_exc_depth == 1 && backing_phys, "kill defers allocator");
    CHECK(!(host_arch_if & EFLAGS_IOPL3), "kill restores caller IOPL");
    kctx_exc_depth = 0; /* setjmp.asm restores trusted depth at landing. */
}
#define v86_active v86_session.active
static void exec_finish(int id, int status, int kind) {
    (void)id; (void)status; (void)kind;
    CHECK(!backing_phys && !v86_session.release_pending, "kill release before owner reclaim");
}
#include "v86_unwind_slice.inc"
static u8 guest_memory[V86_REMAP_END];
static u8 *v86_ptr(u32 seg, u32 off) { return guest_memory + (seg << 4) + off; }
int v86_gcap_keep_if(void) { return 0; }
static int v86_exit_reason, guest_exits;
void v86_exit_to_kernel(void) { guest_exits++; }
#include "v86_int_slice.inc"
static void test_int80(void)
{
    u32 f[17], saved[17];
    for (u32 i = 0; i < 17; i++) f[i] = saved[i] = 0x100 + i;
    f[7] = ~0U;
    f[V86I_EIP] = 0x1236; f[V86I_CS] = 0x2000;
    f[V86I_EFLAGS] = EFLAGS_VM | EFLAGS_IOPL3 | EFLAGS_IF | 0x100;
    f[V86I_ESP] = 0x100; f[V86I_SS] = 0x3000;
    ((u16 *)host_page0)[RING3_SYSCALL_VECTOR * 2] = 0x5678;
    ((u16 *)host_page0)[RING3_SYSCALL_VECTOR * 2 + 1] = 0x4321;
    v86_int80(f);
    CHECK(f[V86I_EIP] == 0x5678 && f[V86I_CS] == 0x4321 && !guest_exits,
          "int80 IVT target");
    CHECK(f[V86I_ESP] == 0xfa && f[V86I_SS] == 0x3000 &&
          f[V86I_EFLAGS] == (EFLAGS_VM | EFLAGS_IOPL3), "int80 VM flags and stack");
    u16 *sp = (u16 *)v86_ptr(0x3000, 0xfa);
    CHECK(sp[0] == 0x1236 && sp[1] == 0x2000 &&
          sp[2] == (EFLAGS_IOPL3 | EFLAGS_IF | 0x100), "int80 guest return IP");
    for (int i = 0; i < 8; i++) CHECK(f[i] == (i == 7 ? ~0U : saved[i]), "int80 pushad intact");
    for (int i = V86I_ES; i <= V86I_GS; i++) CHECK(f[i] == saved[i], "int80 segments intact");
    f[V86I_SS] = 0xffff;
    v86_int80(f);
    CHECK(guest_exits == 1 && v86_exit_reason == V86_EXIT_FAULT, "int80 invalid stack exits");
}
#define TRAMP (KERNEL_LOAD_ADDR + PAGE_SIZE)
static u32 entry(u32 a) { return page_tables[a >> 22][(a >> PAGE_SHIFT) % PTE_COUNT]; }
static int perm(u32 a, int rw)
{
    return (entry(a) & (PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) ==
           ((rw ? PAGE_RW : PAGE_RO) | PTE_USER);
}
static void check_span(u32 a, int rw, const char *unused)
{
    (void)unused;
    CHECK(perm(a, rw) && perm(a + SHM_BLOCK_SIZE - PAGE_SIZE, rw), "SHM USER permissions");
}
static void check_reserved(void)
{
    CHECK(shm_state[0] == SHM_RESERVED && shm_block_span[0] == 0 &&
          shm_block_owner[0] == 0, "DB remains reserved");
    for (int i = SHM_GUI_BLOCK_FIRST; i < SHM_BLOCK_COUNT; i++) {
        CHECK(shm_state[i] == SHM_RESERVED && shm_block_span[i] == 0 &&
              shm_block_owner[i] == 0, "GUI remains reserved");
    }
}
static void check_exhaustion(void)
{
    int count = 0;
    void *ptr;
    while ((ptr = shm_alloc(1)) != 0) {
        CHECK(V2P(ptr) > MEM_SHM_BASE && V2P(ptr) < MEM_SHM_GUI_BASE,
              "allocation excludes DB and GUI");
        count++;
    }
    CHECK(count == SHM_BLOCK_COUNT - SHM_GUI_BLOCK_COUNT - 1,
          "nine allocatable blocks");
    check_reserved();
    shm_cleanup_all();
    check_reserved();
    for (int i = 1; i < SHM_GUI_BLOCK_FIRST; i++) {
        CHECK(perm(MEM_SHM_BASE + (u32)i * SHM_BLOCK_SIZE, 1), "cleanup USER");
    }
}
void _start(void)
{
    u32 args[6] = {KERNEL_LOAD_ADDR, 16 * 1024 * 1024, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, p, old, missing, pa, flushes;
    struct addrspace a, b;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == KERNEL_LOAD_ADDR, "host mapping");
    host_map_fixed_paging();
    paging_init(17408);
    host_pool_boot(17408);
    shm_init();
    p = V2P(shm_alloc(1));
    CHECK(p != 0 && p != MEM_SHM_BASE, "first allocation excludes DB");
    check_reserved();
    CHECK(shm_free(P2V(MEM_SHM_BASE)) == -1, "free rejects DB reservation");
    CHECK(shm_lock(P2V(MEM_SHM_BASE)) == -1, "lock rejects DB reservation");
    CHECK(paging_addrspace_create(&a, 2) == 0, "preboot AS");
    old = entry(MEM_SHM_BASE);
    CHECK(paging_boot_user_shared(TRAMP) == -1 && entry(MEM_SHM_BASE) == old,
          "boot rejects live AS");
    paging_addrspace_destroy(&a);
    CHECK(paging_boot_user_shared(TRAMP) == 0, "boot success");
    CHECK(shm_free(P2V(p)) == 0 && perm(p, 1), "free USER");
    check_reserved();
    check_exhaustion();
    CHECK(paging_boot_user_shared(TRAMP) == -1, "boot once");
    CHECK(perm(MEM_SHM_BASE, 1) && perm(TRAMP, 0), "boot attributes");
    CHECK(paging_addrspace_create(&a, 2) == 0, "first AS");
    CHECK(paging_addrspace_create(&b, 3) == 0, "second AS");
    CHECK(((u32 *)P2V(a.pd_phys))[0] == page_directory[0] &&
          ((u32 *)P2V(b.pd_phys))[0] == page_directory[0], "inherit PDE0");
    host_cr3 = a.pd_phys;
    old = entry(MEM_DMA_POOL_BASE);
    CHECK(paging_set_page(MEM_DMA_POOL_BASE, MEM_DMA_POOL_BASE, PAGE_RW | PTE_USER) == -1,
          "generic set rejects USER");
    CHECK(paging_map_range(MEM_DMA_POOL_BASE, MEM_DMA_POOL_BASE + PAGE_SIZE,
                          MEM_DMA_POOL_BASE, PAGE_RW | PTE_USER) == -1,
          "generic range rejects USER");
    CHECK(paging_map_phys(MEM_DMA_POOL_BASE, MEM_DMA_POOL_BASE, 1, PAGE_RW | PTE_USER) == -1,
          "generic phys rejects USER");
    CHECK(entry(MEM_DMA_POOL_BASE) == old, "generic no change");
    CHECK(paging_map_phys(MEM_DMA_POOL_BASE, MEM_DMA_POOL_BASE, 1, PAGE_RW) == 0,
          "generic supervisor allowed");
    p = V2P(shm_alloc(1));
    flushes = host_flushes;
    CHECK(p == MEM_SHM_BASE + SHM_BLOCK_SIZE && shm_lock(P2V(p)) == 0, "lock");
    CHECK(perm(p, 0), "lock USER");
    CHECK(host_flushes == flushes + 1 && host_flushed_cr3 == a.pd_phys, "SHM active TLB");
    CHECK(shm_free(P2V(p)) == 0 && perm(p, 1), "free USER");
    check_reserved();
    p = V2P(shm_alloc(1));
    CHECK(shm_lock(P2V(p)) == 0, "owned lock");
    shm_free_owned(owner);
    check_reserved();
    CHECK(perm(p, 1) && shm_state[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == SHM_FREE, "owned USER");
    p = V2P(shm_alloc(1));
    CHECK(shm_lock(P2V(p)) == 0, "cleanup lock");
    shm_cleanup_all();
    check_reserved();
    CHECK(perm(p, 1) && shm_state[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == SHM_FREE, "cleanup USER");
    check_span(p, 1, "span");
    old = entry(p);
    CHECK(paging_shm_set_rw(TRAMP, TRAMP + PAGE_SIZE, 1) == -1, "SHM foreign USER range");
    CHECK(paging_shm_set_rw(MEM_SHM_BASE - PAGE_SIZE, MEM_SHM_BASE, 0) == -1, "SHM range");
    CHECK(paging_shm_set_rw(p, MEM_SHM_BASE + MEM_SHM_SIZE + PAGE_SIZE, 0) == -1,
          "SHM upper range");
    CHECK(paging_shm_set_rw(p + 1, p + PAGE_SIZE, 0) == -1 &&
          paging_shm_set_rw(p, p, 0) == -1, "SHM alignment empty");
    page_tables[0][(p >> PAGE_SHIFT) + 1] &= ~PTE_USER;
    missing = paging_shm_user_missing_count;
    CHECK(paging_shm_set_rw(p, p + SHM_BLOCK_SIZE, 0) == -1 &&
          paging_shm_user_missing_count == missing + 1 && entry(p) == old,
          "missing USER atomic reject");
    page_tables[0][(p >> PAGE_SHIFT) + 1] |= PTE_USER;
    page_tables[0][p >> PAGE_SHIFT] = old + PAGE_SIZE;
    CHECK(paging_shm_set_rw(p, p + PAGE_SIZE, 0) == -1, "SHM identity PFN");
    page_tables[0][p >> PAGE_SHIFT] = old & ~PTE_PRESENT;
    CHECK(paging_shm_set_rw(p, p + PAGE_SIZE, 0) == -1, "SHM present");
    page_tables[0][p >> PAGE_SHIFT] = old;
    page_directory[0] ^= PAGE_SIZE;
    CHECK(paging_shm_set_rw(p, p + PAGE_SIZE, 0) == -1, "SHM registered PT");
    page_directory[0] ^= PAGE_SIZE;
    p = V2P(shm_alloc(1));
    page_tables[0][p >> PAGE_SHIFT] &= ~PTE_USER;
    missing = paging_shm_user_missing_count;
    CHECK(shm_lock(P2V(p)) == -1 && shm_state[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == SHM_USED, "lock fails closed");
    CHECK(shm_free(P2V(p)) == -1 && shm_state[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == SHM_USED, "free fails closed");
    shm_free_owned(owner);
    check_reserved();
    CHECK(shm_state[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == SHM_USED && shm_block_owner[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == owner, "owned fails closed");
    shm_cleanup_all();
    check_reserved();
    CHECK(shm_state[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == SHM_USED && shm_block_span[(p - MEM_SHM_BASE) / SHM_BLOCK_SIZE] == 1, "cleanup fails closed");
    CHECK(paging_shm_user_missing_count == missing + 4, "missing USER counted");
    page_tables[0][p >> PAGE_SHIFT] |= PTE_USER;
    shm_free_owned(owner);
    check_reserved();
    test_int80();
    for (u32 i = 0; i < PAGE_SIZE; i++) host_page0[i] = (u8)(i ^ (i >> 8));
    old = entry(0);
    CHECK(paging_v86_map_range(0, PAGE_SIZE, 0, PAGE_RW | PTE_USER) == -1 &&
          entry(0) == old, "V86 outside session unchanged");
    page_tables[0][TVRAM_CHAR_BASE / PAGE_SIZE] |= PTE_USER;
    page_tables[0][V86_TEST_BB / PAGE_SIZE] |= PTE_USER;
    CHECK(v86_mem_setup(2) == 0, "V86 setup");
    CHECK(paging_addrspace_create(&(struct addrspace){0}, 9) == -1,
          "V86 blocks normal AS");
    CHECK(paging_memmap_selftest(TRAMP) == -1, "V86 blocks map test");
    CHECK((entry(0) & PTE_USER) && (entry(V86_REMAP_START + PAGE_SIZE) & PTE_USER),
          "V86 dedicated USER");
    for (u32 i = 0; i < PAGE_SIZE; i++) host_page0[i] = 0;
    flushes = host_flushes;
    v86_mem_teardown();
    for (u32 i = 0; i < PAGE_SIZE; i++)
        CHECK(host_page0[i] == (u8)(i ^ (i >> 8)), "V86 page0 all bytes");
    CHECK(host_flushes == flushes + 2 && host_flushed_cr3 == a.pd_phys, "V86 active TLB");
    CHECK(return_calls == 1 && audit_calls == 1, "V86 return audited");
    CHECK(page_directory[0] & PTE_USER, "V86 restores PDE0 USER");
    CHECK(!(entry(V86_REMAP_START + PAGE_SIZE) & PTE_USER), "V86 removes low USER");
    CHECK(perm(p, 1) && perm(TRAMP, 0), "V86 preserves shared PTE");
    CHECK(entry(0) == old && (entry(TVRAM_CHAR_BASE) & PTE_USER) &&
          (entry(V86_TEST_BB) & PTE_USER), "V86 exact low restore");
    CHECK(((u32 *)P2V(a.pd_phys))[0] & PTE_USER, "V86 active PDE restore");
    v86_mem_teardown();
    CHECK(entry(0) == old && !v86_restore_mismatch, "V86 double end");
    /* Make active PDE USER differ from master, then prove exact restoration. */
    ((u32 *)P2V(a.pd_phys))[0] &= ~PTE_USER;
    CHECK(!v86_mem_setup(2), "V86 second begin");
    v86_session.active = 1;
    v86_session.running = 1;
    v86_session.saved_esp0 = 1234;
    kctx_exc_depth = 1;
    host_arch_if |= EFLAGS_IOPL3;
    _disable();
    exec_pending_transfer(0);
    exec_pending_finish();
    CHECK(release_calls == 1 && return_calls == 2 && audit_calls == 2, "kill gcap release audited");
    host_cr3 = a.pd_phys;
    CHECK(!(((u32 *)P2V(a.pd_phys))[0] & PTE_USER), "V86 active PDE exact");
    CHECK(irq_disabled == ((1U << 12) | (1U << 2)) && restored_esp0 == 1234,
          "kill runtime restored");
    ((u32 *)P2V(a.pd_phys))[0] |= PTE_USER;
    CHECK(!v86_mem_setup(2), "V86 after kill begin");
    v86_session.active = v86_session.running = 1;
    reenter_end = 1;
    ring3_kill_kind(EXEC_KIND_FAULT);
    reenter_end = 0;
    CHECK(end_entries == 1 && fault_kill_count == 1, "direct kill and recursive end");
    CHECK(!v86_mem_setup(2), "V86 after direct kill begin");
    /* End after switching to an unrelated PD: never promote that PDE0. */
    host_cr3 = b.pd_phys;
    ((u32 *)P2V(b.pd_phys))[0] &= ~PTE_USER;
    u32 unrelated_pde = ((u32 *)P2V(b.pd_phys))[0];
    v86_mem_teardown();
    CHECK(((u32 *)P2V(b.pd_phys))[0] == unrelated_pde, "unrelated current PDE unchanged");
    ((u32 *)P2V(b.pd_phys))[0] |= PTE_USER;
    host_cr3 = a.pd_phys;
    CHECK(!v86_mem_setup(2), "V86 after CR3 switch begin");
    /* Inject a rejected map operation; direct restore still removes aliases. */
    v86_map_session = 0;
    v86_mem_teardown();
    CHECK(v86_restore_mismatch == V86_GUEST_MAP_END / PAGE_SIZE,
          "V86 rejected restore counted");
    CHECK(entry(0) == old && (entry(V86_TEST_BB) & PTE_USER),
          "V86 rejected restore safe");
    missing = v86_restore_mismatch;
    for (int step = 1; step <= V86_IDENT_MAP_N + 2; step++) {
        setup_map_fail = step;
        CHECK(v86_mem_setup(2) == -3, "V86 partial setup rejects");
        CHECK(!v86_session.open && !backing_phys && entry(0) == old &&
              !paging_v86_session_open() && v86_restore_mismatch == missing,
              "V86 partial setup rollback");
    }
    setup_alloc_fail = 1;
    CHECK(v86_mem_setup(2) == -2 && !v86_session.open && !backing_phys &&
          entry(0) == old, "V86 allocation failure rollback");
    setup_alloc_fail = 0;
    paging_addrspace_destroy(&a);
    host_cr3 = b.pd_phys;
    CHECK(as_va_to_pa(b.pd_phys, p, &pa) == 0 && pa == p, "second AS SHM writable");
    *(u32 *)P2V(pa) = 0x12345678;
    CHECK(*(u32 *)P2V(p) == 0x12345678, "second AS write");
    host_cr3 = paging_kernel_pd_phys();
    paging_addrspace_destroy(&b);
    CHECK(paging_addrspace_create(&b, 3) == 0, "post V86 new AS");
    CHECK(as_va_to_pa(b.pd_phys, p, &pa) == 0, "post V86 SHM writable");
    CHECK((((u32 *)P2V(b.pd_phys))[0] & PTE_USER) && perm(TRAMP, 0), "post V86 trampoline readable");
    paging_addrspace_destroy(&b);
    {
        char count[5] = {'0' + checks / 1000, '0' + (checks / 100) % 10, '0' + (checks / 10) % 10, '0' + checks % 10, '\n'};
        SAY("PASS shm user checks:");
        say(count, sizeof(count));
    }
    die(0);
}
