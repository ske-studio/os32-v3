#include "types.h"
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
#include "v86_mem.c"
static int owner = 2;
int res_owner_get(void) { return owner; }
void *kmemset(void *p, int c, u32 n)
{
    u8 *d = p;
    while (n--) *d++ = (u8)c;
    return p;
}
void v86_bios_save_real(void) {}
void v86_bios_restore_real(void) {}
void v86_bios_setup(void) {}
void v86_io_apply_policy(void) {}
void v86_io_reset_policy(void) {}
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
    CHECK(v86_mem_setup(2) == 0, "V86 setup");
    CHECK((entry(0) & PTE_USER) && (entry(V86_REMAP_START + PAGE_SIZE) & PTE_USER),
          "V86 dedicated USER");
    flushes = host_flushes;
    v86_mem_teardown();
    CHECK(host_flushes > flushes && host_flushed_cr3 == a.pd_phys, "V86 active TLB");
    CHECK(page_directory[0] & PTE_USER, "V86 restores PDE0 USER");
    CHECK(!(entry(V86_REMAP_START + PAGE_SIZE) & PTE_USER), "V86 removes low USER");
    CHECK(perm(p, 1) && perm(TRAMP, 0), "V86 preserves shared PTE");
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
        char count[4] = {'0' + checks / 100, '0' + (checks / 10) % 10, '0' + checks % 10, '\n'};
        SAY("PASS shm user checks:");
        say(count, sizeof(count));
    }
    die(0);
}
