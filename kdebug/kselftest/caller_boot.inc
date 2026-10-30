/* T2d: bounded boot smoke test. Reuse ledger's AS/data allocation, add only
 * one sparse APP PT. No CR3 switch, callbacks, or persistent test storage. */
static void test_caller_boot(struct addrspace *as, u32 phys)
{
    CallerAccessFrame previous;
    struct caller_access c = {0};
    char src[2] = {'d', 0}, dst[2] = {0};
    u32 pa = 0, root = paging_current_cr3();
    unsigned int flags, entry_flags = irq_save();
    irq_restore(entry_flags);
    int ok = caller_access_enter(&previous, CALLER_TRUSTED);
    check(ok, "caller:boot descriptor enter");
    if (ok) {
        check(caller_access_get(&c) && c.origin == CALLER_TRUSTED,
              "caller:boot trusted descriptor");
        check(copy_caller_cstr(&c, src, dst, sizeof(dst)) && dst[0] == 'd' && !dst[1],
              "caller:boot bounded cstr");
        dst[0] = 'x';
        check(!copy_caller_cstr(&c, src, dst, 1), "caller:boot missing NUL");
        dst[0] = 'x'; dst[1] = 'y';
        check(copy_to_caller(&c, dst, src, sizeof(src)) && dst[0] == 'd' && !dst[1],
              "caller:boot copyout");
        check(!copy_to_caller(&c, 0, src, 1), "caller:boot NULL output");
        caller_access_leave(&previous);
    }
    ok = paging_addrspace_map_user(as, MEM_EXEC_LOAD_ADDR, phys, PAGE_RO | PTE_USER) == 0;
    check(ok, "caller:boot RO map");
    if (ok) {
        flags = irq_save();
        ok = as_access_page(as, MEM_EXEC_LOAD_ADDR, 0, &pa) && pa == phys;
        check(ok, "caller:boot RO read walk");
        pa = phys;
        check(!as_access_page(as, MEM_EXEC_LOAD_ADDR, 1, &pa) && pa == phys,
              "caller:boot RO write unchanged");
        irq_restore(flags);
    }
    flags = irq_save();
    irq_restore(flags);
    check(((flags ^ entry_flags) & EFLAGS_IF) == 0 && paging_current_cr3() == root,
          "caller:boot IF/CR3 unchanged");
}
