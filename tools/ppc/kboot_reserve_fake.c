/* Appended to B1's unchanged firmware fake by kboot_reserve.py. */
static void reserve_test(void)
{
    unsigned which=scenario;
    scenario=0; ac=sc=1; mac=0; expected_stop=0;
    tuple(reg,1,1,0,0x8000000); rn=8;
    /* Synthetic 128MiB mac99-shaped RAM, not an observed firmware snapshot.
     * Loader owns [1MiB,3MiB); firmware reserves low vectors/high storage. */
    tuple(avail,1,1,0x3000,0xfd000);
    tuple(avail+8,1,1,0x300000,0x7b00000); an=16;
    int expected=PM_KBOOT_RESERVE_OK;
    if (which==1) { /* one-byte hole inside the validation window */
        tuple(avail+8,1,1,0x300000,0x400000);
        tuple(avail+16,1,1,0x700001,0x76fffff); an=24;
        expected=PM_KBOOT_RESERVE_WINDOW;
    }
    if (which==2) {
        tuple(avail+8,1,1,0x100000,0x7d00000);
        expected=PM_KBOOT_RESERVE_IMAGE_AVAILABLE;
    }
    if (which==3) {
        tuple(avail+8,1,1,0x300000,0xd00000);
        tuple(avail+16,1,1,0x1000001,0x1ffff); an=24;
        expected=PM_KBOOT_RESERVE_HPT;
    }
    if (which==4) {
        tuple(reg,1,1,0,0xffffff);
        expected=PM_KBOOT_RESERVE_WINDOW;
    }
    if (which==5) { /* alignment must round up, not down */
        tuple(avail+8,1,1,0x300000,0xd00000);
        tuple(avail+16,1,1,0x1000001,0x3ffff); an=24;
    }
    if (which==6) {
        tuple(avail+8,1,1,0x300000,0xbfffff);
        tuple(avail+16,1,1,0x1000000,0x100000); an=24;
        expected=PM_KBOOT_RESERVE_WINDOW;
    }
    if (which==7) {
        tuple(reg,1,1,0x3000,0x7ffd000);
        expected=PM_KBOOT_RESERVE_VECTOR;
    }
    if (which==8) {
        ac=2; tuple(reg,2,1,0,0x8000000); rn=12; put(reg,1);
        expected_stop=1; /* B1 rejects the high BE address cell. */
    }
    if (which==9) {
        tuple(reg,1,1,0,0x180000); expected=PM_KBOOT_RESERVE_IMAGE;
    }
    if (which==10) {
        tuple(avail+8,1,1,0x300000,0xd00000);
        tuple(avail+16,1,1,0x2000000,0x20000); an=24;
        expected=PM_KBOOT_RESERVE_HPT;
    }
    const struct pm_kboot_info *captured=pm_kboot_capture(0);
    struct pm_kboot_info copy=*captured;
    if (which==11) { /* late firmware change must invalidate old snapshot */
        tuple(avail+8,1,1,0x300001,0x7afffff);
        expected=PM_KBOOT_RESERVE_WINDOW;
    }
    if (which==12) {
        copy.ram.v[0].start=0xfffff000; copy.ram.v[0].size=0x2000;
        expected=PM_KBOOT_RESERVE_RANGES;
    }
    int result=pm_kboot_reserve(&copy);
    if (result!=expected) quit(77);
    if (result==PM_KBOOT_RESERVE_OK) {
        if (copy.admitted_end!=0x1000000 || copy.image_first!=0x100000 ||
            copy.image_end!=0x300000 || copy.hpt_bytes!=0x20000 ||
            copy.hpt_pa!=(which==5?0x1020000:0x1000000)) quit(77);
    } else if (copy.admitted_end || copy.image_first || copy.image_end ||
               copy.hpt_pa || copy.hpt_bytes) quit(77);
    quit(0);
}
