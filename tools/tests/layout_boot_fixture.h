/* T3b0 test-only runtime inputs. No proposed addresses enter product sources.
 * Both harnesses use the same synthetic chain end and detected RAM inputs.
 * This is a contract fixture, not an implementation of the future boot adapter. */
struct layout_fixture {
    u32 meta, slack_end, guard_lo, pt, pt_end, guard_hi, stack, stack_end, dma;
    int resident_meta, slack_rw;
};
static const struct layout_fixture layout_fixtures[] = {
    { 0x2F9000, 0x200000, 0x3F0000,
      0x3F1000, 0x3FC000, 0x2FB000,
      0x2FC000, 0x300000, 0x2E8000, 0, 0 },
    { 0x17E000, 0x3E0000, 0x3E0000, 0x3E1000, 0x3FB000,
      0x3FB000, 0x3FC000, 0x400000, 0x7F0000, 1, 1 },
};
/* Detected top in KiB, backing policy, and expected device activation.
 * Non-DMA boot services (including FDC's separate buffer) stay enabled. */
static const struct {
    u32 kb;
    int fixed, old_dma, new_dma, boot_continues, fdc;
} layout_ram_inputs[] = {
    { 8088,  1, 1, 0, 1, 1 },
    { 8192,  1, 1, 1, 1, 1 },
    { 17408, 0, 1, 1, 1, 1 },
    { 65536, 0, 1, 1, 1, 1 },
};
#define LF_PAGE 0x1000UL
#define LF_CORE_END 0x180000UL
#define LF_CHAIN_END 0x1F0000UL
#define LF_DMA_SIZE 0x10000UL

struct layout_boot_decision {
    int dma, pcm_enabled, e82557_enabled, fdc, boot_continues;
};
static struct layout_boot_decision layout_decide_boot(const struct layout_fixture *f, u32 kb)
{
    struct layout_boot_decision result;
    result.dma = kb * 1024 >= f->dma + LF_DMA_SIZE;
    result.pcm_enabled = result.dma;
    result.e82557_enabled = result.dma;
    /* FDC has a separate buffer; a missing DMA pool is not fatal to boot. */
    result.fdc = 1;
    result.boot_continues = 1;
    return result;
}

/* Byte half-open first, then aligned PFNs. Empty is legal and omitted;
 * reversed is an error, including after rounding an unaligned chain end. */
