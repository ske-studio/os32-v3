/* T3b2/b3: actual fixture + allocator + paging + USER walk + DMA math.
 * Machine boundaries and VFS are mocked; VFS asserts live physical pointers
 * and supplies deterministic I/O failures, short counts and corruption. */
#include <stdarg.h>
#include "types.h"
#include "paging.h"
#include "pgalloc.h"
#include "kprintf.h"
#include "vfs.h"
#include "kstring.h"
#include "dma_pool.h"
#include "fdc.h"
static u32 host_cr3;
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(1); } } while (0)
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static u32 operation, fail_at;
static int bad_mapping, bad_pattern;
static int fail_operation(void) { return ++operation == fail_at; }
#define pgalloc_alloc_phys actual_alloc_phys
#define pgalloc_alloc_n_owner actual_alloc_n_owner
#define ledger_owner_new actual_owner_new
#include "pgalloc_host_source.c"
#undef pgalloc_alloc_phys
#undef pgalloc_alloc_n_owner
#undef ledger_owner_new
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    return fail_operation() ? 0 : actual_alloc_phys(owner, n);
}
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 dir, u32 *pfn)
{
    return !fail_operation() && actual_alloc_n_owner(owner, n, first, end, dir, pfn);
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    return !fail_operation() && actual_owner_new(kind, id, name, owner);
}
#define paging_addrspace_map_user actual_map_user
#include "paging_host_source.c"
#undef paging_addrspace_map_user
int paging_addrspace_map_user(struct addrspace *as, u32 va, u32 pa, u32 flags)
{
    if (fail_operation()) return -1;
    if (va == MEM_EXEC_LOAD_ADDR + PAGE_SIZE) {
        if (bad_mapping) pa = as->pd_phys;
        else if (bad_pattern) ((u8 *)P2V(pa))[PAGE_SIZE / 2] ^= 1;
    }
    return actual_map_user(as, va, pa, flags);
}
#include "access_walk_host_source.c"
/* The fixture only maps private owned RAM; these other walk branches are
 * deliberately unavailable, not a replacement for the USER translator. */
