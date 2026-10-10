#define HOST_CALLER_COPY_TEST
#include "setup_host_source.c"
#undef used
#include "ring3_str.h"
#include "os32_kapi_slots.h"
#include "ring3_str.c"
volatile int ring3_in_syscall = 1;
#define RING3_USTACK_TOP MEM_APP_STACK_TOP
#define RING3_STACK_BOTTOM (MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE)
#define RING3_HEAP_TOP (RING3_STACK_BOTTOM - PAGE_SIZE)
#include "exec_host_source.c"
#include "config.h"
int con_sink_is_enabled(void) { return 0; }
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

static void *escape[5];
static int reached, checks;
static const char *case_name;
void ring3_fault_kill(void) { __builtin_longjmp(escape, 1); }
static int touch(void) { reached++; return 7; }
int vfs_read_fd(int fd, void *buf, u32 size) { (void)fd; (void)buf; (void)size; return touch(); }
int vfs_write_fd(int fd, const void *buf, u32 size) { (void)fd; (void)buf; (void)size; return touch(); }
int np2_get_version(char *buf, int size) { (void)buf; (void)size; return touch(); }
void kcg_read_ank(u8 ch, u8 *buf) { (void)ch; (void)buf; (void)touch(); }
int ide_write_sector(int drv, u32 lba, const void *buf) { (void)drv; (void)lba; (void)buf; return touch(); }
int ide_write_sectors(int drv, u32 lba, u32 cnt, const void *buf) { (void)drv; (void)lba; (void)cnt; (void)buf; return touch(); }
static Device disk;
Device *dev_find(const char *name) { (void)name; return &disk; }
int dev_blk_read_lba(Device *d, u32 lba, int n, void *buf) { (void)d; (void)lba; (void)n; (void)buf; return touch(); }
int dev_blk_write_lba(Device *d, u32 lba, int n, const void *buf) { (void)d; (void)lba; (void)n; (void)buf; return touch(); }
int gfx_lease_palette(int f, int n, const u8 *p) { (void)f; (void)n; (void)p; return touch(); }
i32 kbd_inject(const u8 *p, u32 n) { (void)p; (void)n; return touch(); }
void gfx_present_raster(GFX_RasterPalTable *p) { (void)p; (void)touch(); }
int fd_redirect_to_buffer(int fd, u8 *p, u32 size, u32 len) { (void)fd; (void)p; (void)size; (void)len; return touch(); }
void palette_get(int idx, u8 *r, u8 *g, u8 *b) { (void)idx; (void)r; (void)g; (void)b; (void)touch(); }

/* Real collector; only resident heap counters are host boundary values. */
u32 appslot_trim_epoch;
static AppSlot other_slots[APP_SLOT_COUNT];
AppSlot *appslot_at(int id)
{
    if (id < 0 || id >= APP_SLOT_COUNT) return 0;
    return id == 2 ? &slot : &other_slots[id];
}
u32 kmalloc_total(void) { return 8192; }
u32 kmalloc_used(void) { return 128; }
u32 kmalloc_free(void) { return 8064; }
u32 exec_heap_total(void) { return MEM_SHELL_HEAP_SIZE; }
u32 exec_heap_used(void) { return 48; }
u32 kstack_high_water(void) { return 1024; }
u32 kmalloc_peak_bytes, resident_heap_peak, resident_heap_fail, exec_as_leftover_pages;
volatile u32 fault_kill_count, appslot_reclaim_count, ring3_stop_park_count;
u32 memmap_audit_runs, as_audit_runs, v86_return_audit_runs;
u32 memmap_audit_fail, as_audit_fail, v86_return_audit_fail;
int kselftest_pass, kselftest_fail;
#include "memstat_host_source.c"
i32 kapi_mem_stat(i32 id, void *out, u32 size) {
    reached++;
    return mem_stat_body(id, out, size);
}

static void named(int ok, const char *name)
{
    checks++;
    if (!ok) { SAY("FAIL: marker"); report("FAIL: ", 6); while (*name) report(name++, 1); SAY(""); die(1); }
}
#define EXPECT(name, killed, expr) do { \
    case_name = (name); reached = 0; \
    if (!__builtin_setjmp(escape)) { (void)(expr); named(!(killed) && reached == 1, case_name); } \
    else named((killed) && !reached, case_name); \
} while (0)

static void dispatch_early(u32 slot, const u32 *args_src)
{
    u32 nbytes = kapi_argsize[slot];
#include "early_host_source.c"
}

