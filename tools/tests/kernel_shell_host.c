/* E02b: run the unmodified shell loop with scripted exec/device boundaries. */
#include <stdarg.h>
#include "exec.h"
#include "config.h"
#include "pc98.h"
#include "shlib.h"

static unsigned int strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}
static int strncmp(const char *a, const char *b, unsigned int n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
static int strcmp(const char *a, const char *b) { return strncmp(a, b, strlen(b) + 1); }
static void strcpy(char *d, const char *s) { while ((*d++ = *s++)) {} }
static void strcat(char *d, const char *s) { strcpy(d + strlen(d), s); }
static const char *strstr(const char *s, const char *needle)
{
    for (; *s; s++) if (!strncmp(s, needle, strlen(needle))) return s;
    return *needle ? 0 : s;
}
static void say(const char *s)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(strlen(s)) : "memory");
}
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
#define REQUIRE(c) do { if (!(c)) { say("FAIL " #c "\n"); finish(1); } } while (0)

/* Expected values come from the pre-extraction behavior, not helper results. */
struct step {
    const char *path;
    int rc, layout, waits, clears, gui, streak;
};
#define C(r, l, w, c) {SYS_SHELL_BIN, r, l, w, c, 0, 0}
#define G(r, l, w, c, s) {SYS_GSHELL_BIN, r, l, w, c, 1, s}
static struct step steps[10];
static const char *requests[10];
static int step_count, step_index, take_count, waits, clears;
static int config_gui, kcg_ready = 1, reject_reason, probed;
static int kcg_calls, reject_calls, prepare_calls, client_calls, cursor_gui;
static int expected_waits, expected_clears, fatal, halted;
static int expected_kcg, expected_reject, expected_gui, diag_count, expected_diag;
static int diagnostic_pending;
static const char *case_name, *expected_text = "", *forbidden_text = "FATAL:";
static char messages[8192];
static void *done[5];
static volatile u32 tick_count;

static void append(const char *s)
{
    REQUIRE(strlen(messages) + strlen(s) + 2 < sizeof(messages));
    strcat(messages, s);
    strcat(messages, "\n");
}

static int sysconfig_get_int(const char *path, const char *key, int def)
{
    REQUIRE(!strcmp(path, SYS_SYSTEM_CFG) && !strcmp(key, "GUI") && def == 0);
    return config_gui;
}

static int gui_take_next_shell(char *out, int cap)
{
    REQUIRE(!diagnostic_pending);
    if (step_index == step_count) __builtin_longjmp(done, 1);
    REQUIRE(take_count < 10);
    const char *request = requests[take_count];
    requests[take_count++] = NULL; /* consume, including a later fresh request */
    if (!request) return 0;
    REQUIRE((int)strlen(request) < cap);
    strcpy(out, request);
    return 1;
}

static int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
static int kcg_module_ready(void) { kcg_calls++; return kcg_ready; }
int shlib_reject_reason(void) { reject_calls++; return reject_reason; }
u32 shlib_reject_min_api(void) { return KAPI_VERSION + 1; }

static void kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    REQUIRE(attr == ATTR_YELLOW);
    va_start(ap, fmt);
    if (strstr(fmt, "needs KAPI")) {
        REQUIRE(!strcmp(va_arg(ap, const char *), SYS_SHLIB_GUI));
        REQUIRE(va_arg(ap, u32) == KAPI_VERSION + 1);
        REQUIRE(va_arg(ap, u32) == KAPI_VERSION);
    }
    va_end(ap);
    append(fmt);
}

static void console_text_gdc_start(void) { cursor_gui = 0; }
static void console_text_gdc_stop(void) { cursor_gui = 1; }
static void tvram_clear(void) { clears++; }
static void tvram_print(int x, int y, const char *s, u8 attr)
{
    (void)x; (void)y; (void)attr;
    append(s);
    if (!strncmp(s, "FATAL:", 6)) fatal = 1;
}
static void tvram_putchar_at(int x, int y, char ch, u8 attr)
{
    REQUIRE(x >= 16 && x < TVRAM_COLS && y == 0 && ch == ' ' && attr == TATTR_GRAY);
}
static int gfx_cirrus_probed(void) { return probed; }
static void gfx_prepare_backend(void) { prepare_calls++; probed = 1; }
static void gfx_client_to_gshell(void) { client_calls++; }