int shlib_read_page(u32 va, u32 pa) { (void)va; (void)pa; return 0; }
u32 exec_tramp_page_addr(void) { return 0; }
void exec_addrspace_abort(struct addrspace *as) { (void)as; CHECK(0); }
static u32 ksel_fail, entries, skips, page_logs, cleanups, results, last_entered;
static u32 io_operation, io_fail_at, io_logs, io_passes, io_removes;
static u32 io_skips, io_checks;
static u32 observed_pfns[3], io_corrupt;
static u32 dma_logs, dma_passes, dma_skips, dma_fails;
static u32 dma_operation, dma_fail_at;
static int io_short, io_no_copy, io_wrong_mount;
static u8 file_data[PAGE_SIZE + 34];
static u32 file_size, file_offset;
static int files_present[2], file_backend, file_fd, file_mode;
#define file_exists files_present[file_backend]
static int io_fails(void) { return ++io_operation == io_fail_at; }
static void io_pointer(const void *p, u32 n)
{
    u32 logical = PAGE_SIZE - 17 + file_offset;
    CHECK(p == P2V(observed_pfns[logical / PAGE_SIZE] * PAGE_SIZE + logical % PAGE_SIZE));
    CHECK(n == (file_offset == 17 ? PAGE_SIZE : 17));
    int owned = 0;
    for (u32 o = LEDGER_OWNER_FIXED_LAST + 1; o < LEDGER_MAX_OWNERS; o++)
        owned |= pgalloc_page_owned(observed_pfns[logical / PAGE_SIZE], o);
    CHECK(owned);
}
const char *vfs_fstype(const char *prefix)
{
    CHECK(same(prefix, "/") || same(prefix, "/host"));
    if (same(prefix, "/")) return (io_wrong_mount & 1) ? "fat" : "ext2";
    return (io_wrong_mount & 2) ? "" : "hostdrv";
}
int vfs_stat(const char *path, OS32_Stat *st)
{
    file_backend = same(path, "/host/h4-io.tmp");
    (void)st;
    if (io_fails()) return VFS_ERR_IO;
    return file_exists ? VFS_OK : VFS_ERR_NOTFOUND;
}
int vfs_open(const char *path, int mode)
{
    CHECK(same(path, "/h4-io.tmp") || same(path, "/host/h4-io.tmp"));
    CHECK(!file_fd);
    if (io_fails()) return VFS_ERR_IO;
    if (mode != O_RDONLY) {
        CHECK(mode == (O_WRONLY | O_CREAT | O_TRUNC));
        CHECK(!file_exists);
        file_exists = 1;
        file_size = 0;
    } else CHECK(file_exists);
    file_fd = 3;
    file_mode = mode;
    file_offset = 0;
    return file_fd;
}
void vfs_close(int fd)
{
    CHECK(fd == 3 && file_fd == 3);
    file_fd = 0;
}
int vfs_write_fd(int fd, const void *buf, u32 n)
{
    CHECK(fd == 3 && file_fd == 3 && file_mode != O_RDONLY);
    io_pointer(buf, n);
    if (io_fails()) return io_short ? (int)n - 1 : VFS_ERR_IO;
    CHECK(file_offset + n <= sizeof(file_data));
    for (u32 i = 0; i < n; i++) file_data[file_offset + i] = ((const u8 *)buf)[i];
    file_size = file_offset += n;
    return n;
}
int vfs_read_fd(int fd, void *buf, u32 n)
{
    CHECK(fd == 3 && file_fd == 3 && file_mode == O_RDONLY);
    io_pointer(buf, n);
    for (u32 i = 0; i < n; i++)
        CHECK(((u8 *)buf)[i] == (u8)(file_data[file_offset + i] ^ 0xff));
    if (io_fails()) return io_short ? (int)n - 1 : VFS_ERR_IO;
    CHECK(file_offset + n <= file_size);
    for (u32 i = 0; i < n; i++) {
        if (!io_no_copy) ((u8 *)buf)[i] = file_data[file_offset + i];
        if (io_corrupt == file_offset + i + 1) ((u8 *)buf)[i] ^= 1;
    }
    file_offset += n;
    return n;
}
u32 vfs_get_size(int fd)
{
    CHECK(fd == 3 && file_fd == 3);
    return io_fails() ? file_size + 1 : file_size;
}
int vfs_sync(void) { return io_fails() ? VFS_ERR_IO : VFS_OK; }
int vfs_rm(const char *path)
{
    CHECK(same(path, file_backend ? "/host/h4-io.tmp" : "/h4-io.tmp"));
    CHECK(!file_fd);
    io_removes++;
    if (io_fails()) return VFS_ERR_IO;
    if (!file_exists) return VFS_ERR_NOTFOUND;
    file_exists = 0;
    return VFS_OK;
}
int kstrcmp(const char *a, const char *b) { return !same(a, b); }

