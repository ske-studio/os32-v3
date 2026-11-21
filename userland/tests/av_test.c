/* v90 acceptance probe. From the CUI run play, v86 -t, then rearm. */
#include "os32api.h"
#include "os32_gui_shared.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static KernelAPI *av;

static void line(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    av->kprintf(7, "%s", buf);
    av->serial_puts(buf);
}

static int report(const char *name, int rc)
{
    line("%s=%d\n", name, rc);
    return rc;
}

static int positive(const char *s, int limit)
{
    int n = 0;
    while (*s) {
        int digit = *s++ - '0';
        if (digit < 0 || digit > 9 || n > (limit - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    return n;
}

static int hold(u32 ticks)
{
    u32 start = av->get_tick();
    while (av->get_tick() - start < ticks) {
        int rc = av->sys_yield();
        if (rc < 0) return report("sys_yield", rc);
    }
    return 0;
}

static int focus(int count, int ms)
{
    u32 app, owner, generation;
    if (report("caller_identity", av->caller_identity(&app, &owner, &generation)) < 0)
        return 1;
    int slot = av->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    if (slot != OS32_ERR_NOSYS) {
        if (report("gui_call(INIT)", slot) < 0) return 1;
        GuiWinSpec spec = { .rect = {40, 60, 200, 80}, .flags = GUI_WF_DEFAULT,
                            .title = "av_test focus" };
        volatile u8 *req = (volatile u8 *)(av->shm_base + GUI_SHM_OFFSET +
            (u32)slot * GUI_SLOT_SIZE + GUI_SLOT_REQ_OFF);
        for (u32 i = 0; i < sizeof(spec); i++) req[i] = ((u8 *)&spec)[i];
        if (report("gui_call(WIN_CREATE)", av->gui_call(GUI_OP_WIN_CREATE, 0)) < 0)
            return 1;
        /* The WM reclaims this window on app exit. */
    }
    while (count--) {
        int fg = av->av_focus_get();
        if (fg < 0) {
            report("av_focus_get", fg);
            return 1;
        }
        line("focus=%d self=%u\n", fg, app);
        if (count && hold(((u32)ms + 9) / 10)) return 1;
    }
    return 0;
}

static void volume(int level)
{
    av->ssg_volume(0, level);
    line("ssg_volume=void\n");
}

static void start_sound(int fm, int ssg)
{
    if (fm) {
        av->fm_set_tone_num(0, 2);  /* Organ sustains while keyed on. */
        line("fm_set_tone_num=void\n");
        av->fm_note_on(0, 57);
        line("fm_note_on=void\n");
    }
    if (ssg) {
        av->ssg_tone(0, 288);
        line("ssg_tone=void\n");
        volume(12);
    }
}

static void stop_sound(int fm, int ssg)
{
    if (fm) {
        av->fm_note_off(0);
        line("fm_note_off=void\n");
    }
    if (ssg) volume(0);
}

int main(int argc, char **argv, KernelAPI *api)
{
    av = api;
    if (argc == 4 && !strcmp(argv[1], "focus")) {
        int count = positive(argv[2], 0x7fffffff);
        int ms = positive(argv[3], 0x7fffffff);
        if (count && ms) return focus(count, ms);
    }
    if (argc == 2 && !strcmp(argv[1], "play")) {
        start_sound(1, 1);
        line("PLAY FM ch=0 SSG ch=0 volume=12\n");
        return 0;
    }
    if (argc == 3) {
        int seconds = positive(argv[2], 0x7fffffff / 100);
        int rearm = !strcmp(argv[1], "rearm");
        int fm = !strcmp(argv[1], "fm");
        int ssg = !strcmp(argv[1], "ssg");
        if (seconds && (fm || ssg || rearm)) {
            if (rearm) {
                volume(12);
                line("SSG REARM ch=0, hold %d\n", seconds);
            } else {
                start_sound(fm, ssg);
                line("%s ch=0, hold %d\n", fm ? "FM" : "SSG", seconds);
            }
            int failed = hold(seconds * 100u) != 0;
            stop_sound(fm || rearm, ssg || rearm);
            return failed;
        }
    }
    line("usage: av_test play | fm|ssg|rearm <seconds> | focus <count> <ms>\n");
    return 1;
}
