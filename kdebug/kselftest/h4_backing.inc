/* T3b2: H-4(a) backing fixture. Keep the 32MiB acceptance boundary independent
 * of the boot identity-map size. Both cases deliberately use the SAME high VA.
 * T3b3 performs I/O inside each case, before destroying its AS/backing. */
#define H4_PHYS_BOUNDARY (32UL * MEM_1MB)
#define H4_BUFFER_PAGES 3U
#define H4_BUFFER_OFFSET (PAGE_SIZE - 17U)
#define H4_BUFFER_BYTES (PAGE_SIZE + 34U)

static u8 h4_pattern(u32 page, u32 offset)
{
    return (u8)(0x31U + page * 0x29U + offset % 251U);
}

/* Record every buffer page, including the partial first/last pages. A high
 * virtual address is never evidence about the physical backing. */
static int h4_collect_pfns(struct addrspace *as, int high, u32 *pfns)
{
    int valid = 1;
    const char *kind = high ? "high" : "low";
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) {
        u32 va = MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE, pa = 0;
        unsigned int flags = irq_save();
        int mapped = as_access_page(as, va, 1, &pa);
        irq_restore(flags);
        pfns[i] = pa / PAGE_SIZE;
        int range = high ? pfns[i] > H4_PHYS_BOUNDARY / PAGE_SIZE :
                           pfns[i] < H4_PHYS_BOUNDARY / PAGE_SIZE;
        valid &= mapped && range;
        kprintf(KLOG_ATTR, "[h4] %s page=%u va=%08X pfn=%08X user_rw=%u range=%u\n",
                kind, i, va, pfns[i], mapped, range);
    }
    return valid;
}

/* This entry is reached only after allocation, USER mapping and ALL PFNs
 * have passed. Walk the live AS again to read the page-crossing buffer;
 * do not treat a direct read of the original allocation as USER evidence. */
static int h4_buffer_entry(struct addrspace *as, u32 buffer, u32 bytes)
{
    u32 done = 0;
    while (done < bytes) {
        u32 va = buffer + done, pa = 0;
        u32 offset = va % PAGE_SIZE;
        u32 page = (va - MEM_EXEC_LOAD_ADDR) / PAGE_SIZE;
        u32 n = PAGE_SIZE - offset;
        if (n > bytes - done) n = bytes - done;
        unsigned int flags = irq_save();
        int mapped = as_access_page(as, va, 0, &pa);
        irq_restore(flags);
        if (!mapped) return 0;
        const u8 *p = P2V(pa);
        for (u32 j = 0; j < n; j++)
            if (p[j] != h4_pattern(page, offset + j)) return 0;
        done += n;
    }
    return 1;
}

/* Walk each USER page to its identity alias. VFS receives the actual backing,
 * including >32MiB pages, never a low bounce buffer owned by this fixture.
 * mode: 0=restore pattern, 1=write, 2=poison then read into the same pages. */
static int h4_io_buffer(struct addrspace *as, u32 buffer, u32 bytes,
                        int fd, int mode)
{
    for (u32 done = 0; done < bytes;) {
        u32 va = buffer + done, pa = 0, offset = va % PAGE_SIZE;
        u32 page = (va - MEM_EXEC_LOAD_ADDR) / PAGE_SIZE;
        u32 n = PAGE_SIZE - offset;
        if (n > bytes - done) n = bytes - done;
        unsigned int flags = irq_save();
        int mapped = as_access_page(as, va, 1, &pa);
        irq_restore(flags);
        if (!mapped) return 0;
        u8 *p = P2V(pa);
        if (mode != 1)
            for (u32 j = 0; j < n; j++)
                p[j] = h4_pattern(page, offset + j) ^ (mode == 2 ? 0xff : 0);
        if (mode == 1 && vfs_write_fd(fd, p, n) != (int)n) return 0;
        if (mode == 2 && vfs_read_fd(fd, p, n) != (int)n) return 0;
        done += n;
    }
    return 1;
}

#define H4_ROOT_FILE "/h4-io.tmp"
#define H4_HOST_FILE "/host/h4-io.tmp"