static void check(int cond, const char *name)
{
    if (same(name, "h4: I/O roundtrip and cleanup")) io_checks++;
    ksel_fail += !cond;
}
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    va_list ap;
    va_start(ap, fmt);
    if (same(fmt, "[h4] %s entry va=%08X bytes=%u pages=%u pattern=%s\n")) {
        (void)va_arg(ap, const char *);
        CHECK(va_arg(ap, u32) == MEM_EXEC_LOAD_ADDR + PAGE_SIZE - 17);
        CHECK(va_arg(ap, u32) == PAGE_SIZE + 34);
        CHECK(va_arg(ap, u32) == 3);
        CHECK(same(va_arg(ap, const char *), bad_pattern ? "FAIL" : "ok"));
        entries++;
    } else if (same(fmt, "[h4] %s SKIP (not run): %s allocation failed\n")) {
        skips++;
    } else if (same(fmt, "[h4] %s page=%u va=%08X pfn=%08X user_rw=%u range=%u\n")) {
        int high = same(va_arg(ap, const char *), "high");
        u32 page = va_arg(ap, u32), va = va_arg(ap, u32), pfn = va_arg(ap, u32);
        CHECK(page < 3 && va == MEM_EXEC_LOAD_ADDR + page * PAGE_SIZE);
        int mapped = va_arg(ap, int);
        CHECK(mapped || pfn == 0);
        int range = va_arg(ap, int);
        CHECK(range == (high ? pfn > 32 * MEM_1MB / PAGE_SIZE :
                              pfn < 32 * MEM_1MB / PAGE_SIZE));
        observed_pfns[page] = pfn;
        page_logs++;
    } else if (same(fmt, "[h4] %s cleanup owner=%u pages=%u retired=%u\n")) {
        (void)va_arg(ap, const char *);
        CHECK(va_arg(ap, u32) > LEDGER_OWNER_FIXED_LAST);
        CHECK(va_arg(ap, u32) == 0);
        CHECK(va_arg(ap, int) == 1);
        cleanups++;
    } else if (same(fmt, "[h4] %s result=%s entered=%u free_restored=%u\n")) {
        (void)va_arg(ap, const char *);
        const char *result = va_arg(ap, const char *);
        last_entered = va_arg(ap, int);
        CHECK(last_entered == (same(result, "PASS") || bad_pattern || io_fail_at ||
                              io_corrupt || io_no_copy || io_wrong_mount));
        CHECK(va_arg(ap, int) == 1);
        results++;
    } else if (same(fmt, "[h4] io %s backend=%s result=%s stage=%s bytes=%u removed=%u\n")) {
        (void)va_arg(ap, const char *);
        const char *backend = va_arg(ap, const char *);
        CHECK(same(backend, "ext2") || same(backend, "hostdrv"));
        const char *result = va_arg(ap, const char *);
        const char *stage = va_arg(ap, const char *);
        io_passes += same(result, "PASS");
        io_skips += same(result, "SKIP");
        CHECK(same(result, "SKIP") == same(stage, "mount"));
        CHECK(va_arg(ap, u32) == PAGE_SIZE + 34);
        (void)va_arg(ap, int);
        io_logs++;
    } else if (same(fmt, "[h4] io dma pool pa=%08X bytes=%u range_bank=%u\n")) {
        u32 pa = va_arg(ap, u32), size = va_arg(ap, u32);
        int valid = va_arg(ap, int);
        CHECK(valid == (pa >= 0x7f0000 && pa + size <= 0x800000 &&
                        size == PAGE_SIZE && pa / 0x10000 == (pa + size - 1) / 0x10000));
        dma_logs++;
    } else if (same(fmt, "[h4] io dma pool result=%s allocated=%u free_restored=%u transfer=none\n")) {
        const char *result = va_arg(ap, const char *);
        dma_passes += same(result, "PASS");
        dma_skips += same(result, "SKIP");
        dma_fails += same(result, "FAIL");
        CHECK(va_arg(ap, u32) == dma_logs);
        CHECK(va_arg(ap, int) == 1);
    }
    va_end(ap);
}
#define dma_alloc actual_dma_alloc
#include "dma_pool_host_source.c"
#undef dma_alloc
int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)
{
    if (++dma_operation == dma_fail_at) return DMA_POOL_ERR_NOSPC;
    return actual_dma_alloc(size, align, limit, out);
}
#include "h4_backing_source.c"

