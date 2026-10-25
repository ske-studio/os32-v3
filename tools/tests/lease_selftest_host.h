/* Link the moved boot test to real paging/ledger/lease, with host-only checks. */
#define KDEBUG_SELFTEST_HOST
#include "../../kdebug/selftest_lease.c"
#ifndef LEASE_SELFTEST_AS
#define LEASE_SELFTEST_AS ((const struct addrspace *)0)
#endif

static u32 host_lease_image(u32 root)
{
    u32 *pd = P2V(root), hash = 0;
    for (u32 i = 0; i < PDE_COUNT; i++) {
        hash = hash * 33u + (pd[i] & ~(PTE_ACCESSED | PTE_DIRTY));
        if (!(pd[i] & PTE_PRESENT)) continue;
        u32 *pt = P2V(pd[i] & PTE_FRAME_MASK);
        for (u32 j = 0; j < PTE_COUNT; j++)
            hash = hash * 33u + (pt[j] & ~(PTE_ACCESSED | PTE_DIRTY));
    }
    return hash;
}

static int lease_selftest(void)
{
    const struct addrspace *as = LEASE_SELFTEST_AS;
    struct addrspace saved_as = {0};
    struct ledger_owner owners[LEDGER_MAX_OWNERS];
    u32 cr3 = paging_current_cr3(), free = pgalloc_free_pages();
    u32 live = paging_live_addrspaces();
    u32 master_image = host_lease_image(paging_kernel_pd_phys());
    u32 as_image = 0;
    if (as) { saved_as = *as; as_image = host_lease_image(as->pd_phys); }
    for (u32 i = 0; i < LEDGER_MAX_OWNERS; i++) owners[i] = ledger_owners[i];
    lease_selftest_result = ~0u;
    int rc = kst_lease();
    CHECK(lease_selftest_result == (u32)rc);
    CHECK(paging_current_cr3() == cr3 && pgalloc_free_pages() == free);
    CHECK(paging_live_addrspaces() == live);
    CHECK(host_lease_image(paging_kernel_pd_phys()) == master_image);
    for (u32 i = 0; i < sizeof(owners); i++)
        CHECK(((const u8 *)owners)[i] == ((const u8 *)ledger_owners)[i]);
    if (as) {
        for (u32 i = 0; i < sizeof(saved_as); i++)
            CHECK(((const u8 *)&saved_as)[i] == ((const u8 *)as)[i]);
        CHECK(host_lease_image(as->pd_phys) == as_image);
    }
    return rc;
}