static int layout_slack(u32 chain, u32 end, u32 *first)
{
    if (chain > end) return -1;
    *first = (chain + LF_PAGE - 1) & ~(LF_PAGE - 1);
    return *first == end ? 0 : 1;
}
static int layout_region(u32 first, u32 end, u32 type)
{
    if (first == end) return 1;
    if (first > end || ((first | end) & (LF_PAGE - 1))) return 0;
    return ledger_register_region(type, LEDGER_OWNER_KERNEL,
                                  first / LF_PAGE, end / LF_PAGE,
                                  LEDGER_CACHE_WB, LEDGER_RF_PERMANENT);
}
static void layout_pte(u32 first, u32 end, int present)
{
    u32 a;
    for (a = first; a < end; a += LF_PAGE) {
        u32 entry = page_tables[a / (LF_PAGE * PTE_COUNT)][a / LF_PAGE % PTE_COUNT];
        CHECK(!!(entry & PTE_PRESENT) == present);
        if (present) {
            CHECK((entry & ~(LF_PAGE - 1)) == a);
            CHECK((entry & (PTE_USER | PTE_PCD | PTE_PWT)) == 0);
            CHECK(entry & PTE_RW);
        }
    }
}
static void layout_boot_fixture(u32 kb)
{
    u32 n;
    for (n = 0; n < sizeof(layout_fixtures) / sizeof(layout_fixtures[0]); n++) {
        const struct layout_fixture *f = &layout_fixtures[n];
        struct physmem supply;
        u32 first, pages, i, p, hits, before;
        struct layout_boot_decision decision = layout_decide_boot(f, kb);
        int fixed = kb <= 12292, dma = decision.dma;
        int expected_dma;
        /* Only replace the region manifest being exercised. The initialized
         * allocator is retained; this fixture runs last in each process. */
        for (i = 0; i < sizeof(layout_ram_inputs) / sizeof(layout_ram_inputs[0]); i++)
            if (layout_ram_inputs[i].kb == kb) break;
        CHECK(i < sizeof(layout_ram_inputs) / sizeof(layout_ram_inputs[0]));
        CHECK(fixed == layout_ram_inputs[i].fixed);
        expected_dma = f->resident_meta ? layout_ram_inputs[i].new_dma :
                                         layout_ram_inputs[i].old_dma;
        CHECK(decision.dma == expected_dma);
        CHECK(decision.pcm_enabled == expected_dma);
        CHECK(decision.e82557_enabled == expected_dma);
        CHECK(decision.boot_continues == layout_ram_inputs[i].boot_continues);
        CHECK(decision.fdc == layout_ram_inputs[i].fdc);
        ledger_region_count = 0;
        physmem_init(&supply);
        CHECK(physmem_add_trusted(&supply, 0, kb / 4, PHYSMEM_SOURCE_MACHINE));
        if (f->resident_meta) {
            CHECK(layout_region(0x100000, f->meta, LEDGER_R_FIXED));
            CHECK(layout_region(f->meta + 2 * LF_PAGE, LF_CORE_END, LEDGER_R_FIXED));
        } else {
            CHECK(layout_region(0x100000, LF_CORE_END, LEDGER_R_FIXED));
        }
        CHECK(layout_region(f->meta, f->meta + 2 * LF_PAGE, LEDGER_R_FIXED));
        /* The dynamic backing is distinct from the core's spare two pages. */
        if (!fixed)
            CHECK(layout_region((kb < 15360 ? kb : 15360) * 1024 -
                                ((PGALLOC_META_BYTES(kb / 4) + LF_PAGE - 1) / LF_PAGE +
                                 1 + (kb > 32768 ? kb / 4096 - 8 : 0)) * LF_PAGE,
                                (kb < 15360 ? kb : 15360) * 1024,
                                LEDGER_R_FIXED));
        for (p = f->meta / LF_PAGE; p < f->meta / LF_PAGE + 2; p++) {
            hits = 0;
            for (i = 0; i < ledger_region_count; i++)
                if (ledger_regions[i].first <= p && ledger_regions[i].end > p) hits++;
            CHECK(hits == 1);
        }
        CHECK(!layout_region(f->meta, f->meta + LF_PAGE, LEDGER_R_FIXED));

        CHECK(layout_slack(f->slack_end, f->slack_end, &first) == 0);
        CHECK(layout_slack(f->slack_end + 1, f->slack_end, &first) == -1);
        CHECK(layout_slack(f->slack_end - 1, f->slack_end, &first) == 0);
        CHECK(layout_slack(f->slack_end - LF_PAGE, f->slack_end, &first) == 1);
        CHECK(first == f->slack_end - LF_PAGE);
        CHECK(layout_slack(LF_CHAIN_END + 1, f->slack_end, &first) == 1);
        CHECK(first == LF_CHAIN_END + LF_PAGE);
        CHECK(layout_slack(LF_CHAIN_END, f->slack_end, &first) == 1);
        /* Consume the whole eligible SLACK once with the real RAM allocator;
         * a second provider must fail, rather than supply the same pages twice. */
        for (i = 0; i < ledger_region_count; i++)
            CHECK(physmem_exclude(&supply, ledger_regions[i].first,
                                 ledger_regions[i].end, PHYSMEM_RESERVED));
        CHECK(physmem_reserve_ram(&supply, first / LF_PAGE, f->slack_end / LF_PAGE));
        CHECK(!physmem_reserve_ram(&supply, first / LF_PAGE, f->slack_end / LF_PAGE));
        CHECK(physmem_count(&supply, first / LF_PAGE, f->slack_end / LF_PAGE,
                            PHYSMEM_RAM, &pages) && pages == 0);

        CHECK(layout_region(f->guard_lo, f->guard_lo + LF_PAGE, LEDGER_R_FIXED));
        CHECK(layout_region(f->pt, f->pt_end, LEDGER_R_FIXED));
        CHECK(layout_region(f->guard_hi, f->guard_hi + LF_PAGE, LEDGER_R_FIXED));
        CHECK(layout_region(f->stack, f->stack_end, LEDGER_R_FIXED));
        before = ledger_region_count;
        CHECK(!layout_region(f->pt - LF_PAGE, f->pt + LF_PAGE, LEDGER_R_FIXED));
        CHECK(!layout_region(f->guard_hi, f->stack + LF_PAGE, LEDGER_R_FIXED));
        CHECK(!layout_region(f->stack_end - LF_PAGE, f->stack_end + LF_PAGE, LEDGER_R_FIXED));
        CHECK(ledger_region_count == before);
        if (f->resident_meta) {
            CHECK(f->pt_end - f->pt == 26 * LF_PAGE);
            CHECK(f->guard_lo + LF_PAGE == f->pt && f->pt_end == f->guard_hi);
            CHECK(f->guard_hi + LF_PAGE == f->stack && f->stack_end == 0x400000);
            CHECK(dma == (kb >= 8192));
        }
        if (f->resident_meta)
            CHECK((f->dma & (LF_DMA_SIZE - 1)) == 0 && f->dma + LF_DMA_SIZE <= 0x1000000);
        if (dma) {
            CHECK(layout_region(f->dma, f->dma + LF_DMA_SIZE, LEDGER_R_DMA));
            CHECK(physmem_reserve_ram(&supply, f->dma / LF_PAGE,
                                      (f->dma + LF_DMA_SIZE) / LF_PAGE));
            CHECK(!physmem_reserve_ram(&supply, f->dma / LF_PAGE,
                                       (f->dma + LF_DMA_SIZE) / LF_PAGE));
        }
        /* Test-side replay of supplied attributes through real PTE helpers.
         * Production paging_init is covered separately in both layouts. */
        CHECK(paging_map_range(first, f->slack_end, first, PAGE_RW) == 0);
        if (!f->slack_rw) CHECK(paging_set_not_present(first, f->slack_end - 1) == 0);
        CHECK(paging_map_range(f->pt, f->pt_end, f->pt, PAGE_RW) == 0);
        CHECK(paging_map_range(f->stack, f->stack_end, f->stack, PAGE_RW) == 0);
        CHECK(paging_set_not_present(f->guard_lo, f->guard_lo + LF_PAGE - 1) == 0);
        CHECK(paging_set_not_present(f->guard_hi, f->guard_hi + LF_PAGE - 1) == 0);
        CHECK(paging_map_range(f->meta, f->meta + 2 * LF_PAGE, f->meta, PAGE_RW) == 0);
        if (!f->resident_meta && !fixed)
            CHECK(paging_set_not_present(f->meta, f->meta + 2 * LF_PAGE - 1) == 0);
        layout_pte(first, f->slack_end, f->slack_rw);
        layout_pte(f->pt, f->pt_end, 1);
        layout_pte(f->guard_lo, f->guard_lo + LF_PAGE, 0);
        layout_pte(f->guard_hi, f->guard_hi + LF_PAGE, 0);
        layout_pte(f->stack, f->stack_end, 1);
        layout_pte(f->meta, f->meta + 2 * LF_PAGE, f->resident_meta || fixed);
        if (dma) CHECK(paging_map_range(f->dma, f->dma + LF_DMA_SIZE, f->dma, PAGE_RW) == 0);
        else CHECK(paging_set_not_present(f->dma, f->dma + LF_DMA_SIZE - 1) == 0);
        layout_pte(f->dma, f->dma + LF_DMA_SIZE, dma);
    }
}
