"""V10: execute the real cmd_ver with a value-only fake API (ILP32)."""
import subprocess
from ver_test_utils import ROOT, arguments, execute, function


FIXTURE = r'''
#include "support.h"
#include "os32_ver_info.h"
#include "os32_kapi_shared.h"
#include <stdarg.h>
#define SH_STATUS_ERROR 1
static OS32_VerInfo report;
static u32 calls, used;
static int api_rc;
static char output[OS32_VER_LINES * (OS32_VER_TEXT + 3)];
static u8 colors[sizeof(output)];
static void print(u8 attr, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    CHECK(equal(fmt, "%s"));
    const char *s = va_arg(ap, const char *);
    while (*s) {
        CHECK(used + 1 < sizeof(output));
        colors[used] = attr;
        output[used++] = *s++;
    }
    output[used] = 0;
    va_end(ap);
}
static int get_report(void *out, u32 size) {
    calls++;
    CHECK(size == sizeof(report));
    if (api_rc) return api_rc;
    *(OS32_VerInfo *)out = report;
    return 0;
}
/* No legacy getters: any accidental use fails compilation. */
static struct {
    void (*kprintf)(u8, const char *, ...);
    int (*sys_ver_info)(void *, u32);
} api = {print, get_report}, *g_api = &api;
#include "ver_shell_source.c"
static void line(u32 i, u32 kind, const char *s) {
    report.lines[i].kind = kind;
    u32 n = 0;
    do { CHECK(n < OS32_VER_TEXT); report.lines[i].text[n] = s[n]; } while (s[n++]);
}
static void reset(void) { calls = used = 0; output[0] = 0; }
static void check_line(u32 *pos, const char *s, u8 color) {
    while (*s) {
        CHECK(output[*pos] == *s++);
        CHECK(colors[(*pos)++] == color);
    }
}
void _start(void) {
    line(0, OS32_VER_HEAD, "PC-9821 OS32 v3.0-dev (Ring3 Native)");
    line(1, OS32_VER_DEVICE | OS32_VER_INDENT, "CPU: x86 32-bit PM+Paging; calib=measured");
    line(2, OS32_VER_IDENTITY | OS32_VER_INDENT, "Build: kernel time");
    line(3, OS32_VER_IDENTITY | OS32_VER_INDENT, "Commit: 7daac862-dirty");
    line(4, OS32_VER_IDENTITY | OS32_VER_INDENT, "Image CRC: 1234abcd (12345 bytes, HDD loader)");
    line(5, OS32_VER_DEBUG, "kdebug: on (pair 1234abcd)");
    line(6, OS32_VER_DEBUG, "kdebug pages: body=2 owner=3");
    line(7, OS32_VER_DEBUG, "sqlite stack high-water=0 of 65536");
    line(8, OS32_VER_DEBUG | OS32_VER_INDENT, "services: SERVICES_READY public=1");
    line(9, OS32_VER_DEVICE, "literal %s %08x");
    /* INDENT is independent of the low-byte color group. */
    line(10, OS32_VER_HEAD | OS32_VER_INDENT, "indented head");
    report.count = 11;
    CHECK(!cmd_ver(0, 0) && calls == 1);
    u32 pos = 0;
    check_line(&pos, "PC-9821 OS32 v3.0-dev (Ring3 Native)\n", ATTR_GREEN);
    check_line(&pos, "  CPU: x86 32-bit PM+Paging; calib=measured\n", ATTR_CYAN);
    check_line(&pos, "  Build: kernel time\n  Commit: 7daac862-dirty\n", ATTR_WHITE);
    check_line(&pos, "  Image CRC: 1234abcd (12345 bytes, HDD loader)\nkdebug: on (pair 1234abcd)\n", ATTR_WHITE);
    check_line(&pos, "kdebug pages: body=2 owner=3\nsqlite stack high-water=0 of 65536\n  services: SERVICES_READY public=1\n", ATTR_WHITE);
    check_line(&pos, "literal %s %08x\n", ATTR_CYAN);
    check_line(&pos, "  indented head\n", ATTR_GREEN);
    CHECK(pos == used);
    /* count alone selects rows; maximum public count and full text payload. */
    reset(); report.count = OS32_VER_LINES;
    for (u32 i = 0; i < OS32_VER_LINES; i++) {
        report.lines[i].kind = OS32_VER_DEBUG;
        for (u32 j = 0; j < OS32_VER_TEXT - 1; j++) report.lines[i].text[j] = 'x';
        report.lines[i].text[OS32_VER_TEXT - 1] = 0;
    }
    CHECK(!cmd_ver(0, 0) && calls == 1);
    CHECK(used == OS32_VER_LINES * OS32_VER_TEXT);
    for (u32 i = 0; i < used; i++) CHECK(output[i] == (i % OS32_VER_TEXT == OS32_VER_TEXT - 1 ? '\n' : 'x'));
    reset(); report.count = 0;
    CHECK(!cmd_ver(0, 0) && calls == 1 && !used);
    /* Failed acquisition returns to the command loop, without rendering rows. */
    reset(); api_rc = -1;
    CHECK(cmd_ver(0, 0) == SH_STATUS_ERROR && calls == 1);
    CHECK(equal(output, "ver: sys_ver_info failed\n"));
    for (u32 i = 0; i < used; i++) CHECK(colors[i] == ATTR_RED);
    say("PASS ver shell: single getter, count, literal text, colors, CRC/kdebug/services, INDENT, error\n");
    die(0);
}
'''


def run(source, runner):
    try:
        return execute(FIXTURE, runner, {'ver_shell_source.c': source})
    except subprocess.CalledProcessError as error:
        print(error.stderr)
        raise


def main():
    args = arguments()
    source = function((ROOT/'userland/shell/cmd_base.c').read_text(), 'static int cmd_ver(')
    assert run(source, args.runner).returncode == 0
    if args.mutate:
        old = 'if (line->kind & OS32_VER_INDENT)'
        assert source.count(old) == 1
        result = run(source.replace(old, 'if (0)'), args.runner)
        assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED: INDENT ignored')


if __name__ == '__main__':
    main()