#define HOST_RAM_END (64UL * MEM_1MB)
static u32 storage[(PGALLOC_META_BYTES(HOST_RAM_END / PAGE_SIZE) + 3) / 4];
static void boot_pool(void)
{
    u32 args[6] = {MEM_1MB, HOST_RAM_END - MEM_1MB, 3, 0x32, ~0U, 0}, mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(args) : "memory");
    CHECK(mapped == MEM_1MB);
    host_map_fixed_paging();
    paging_init(HOST_RAM_END / 1024);
    struct physmem model;
    physmem_bootstrap_legacy(&model, 16 * 1024);
    CHECK(physmem_exclude(&model, MEM_SYSTEM_SPACE_BASE / PAGE_SIZE,
                         MEM_SYSTEM_SPACE_END / PAGE_SIZE, PHYSMEM_RESERVED));
    CHECK(physmem_add_trusted(&model, MEM_HIGH_RAM_BASE / PAGE_SIZE,
                              HOST_RAM_END / PAGE_SIZE, PHYSMEM_SOURCE_SYNTHETIC));
    workspace_first = MEM_SYSTEM_SPACE_BASE / PAGE_SIZE - 16;
    workspace_end = MEM_SYSTEM_SPACE_BASE / PAGE_SIZE;
    CHECK(physmem_reserve_ram(&model, workspace_first, workspace_end));
    init_core(&model, storage, HOST_RAM_END / PAGE_SIZE);
    model_mode = 1;
    CHECK(pgalloc_stage_online());
}
static void reset_observation(u32 failure)
{
    operation = ksel_fail = entries = skips = page_logs = cleanups = results = 0;
    last_entered = 0;
    dma_logs = dma_passes = dma_skips = dma_fails = 0;
    dma_operation = dma_fail_at = 0;
    io_operation = io_fail_at = io_logs = io_passes = io_removes = io_corrupt = 0;
    io_skips = io_checks = 0;
    io_short = io_no_copy = io_wrong_mount = 0;
    CHECK(!file_fd);
    files_present[0] = files_present[1] = 0;
    fail_at = failure;
}
static void recovered(u32 before, u32 root, u32 flags)
{
    CHECK(!file_fd);
    CHECK(pgalloc_free_pages() == before && host_cr3 == root && host_arch_if == flags);
    for (u32 o = LEDGER_OWNER_FIXED_LAST + 1; o < LEDGER_MAX_OWNERS; o++)
        CHECK(!ledger_owner_pages(o) && !ledger_owners[o].kind);
    CHECK(ledger_selfcheck("h4-host"));
    CHECK(!ledger_bad_free && !ledger_retire_refused);
}
static void classification_and_patterns(void)
{
    struct addrspace as;
    u32 owner, low, pfns[H4_BUFFER_PAGES], high[H4_BUFFER_PAGES], freed;
    reset_observation(0);
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 0, "control", &owner));
    CHECK(!paging_addrspace_create(&as, owner));
    low = pgalloc_alloc_phys(owner, 1);
    CHECK(low && low < H4_PHYS_BOUNDARY);
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) {
        /* Nonadjacent, reversed PFNs: VA order, not physical order, defines
         * the known page pattern. Include exactly 32MiB as a negative below. */
        high[i] = H4_PHYS_BOUNDARY / PAGE_SIZE + 7 - 2 * i;
        u32 pfn;
        CHECK(pgalloc_alloc_n_owner(owner, 1, high[i], high[i] + 1,
                                   LEDGER_BOTTOM_UP, &pfn) && pfn == high[i]);
        CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE,
                                        high[i] * PAGE_SIZE, PAGE_RW | PTE_USER));
        for (u32 j = 0; j < PAGE_SIZE; j++)
            ((u8 *)P2V(high[i] * PAGE_SIZE))[j] = h4_pattern(i, j);
    }
    CHECK(h4_collect_pfns(&as, 1, pfns));
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) CHECK(pfns[i] == high[i]);
    CHECK(h4_buffer_entry(&as, MEM_EXEC_LOAD_ADDR + H4_BUFFER_OFFSET, H4_BUFFER_BYTES));
    CHECK(h4_io_roundtrip(&as, 1, 0) == 1 && h4_io_roundtrip(&as, 1, 1) == 1);
    CHECK(io_logs == 2 && io_passes == 2 && io_removes == 2 && !file_exists);
    /* Corrupt each of the three covered portions, including the final byte. */
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) {
        u32 offset = i == 0 ? H4_BUFFER_OFFSET : i == 1 ? PAGE_SIZE / 2 : 16;
        u8 *p = P2V(high[i] * PAGE_SIZE + offset);
        *p ^= 1;
        CHECK(!h4_buffer_entry(&as, MEM_EXEC_LOAD_ADDR + H4_BUFFER_OFFSET, H4_BUFFER_BYTES));
        *p ^= 1;
        /* One low page anywhere must invalidate the whole high buffer. */
        CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE,
                                        low, PAGE_RW | PTE_USER));
        CHECK(!h4_collect_pfns(&as, 1, pfns));
        CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE,
                                        high[i] * PAGE_SIZE, PAGE_RW | PTE_USER));
    }
    u32 boundary;
    CHECK(pgalloc_alloc_n_owner(owner, 1, H4_PHYS_BOUNDARY / PAGE_SIZE,
                               H4_PHYS_BOUNDARY / PAGE_SIZE + 1, LEDGER_BOTTOM_UP, &boundary));
    CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                    boundary * PAGE_SIZE, PAGE_RW | PTE_USER));
    CHECK(!h4_collect_pfns(&as, 1, pfns));
    CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                    high[1] * PAGE_SIZE, PAGE_RO | PTE_USER));
    CHECK(!h4_collect_pfns(&as, 1, pfns));
    /* The production collector must reject missing RW, not just range. */
    u32 *pt = P2V(as.app_pt_phys[0]);
    u32 idx = ((MEM_EXEC_LOAD_ADDR + PAGE_SIZE) >> PAGE_SHIFT) % PTE_COUNT;
    u32 pa;
    unsigned int flags = irq_save();
    CHECK(!as_access_page(&as, MEM_EXEC_LOAD_ADDR + PAGE_SIZE, 1, &pa));
    irq_restore(flags);
    pt[idx] = 0;
    CHECK(!h4_collect_pfns(&as, 1, pfns));
    paging_addrspace_destroy(&as);
    CHECK(ledger_reclaim_owner(owner, &freed) && !ledger_owner_pages(owner));
    CHECK(ledger_owner_retire(owner));
}
static void dma_controls(void)
{
    CHECK(MEM_DMA_POOL_BASE == 0x7f0000 && MEM_DMA_POOL_SIZE == 0x10000);
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES);
    struct dma_buf held;
    CHECK(!dma_alloc(DMA_POOL_MAX_BYTES, 0, DMA_PHYS_LIMIT, &held));
    u32 free = dma_pool_free_pages();
    reset_observation(0);
    h4_dma_pool();
    CHECK(!ksel_fail && dma_logs == free && dma_passes == 1);
    CHECK(dma_pool_free_pages() == free && !dma_free(&held));
    /* No allocation is SKIP; any partially allocated free pool is FAIL.
     * Each injected failure must still return every acquired page. */
    for (u32 fail = 1; fail <= DMA_POOL_PAGES; fail++) {
        reset_observation(0);
        dma_fail_at = fail;
        h4_dma_pool();
        CHECK(dma_logs == fail - 1 && !dma_passes);
        CHECK(ksel_fail == (fail > 1) && dma_fails == (fail > 1));
        CHECK(dma_skips == (fail == 1));
        CHECK(dma_pool_free_pages() == DMA_POOL_PAGES && !dma_pool_bad_free());
    }
    reset_observation(0);
    /* An old pool address must be rejected even though it is ISA-addressable.
     * The real allocator is used; only its configured base is perturbed. */
    s_pool.base = 0x3e0000;
    reset_observation(0);
    h4_dma_pool();
    CHECK(ksel_fail == DMA_POOL_PAGES && dma_fails == 1);
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES);
    dma_pool_init();
    struct dma_buf full[2];
    CHECK(!dma_alloc(DMA_POOL_MAX_BYTES, 0, DMA_PHYS_LIMIT, &full[0]));
    CHECK(!dma_alloc(DMA_POOL_MAX_BYTES, 0, DMA_PHYS_LIMIT, &full[1]));
    reset_observation(0);
    h4_dma_pool();
    CHECK(!ksel_fail && !dma_logs && dma_skips == 1);
    CHECK(!dma_free(&full[0]) && !dma_free(&full[1]));
    CHECK(dma_pool_free_pages() == DMA_POOL_PAGES && !dma_pool_bad_free());
}

