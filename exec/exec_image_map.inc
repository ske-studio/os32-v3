/* Image memory budget, mapping and launch-data storage. Included by exec.c. */
/* shlib の私有 data/BSS も起動時の物理予算に含める。 */
static u32 exec_ring3_extra_pages(void)
{
    return shlib_data_pages();
}

static u32 exec_ring3_pages(u32 load_base, u32 sbrk_end, u32 exec_heap_size,
                            u32 stack_size)
{
    u32 pages = (sbrk_end - load_base + exec_heap_size + stack_size) / PAGE_SIZE
              + 2 /* PD + first lease PT */ + exec_ring3_extra_pages();
    /* 疎な PT の集合。image/shlib、heap/stack が同じ PDE を使っても 1 枚。 */
    for (u32 pde = APP_BAND_PDE; pde < MEM_APP_STACK_TOP >> PDE_SHIFT; pde++) {
        if ((pde >= PDE_INDEX(load_base) && pde <= (sbrk_end - 1) >> PDE_SHIFT) ||
            (pde >= MEM_EXEC_HEAP_BASE >> PDE_SHIFT &&
             pde <= (MEM_EXEC_HEAP_BASE + exec_heap_size - 1) >> PDE_SHIFT) ||
            (pde >= (MEM_APP_STACK_TOP - stack_size) >> PDE_SHIFT) ||
            pde == MEM_SHLIB_BASE >> PDE_SHIFT)
            pages++;
    }
    return pages;
}

/* ======================================================================== */
/*  app_map_region — アプリ帯の 1 領域を per-app 物理で張る (D1)             */
/*                                                                          */
/*  まず連続で取り (P1 の map_user_range_phys)、断片化で取れなければ          */
/*  ページ単位に倒す。per-app 物理にした副産物で連続は必須ではない —          */
/*  K5a の申し送り D10 が「断片化が出たらページ単位へ倒せ」と書いた点。       */
/*  途中で尽きたら -1。張り終えたぶんは呼び出し側の巻き戻し                   */
/*  (exec_teardown_app → paging_addrspace_free_user_range) が PTE を辿って   */
/*  返すので、ここで部分解放はしない。                                        */
/* ======================================================================== */
static int app_map_region(struct addrspace *as, u32 vstart, u32 vend)
{
    u32 pages, phys, v;

    if (vstart >= vend) return 0;
    pages = (vend - vstart) / PAGE_SIZE;

    phys = pgalloc_alloc_phys(as->owner, (int)pages);
    if (phys) {
        kmemset(P2V(phys), 0, pages * PAGE_SIZE);
        if (paging_addrspace_map_user_range_phys(as, vstart, vend, phys,
                                                 PAGE_RW | PTE_USER) == 0) {
            return 0;
        }
        pgalloc_free_n_owner(as->owner, phys / PAGE_SIZE, (int)pages);
        return -1;
    }

    for (v = vstart; v < vend; v += PAGE_SIZE) {
        phys = pgalloc_alloc_phys(as->owner, 1);
        if (!phys) return -1;
        kmemset(P2V(phys), 0, PAGE_SIZE);
        if (paging_addrspace_map_user(as, v, phys, PAGE_RW | PTE_USER) != 0) {
            pgalloc_free_n_owner(as->owner, phys / PAGE_SIZE, 1);
            return -1;
        }
    }
    return 0;
}

/* Copy launch data through physical backing while the master PD is active. */
/* Read the launcher's bytes after switching to master; no high-VA dereference. */
static const char *exec_image_reject_reason(const OS32Header *hdr, int is_shell,
                                            u32 load_base, u32 max_size)
{
    const char *reason = 0;
    if (hdr->load_addr != load_base) reason = "Error: invalid image load address\n";
    else if (hdr->flags & OS32X_FLAG_SHLIB) reason = "Error: shared library is not executable\n";
    else if (!hdr->text_size || hdr->entry_offset >= hdr->text_size ||
             hdr->text_size > max_size || hdr->bss_size > max_size - hdr->text_size)
        reason = "Error: invalid image entry or range\n";
    else if (is_shell && hdr->shlib_protocol)
        reason = "Error: resident shell must have shlib_protocol=0\n";
    else if (hdr->shlib_protocol && !shlib_loaded())
        reason = "Error: required shared library is not loaded\n";
    return reason;
}

static int exec_stack_bytes(u32 requested, u32 command_bytes, u32 *bytes)
{
    u32 size = MEM_EXEC_STACK_SIZE;
    if (requested) {
        if (requested > S32_MAX - (PAGE_SIZE - 1)) return -1;
        size = PAGE_ALIGN_UP(requested);
        if (size < MEM_APP_STACK_MIN) size = MEM_APP_STACK_MIN;
    }
    if (size > MEM_APP_STACK_TOP - MEM_EXEC_HEAP_BASE - PAGE_SIZE ||
        command_bytes > size - OS32_MAX_ARGS * sizeof(u32) - 64) return -1;
    *bytes = size;
    return 0;
}

static u8 launch_read_byte(u32 pd, const char *p, int *failed)
{
    u32 pa = (u32)p;
    if ((u32)p >= MEM_APP_BAND_BASE && (u32)p < MEM_LEASE_END &&
        as_va_to_pa_read(pd, (u32)p, &pa)) {
        *failed = 1;
        return 0;
    }
    return *(const u8 *)P2V(pa);
}

static int app_store(AppSlot *a, u32 va, const void *src, u32 len)
{
    const u8 *p = src;
    while (len) {
        u32 pa = va, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (n > len) n = len;
        if (a->cpl3 && as_va_to_pa(a->as->pd_phys, va, &pa)) return -1;
        kmemcpy(P2V(pa), p, n);
        va += n; p += n; len -= n;
    }
    return 0;
}