static void caller_copy_tests(void)
{
    CallerAccessFrame previous;
    u32 va = MEM_EXEC_LOAD_ADDR, tail = va + PAGE_SIZE - 1;
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 index = (va >> PAGE_SHIFT) % PTE_COUNT, saved = pt[index];
    named(caller_access_enter(&previous, CALLER_USER), "caller frame");
    u32 ls_args[] = {va, va, 0};
    u32 boot_args[] = {va, 0};
    u32 gui_args[] = {va, 0};
    u32 read_args[] = {99, 0, 64};
    u32 write_args[] = {1, 0, 64};
    EXPECT("sys_ls NULL ctx", 0, (dispatch_early(KAPI_SLOT_SYS_LS, ls_args), touch()));
    EXPECT("v86_boot2 NULL second", 0, (dispatch_early(KAPI_SLOT_V86_BOOT2, boot_args), touch()));
    EXPECT("gui_register NULL pump", 0, (dispatch_early(KAPI_SLOT_GUI_REGISTER, gui_args), touch()));
    ls_args[2] = 0x100000;
    EXPECT("sys_ls bad ctx", 1, (dispatch_early(KAPI_SLOT_SYS_LS, ls_args), touch()));
    named(!ring3_ptr_ok(0xA0000), "unleased VRAM refused");
    named(!(kapi_argptr[KAPI_SLOT_SYS_READ] & 2), "read delegated");
    named(!(kapi_argptr[KAPI_SLOT_SYS_WRITE] & 2), "write delegated");
    named(kapi_argptr[KAPI_SLOT_SYS_OPEN] & 1, "string early guard");
    EXPECT("output NULL", 1, wrap_sys_read(1, 0, 0x120000));
    EXPECT("dispatch output NULL", 1,
           (dispatch_early(KAPI_SLOT_SYS_READ, read_args), wrap_sys_read(99, 0, 64)));
    EXPECT("dispatch input NULL", 1,
           (dispatch_early(KAPI_SLOT_SYS_WRITE, write_args), wrap_sys_write(1, 0, 64)));
    EXPECT("fixed output NULL", 1, wrap_kcg_read_ank(1, 0));
    EXPECT("output empty NULL", 0, wrap_sys_read(1, 0, 0));
    EXPECT("output good", 0, wrap_sys_read(1, (void *)va, PAGE_SIZE));
    EXPECT("output tail", 1, wrap_sys_read(1, (void *)tail, 2));
    EXPECT("input good", 0, wrap_sys_write(1, (void *)va, PAGE_SIZE));
    EXPECT("input tail", 1, wrap_sys_write(1, (void *)tail, 2));
    EXPECT("input empty NULL", 0, wrap_sys_write(1, 0, 0));
    const u32 invalid[] = {0, 0xA0000, 0x100000, 0xfffffff0U};
    for (u32 i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        EXPECT("input bad band", 1, wrap_sys_write(1, (void *)invalid[i], 0x11f000));
        EXPECT("output bad band", 1, wrap_sys_read(1, (void *)invalid[i], 0x11f000));
    }
    EXPECT("signed output NULL", 1, wrap_np2_get_version(0, 4));
    EXPECT("signed output zero", 0, wrap_np2_get_version(0, 0));
    EXPECT("signed output negative", 0, wrap_np2_get_version(0, -1));
    EXPECT("signed output good", 0, wrap_np2_get_version((char *)va, 4));
    EXPECT("output length overflow", 1, wrap_sys_read(1, (void *)va, 0xffffffffU));
    /* No invalid case may enter the collector, even with a stale USER frame. */
    EXPECT("mem_stat NULL", 1, wrap_mem_stat(-1, 0, sizeof(MemStat)));
    EXPECT("mem_stat NP tail", 1, wrap_mem_stat(-1, (void *)tail, sizeof(MemStat)));
    EXPECT("mem_stat supervisor", 1, wrap_mem_stat(-1, (void *)MEM_SHELL_HEAP_BASE, sizeof(MemStat)));
    EXPECT("mem_stat overflow", 1, wrap_mem_stat(-1, (void *)va, ~0U));
    u32 generation = space.generation;
    space.generation++;
    EXPECT("mem_stat stale USER", 1, wrap_mem_stat(-1, (void *)va, sizeof(MemStat)));
    MemStat untouched;
    for (u32 i = 0; i < sizeof(untouched); i++) ((u8 *)&untouched)[i] = 0xa5;
    EXPECT("mem_stat stale zero", 0, named(wrap_mem_stat(-1, &untouched, 0) == OS32_ERR_INVAL, "mem_stat zero INVAL"));
    for (u32 i = 0; i < sizeof(untouched); i++) named(((u8 *)&untouched)[i] == 0xa5, "mem_stat zero unchanged");
    space.generation = generation;
    named(!paging_addrspace_map_user(&space, va + PAGE_SIZE, payload + PAGE_SIZE,
                                    PAGE_RW | PTE_USER), "map second page");
    /* Linux VA mapping supplies the host copyout destination. Product PTEs
     * above independently drive the real USER/RW range gate. */
    u32 map_args[6] = {va, 2 * PAGE_SIZE, 3, 0x32, ~0U, 0}, mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(map_args) : "memory");
    named(mapped == va, "mem_stat host output");
    MemStat expected;
    named(mem_stat_body(-1, &expected, sizeof(expected)) == sizeof(expected), "mem_stat control snapshot");
    EXPECT("mem_stat two pages", 0, named(wrap_mem_stat(-1, (void *)tail, sizeof(MemStat)) == sizeof(MemStat), "mem_stat return"));
    for (u32 i = 0; i < sizeof(expected); i++) named(((u8 *)tail)[i] == ((u8 *)&expected)[i], "mem_stat all bytes");
    EXPECT("input two pages", 0, wrap_sys_write(1, (void *)tail, 2));
    EXPECT("output two pages", 0, wrap_sys_read(1, (void *)tail, 2));
    pt[index + 1] &= ~PTE_RW;
    EXPECT("mem_stat RO tail", 1, wrap_mem_stat(-1, (void *)tail, sizeof(MemStat)));
    EXPECT("input RO tail", 0, wrap_sys_write(1, (void *)tail, 2));
    EXPECT("output RO tail", 1, wrap_sys_read(1, (void *)tail, 2));
    pt[index + 1] = 0;
    EXPECT("input length overflow", 1, wrap_sys_write(1, (void *)va, 0xffffffffU));
    slot.disk_write_authorized = 1;
    EXPECT("input multiplication", 1, wrap_ide_write_sectors(0, 0, 0x800001U, (void *)va));
    slot.disk_write_authorized = 0;
    reached = 0;
    named(wrap_ide_write_sector(0, 0, (void *)va) == -1 && reached == 0,
          "unauthorized disk write");
    slot.disk_write_authorized = exec_disk_write_path_allowed("/sbin/install.bin");
    EXPECT("IDE one", 0, wrap_ide_write_sector(0, 0, (void *)va));
    EXPECT("IDE tail", 1, wrap_ide_write_sector(0, 0, (void *)tail));
    reached = 0;
    named(wrap_ide_write_sectors(0, 0, 0, 0) == 0 && reached == 0, "IDE zero probe");
    for (disk.sect_size = 512; disk.sect_size <= 2048; disk.sect_size *= 2) {
        EXPECT("device NULL", 1, wrap_dev_blk_read("d", 0, 1, 0));
        EXPECT("device read zero", 0, wrap_dev_blk_read("d", 0, 0, 0));
        EXPECT("device write NULL", 1, wrap_dev_blk_write("d", 0, 1, 0));
        EXPECT("device write zero", 0, wrap_dev_blk_write("d", 0, 0, 0));
        EXPECT("device read", 0, wrap_dev_blk_read("d", 0, 1, (void *)(va + PAGE_SIZE - disk.sect_size)));
        EXPECT("device write", 0, wrap_dev_blk_write("d", 0, 1, (void *)(va + PAGE_SIZE - disk.sect_size)));
        EXPECT("device read tail", 1, wrap_dev_blk_read("d", 0, 1, (void *)(va + PAGE_SIZE - disk.sect_size + 1)));
        EXPECT("device write tail", 1, wrap_dev_blk_write("d", 0, 1, (void *)(va + PAGE_SIZE - disk.sect_size + 1)));
    }
    EXPECT("palette tail", 1, wrap_gfx_lease_palette(0, 1, (u8 *)tail));
    EXPECT("palette good", 0, wrap_gfx_lease_palette(0, 1, (u8 *)va));
    EXPECT("inject tail", 1, wrap_kbd_inject((u8 *)tail, 2));
    EXPECT("inject good", 0, wrap_kbd_inject((u8 *)va, 2));
    EXPECT("raster tail", 1, wrap_gfx_present_raster((void *)tail));
    EXPECT("raster NULL", 1, wrap_gfx_present_raster(0));
    EXPECT("raster good", 0, wrap_gfx_present_raster((void *)va));
    EXPECT("redirect tail", 1, wrap_sys_redirect_fd_buf(0, (u8 *)tail, 2, 2));
    EXPECT("redirect bad len", 1, wrap_sys_redirect_fd_buf(0, (u8 *)va, 8, PAGE_SIZE + 1));
    EXPECT("redirect good", 0, wrap_sys_redirect_fd_buf(0, (u8 *)va, 8, 8));
    EXPECT("multi-output atomic", 1, wrap_gfx_get_palette(0, (u8 *)va, 0, (u8 *)va));
    pt[index] &= ~PTE_RW;
    EXPECT("RO input", 0, wrap_sys_write(1, (void *)va, PAGE_SIZE));
    EXPECT("RO output", 1, wrap_sys_read(1, (void *)va, 1));
    pt[index] = saved;
    ring3_in_syscall = 0;
    EXPECT("trusted output", 0, wrap_sys_read(1, 0, 16));
    EXPECT("trusted input", 0, wrap_sys_write(1, 0, 16));
    ring3_in_syscall = 1; ring3_wm_depth = 1;
    EXPECT("WM output", 0, wrap_sys_read(1, 0, 16));
    EXPECT("WM input", 0, wrap_sys_write(1, 0, 16));
    ring3_wm_depth = 0;
    caller_access_leave(&previous);
    SAY("PASS: generated wrappers + real B1");
    char digits[12]; u32 n = (u32)checks, used = 0;
    do { digits[used++] = '0' + n % 10; n /= 10; } while (n);
    while (used) report(&digits[--used], 1);
    SAY(" conditions");
}
