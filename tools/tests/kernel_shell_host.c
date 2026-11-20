/* E02b: run the unmodified shell loop with scripted exec/device boundaries. */
#include <stdarg.h>
static unsigned long host_cr3;
#define CR0_EM (1u << 2)
#define CR0_TS (1u << 3)
static void host_halt(void);
#include "kernel.c"
#include "exec.h"
#include "config.h"
#include "pc98.h"
#include "shlib.h"

static unsigned int fixture_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}
static int fixture_strncmp(const char *a, const char *b, unsigned int n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
static int fixture_strcmp(const char *a, const char *b) { return fixture_strncmp(a, b, fixture_strlen(b) + 1); }
static void fixture_strcpy(char *d, const char *s) { while ((*d++ = *s++)) {} }
static void fixture_strcat(char *d, const char *s) { fixture_strcpy(d + fixture_strlen(d), s); }
static const char *fixture_strstr(const char *s, const char *needle)
{
    for (; *s; s++) if (!fixture_strncmp(s, needle, fixture_strlen(needle))) return s;
    return *needle ? 0 : s;
}
static void say(const char *s)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(fixture_strlen(s)) : "memory");
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
static char screen[TVRAM_ROWS][TVRAM_COLS + 1];
static u8 colors[TVRAM_ROWS][TVRAM_COLS];
static int dual, cursor_col, cursor_row;
static const char *expected_warning;
static void *done[5];
volatile u32 tick_count;

static void append(const char *s)
{
    REQUIRE(fixture_strlen(messages) + fixture_strlen(s) + 2 < sizeof(messages));
    fixture_strcat(messages, s);
    fixture_strcat(messages, "\n");
}

int sysconfig_get_int(const char *path, const char *key, int def)
{
    REQUIRE(!fixture_strcmp(path, SYS_SYSTEM_CFG) && !fixture_strcmp(key, "GUI") && def == 0);
    return config_gui;
}

int gui_take_next_shell(char *out, int cap)
{
    REQUIRE(!diagnostic_pending);
    if (step_index == step_count) __builtin_longjmp(done, 1);
    REQUIRE(take_count < 10);
    const char *request = requests[take_count];
    requests[take_count++] = NULL; /* consume, including a later fresh request */
    if (!request) return 0;
    REQUIRE((int)fixture_strlen(request) < cap);
    fixture_strcpy(out, request);
    return 1;
}

int kstrcmp(const char *a, const char *b) { return fixture_strcmp(a, b); }
int kcg_module_ready(void) { kcg_calls++; return kcg_ready; }
int shlib_reject_reason(void) { reject_calls++; return reject_reason; }
u32 shlib_reject_min_api(void) { return KAPI_VERSION + 1; }

void kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    REQUIRE(attr == ATTR_YELLOW);
    va_start(ap, fmt);
    if (fixture_strstr(fmt, "needs KAPI")) {
        REQUIRE(!fixture_strcmp(va_arg(ap, const char *), SYS_SHLIB_GUI));
        REQUIRE(va_arg(ap, u32) == KAPI_VERSION + 1);
        REQUIRE(va_arg(ap, u32) == KAPI_VERSION);
    }
    va_end(ap);
    append(fmt);
}

