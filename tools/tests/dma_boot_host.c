/* Execute the DMA boot gate extracted from kernel.c with the real pool.
 * Device/ledger operations are observations; D's real reservation tests remain
 * in memory_boot_host.c. Each process starts with the pool uninitialized. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "dma_pool.h"
#include "dma_pool_source.c"
#include "dma8237_math.c"
#include "dma_pool_math.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int available, conflict, reserved, tested, bound, pcm, diag, loaded;
static int debug_enabled = 1;
static jmp_buf halt;
static void untouched(void) { CHECK(!s_pool.ready && !tested && !bound && !pcm); }
static int memory_boot_init(u32 kb)
{
    (void)kb;
    untouched();
    reserved = !conflict;
    return reserved;
}
static int memory_boot_dma_available(void) { CHECK(reserved); return available; }
static void shm_init(void) { CHECK(reserved); untouched(); }
static int paging_boot_user_shared(u32 addr) { (void)addr; return 0; }
static u32 exec_tramp_page_addr(void) { return 0; }
static int selftest(void)
{
    struct dma_buf b;
    CHECK(reserved && available && s_pool.ready && !bound && !pcm);
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES);
    CHECK(dma_alloc(16384, 0, DMA_PHYS_LIMIT, &b) == 0);
    CHECK(b.pa == MEM_DMA_POOL_BASE && b.va == P2V(b.pa));
    CHECK(dma_mark_leaked(&b) == 0);
    dma_pool_init(); /* real selftest discards its spans before probe */
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES && !dma_pool_leaked());
    tested++;
    return 0;
}
static struct { struct { int (*selftest_run)(void); } ops; } kdebug;
static void kdebug_load(void) { loaded++; kdebug.ops.selftest_run = debug_enabled ? selftest : 0; }
static void kselftest_skipped(void) { CHECK(!debug_enabled); tested++; }
static u32 ledger_resource_import_pci(void) { CHECK(reserved); return 0; }
static u32 ledger_res_overflow;
static void kernel_pci_line_bits(void) { }
static void pci_bind_set_line_state_hook(void (*hook)(void)) { (void)hook; }
static int pci_drivers;
#define PCI_DRIVER_COUNT 1
#define TATTR_WHITE 0
static void pci_bind_all(int drivers, int count)
{
    struct dma_buf b;
    (void)drivers; (void)count;
    CHECK(reserved && available && tested == 1 && !pcm);
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES);
    CHECK(dma_alloc(16384, 0, DMA_PHYS_LIMIT, &b) == 0);
    bound++;
}
static void pcm_init(void)
{
    CHECK(reserved && available && tested == 1 && bound == 1);
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES - 4);
    pcm++;
}
static void kprintf(int attr, const char *fmt, ...)
{ (void)attr; if (strstr(fmt, "RAM short")) diag++; }
#include "kernel_dma_gate.c"
int main(int argc, char **argv)
{
    struct dma_buf b = {123, (void *)456, 789};
    CHECK(argc == 2);
    available = strcmp(argv[1], "short") != 0;
    conflict = strcmp(argv[1], "conflict") == 0;
    debug_enabled = strcmp(argv[1], "nodebug") != 0;
    CHECK(dma_alloc(4096, 0, DMA_PHYS_LIMIT, &b) == DMA_POOL_ERR_STATE);
    CHECK(b.pa == 123 && b.va == (void *)456 && b.size == 789);
    if (!setjmp(halt)) {
        host_kernel_dma(8192);
        CHECK(!conflict && loaded == 1);
        if (available) CHECK(tested == 1 && bound == 1 && pcm == 1 && !diag);
        else { untouched(); CHECK(diag == 1); }
    } else {
        CHECK(conflict && !loaded && !diag);
        untouched();
    }
    return 0;
}