static void boot_diag_exec(const char *path, int rc, int gui, int streak)
{
    if (path) {
        REQUIRE(!diagnostic_pending);
        diagnostic_pending = 1;
        return;
    }
    REQUIRE(diagnostic_pending && step_index > 0);
    const struct step *s = &steps[step_index - 1];
    REQUIRE(rc == s->rc && gui == s->gui && streak == s->streak);
    diagnostic_pending = 0;
    diag_count++;
}

int exec_run(const char *path)
{
    REQUIRE(step_index < step_count);
    const struct step *s = &steps[step_index++];
    REQUIRE(!strcmp(path, s->path));
    REQUIRE(waits == s->waits && clears == s->clears);
    REQUIRE(cursor_gui == s->gui);
    /* Refusal diagnostics must precede the CUI load, not appear afterward. */
    if (config_gui && reject_reason && kcg_ready && step_index == 1)
        REQUIRE(strstr(messages, expected_text));
    return s->rc;
}

int exec_layout_rejected(void)
{
    REQUIRE(step_index > 0);
    return steps[step_index - 1].layout;
}

static void _halt(void)
{
    if (fatal) { halted = 1; __builtin_longjmp(done, 1); }
    REQUIRE(++waits <= 100); /* A lost fail-stop/wait cannot hang the fixture. */
    tick_count++;
}

#include "kernel_shell_source.inc"

static void plan(const struct step *s, int count, int end_waits, int end_clears)
{
    for (int i = 0; i < count; i++) steps[i] = s[i];
    step_count = expected_diag = count;
    expected_waits = end_waits;
    expected_clears = end_clears;
}
#define PLAN(w, c, ...) do { const struct step s[] = {__VA_ARGS__}; \
    plan(s, sizeof(s) / sizeof(s[0]), w, c); } while (0)

