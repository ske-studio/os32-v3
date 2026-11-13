/* ILP32 fake firmware bus; real OF wrapper and real display implementation. */
#include "ofw.h"
#include "rh_observe.h"
#include "kstring.h"
static u8 guarded[PM_RH_FB_LIMIT + 32];
static unsigned scenario, synced, writes, translations, color_case, color_calls, color_writes;
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
        } else if (!kstrcmp(p,"address")) put(out, scenario == 18 ? 0xfffe0000U : scenario == 8 ? 0xfffffff0U : scenario >= 12 && scenario != 15 ? 0x90000000U : 0x90000004U);
        else if (!kstrcmp(p,"width")) put(out, scenario == 3 ? 0xffffffffU : scenario == 13 ? 800 : 640);
        else if (!kstrcmp(p,"height")) put(out, scenario == 4 ? 1 : scenario == 12 ? 480 : scenario == 13 ? 600 : scenario == 16 ? 0xffffffffU : scenario == 17 ? 3000 : 480);
        else if (!kstrcmp(p,"depth")) put(out, scenario == 2 ? 24 : scenario == 10 ? 16 : scenario >= 11 ? 8 : 32);
        else if (!kstrcmp(p,"linebytes")) put(out, scenario == 5 ? 1 : scenario == 6 ? 0xffffffffU : scenario == 13 ? 800 : scenario >= 11 ? 768 : 2560);
        else if (!kstrcmp(p,"mmu")) put(out, 9);
        else if (!kstrcmp(p,"stdout")) put(out, 8);
        else finish(98);
        return 0;
    }
    if (!kstrcmp(name,"call-method")) {
        if (!kstrcmp(ppc_cell_ptr(v[0]), "color@")) {
            CHECK(v[1] == 8 && a[1] == 3 && a[2] == 4);
            u32 index = v[2];
            CHECK(index == 0 || index == 255);
            color_calls++;
            if (color_case == 6) return 0; /* exception left catch untouched */
            if (color_case == 7) { v[3] = 0; return -33; } /* CI failure, not method catch */
            v[3] = color_case >= 3 ? 1 : 0;
            u32 bright = color_case == 2 ? 127 :
                ((index == 0) == (color_case == 0) ? 255 : 0);
            v[4] = v[5] = v[6] = bright;
            return 0;
        }
        if (!kstrcmp(ppc_cell_ptr(v[0]), "color!")) {
            CHECK(v[1] == 8 && a[1] == 6 && a[2] == 1);
            CHECK(color_case >= 3 && color_writes < 2);
            u32 value = color_writes++ ? 255 : 0;
            CHECK(v[2] == value && v[3] == value && v[4] == value && v[5] == value);
            v[6] = color_case == 4 || (color_case == 5 && value == 255) ? 1 : 0;
            return 0;
        }
        CHECK(!kstrcmp(ppc_cell_ptr(v[0]),"translate") && v[1]==9 && a[1]==3 && a[2]==4);
        u32 va = v[2];
        translations++;
        v[3] = 0; /* catch */
        v[4] = scenario == 7 ? 0 : ~0U; /* true */
        v[5] = 0x2a; /* mode */
        v[6] = (scenario == 18 ? 0xa0000000U + (va-0xfffe0000U) : scenario == 19 ? 0xfffe0000U + (va-0x90000000U) : 0xa0000000U + (va-0x90000000U));
        if ((scenario == 9 && va == 0x90001000U) || (scenario == 14 && va == 0x90059000U)) v[6] += 4096;
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
    for (scenario=12; scenario<=19; scenario++) {
        CHECK(pm_rh_capture(8)==1);
        u32 band = pm_rh_framebuffer()->bytes;
        translations=0;
        int rc=pm_rh_capture_full();
        if (scenario==14) CHECK(rc==PM_RH_FULL_TRANSLATE);
        else if ((scenario>=16)) CHECK(rc==PM_RH_FULL_SIZE);
        else {
            CHECK(rc==PM_RH_FULL_OK);
            CHECK(pm_rh_full_framebuffer()->bytes == (scenario==13 ? 480000U : 368640U));
            CHECK(translations == (scenario==13 ? 118U : scenario==15 ? 91U : 90U));
        }
        CHECK(pm_rh_framebuffer()->bytes==band);
        if (rc!=PM_RH_FULL_OK) CHECK(!pm_rh_full_framebuffer()->bytes);
    }
    scenario = 12;
    for (color_case = 0; color_case < 8; color_case++) {
        CHECK(pm_rh_capture(8) == 1);
        CHECK(pm_rh_full_colors()->rc == PM_RH_COLOR_UNCHECKED);
        color_calls = color_writes = 0;
        CHECK(pm_rh_capture_full() == PM_RH_FULL_OK);
        const struct pm_rh_colors *c = pm_rh_full_colors();
        CHECK(color_calls == (color_case >= 3 ? 1 : 2));
        if (color_case < 2) {
            CHECK(c->rc == PM_RH_COLOR_OK);
            CHECK(c->foreground == (color_case == 0 ? 0U : 255U));
            CHECK(c->background == (color_case == 0 ? 255U : 0U));
        } else if (color_case == 3 || color_case >= 6) {
            CHECK(c->rc == PM_RH_COLOR_WRITTEN && c->foreground == 255 && c->background == 0);
        } else CHECK(c->rc == (color_case == 2 ? PM_RH_COLOR_EQUAL : PM_RH_COLOR_FAILED));
        CHECK(color_writes == (color_case < 3 ? 0 : color_case == 4 ? 1 : 2));
        pm_rh_discard_full();
        CHECK(pm_rh_full_colors()->rc == PM_RH_COLOR_UNCHECKED);
    }
    unsigned count = ofw_detach();
    pm_rh_stage(2); pm_rh_phy(3,4,5,6);
    CHECK(ofw_call_count()==count);
    pm_rh_bind(0); pm_rh_stage(5);
    finish(0);
}