/* Return 1=passed, 0=backend unavailable, -1=failed. */
static int h4_io_roundtrip(struct addrspace *as, int high, int host)
{
    const char *path = host ? H4_HOST_FILE : H4_ROOT_FILE;
    const char *backend = host ? "hostdrv" : "ext2";
    const u32 buffer = MEM_EXEC_LOAD_ADDR + H4_BUFFER_OFFSET;
    OS32_Stat st;
    int fd = -1, attempted = 0, opened = 0, ok = 0, removed = 0;
    const char *stage = "mount";
    int available = !kstrcmp(vfs_fstype(host ? "/host" : "/"), backend);
    if (!available) goto out;
    /* Boot is single-task. Remove this fixture's leftover from an interrupted
     * boot before starting a fresh roundtrip (HostDrv has no O_EXCL). */
    stage = "absent";
    int status = vfs_stat(path, &st);
    if (status == VFS_OK) {
        stage = "remove-stale";
        if (vfs_rm(path) != VFS_OK) goto out;
    } else if (status != VFS_ERR_NOTFOUND) goto out;
    stage = "prepare";
    if (!h4_io_buffer(as, buffer, H4_BUFFER_BYTES, -1, 0)) goto out;
    stage = "open-write";
    attempted = 1;
    fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) goto out;
    opened = 1;
    stage = "write";
    if (!h4_io_buffer(as, buffer, H4_BUFFER_BYTES, fd, 1)) goto out;
    vfs_close(fd);
    fd = -1;
    stage = "sync";
    if (vfs_sync() != VFS_OK) goto out;
    stage = "open-read";
    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) goto out;
    stage = "size";
    if (vfs_get_size(fd) != H4_BUFFER_BYTES) goto out;
    stage = "read";
    if (!h4_io_buffer(as, buffer, H4_BUFFER_BYTES, fd, 2)) goto out;
    stage = "compare";
    ok = h4_buffer_entry(as, buffer, H4_BUFFER_BYTES);
out:
    if (fd >= 0) vfs_close(fd);
    if (attempted) {
        int rc = vfs_rm(path);
        removed = rc == VFS_OK || (!opened && rc == VFS_ERR_NOTFOUND);
        if (!removed) { ok = 0; stage = "remove"; }
    }
    if (available) check(ok, "h4: I/O roundtrip and cleanup");
    kprintf(KLOG_ATTR, "[h4] io %s backend=%s result=%s stage=%s bytes=%u removed=%u\n",
            high ? "high" : "low", backend,
            !available ? "SKIP" : ok ? "PASS" : "FAIL",
            stage, H4_BUFFER_BYTES, removed);
    return !available ? 0 : ok ? 1 : -1;
}

/* Allocate only presently free pool pages; never reinitialize a live pool
 * after PCI/PCM binding. These buffers are not submitted to any device. */
static void h4_dma_pool(void)
{
    struct dma_buf buffers[DMA_POOL_PAGES];
    u32 before = dma_pool_free_pages(), count = 0;
    int ok = 1;
    while (count < before) {
        struct dma_buf *b = &buffers[count];
        if (dma_alloc(MEM_PAGE_SIZE, 0, DMA_PHYS_LIMIT, b) != 0) break;
        int valid = b->pa >= MEM_DMA_POOL_BASE &&
                    b->pa < MEM_DMA_POOL_BASE + MEM_DMA_POOL_SIZE &&
                    b->size == MEM_PAGE_SIZE &&
                    b->size <= MEM_DMA_POOL_BASE + MEM_DMA_POOL_SIZE - b->pa &&
                    b->va == P2V(b->pa) &&
                    dma_range_ok(b->pa, b->size, DMA_PHYS_LIMIT);
        check(valid, "h4: DMA allocation range/bank");
        ok &= valid;
        kprintf(KLOG_ATTR, "[h4] io dma pool pa=%08X bytes=%u range_bank=%u\n",
                b->pa, b->size, valid);
        count++;
    }
    if (count) {
        int complete = count == before;
        check(complete, "h4: DMA allocation count matches free pages");
        ok &= complete;
    }
    for (u32 i = 0; i < count; i++) {
        int freed = dma_free(&buffers[i]) == 0;
        check(freed, "h4: DMA free");
        ok &= freed;
    }
    int restored = dma_pool_free_pages() == before;
    check(restored, "h4: DMA free pages restored");
    ok &= restored;
    kprintf(KLOG_ATTR, "[h4] io dma pool result=%s allocated=%u free_restored=%u transfer=none\n",
            !ok ? "FAIL" : !count ? "SKIP" : "PASS",
            count, restored);
}

