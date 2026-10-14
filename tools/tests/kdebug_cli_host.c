/* Product CLI functions are extracted by test_mem_stat.py. Only FS/API edges
 * are simulated; verify configuration preservation and delayed activation. */
#include <stdarg.h>
#include "os32api.h"
#include "config.h"
#define SH_STATUS_ERROR 1
#define SH_STATUS_USAGE 2
/* CFG_DEFINES */
RING3_WINDOW_DEFINE
static KernelAPI api, *g_api = &api;
static char file[CFG_OUT_MAX] = "GUI=0\n KDEBUG = 1\nKDEBUG=0\n# keep\n";
static u32 file_len = sizeof("GUI=0\n KDEBUG = 1\nKDEBUG=0\n# keep\n") - 1;
static u32 offset, calls, opens, writes, write_opens, state, version = 75;
static int missing, open_fail, read_fail, probe_fail, write_fail, stat_fail;
static int refusals, refused_limit;
static void emit(char c) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(&c), "d"(1) : "memory");
}
static void die(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; } return *a == *b;
}
static void print(u8 attr, const char *fmt, ...) {
    (void)attr;
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt++ != '%') { emit(fmt[-1]); continue; }
        u32 width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        char spec = *fmt++;
        if (spec == 's') { const char *s = va_arg(ap, const char *); while (*s) emit(*s++); }
        else {
            u32 n = va_arg(ap, u32), radix = spec == 'x' ? 16 : 10;
            if (spec == 'd' && (i32)n < 0) { emit('-'); n = 0U - n; }
            char buf[16]; u32 count = 0;
            do { buf[count++] = "0123456789abcdef"[n % radix]; n /= radix; } while (n);
            while (width > count) { emit('0'); width--; }
            while (count) emit(buf[--count]);
        }
    }
    va_end(ap);
}
#define CHECK(expr) do { if (!(expr)) { print(0, "FAIL: %s\n", #expr); die(1); } } while (0)
static int open_file(const char *path, int flags) {
    CHECK(str_eq(path, SYS_SYSTEM_CFG)); opens++;
    offset = 0;
    if (flags & KAPI_O_TRUNC) { write_opens++; file_len = 0; return 1; }
    if (open_fail) return OS32_ERR_IO;
    return missing ? OS32_ERR_NOTFOUND : 1;
}
static void close_file(int fd) { CHECK(fd == 1); }
static int read_file(int fd, void *out, u32 n) {
    CHECK(fd == 1); if (read_fail) return OS32_ERR_IO;
    if (probe_fail && n == 1) return OS32_ERR_IO;
    u32 got = file_len - offset;
    if (got > n) got = n;
    for (u32 i = 0; i < got; i++) ((char *)out)[i] = file[offset++];
    return got;
}
static int write_file(int fd, const void *in, u32 n) {
    CHECK(fd == 1); writes++; if (write_fail) return OS32_ERR_IO;
    CHECK(n < sizeof(file));
    for (u32 i = 0; i < n; i++) file[i] = ((const char *)in)[i];
    file[n] = 0; file_len = n; return n;
}
static i32 stat(i32 id, void *out, u32 size) {
    calls++; CHECK(version >= 75 && id == 0 && size == 220);
    if (stat_fail) return OS32_ERR_INVAL;
    MemStat s = {0}; s.size = sizeof(s);
    s.kdebug_state = state; s.kdebug_pair_crc = 0x1234abcd;
    s.kdebug_pages = 2; s.kdebug_owner_pages = 3; s.kdebug_open_count = 7;
    for (u32 i = 0; i < sizeof(s); i++) ((u8 *)out)[i] = ((u8 *)&s)[i];
    return sizeof(s);
}
static void build_info(char *out, int size) { CHECK(size >= 8); out[0] = 'x'; out[1] = 0; }
static int image_info(BootImageInfo *out) {
    *out = (BootImageInfo){0}; out->crc_valid = 1; out->image_crc = 0x1234abcd;
    out->source = 2; return 0;
}
static void sh_refuse(const char *what, int limit) {
    CHECK(str_eq(what, "system.cfg")); refusals++; refused_limit = limit;
}
/* PRODUCT_SOURCE */
static void config_failures_and_limits(void) {
    const char *original = "GUI=1\nOTHER=keep\n";
    file_len = 0;
    while (original[file_len]) { file[file_len] = original[file_len]; file_len++; }
    file[file_len] = 0;
    u32 before_writes = writes, before_opens = write_opens, before_len = file_len;
    open_fail = 1;
    CHECK(cfg_set_key("KDEBUG", "1") == -1);
    open_fail = 0;
    read_fail = 1;
    CHECK(cfg_set_key("KDEBUG", "1") == -1);
    read_fail = 0;
    CHECK(writes == before_writes && write_opens == before_opens && file_len == before_len);
    CHECK(str_eq(file, original));

    for (u32 i = 0; i < 1023; i++) file[i] = '#';
    file[1023] = 0; file_len = 1023;
    probe_fail = 1;
    CHECK(cfg_set_key("KDEBUG", "1") == -1);
    probe_fail = 0;
    CHECK(writes == before_writes && write_opens == before_opens && file_len == 1023);

    /* 1014 preserved bytes + KDEBUG=1\n (9 B) = 1023 B. */
    file[1013] = '\n'; file[1014] = 0; file_len = 1014;
    CHECK(!cfg_set_key("KDEBUG", "1") && file_len == 1023 && writes == before_writes + 1);
    int value;
    CHECK(!cfg_get_kdebug(&value) && value == 1);
    before_writes = writes; before_opens = write_opens;
    /* One more preserved byte makes the output 1024 B. */
    for (u32 i = 0; i < 1014; i++) file[i] = '#';
    file[1014] = '\n'; file[1015] = 0; file_len = 1015;
    refusals = 0;
    CHECK(cfg_set_key("KDEBUG", "1") == -1 && refusals == 1 && refused_limit == 1023);
    CHECK(writes == before_writes && write_opens == before_opens && file_len == 1015);
    /* NOTFOUND alone allows creation. */
    missing = 1;
    CHECK(!cfg_set_key("KDEBUG", "0") && str_eq(file, "KDEBUG=0\n"));
    missing = 0;
    print(0, "%s", "PASS cfg read failures/no writes and 1023/1024 boundary\n");
}
void _start(void) {
    api.version = version; api.kprintf = print; api.mem_stat = stat;
    api.sys_open = open_file; api.sys_close = close_file;
    api.sys_read = read_file; api.sys_write = write_file;
    api.sys_get_build_info = build_info; api.boot_image_info = image_info;
    int value;
    CHECK(!cfg_get_kdebug(&value) && value == 1);
    char *show[] = {"kdebug"}, *on[] = {"kdebug", "on"}, *off[] = {"kdebug", "off"};
    for (state = 0; state <= 13; state++) CHECK(!cmd_kdebug(1, show));
    state = 1; CHECK(!cmd_ver(0, 0));
    u32 before = calls;
    CHECK(!cmd_kdebug(2, off) && calls == before && state == 1);
    CHECK(str_eq(file, "GUI=0\n# keep\nKDEBUG=0\n"));
    CHECK(!cfg_get_kdebug(&value) && value == 0);
    CHECK(!cmd_kdebug(2, on) && str_eq(file, "GUI=0\n# keep\nKDEBUG=1\n"));
    CHECK(!cfg_get_kdebug(&value) && value == 1);
    missing = 1; CHECK(!cfg_get_kdebug(&value) && value == 0); missing = 0;
    read_fail = 1; CHECK(cmd_kdebug(1, show) == SH_STATUS_ERROR); read_fail = 0;
    write_fail = 1; CHECK(cmd_kdebug(2, off) == SH_STATUS_ERROR); write_fail = 0;
    stat_fail = 1; CHECK(cmd_kdebug(1, show) == SH_STATUS_ERROR); stat_fail = 0;
    char *bad[] = {"kdebug", "bad"}; before = opens;
    CHECK(cmd_kdebug(2, bad) == SH_STATUS_USAGE && opens == before);
    CHECK(cmd_kdebug(3, on) == SH_STATUS_USAGE && opens == before);
    version = api.version = 74; before = calls;
    CHECK(!cmd_ver(0, 0) && calls == before);
    CHECK(cmd_kdebug(1, show) == SH_STATUS_ERROR && calls == before);
    config_failures_and_limits();
    print(0, "%s", "PASS kdebug CLI config/state/version/errors\n");
    die(0);
}
