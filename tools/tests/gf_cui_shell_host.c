/* Real CUI parser; only the KAPI transport is replaced. */
#define SHELL_H
#include "../../include/types.h"
#include "../../sdk/include/os32/os32_kapi_shared.h"
#include "gf_command_runtime_host.h"
static int equal(const char *a, const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
#define str_eq(a,b) equal(a,b)
#include "../../kdebug/gf_command_wire.h"
static u32 calls;
static struct gf2_request captured;
static i32 transport_rc, reply_rc;
static void print(u8 attr, const char *format, ...) { (void)attr; (void)format; }
static i32 call(u32 op, void *buf, u32 size)
{
    assert(op == GF_OP_COMMAND && size == sizeof(captured));
    captured = *(struct gf2_request *)buf;
    assert(captured.size == sizeof(captured) && !captured.rc);
    ++calls;
    ((struct gf2_request *)buf)->rc = reply_rc;
    return transport_rc;
}
static struct {
    u32 version;
    void (*kprintf)(u8, const char *, ...);
    i32 (*kdebug_call)(u32, void *, u32);
} api = {78, print, call}, *g_api = &api;
#include "../../userland/shell/cmd_gf2.c"
void _start(void)
{
    char *enter[] = {"gf2", "enter"}, *leave[] = {"gf2", "leave"};
    assert(!shell_cmd_gf2(2, enter) && captured.test == GF4_DISPLAY && captured.session == 9);
    assert(!shell_cmd_gf2(2, leave) && captured.test == GF4_DISPLAY && captured.session == 10);
    const char *names[] = {"info", "arg-lo", "arg-hi", "clock-read", "clock-calc", "clock-prog",
        "ram-prog", "perf-count", "perf-get", "perf-select", "therm", "fan-mode", "fan-get",
        "fan-cstate", "volt-get", "volt-set", "volt-id", "gpio-find", "gpio-get", "gpio-set",
        "tile-find", "tile-set", "tile-clear", "save", "restore", "free", "poll", "wait", "last-rc"};
    for (u32 i=0; i<sizeof(names)/sizeof(*names); ++i) {
        char *args[] = {"gf2", "feature", (char *)names[i], "7", "65535"};
        assert(!shell_cmd_gf2(i==1 || i==2 ? 5:3, args));
        assert(captured.test == 11 && captured.session == (i==1 || i==2 ? 0xffff0700U | i : i));
    }
    const char *bad[] = {"", "-1", "+1", "65536", "4294967296", "12x", " 1"};
    for (u32 i=0; i<sizeof(bad)/sizeof(*bad); ++i) {
        char *args[] = {"gf2", "feature", "arg-lo", "0", (char *)bad[i]};
        u32 before=calls;
        assert(shell_cmd_gf2(5,args)==2 && calls==before);
    }
    char *index[] = {"gf2", "feature", "arg-hi", "8", "0"};
    assert(shell_cmd_gf2(5,index)==2);
    char *unknown[] = {"gf2", "feature", "unknown"};
    assert(shell_cmd_gf2(3,unknown)==2);
    char *display[] = {"gf2", "display", "262143"};
    assert(!shell_cmd_gf2(3,display) && captured.test==10 && captured.session==262143);
    display[2]="262144";assert(shell_cmd_gf2(3,display)==2);
    transport_rc=-123;assert(shell_cmd_gf2(2,enter)==1);
    transport_rc=0;reply_rc=-509;assert(shell_cmd_gf2(2,enter)==1);
    reply_rc=0;api.version=77;u32 before=calls;
    assert(shell_cmd_gf2(2,enter)==1 && calls==before);
    api.version=78;api.kdebug_call=0;
    assert(shell_cmd_gf2(2,enter)==1 && calls==before);
    __asm__ volatile("int $0x80"::"a"(1),"b"(0));
    __builtin_unreachable();
}
