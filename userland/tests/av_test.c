/* v90 acceptance probe. Run focus in a GUI terminal, v86 from the CUI. */
#include "os32api.h"
#include <string.h>

static KernelAPI *av;

static int report(const char *name, int rc)
{
    av->kprintf(7, "%s=%d\n", name, rc);
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
    while (count--) {
        int fg = av->av_focus_get();
        if (fg < 0) {
            report("av_focus_get", fg);
            return 1;
        }
        av->kprintf(7, "focus=%d self=%u\n", fg, owner);
        if (count && hold(((u32)ms + 9) / 10)) return 1;
    }
    return 0;
}

static void volume(int level)
{
    av->ssg_volume(0, level);
    av->kprintf(7, "ssg_volume=void\n");
}

static int v86_roundtrip(int seconds, u32 ticks)
{
    int kind, code;
    av->kprintf(7, "V86 IN, hold %d\n", seconds);
    if (hold(ticks)) return 1;
    /* The existing CUI-only child makes one v86_selftest call. */
    int rc = report("exec_run", av->exec_run("/bin/v86.bin -t"));
    if (report("exec_last_result", av->exec_last_result(&kind, &code)) != 0) return 1;
    av->kprintf(7, "v86 -t kind=%d code=%d\n", kind, code);
    if (rc != 0 || kind != EXEC_KIND_EXITED || code != 0) return 1;
    av->kprintf(7, "V86 OUT, hold %d\n", seconds);
    if (hold(ticks)) return 1;
    volume(12);
    av->kprintf(7, "SSG REARM ch=0, hold %d\n", seconds);
    return hold(ticks) != 0;
}

static int sound(int fm, int ssg, int seconds)
{
    u32 ticks = seconds * 100u;
    int failed;
    if (fm) {
        av->fm_set_tone_num(0, 2);  /* Organ sustains while keyed on. */
        av->kprintf(7, "fm_set_tone_num=void\n");
        av->fm_note_on(0, 57);
        av->kprintf(7, "fm_note_on=void\n");
    }
    if (ssg) {
        av->ssg_tone(0, 288);
        av->kprintf(7, "ssg_tone=void\n");
        volume(12);
    }
    failed = fm && ssg ? v86_roundtrip(seconds, ticks) : hold(ticks) != 0;
    if (fm) {
        av->fm_note_off(0);
        av->kprintf(7, "fm_note_off=void\n");
    }
    if (ssg) volume(0);
    return failed;
}

int main(int argc, char **argv, KernelAPI *api)
{
    av = api;
    if (argc == 4 && !strcmp(argv[1], "focus")) {
        int count = positive(argv[2], 0x7fffffff);
        int ms = positive(argv[3], 0x7fffffff);
        if (count && ms) return focus(count, ms);
    }
    if (argc == 3) {
        int seconds = positive(argv[2], 0x7fffffff / 100);
        int v86 = !strcmp(argv[1], "v86");
        int fm = v86 || !strcmp(argv[1], "fm");
        int ssg = v86 || !strcmp(argv[1], "ssg");
        if (seconds && (fm || ssg)) return sound(fm, ssg, seconds);
    }
    av->kprintf(7, "usage: av_test fm|ssg|v86 <seconds> or av_test focus <count> <ms>\n");
    return 1;
}
