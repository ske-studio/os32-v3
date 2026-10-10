/* Guest verdicts wired to the real pipe/TVRAM implementations (LP64 host).
 * Console address translation alone is replaced with guarded host storage. */
#include "os32api.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tvram.h"

static int owner = 2, user = 1, break_buf, break_own, break_foreign;
int res_owner_get(void) { return owner; }
int ring3_call_from_user(void) { return user; }
void *kmalloc(u32 bytes) { return malloc(bytes); }
void kfree(void *p) { free(p); }
#include "pipe_source.c"

static int kout_na, kout_ok;
static void quiet(u8 attr, const char *fmt, ...)
{
    char message[512];
    va_list args;
    (void)attr;
    va_start(args, fmt); vsnprintf(message, sizeof(message), fmt, args); va_end(args);
    if (strstr(message, "2d NULL:") || strstr(message, "3c NULL:")) {
        assert(strstr(message, "N/A") && !strstr(message, "SKIP")); kout_na++;
    }
    if (strstr(message, "KOUT PASS")) {
        assert(strstr(message, "0 failure(s), 0 skip(s)")); kout_ok++;
    }
}
static u8 *get_buf(int id)
{ return break_buf ? (u8 *)1 : pipe_get_buf(id); }
static u32 get_len(int id)
{
    u32 n = pipe_get_len(id);
    if (owner == 2 && break_own) return n + 1;
    if (owner == 3 && break_foreign) return 17;
    return n;
}
static int alloc_seeded(void)
{ int id = pipe_alloc(); if (id >= 0) pipe_set_len(id, 17); return id; }
#define PIPE_OWNER_LENGTH 17U
#define main pipe_guest_main
#include "pipe_guest.c"
#undef main
static KernelAPI api;
static int child_rc;
static int run_child(const char *command)
{
    char path[128], mode[32], id[32];
    assert(sscanf(command, "%127s %31s %31s", path, mode, id) == 3);
    char *args[] = {path, mode, id};
    owner = 3;
    child_rc = pipe_guest_main(3, args, &api);
    owner = 2;
    return child_rc;
}
static int last_result(int *kind, int *code)
{ *kind = EXEC_KIND_EXITED; *code = child_rc; return 0; }

static u16 text[TVRAM_COLS * TVRAM_ROWS + 2];
static u16 attrs[TVRAM_COLS * TVRAM_ROWS + 2];
#define TVRAM_TEXT TVRAM_CHAR_BASE
static void *tv_address(u32 address)
{
    if (address >= TVRAM_TEXT && address < TVRAM_TEXT + TVRAM_COLS * TVRAM_ROWS * 2)
        return (u8 *)(text + 1) + address - TVRAM_TEXT;
    if (address >= TVRAM_ATTR && address < TVRAM_ATTR + TVRAM_COLS * TVRAM_ROWS * 2)
        return (u8 *)(attrs + 1) + address - TVRAM_ATTR;
    /* A bad target address is a rejected test, never a host memory access. */
    fprintf(stderr, "FAIL TVRAM address %lx\n", address);
    exit(1);
}
#define P2V_IO(address) tv_address(address)
#include "console_source.c"
static void console_size(int *w, int *h) { *w = TVRAM_COLS; *h = TVRAM_ROWS; }
#define main audit_guest_main
#include "audit_guest.c"
#undef main
#define main kout_guest_main
#include "kout_guest.c"
#undef main
static int kout_open(const char *path, int mode) { (void)path; (void)mode; return 3; }
static int kout_read(int fd, void *buf, u32 len)
{ if (fd == 99) return -1; memset(buf, 'a', len); return (int)len; }
static void kout_close(int fd) { (void)fd; }
static void kout_version(char *buf, int len) { if (len > 0) buf[0] = 0; }
static void kout_rtc(void *p)
{ RTC_Time_Ext *rtc = p; memset(rtc, 0, sizeof(*rtc)); rtc->month = rtc->day = 1; }
static int kout_pci_count(void) { return 0; }
static int kout_pci(u32 index, void *p) { (void)index; (void)p; return -1; }
static int kout_present(int drv) { return drv == 0; }
static int kout_sector(int drv, u32 lba, void *buf)
{ (void)lba; if (drv != 0) return -1; memset(buf, 0, KOUT_SECTOR); return 0; }

int main(void)
{
    char *args[] = {"pipe_owner_test"};
    api.kprintf = quiet;
    api.sys_pipe_alloc = alloc_seeded;
    api.sys_pipe_free = pipe_free;
    api.sys_pipe_get_buf = get_buf;
    api.sys_pipe_get_len = get_len;
    api.exec_run = run_child;
    api.exec_last_result = last_result;
    pipe_buffer_init();
    assert(pipe_guest_main(1, args, &api) == 0);
    for (int i = 0; i < 3; i++) {
        break_buf = i == 0; break_own = i == 1; break_foreign = i == 2;
        assert(pipe_guest_main(1, args, &api) == 1);
    }
    break_buf = break_own = break_foreign = 0;
    int id = alloc_seeded();
    user = 0; owner = 1;
    assert(pipe_get_buf(id) && pipe_get_len(id) == 17);
    pipe_free(id); user = 1; owner = 2;
    api.console_get_size = console_size;
    api.tvram_putchar_at = tvram_putchar_at;
    api.tvram_putkanji_at = tvram_putkanji_at;
    api.tvram_readchar_at = tvram_readchar_at;
    api.tvram_reverse_cell = tvram_reverse_cell;
    text[0] = text[TVRAM_COLS * TVRAM_ROWS + 1] = 0x9876;
    attrs[0] = attrs[TVRAM_COLS * TVRAM_ROWS + 1] = 0x5432;
    for (int i = 1; i <= TVRAM_COLS * TVRAM_ROWS; i++) {
        text[i] = 'A'; attrs[i] = ATTR_WHITE;
    }
    char *tvargs[] = {"audit_test", "tvram"};
    int rc = audit_guest_main(2, tvargs, &api);
    assert(text[0] == 0x9876 && text[TVRAM_COLS * TVRAM_ROWS + 1] == 0x9876);
    assert(attrs[0] == 0x5432 && attrs[TVRAM_COLS * TVRAM_ROWS + 1] == 0x5432);
    api.version = KAPI_VERSION;
    api.sys_open = kout_open; api.sys_read = kout_read; api.sys_close = kout_close;
    api.np2_get_version = kout_version; api.rtc_read = kout_rtc;
    api.pci_count = kout_pci_count; api.pci_get = kout_pci;
    api.ide_drive_present = kout_present; api.ide_read_sector = kout_sector;
    assert(kout_guest_main(1, args, &api) == 0 && kout_na == 2 && kout_ok == 1);
    puts(rc ? "FAIL audit TVRAM" : "PASS h3b pipe verdicts and TVRAM");
    return rc;
}