/* Compile the actual FDC setup, without any device operations. Its BSS
 * address comes from this build's kernel ELF; synthetic cases also cover
 * both choices made by fdc_buf_layout. No driver/export change is needed. */
extern char __bss_end[];
static u32 host_fdc_base;
#include "fdc_setup_source.c"
static void fdc_window(void)
{
    CHECK(HOST_FDC_BSS >= HOST_BSS_START);
    CHECK(HOST_FDC_BSS + FDC_BUF_BYTES <= (u32)__bss_end);
    CHECK(HOST_FDC_BSS + FDC_BUF_BYTES <= MEM_DMA_POOL_BASE);
    const u32 bases[] = {HOST_FDC_BSS, 0x200000, 0x20fff0};
    for (u32 i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        host_fdc_base = bases[i];
        s_dma = 0;
        fdc_buf_setup();
        CHECK(s_dma_pa == V2P(s_dma));
        CHECK(s_dma_pa >= host_fdc_base);
        CHECK(s_dma_pa + FDC_DMA_BUF_SIZE <= host_fdc_base + FDC_BUF_BYTES);
        CHECK(dma_range_ok(s_dma_pa, FDC_DMA_BUF_SIZE, DMA_PHYS_LIMIT));
        CHECK(s_slots >= (u8 *)P2V(host_fdc_base));
        CHECK(s_slots + FDC_CACHE_BYTES <= (u8 *)P2V(host_fdc_base + FDC_BUF_BYTES));
        CHECK(s_dma + FDC_DMA_BUF_SIZE <= s_slots || s_slots + FDC_CACHE_BYTES <= s_dma);
    }
    say("[h4] io fdc BSS window: PASS (ELF address + actual setup; no transfer)\n");
}

