typedef unsigned int u32;
#define PAGE_SIZE 4096u
#define APPMEM_EXTENT_MAX 1
#define APPMEM_ANON 1
#define APPMEM_EXEC_ARENA 2
#define APPMEM_EXEC_LARGE 3
#define ATTR_RED 0
struct appmem_extent { u32 kind, end, base; };
struct addrspace { u32 pd_phys, owner, appmem_poisoned; struct { struct appmem_extent e[1]; } appmem; };
typedef struct { int cpl3; struct addrspace *as; u32 sbrk_heap_limit, load_addr, exec_heap_size, exec_heap_base, stack_base, stack_top; } AppSlot;
static u32 exec_as_leftover_pages;
static int release_rc, order, failed, reclaim, retire, freed;
static u32 ledger_owner_pages(u32 owner) { (void)owner; return 3; }
static int lease_revoke_all(struct addrspace *as) { (void)as; if (order != 0) failed=1; order=1; return 0; }
static int gfx2_owner_release(struct addrspace *as) { (void)as; if (order != 1) failed=1; order=2; return release_rc; }
static void paging_addrspace_poison(struct addrspace *as) { if (order != 2) failed=1; as->appmem_poisoned=1; }
static void kprintf(int attr, const char *s) { (void)attr; (void)s; }
static void shlib_addrspace_detach(struct addrspace *as) { (void)as; if (order != 2) failed=1; }
static u32 paging_addrspace_free_user_range(struct addrspace *as, u32 a, u32 b) { (void)as; return (b-a)/PAGE_SIZE; }
static void paging_addrspace_destroy(struct addrspace *as) { (void)as; }
static int ledger_reclaim_owner(u32 owner, u32 *left) { (void)owner; reclaim++; *left=0; return 1; }
static int ledger_owner_retire(u32 owner) { (void)owner; retire++; return 0; }
static void serial_puts_polled(const char *s) { (void)s; }
static void exec_owner_diag(AppSlot *a, int b, u32 c, u32 d) { (void)a; (void)b; (void)c; (void)d; }
static void kfree(void *p) { (void)p; freed++; }
#include "teardown.inc"
static int run(void) {
    struct addrspace as = { .pd_phys=1, .owner=2 };
    AppSlot app = { .cpl3=1, .as=&as };
    exec_teardown_app(&app);
    if (failed || order != 2 || freed != 1 || app.as || app.cpl3) return 1;
    if (release_rc ? (!as.appmem_poisoned || reclaim || retire) : (as.appmem_poisoned || reclaim != 1 || retire != 1)) return 2;
    return 0;
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