int main(int argc, char **argv)
{
    REQUIRE(argc == 2);
    case_name = argv[1];
    if (!strcmp(case_name, "cui") || !strcmp(case_name, "tick-wrap") ||
        !strcmp(case_name, "cui-ignores-shlib")) {
        PLAN(10, 1, C(0, 0, 0, 0));
        if (!strcmp(case_name, "tick-wrap")) tick_count = 0xfffffffbUL;
        if (!strcmp(case_name, "cui-ignores-shlib")) reject_reason = SHLIB_REJECT_LAYOUT;
    } else if (!strcmp(case_name, "gui") || !strcmp(case_name, "gui-probed")) {
        config_gui = expected_kcg = expected_gui = 1; expected_reject = 2;
        probed = !strcmp(case_name, "gui-probed");
        PLAN(10, 1, G(0, 0, 0, 0, 0));
    } else if (!strcmp(case_name, "kcg")) {
        config_gui = expected_kcg = 1; kcg_ready = 0;
        PLAN(20, 2, {SYS_SHELL_BIN " --kcg-unavailable", 0, 0, 0, 0, 0, 0},
             C(0, 0, 10, 1));
    } else if (!strcmp(case_name, "shlib-layout") || !strcmp(case_name, "shlib-api")) {
        config_gui = expected_kcg = 1;
        if (!strcmp(case_name, "shlib-layout")) {
            reject_reason = SHLIB_REJECT_LAYOUT; expected_reject = 1;
            expected_text = "GUI shlib: rebuild required (KAPI data layout) -> CUI shell";
            forbidden_text = "update the kernel first";
        } else {
            reject_reason = SHLIB_REJECT_MIN_API; expected_reject = 2;
            expected_text = "update the kernel first (hsync boot / deploy-kernel)";
            forbidden_text = "rebuild /sys";
        }
        PLAN(10, 2, C(0, 0, 0, 1));
    } else if (!strcmp(case_name, "load-fail") || !strcmp(case_name, "load-layout") ||
               !strcmp(case_name, "request-again")) {
        config_gui = expected_kcg = expected_gui = 1; expected_reject = 2;
        int layout = !strcmp(case_name, "load-layout");
        PLAN(10, 2, G(EXEC_ERR_NOT_FOUND, layout, 0, 0, 0), C(0, 0, 0, 1));
        expected_text = layout ? "gshell: rebuild required (KAPI data layout) -> CUI shell"
                               : "gshell load failed -> CUI shell";
        if (!strcmp(case_name, "request-again")) {
            config_gui = 0; requests[0] = requests[2] = SYS_GSHELL_BIN;
            PLAN(20, 3, G(EXEC_ERR_NOT_FOUND, 0, 0, 0, 0), C(0, 0, 0, 1),
                 G(0, 0, 10, 2, 0));
            expected_kcg = expected_gui = 2; expected_reject = 4;
        }
    } else if (!strcmp(case_name, "three-faults")) {
        config_gui = 1; expected_kcg = expected_gui = 3; expected_reject = 6;
        PLAN(40, 4, G(EXEC_ERR_FAULT, 0, 0, 0, 0), G(EXEC_ERR_FAULT, 0, 10, 1, 1),
             G(EXEC_ERR_FAULT, 0, 20, 2, 2), C(0, 0, 30, 3));
        expected_text = "gshell crashed 3x -> CUI shell";
    } else if (!strcmp(case_name, "success-resets") || !strcmp(case_name, "request-resets")) {
        config_gui = 1; expected_kcg = expected_gui = 4; expected_reject = 8;
        if (!strcmp(case_name, "success-resets")) {
            PLAN(40, 4, G(EXEC_ERR_FAULT, 0, 0, 0, 0), G(0, 0, 10, 1, 1),
                 G(EXEC_ERR_FAULT, 0, 20, 2, 0), G(EXEC_ERR_FAULT, 0, 30, 3, 1));
        } else {
            requests[2] = SYS_GSHELL_BIN;
            PLAN(40, 4, G(EXEC_ERR_FAULT, 0, 0, 0, 0), G(EXEC_ERR_FAULT, 0, 10, 1, 1),
                 G(EXEC_ERR_FAULT, 0, 20, 2, 0), G(EXEC_ERR_FAULT, 0, 30, 3, 1));
        }
        forbidden_text = "gshell crashed 3x";
    } else if (!strcmp(case_name, "cui-fault")) {
        PLAN(20, 2, C(EXEC_ERR_FAULT, 0, 0, 0), C(0, 0, 10, 1));
    } else {
        int first = !strcmp(case_name, "layout-first") || !strcmp(case_name, "layout-both");
        int second = !strcmp(case_name, "layout-second") || !strcmp(case_name, "layout-both");
        if (!strcmp(case_name, "fdd-success") || !strcmp(case_name, "fdd-fault")) {
            int rc = !strcmp(case_name, "fdd-fault") ? EXEC_ERR_FAULT : 0;
            PLAN(20, 2, C(EXEC_ERR_NOT_FOUND, 1, 0, 0), C(rc, 1, 0, 0), C(0, 0, 10, 1));
            expected_diag = 2;
        } else {
            REQUIRE(first || second || !strcmp(case_name, "fdd-fail"));
            PLAN(0, 0, C(EXEC_ERR_NOT_FOUND, first, 0, 0), C(EXEC_ERR_NOT_FOUND, second, 0, 0));
            expected_diag = 1;
            expected_text = first || second ? "FATAL: shell.bin: rebuild required (KAPI data layout)"
                                            : "FATAL: shell.bin load failed";
            forbidden_text = first || second ? "FATAL: shell.bin load failed" : "rebuild /sys";
        }
    }
    int initially_probed = probed;
    if (!__builtin_setjmp(done)) kernel_shell_loop();
    REQUIRE(step_index == step_count && waits == expected_waits && clears == expected_clears);
    REQUIRE(kcg_calls == expected_kcg && reject_calls == expected_reject);
    REQUIRE(client_calls == expected_gui && prepare_calls == (expected_gui > 0 && !initially_probed));
    REQUIRE(diag_count == expected_diag && !diagnostic_pending);
    REQUIRE(halted == (!strncmp(expected_text, "FATAL:", 6)));
    REQUIRE(strstr(messages, expected_text) && !strstr(messages, forbidden_text));
    say("PASS "); say(case_name); say("\n");
    return 0;
}

void host_start(unsigned int *sp) { finish(main((int)sp[0], (char **)(sp + 1))); }
__asm__(".global _start\n_start:\nmov %esp, %eax\nand $-16, %esp\n"
        "sub $12, %esp\npush %eax\ncall host_start\n");
