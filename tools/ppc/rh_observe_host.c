/* ILP32 fake firmware bus; real OF wrapper and real display implementation. */
#include "ofw.h"
#include "rh_observe.h"
#include "kstring.h"
static u8 guarded[PM_RH_FB_LIMIT + 32];
static unsigned scenario, synced, writes;
static void finish(unsigned rc) __attribute__((noreturn));
static void finish(unsigned rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) finish(1); } while (0)
void ppc_disable(void) { finish(97); }
void ppc_sync(void) { synced++; }
static void put(void *ptr, u32 v)
{
    u8 *p = ptr;
    for (unsigned i=0; i<4; i++) p[i] = v >> (24-i*8);
}
static int firmware(void *ptr)
{
    cell *a = ptr, *v = a+3;
    const char *name = ppc_cell_ptr(a[0]);
    if (!kstrcmp(name, "finddevice")) { v[1] = 7; return 0; }
    if (!kstrcmp(name, "instance-to-package")) { v[1] = 0xff000008; return 0; }
    if (!kstrcmp(name, "write")) { writes++; v[3] = v[2]; return 0; }
    if (!kstrcmp(name, "getprop")) {
        const char *p = ppc_cell_ptr(v[1]);
        void *out = ppc_cell_ptr(v[2]);
        v[4] = 4;
        if (!kstrcmp(p,"device_type")) {
            kmemcpy(out, scenario == 1 ? "serial\0" : "display", 7); v[4]=7;
        } else if (!kstrcmp(p,"address")) put(out, scenario == 8 ? 0xfffffff0U : 0x90000004U);
        else if (!kstrcmp(p,"width")) put(out, scenario == 3 ? 0xffffffffU : 640);
        else if (!kstrcmp(p,"height")) put(out, scenario == 4 ? 1 : 480);
        else if (!kstrcmp(p,"depth")) put(out, scenario == 2 ? 24 : scenario == 10 ? 16 : scenario == 11 ? 8 : 32);
        else if (!kstrcmp(p,"linebytes")) put(out, scenario == 5 ? 1 : scenario == 6 ? 0xffffffffU : scenario == 11 ? 768 : 2560);
        else if (!kstrcmp(p,"mmu")) put(out, 9);
        else if (!kstrcmp(p,"stdout")) put(out, 8);
        else finish(98);
        return 0;
    }
    if (!kstrcmp(name,"call-method")) {
        CHECK(!kstrcmp(ppc_cell_ptr(v[0]),"translate") && v[1]==9 && a[1]==3 && a[2]==4);
        u32 va = v[2];
        v[3] = 0; /* catch */
        v[4] = scenario == 7 ? 0 : ~0U; /* true */
        v[5] = 0x2a; /* mode */
        v[6] = 0xa0000000U + (va-0x90000000U);
        if (scenario == 9 && va == 0x90001000U) v[6] += 4096;
        return 0;
    }
    finish(99);
}
void _start(void)
{
    ofw_init(firmware);
    CHECK(pm_rh_of_mark(0)==0 && writes==1);
    for (scenario=0; scenario<=11; scenario++) {
        int rc = pm_rh_capture(8);
        if (scenario==1) { CHECK(rc==0); continue; }
        if (scenario>=2 && scenario<=9) { CHECK(rc==-1); continue; }
        CHECK(rc==1);
        const struct pm_rh_fb *fb = pm_rh_framebuffer();
        CHECK(fb->pa==0xa0000004U && fb->bytes==fb->stride*64 && fb->height==480);
        for (unsigned i=0; i<sizeof(guarded); i++) guarded[i]=0xa5;
        unsigned count = ofw_call_count();
        pm_rh_bind((u32)(guarded+16));
        pm_rh_stage(1);
        CHECK(pm_rh_last_stage()==1 && pm_rh_reached()==2 && synced);
        unsigned scale=4, bpp=fb->depth/8;
        /* Independent digit 1 bit: upper row is 00100, slot starts at 40. */
        unsigned on=16+(40+2*scale)*bpp, off=16+40*bpp;
        CHECK(guarded[on+(bpp==4?1:0)]==0xff && guarded[off]==0);
        pm_rh_stage(3);
        CHECK(pm_rh_reached()==10 && guarded[on+(bpp==4?1:0)]==0xff);
        pm_rh_stage(PM_RH_ESCC_INIT);
        CHECK(pm_rh_last_stage()==15);
        pm_rh_phy(1,2,0x1234,0xabcd);
        for (unsigned i=0;i<16;i++) CHECK(guarded[i]==0xa5);
        for (unsigned i=16+fb->bytes;i<sizeof(guarded);i++) CHECK(guarded[i]==0xa5);
        CHECK(ofw_call_count()==count);
    }
    unsigned count = ofw_detach();
    pm_rh_stage(2); pm_rh_phy(3,4,5,6);
    CHECK(ofw_call_count()==count);
    pm_rh_bind(0); pm_rh_stage(5);
    finish(0);
}