void _start(void)
{
    boot_pool();
    dma_pool_init();
    fdc_window();
    u32 before = pgalloc_free_pages(), root = host_cr3;
    for (u32 f = 0; f < 2; f++) {
        host_arch_if = f ? 0x202 : 2;
        reset_observation(0);
        test_h4_backing();
        CHECK(!ksel_fail && entries == 2 && !skips && page_logs == 6 && cleanups == 2 && results == 2);
        CHECK(io_logs == 4 && io_passes == 4 && io_removes == 4 && !file_exists);
        recovered(before, root, f ? 0x202 : 2);
        for (int high = 0; high < 2; high++) {
            reset_observation(0);
            CHECK(h4_backing_case(high) == 1 && !ksel_fail);
            u32 operations = operation, io_operations = io_operation;
            /* Every I/O failure, on both backends, must fail (never SKIP).
             * This includes open, each page, sync, size and remove. */
            for (u32 fail = 1; fail <= io_operations; fail++) {
                for (int short_io = 0; short_io < 2; short_io++) {
                    reset_observation(0);
                    io_fail_at = fail;
                    io_short = short_io;
                    CHECK(h4_backing_case(high) == -1 && ksel_fail == 1);
                    CHECK(!skips && io_logs == 2 && io_passes == 1);
                    CHECK(io_removes >= 1);
                    recovered(before, root, f ? 0x202 : 2);
                }
            }
            for (u32 corrupt = 0; corrupt < 4; corrupt++) {
                reset_observation(0);
                if (corrupt < 3)
                    io_corrupt = corrupt == 0 ? 1 : corrupt == 1 ? 18 : sizeof(file_data);
                else io_no_copy = 1;
                CHECK(h4_backing_case(high) == -1 && ksel_fail == 2);
                CHECK(!skips && io_logs == 2 && !io_passes);
                CHECK(io_removes == 2);
                recovered(before, root, f ? 0x202 : 2);
            }
            /* FD root, absent HostDrv, and neither backend: unavailable
             * backends log SKIP without contributing a check or failure. */
            for (int missing = 1; missing <= 3; missing++) {
                reset_observation(0);
                io_wrong_mount = missing;
                CHECK(h4_backing_case(high) == (missing != 3) && !ksel_fail);
                CHECK(!skips && io_logs == 2 && io_skips == (missing == 3 ? 2U : 1U));
                CHECK(io_passes == 2 - io_skips && io_checks == io_passes);
                CHECK(io_removes == io_passes);
                CHECK(!files_present[0] && !files_present[1]);
                recovered(before, root, f ? 0x202 : 2);
                if (missing != 3) {
                    reset_observation(0);
                    io_wrong_mount = missing;
                    io_fail_at = 1;
                    CHECK(h4_backing_case(high) == -1 && ksel_fail == 1);
                    CHECK(io_skips == 1 && io_checks == 1 && !io_passes);
                    recovered(before, root, f ? 0x202 : 2);
                }
            }
            /* Simulate interruption after write, including a failed stale
             * removal. A later attempt must recover without manual cleanup. */
            reset_observation(0);
            files_present[0] = files_present[1] = 1;
            CHECK(h4_backing_case(high) == 1 && !ksel_fail);
            CHECK(io_passes == 2 && io_removes == 4);
            CHECK(!files_present[0] && !files_present[1]);
            recovered(before, root, f ? 0x202 : 2);
            for (int backend = 0; backend < 2; backend++) {
                reset_observation(0);
                files_present[backend] = 1;
                io_fail_at = backend ? io_operations / 2 + 2 : 2;
                CHECK(h4_backing_case(high) == -1 && ksel_fail == 1);
                CHECK(io_passes == 1 && files_present[backend]);
                CHECK(!files_present[!backend] && io_removes == 2);
                recovered(before, root, f ? 0x202 : 2);
                io_fail_at = 0;
                CHECK(h4_backing_case(high) == 1 && ksel_fail == 1);
                CHECK(io_passes == 3 && io_removes == 5);
                CHECK(!files_present[0] && !files_present[1]);
                recovered(before, root, f ? 0x202 : 2);
            }
            for (u32 fail = 1; fail <= operations; fail++) {
                reset_observation(fail);
                CHECK(h4_backing_case(high) == 0 && !ksel_fail);
                CHECK(!entries && skips == 1 && !page_logs && results == 1);
                CHECK(cleanups == (fail != 1));
                recovered(before, root, f ? 0x202 : 2);
            }
            for (int fault = 0; fault < 2; fault++) {
                reset_observation(0);
                bad_mapping = !fault;
                bad_pattern = fault;
                CHECK(h4_backing_case(high) == -1 && ksel_fail == 1);
                CHECK(entries == (u32)fault && last_entered == (u32)fault);
                CHECK(!skips && page_logs == 3 && cleanups == 1 && results == 1);
                recovered(before, root, f ? 0x202 : 2);
                bad_mapping = bad_pattern = 0;
            }
        }
    }
    dma_controls();
    classification_and_patterns();
    recovered(before, root, host_arch_if);
    /* A real empty high interval, without injection: retain all high RAM
     * under a different owner, leaving low RAM available for AS tables. */
    u32 holder, pfn, freed;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 0, "held", &holder));
    while (pgalloc_alloc_n_owner(holder, 1, H4_PHYS_BOUNDARY / PAGE_SIZE + 1,
                                pgalloc_limit_pfn(), LEDGER_BOTTOM_UP, &pfn)) {}
    reset_observation(0);
    CHECK(h4_backing_case(1) == 0 && skips == 1 && !entries && !ksel_fail);
    CHECK(ledger_reclaim_owner(holder, &freed) && ledger_owner_retire(holder));
    recovered(before, root, host_arch_if);
    say("PASS: h4 real high/low USER PFNs, strict boundary, mixed pages, patterns, all allocation/map failures, owner recovery, I/O errors/short counts/corruption/cleanup, DMA pool and FDC window\n");
    die(0);
}