/* Return 1=completed, 0=not run (resources), -1=failed. All exits after
 * owner creation converge on destroy/reclaim/retire, including partial maps. */
static int h4_backing_case(int high)
{
    struct addrspace as;
    u32 owner = 0, pfns[H4_BUFFER_PAGES], left = 0;
    u32 before = pgalloc_free_pages();
    int created = 0, entered = 0, result = 0;
    const char *kind = high ? "high" : "low", *stage = "owner";
    if (!ledger_owner_new(LEDGER_KIND_AS, 0, "h4", &owner)) goto out;
    stage = "AS";
    if (paging_addrspace_create(&as, owner) != 0) goto out;
    created = 1;
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) {
        u32 pa, pfn;
        stage = "backing";
        if (high) {
            if (!pgalloc_alloc_n_owner(owner, 1,
                    H4_PHYS_BOUNDARY / PAGE_SIZE + 1, pgalloc_limit_pfn(),
                    LEDGER_BOTTOM_UP, &pfn)) goto out;
            pa = pfn * PAGE_SIZE;
        } else {
            pa = pgalloc_alloc_phys(owner, 1);
            if (!pa) goto out;
        }
        u8 *p = P2V(pa);
        for (u32 j = 0; j < PAGE_SIZE; j++) p[j] = h4_pattern(i, j);
        stage = "USER map";
        if (paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE,
                                     pa, PAGE_RW | PTE_USER) != 0) goto out;
    }
    result = -1;
    int valid = h4_collect_pfns(&as, high, pfns);
    check(valid, "h4: all USER PFNs in physical range");
    if (!valid) goto out;
    entered = 1;
    int pattern = h4_buffer_entry(&as, MEM_EXEC_LOAD_ADDR + H4_BUFFER_OFFSET,
                                 H4_BUFFER_BYTES);
    check(pattern, "h4: page-crossing pattern");
    kprintf(KLOG_ATTR, "[h4] %s entry va=%08X bytes=%u pages=%u pattern=%s\n",
            kind, MEM_EXEC_LOAD_ADDR + H4_BUFFER_OFFSET,
            H4_BUFFER_BYTES, H4_BUFFER_PAGES, pattern ? "ok" : "FAIL");
    if (pattern) {
        int root_io = h4_io_roundtrip(&as, high, 0);
        int host_io = h4_io_roundtrip(&as, high, 1);
        if (root_io >= 0 && host_io >= 0) result = root_io > 0 || host_io > 0;
    }
out:
    if (!result && !entered)
        kprintf(KLOG_ATTR, "[h4] %s SKIP (not run): %s allocation failed\n", kind, stage);
    if (created) paging_addrspace_destroy(&as);
    if (owner) {
        int reclaimed = ledger_reclaim_owner(owner, &left);
        u32 pages = ledger_owner_pages(owner);
        int retired = ledger_owner_retire(owner);
        check(reclaimed && !pages && retired, "h4: owner pages 0 and retired");
        if (!reclaimed || pages || !retired) result = -1;
        kprintf(KLOG_ATTR, "[h4] %s cleanup owner=%u pages=%u retired=%u\n",
                kind, owner, pages, retired);
    }
    int restored = pgalloc_free_pages() == before;
    check(restored, "h4: free pages restored");
    if (!restored) result = -1;
    kprintf(KLOG_ATTR, "[h4] %s result=%s entered=%u free_restored=%u\n", kind,
            result > 0 ? "PASS" : result < 0 ? "FAIL" : "SKIP", entered, restored);
    return result;
}

static void test_h4_backing(void)
{
    (void)h4_backing_case(0);
    (void)h4_backing_case(1);
    h4_dma_pool();
}
/* T3b2 end */
