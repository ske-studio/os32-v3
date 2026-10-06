/* Real loader policy, saved USER identity and generated disk wrappers. */
#define HOST_CALLER_COPY_TEST
#define appslot_get walk_appslot_get
#include "setup_host_source.c"
#undef appslot_get
#undef used
static AppSlot shell_slot = { .state = APP_STATE_RUNNING };
AppSlot *appslot_get(int id)
{
    return id == APP_ID_SHELL ? &shell_slot : walk_appslot_get(id);
}
#include "ring3_str.h"
#include "ring3_str.c"
volatile int ring3_in_syscall = 1;
#define RING3_USTACK_TOP MEM_APP_STACK_TOP
#define RING3_STACK_BOTTOM (MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE)
#define RING3_HEAP_TOP (RING3_STACK_BOTTOM - PAGE_SIZE)
#include "exec_host_source.c"
#include "config.h"
static int gui_mode;
int con_sink_is_enabled(void) { return gui_mode; }
int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
char *kstrncpy(char *d, const char *s, u32 n)
{
    u32 i = 0;
    if (n) { while (i + 1 < n && s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
    return d;
}
#include "resolve_host_source.c"
#include "disk_host_source.c"
#include "generated_host_source.c"
static int reached;
void ring3_fault_kill(void) { SAY("FAIL: unexpected pointer fault"); die(1); }
int ide_write_sector(int drv, u32 lba, const void *buf)
{ (void)drv; (void)lba; (void)buf; reached++; return 7; }
int ide_write_sectors(int drv, u32 lba, u32 cnt, const void *buf)
{ (void)drv; (void)lba; (void)cnt; (void)buf; reached++; return 7; }
int ext2_format(int drv, u32 sectors)
{ (void)drv; (void)sectors; reached++; return 7; }
int ext2_format_at(int drv, u32 start, u32 length)
{ (void)drv; (void)start; (void)length; reached++; return 7; }
static Device disk = { .sect_size = 512 };
Device *dev_find(const char *name) { (void)name; return &disk; }
int dev_blk_write_lba(Device *d, u32 lba, int count, const void *buf)
{ (void)d; (void)lba; (void)count; (void)buf; reached++; return 7; }
static void writes(int allowed)
{
    int expected = allowed ? 7 : -1;
    void *buf = (void *)MEM_EXEC_LOAD_ADDR;
    reached = 0;
    CHECK(wrap_ide_write_sector(0, 0, buf) == expected);
    CHECK(wrap_ide_write_sectors(0, 0, 1, buf) == expected);
    CHECK(wrap_dev_blk_write("disk", 0, 1, buf) == expected);
    CHECK(wrap_ext2_format(0, 4096) == expected);
    CHECK(wrap_ext2_format_at(0, 18, 4096) == expected);
    CHECK(reached == (allowed ? 5 : 0));
    reached = 0;
    CHECK(wrap_ide_write_sectors(0, 0, 0, 0) == (allowed ? 0 : -1));
    CHECK(reached == 0);
    if (!allowed) {
        CHECK(wrap_ide_write_sector(0, 0, 0) == -1);
        CHECK(wrap_ide_write_sectors(0, 0, 0, 0) == -1);
        CHECK(wrap_dev_blk_write("disk", 0, 0, 0) == -1);
        CHECK(reached == 0);
    }
}
static void caller_copy_tests(void)
{
    CallerAccessFrame previous;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    writes(0);
    const char *denied[] = {"", "install.bin", "/tmp/install.bin", "/sys/install.bin",
        "/host/sbin/install.bin", "/sbin/other.bin", "/sbin/install.bin.extra",
        "/sbin/../tmp/install.bin"};
    for (u32 i = 0; i < sizeof(denied) / sizeof(denied[0]); i++) {
        slot.disk_write_authorized = exec_disk_write_path_allowed(denied[i]);
        writes(0);
    }
    slot.disk_write_authorized = exec_disk_write_path_allowed("/sbin/install.bin");
    writes(1);
    slot.disk_write_authorized = exec_disk_write_path_allowed("/sbin/cdinst.bin");
    writes(1);
    kstrncpy(cwd, "/sbin/", sizeof(cwd));
    const char *allowed[] = {"install.bin", "./install.bin", "/sbin//install.bin",
        "/sbin/../sbin/install.bin", "cdinst.bin", "./cdinst.bin"};
    for (u32 i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        slot.disk_write_authorized = exec_disk_write_path_allowed(allowed[i]);
        writes(1);
    }
    kstrncpy(cwd, "/tmp/", sizeof(cwd));
    slot.disk_write_authorized = exec_disk_write_path_allowed("./install.bin");
    writes(0);
    char too_long[VFS_MAX_PATH + 1];
    for (u32 i = 0; i < sizeof(too_long) - 1; i++) too_long[i] = 'a';
    too_long[sizeof(too_long) - 1] = 0;
    CHECK(!exec_disk_write_path_allowed(too_long));
    slot.disk_write_authorized = exec_disk_write_path_allowed("/sbin/install.bin");
    slot.gui = 1; writes(0); slot.gui = 0;
    gui_mode = 1; writes(0); gui_mode = 0;
    slot.state = APP_STATE_PARKED; writes(0); slot.state = APP_STATE_RUNNING;
    ring3_wm_depth = 1; writes(0); ring3_wm_depth = 0;
    resource_owner = APP_ID_SHELL; writes(0); resource_owner = 2;
    host_cr3++; writes(0); host_cr3--;
    space.generation++; writes(0); space.generation--;
    caller_access_invalidate(); writes(0);
    CHECK(caller_access_enter(&previous, CALLER_USER)); writes(1);
    /* Direct CPL0 calls only authorize the resident shell. */
    ring3_in_syscall = 0; writes(0);
    current = resource_owner = APP_ID_SHELL; writes(1);
    shell_slot.cpl3 = 1; writes(0); shell_slot.cpl3 = 0;
    gui_mode = 1; writes(0); gui_mode = 0;
    current = resource_owner = 2; ring3_in_syscall = 1;
    slot.disk_write_authorized = 0; writes(0); /* reused / child slot */
    caller_access_leave(&previous);
    SAY("PASS: disk authorization / all five wrappers / zero denied I/O");
}