void console_text_gdc_start(void) { cursor_gui = 0; }
void console_text_gdc_stop(void) { cursor_gui = 1; }
int console_get_cursor_x(void) { return cursor_col; }
void console_set_cursor(int x, int y) { cursor_col = x; cursor_row = y; }
void tvram_clear(void)
{
    clears++;
    for (int y = 0; y < TVRAM_ROWS; y++)
        for (int x = 0; x < TVRAM_COLS; x++) screen[y][x] = ' ';
    console_set_cursor(0, 0);
}
void tvram_print(int x, int y, const char *s, u8 attr)
{
    REQUIRE(y >= 0 && y < TVRAM_ROWS && x >= 0);
    for (int i = 0; s[i] && x < TVRAM_COLS; i++, x++) {
        colors[y][x] = attr;
        screen[y][x] = s[i];
    }
    append(s);
    if (!fixture_strncmp(s, "FATAL:", 6)) fatal = 1;
}
void tvram_readchar_at(int x, int y, u16 *code, u8 *attr)
{
    *code = screen[y][x];
    *attr = colors[y][x];
}
void tvram_putchar_at(int x, int y, char ch, u8 attr)
{
    REQUIRE(x >= 0 && x < TVRAM_COLS && y >= 0 && y < TVRAM_ROWS && ch == ' ' && attr == TATTR_GRAY);
    screen[y][x] = ch;
}
int gfx_cirrus_probed(void) { return probed; }
void gfx_prepare_backend(void) { prepare_calls++; probed = 1; }
void gfx_client_to_gshell(void) { client_calls++; }
void geforce_heat_notice(void) {}

static char serial_line[256];
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s)
{
    REQUIRE(fixture_strlen(serial_line) + fixture_strlen(s) < sizeof(serial_line));
    fixture_strcat(serial_line, s);
    if (fixture_strcmp(s, "\r\n")) return;
    if (!fixture_strncmp(serial_line, "[boot] exec ret rc=", 19)) {
        REQUIRE(diagnostic_pending && step_index > 0);
        const struct step *step = &steps[step_index - 1];
        char expected[128], *p = expected;
        const char *prefix = "[boot] exec ret rc=";
        while (*prefix) *p++ = *prefix++;
        int values[] = {step->rc, step->gui, step->streak};
        const char *suffix[] = {" gui=", " streak=", "\r\n"};
        for (int i = 0; i < 3; i++) {
            int n = values[i];
            unsigned int value = n < 0 ? 0u - (unsigned int)n : (unsigned int)n;
            char digits[11]; int count = 0;
            if (n < 0) *p++ = '-';
            do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
            while (count) *p++ = digits[--count];
            const char *tail = suffix[i]; while (*tail) *p++ = *tail++;
        }
        *p = 0;
        REQUIRE(!fixture_strcmp(serial_line, expected));
        diagnostic_pending = 0; diag_count++;
    } else {
        REQUIRE(!diagnostic_pending && !fixture_strncmp(serial_line, "[boot] exec ", 12));
        diagnostic_pending = 1;
    }
    serial_line[0] = 0;
}

int exec_run(const char *path)
{
    REQUIRE(step_index < step_count);
    const struct step *s = &steps[step_index++];
    REQUIRE(!fixture_strcmp(path, s->path));
    REQUIRE(waits == s->waits && clears == s->clears);
    REQUIRE(cursor_gui == s->gui);
    /* Refusal diagnostics must precede the CUI load, not appear afterward. */
    if (config_gui && reject_reason && kcg_ready && step_index == 1)
        REQUIRE(fixture_strstr(messages, expected_text));
    if (!s->gui && expected_warning) {
        int retry_case = !fixture_strcmp(case_name, "gui-fdd");
        if (s->rc == 0) {
            shell_print("================================\n OS32 External Shell Started\n"
                        "================================\n", TATTR_GRAY);
        }
        int warning_row = -1, loading_row = -1;
        for (int y = 0; y < TVRAM_ROWS; y++) {
            if (fixture_strstr(screen[y], expected_warning)) warning_row = y;
            if (fixture_strstr(screen[y], "Loading shell...")) loading_row = y;
        }
        if (retry_case && step_index == 4) {
            REQUIRE(warning_row < 0);
            if (dual) {
                int len = fixture_strlen(expected_warning);
                REQUIRE(screen[0][len] == '#');
                for (int x = 0; x < len; x++) REQUIRE(screen[0][x] == ' ');
            }
        } else {
            REQUIRE(loading_row >= 0 && warning_row >= 0 && cursor_row > warning_row);
            if (retry_case && step_index == 3 && dual)
                tvram_print(fixture_strlen(expected_warning), warning_row, "#", TATTR_GRAY);
        }
    }
    return s->rc;
}

