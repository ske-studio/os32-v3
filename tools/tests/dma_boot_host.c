/* Execute the DMA boot gate extracted from kernel.c with the real pool.
 * Device/ledger operations are observations; D's real reservation tests remain
 * in memory_boot_host.c. Each process starts with the pool uninitialized. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "dma_pool.h"
#include "pgalloc.h"
#include "module.h"
#include "sqlite_engine.h"
#include "dev.h"
#include "../../fs/vfs.h"
#include "pc98.h"
enum services_stage services_stage;
volatile u32 tick_count;
struct ledger_region ledger_regions[LEDGER_MAX_REGIONS];
u32 ledger_region_count;
#include "dma_pool_source.c"
#include "dma8237_math.c"
#include "dma_pool_math.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int available, conflict, reserved, tested, bound, pcm, diag, loaded;
static int debug_enabled = 1;
static int dma_tested, dma_skipped, unicode_loads;
static int cd_loaded;
static u32 boot_drive = BOOT_DRIVE_FDD;
static void kprintf(int attr, const char *fmt, ...);
static void test_dma_pool(void) { dma_tested++; }
#include "selftest_dma_gate.c"
static jmp_buf halt;
static void untouched(void) { CHECK(!s_pool.ready && !tested && !bound && !pcm); }
static int memory_boot_init(u32 kb)
{
    available = kb * 1024UL >= MEM_DMA_POOL_END + 1;
    untouched();
    reserved = !conflict;
    if (available && reserved) {
        ledger_region_count = 1;
        ledger_regions[0].type = LEDGER_R_DMA;
        ledger_regions[0].first = MEM_DMA_POOL_BASE / PAGE_SIZE;
        ledger_regions[0].end = (MEM_DMA_POOL_END + 1) / PAGE_SIZE;
    }
    return reserved;
}
static int memory_boot_dma_available(void) { CHECK(reserved); return available; }
static void shm_init(void) { CHECK(reserved); untouched(); }
int paging_boot_user_shared(u32 addr) { (void)addr; return 0; }
static u32 exec_tramp_page_addr(void) { return 0; }
static void selftest(void)
{
    struct dma_buf b;
    CHECK(reserved && !bound && !pcm);
    host_selftest_dma();
    CHECK(dma_tested == available && dma_skipped == !available);
    tested++; /* Non-DMA selftests must run even on short RAM. */
    if (!available) { CHECK(!s_pool.ready); return; }
    CHECK(s_pool.ready);
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES);
    CHECK(dma_alloc(16384, 0, DMA_PHYS_LIMIT, &b) == 0);
    CHECK(b.pa == MEM_DMA_POOL_BASE && b.va == P2V(b.pa));
    CHECK(dma_mark_leaked(&b) == 0);
    dma_pool_init(); /* real selftest discards its spans before probe */
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES && !dma_pool_leaked());
}
struct kdebug kdebug;
void kdebug_load(void)
{
    loaded++;
    kdebug.state = debug_enabled ? KDEBUG_RUNNING : KDEBUG_OFF;
    kdebug.ops.selftest_run = debug_enabled ? selftest : 0;
}
static void kselftest_skipped(void) { CHECK(!debug_enabled); tested++; }
static int cd_module_load(void)
{
    CHECK(reserved && tested == 1 && loaded == 1);
    cd_loaded++;
    return 0;
}
int dev_get_names(const char **names, int max)
{
    (void)names; (void)max;
    CHECK(cd_loaded == 1);
    return 0;
}
Device *dev_find(const char *name) { (void)name; return NULL; }
int vfs_mount(const char *prefix, const char *name, const char *fstype)
{
    (void)prefix; (void)name; (void)fstype;
    CHECK(0); /* no block devices in this DMA fixture */
    return VFS_ERR_IO;
}
static void unicode_file_load(void)
{
    CHECK(reserved && tested == 1 && loaded == 1 && cd_loaded == 1 && !bound && !pcm);
    unicode_loads++;
}
u32 ledger_resource_import_pci(void) { CHECK(reserved); return 0; }
u32 ledger_res_overflow;
static void kernel_pci_line_bits(void) { }
static void pci_bind_set_line_state_hook(void (*hook)(void)) { (void)hook; }
static int pci_drivers;
#define PCI_DRIVER_COUNT 1
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
{
    (void)attr;
    if (strstr(fmt, "[dma] RAM short")) diag++;
    if (strstr(fmt, "[selftest] DMA pool skipped")) dma_skipped++;
}
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
        host_kernel_dma(available ? 8192 : 8088);
        CHECK(!conflict && loaded == 1 && unicode_loads == 1);
        if (available) CHECK(tested == 1 && bound == 1 && pcm == 1 && !diag);
        else { CHECK(!s_pool.ready && !bound && !pcm && tested == 1 && diag == 1); }
    } else {
        CHECK(conflict && !loaded && !diag && !unicode_loads);
        untouched();
    }
    return 0;
}
