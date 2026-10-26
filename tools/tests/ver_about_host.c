/* V11: real formatter -> real cmd_ver and Rust About, using the same snapshot.
 * Source slices are supplied by test_about_info.py; ILP32, no GUI or devices. */
#include "support.h"
#include "ver_info.h"
#include "../lib/kstring.h"
#include <stdarg.h>
u32 input_irq(void) { return 1; }
#include "ver_format_source.c"
#include "ver_state_source.c"
#define SH_STATUS_ERROR 1
static OS32_VerInfo report;
static u32 calls;
static void print(u8 attr, const char *fmt, ...) {
    (void)attr;
    CHECK(equal(fmt, "%s"));
    va_list ap;
    va_start(ap, fmt);
    say(va_arg(ap, const char *));
    va_end(ap);
}
static int get_report(void *out, u32 size) {
    CHECK(size == sizeof(report));
    calls++;
    *(OS32_VerInfo *)out = report;
    return 0;
}
static struct {
    void (*kprintf)(u8, const char *, ...);
    int (*sys_ver_info)(void *, u32);
} api = {print, get_report}, *g_api = &api;
#include "ver_shell_source.c"
static void number(u32 n) {
    char buf[11]; u32 i = sizeof(buf)-1;
    buf[i] = 0;
    do { buf[--i] = '0' + n % 10; n /= 10; } while (n);
    say(buf+i);
}
void _start(void) {
    CHECK(sizeof(OS32_VerInfo) == 1364 && sizeof(OS32_VerLine) == 68);
    for (u32 c = 0; c < 26; c++) {
        VerSnapshot s;
        kmemset(&s, 0, sizeof(s));
        s.cpu_measured = s.pit_valid = s.ser_ready = s.ser_fifo = s.pcm_present = 1;
        s.pit_hz = 100; s.pit_input = 1996800; s.ser_baud = 9600;
        s.gfx.width = 640; s.gfx.height = 480; s.gfx.bpp = 8;
        s.gfx_kind = 2; s.gfx_active = 1; s.api_version = KAPI_VERSION;
        kstrncpy(s.build, "Oct 26 2026 12:34:56", sizeof(s.build));
        s.boot_ok = s.debug_ok = 1;
        s.boot.crc_valid = 1; s.boot.source = 2;
        s.boot.image_crc = 0x0123abcd; s.boot.image_size = 123456;
        kstrncpy(s.boot.commit, "0acd73e4-dirty", sizeof(s.boot.commit));
        s.debug.kdebug_state = c < 17 ? c : 1;
        s.debug.kdebug_pair_crc = 0x12ab;
        s.services.stack_used = 1234; s.services.stack_size = 32768;
        s.services.public = 1;
        kstrncpy(s.services.state, "SERVICES_READY", sizeof(s.services.state));
        if (c == 17) s.boot_ok = 0;
        if (c == 18) s.boot.crc_valid = 0;
        if (c == 19) s.boot.source = 1;
        if (c == 20) s.boot.source = 0;
        if (c == 21) s.debug_ok = 0;
        if (c == 22) {
            s.pit_valid = s.ser_ready = s.pcm_present = 0;
            s.gfx_kind = 0; s.gfx.width = 0;
        }
        if (c == 23) {
            s.pit_hz = s.pit_input = s.ser_baud = s.api_version = ~0u;
            s.boot.image_crc = s.boot.image_size = ~0u;
            s.debug.kdebug_pages = s.debug.kdebug_owner_pages = ~0u;
            s.services.stack_used = s.services.stack_size = s.services.public = ~0u;
            kmemset(s.build, 'B', sizeof(s.build)-1);
            kmemset(s.boot.commit, 'C', sizeof(s.boot.commit)-1);
        }
        if (c == 24) { s.gfx_kind = 1; s.gfx.height = 400; s.gfx.bpp = 4; s.gfx_active = 0; }
        if (c == 25) { s.gfx_kind = 3; s.gfx_active = 0; }
        ver_format(&s, &report);
        say("REPORT\n");
        for (u32 i = 0; i < report.count; i++) {
            number(report.lines[i].kind); say("\t"); say(report.lines[i].text); say("\n");
        }
        say("CUI\n");
        calls = 0;
        CHECK(cmd_ver(0, 0) == 0 && calls == 1);
        say("END\n");
    }
    die(0);
}