int exec_layout_rejected(void)
{
    REQUIRE(step_index > 0);
    return steps[step_index - 1].layout;
}

static void host_halt(void)
{
    if (fatal) { halted = 1; __builtin_longjmp(done, 1); }
    REQUIRE(++waits <= 100); /* A lost fail-stop/wait cannot hang the fixture. */
    tick_count++;
}



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
    dual = !fixture_strncmp(case_name, "dual-", 5);
    if (dual) case_name += 5;
    if (!fixture_strcmp(case_name, "cui") || !fixture_strcmp(case_name, "tick-wrap") ||
        !fixture_strcmp(case_name, "cui-ignores-shlib")) {
        PLAN(10, 1, C(0, 0, 0, 0));
        if (!fixture_strcmp(case_name, "tick-wrap")) tick_count = 0xfffffffbUL;
        if (!fixture_strcmp(case_name, "cui-ignores-shlib")) reject_reason = SHLIB_REJECT_LAYOUT;
    } else if (!fixture_strcmp(case_name, "gui") || !fixture_strcmp(case_name, "gui-probed")) {
        config_gui = expected_kcg = expected_gui = 1; expected_reject = 2;
        probed = !fixture_strcmp(case_name, "gui-probed");
        PLAN(10, 1, G(0, 0, 0, 0, 0));
    } else if (!fixture_strcmp(case_name, "kcg")) {
        config_gui = expected_kcg = 1; kcg_ready = 0;
        PLAN(20, 2, {SYS_SHELL_BIN " --kcg-unavailable", 0, 0, 0, 0, 0, 0},
             C(0, 0, 10, 1));
    } else if (!fixture_strcmp(case_name, "shlib-layout") || !fixture_strcmp(case_name, "shlib-api")) {
        config_gui = expected_kcg = 1;
        if (!fixture_strcmp(case_name, "shlib-layout")) {
            reject_reason = SHLIB_REJECT_LAYOUT; expected_reject = 1;
            expected_text = "GUI shlib: rebuild required (KAPI data layout) -> CUI shell";
            forbidden_text = "update the kernel first";
        } else {
            reject_reason = SHLIB_REJECT_MIN_API; expected_reject = 2;
            expected_text = "update the kernel first (hsync boot / deploy-kernel)";
            forbidden_text = "rebuild /sys";
        }
        PLAN(10, 2, C(0, 0, 0, 1));
    } else if (!fixture_strcmp(case_name, "load-fail") || !fixture_strcmp(case_name, "load-layout") ||
               !fixture_strcmp(case_name, "request-again")) {
        config_gui = expected_kcg = expected_gui = 1; expected_reject = 2;
        int layout = !fixture_strcmp(case_name, "load-layout");
        PLAN(10, 2, G(EXEC_ERR_NOT_FOUND, layout, 0, 0, 0), C(0, 0, 0, 1));
        expected_text = layout ? "gshell: rebuild required (KAPI data layout) -> CUI shell"
                               : "gshell load failed -> CUI shell";
        expected_warning = expected_text;
        if (!fixture_strcmp(case_name, "request-again")) {
            config_gui = 0; requests[0] = requests[2] = SYS_GSHELL_BIN;
            PLAN(20, 3, G(EXEC_ERR_NOT_FOUND, 0, 0, 0, 0), C(0, 0, 0, 1),
                 G(0, 0, 10, 2, 0));
            expected_kcg = expected_gui = 2; expected_reject = 4;
        }
    } else if (!fixture_strcmp(case_name, "gui-fdd")) {
        config_gui = expected_gui = expected_kcg = 1; expected_reject = 2;
        PLAN(20, 3, G(EXEC_ERR_NOT_FOUND, 0, 0, 0, 0), C(EXEC_ERR_NOT_FOUND, 0, 0, 1),
             C(0, 0, 0, 1), C(0, 0, 10, 2));
        expected_diag = 3;
        expected_warning = "gshell load failed -> CUI shell";
    } else if (!fixture_strcmp(case_name, "three-faults")) {
        config_gui = 1; expected_kcg = expected_gui = 3; expected_reject = 6;
        PLAN(40, 4, G(EXEC_ERR_FAULT, 0, 0, 0, 0), G(EXEC_ERR_FAULT, 0, 10, 1, 1),
             G(EXEC_ERR_FAULT, 0, 20, 2, 2), C(0, 0, 30, 3));
        expected_warning = expected_text = "gshell crashed 3x -> CUI shell";
    } else if (!fixture_strcmp(case_name, "success-resets") || !fixture_strcmp(case_name, "request-resets")) {
        config_gui = 1; expected_kcg = expected_gui = 4; expected_reject = 8;
        if (!fixture_strcmp(case_name, "success-resets")) {
            PLAN(40, 4, G(EXEC_ERR_FAULT, 0, 0, 0, 0), G(0, 0, 10, 1, 1),
                 G(EXEC_ERR_FAULT, 0, 20, 2, 0), G(EXEC_ERR_FAULT, 0, 30, 3, 1));
        } else {
            requests[2] = SYS_GSHELL_BIN;
            PLAN(40, 4, G(EXEC_ERR_FAULT, 0, 0, 0, 0), G(EXEC_ERR_FAULT, 0, 10, 1, 1),
                 G(EXEC_ERR_FAULT, 0, 20, 2, 0), G(EXEC_ERR_FAULT, 0, 30, 3, 1));
        }
        forbidden_text = "gshell crashed 3x";
    } else if (!fixture_strcmp(case_name, "cui-fault")) {
        PLAN(20, 2, C(EXEC_ERR_FAULT, 0, 0, 0), C(0, 0, 10, 1));
    } else {
        int first = !fixture_strcmp(case_name, "layout-first") || !fixture_strcmp(case_name, "layout-both");
        int second = !fixture_strcmp(case_name, "layout-second") || !fixture_strcmp(case_name, "layout-both");
        if (!fixture_strcmp(case_name, "fdd-success") || !fixture_strcmp(case_name, "fdd-fault")) {
            int rc = !fixture_strcmp(case_name, "fdd-fault") ? EXEC_ERR_FAULT : 0;
            PLAN(20, 2, C(EXEC_ERR_NOT_FOUND, 1, 0, 0), C(rc, 1, 0, 0), C(0, 0, 10, 1));
            expected_diag = 2;
        } else {
            REQUIRE(first || second || !fixture_strcmp(case_name, "fdd-fail"));
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
    REQUIRE(halted == (!fixture_strncmp(expected_text, "FATAL:", 6)));
    REQUIRE(fixture_strstr(messages, expected_text) && !fixture_strstr(messages, forbidden_text));
    say("PASS "); say(case_name); say("\n");
    return 0;
}

void host_start(unsigned int *sp) { finish(main((int)sp[0], (char **)(sp + 1))); }
__asm__(".global _start\n_start:\nmov %esp, %eax\nand $-16, %esp\n"
        "sub $12, %esp\npush %eax\ncall host_start\n");

int con_dual_active(void) { return 0; }
int con_dual_was_live(void) { return 0; }
int con_dual_cfg(void) { return dual; }
void con_dual_configure(int on) { (void)on; }
void console_session_clear(void) { if (dual) clears++; else tvram_clear(); }
void shell_print(const char *str, u8 color)
{
    append(str);
    while (*str) {
        REQUIRE(cursor_row >= 0 && cursor_row < TVRAM_ROWS);
        if (*str == '\n') {
            console_set_cursor(0, cursor_row + 1);
        } else {
            REQUIRE(cursor_col >= 0 && cursor_col < TVRAM_COLS);
            colors[cursor_row][cursor_col] = color;
            screen[cursor_row][cursor_col++] = *str;
        }
        str++;
    }
}
