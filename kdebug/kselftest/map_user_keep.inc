/* ------------------------------------------------------------------------ */
/* Shared PT promotion is rejected without changing PFN/cache/PDE bits.
 * rc bits: 1=missing PT, 2=AS creation, 4=keep, 64=single page,
 * 128=identity range, 256=physical range, 8=free, 16=entry changed,
 * 32=AS cleanup (paging_map_user_keep_selftest). */
static void test_map_user_keep(void)
{
    int rc = paging_map_user_keep_selftest();
    check(rc == 0, "shared PT map rejected (PTE/PDE unchanged)");
    if (rc != 0) {
        kprintf(0xC1, "[selftest]   paging_map_user_keep_selftest rc=%d\n", rc);
    }
}
