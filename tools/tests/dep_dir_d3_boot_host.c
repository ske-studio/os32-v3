/* Run the product table/dispatcher/wrappers with observable device boundaries. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include "boot_platform.h"
#include "boot_display.h"
#include "pc98.h"
#include "tvram.h"
#include "fdc.h"
#include "ide.h"
#include "dev.h"
#include "vfs.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static char events[8192];
static int used, irq_on, failing, disk_count;
static u32 media_drive;
static int host_present, mount_fail, root_fail, media_144 = 1;
static int child_mode, halted;
static jmp_buf stop;
static void _halt(void) { halted++; longjmp(stop, 1); }
static void event(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    used += vsnprintf(events + used, sizeof(events) - used, fmt, ap);
    va_end(ap);
    CHECK(used < (int)sizeof(events));
}
/* Exercise the shared renderer and formatting against fake TVRAM. */
static unsigned char chars[TVRAM_ROWS * TVRAM_BPR];
static unsigned char attrs[TVRAM_ROWS * TVRAM_BPR];
static void *fake_tvram(u32 addr)
{
    if (addr >= TVRAM_ATTR && addr < TVRAM_ATTR + sizeof(attrs))
        return attrs + addr - TVRAM_ATTR;
    CHECK(addr >= TVRAM_BASE && addr < TVRAM_BASE + sizeof(chars));
    return chars + addr - TVRAM_BASE;
}
#define P2V_IO(addr) fake_tvram(addr)
static int kutoa_dec(u32 v, char *b, int n) { return snprintf(b, n, "%lu", v); }
static int kutoa_hex(u32 v, char *b, int n, int upper)
{ return snprintf(b, n, upper ? "%lX" : "%lx", v); }
#define tvram_print render_tvram
#include "boot_display_source.inc"
#undef tvram_print
void tvram_print(int x, int y, const char *s, u8 attr)
{ event("T%d,%d,%u:%s;", x, y, attr, s); render_tvram(x, y, s, attr); }
void kprintf(u8 attr, const char *fmt, ...)
{
    char line[128]; va_list ap;
    va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    event("L%u:%s;", attr, line);
}
static void pc98_d2_bind(void) { event("bind-d2;"); }
static void pc98_boot_disk_bind(void) { event("bind-disk;"); }
static void pc98_boot_names_bind(void) { event("bind-names;"); }
static void dma8237_init(void) { CHECK(!irq_on); event("dma;"); }
static void kbd_init(void) { CHECK(irq_on); event("kbd;"); }
static void mouse_init(void) { event("mouse;"); }
static void _enable(void) { irq_on = 1; event("enable;"); }
int fdc_init(void) { CHECK(irq_on); event("fdc;"); return failing ? -1 : 0; }
int fdc_get_last_init_status(u8 *st, u8 *before, u8 *after)
{ *st=0x12; *before=0x34; *after=0x56; event("fdc-status;"); return failing ? -1 : 0; }
int fdc_set_media_by_daua(int unit, u32 drive)
{ CHECK(unit == 0 && drive == media_drive); event("media;"); return -1; }
static unsigned int inp(unsigned int port) { CHECK(port == FDC_IO_3MODE); event("inp;"); return media_144 ? FDC_3M_CUR_144 : 0; }
void dev_init(void) { event("dev;"); }
static void loop_dev_init(void) { event("loop;"); }
static void path_init(void) { event("path;"); }
static int ext2_init(void) { event("ext2;"); return failing == 1 ? -1 : VFS_OK; }
static int fatfs_init(void) { event("fat;"); return failing == 2 ? -1 : VFS_OK; }
static int hostdrvfs_init(void) { event("hostfs;"); return failing == 3 ? -1 : VFS_OK; }
static int serialfs_init(void) { event("serialfs;"); return failing == 4 ? -1 : VFS_OK; }
static Device children[] = {
    {.name="fd0", .type=DEV_BLOCK, .sect_size=512, .total_sects=1},
    {.name="hd0", .type=DEV_BLOCK, .sect_size=512, .total_sects=1},
    {.name="cd0", .type=DEV_BLOCK, .sect_size=2048, .total_sects=1},
    {.name="empty", .type=DEV_BLOCK},
    {.name="char", .type=DEV_CHAR},
    {.name="no-size", .type=DEV_BLOCK, .total_sects=1},
    {.name="no-sects", .type=DEV_BLOCK, .sect_size=512},
};
int dev_get_names(const char **names, int max)
{
    CHECK(max >= 8);
    for (unsigned i=0; i<sizeof(children)/sizeof(children[0]); i++) names[i]=children[i].name;
    names[7]="missing";
    return 8;
}
Device *dev_find(const char *name)
{
    if (!child_mode) { CHECK(!strcmp(name, "test")); return NULL; }
    for (unsigned i=0; i<sizeof(children)/sizeof(children[0]); i++)
        if (!strcmp(name, children[i].name)) return &children[i];
    return NULL;
}
static void path_set_device_validator(int (*fn)(const char *)) { CHECK(fn("test") == 0); event("validator;"); }
static void pci_init(void) { event("pci;"); }
int ide_init(void) { event("ide;"); return disk_count; }
int ide_drive_present(int d) { event("present%d;", d); return d == 0 || d == 2; }
void dev_register_hdd(int d) { event("hdd%d;", d); }
static int hostdrvfs_detect(void) { event("host-detect;"); return host_present; }
int vfs_mount(const char *p, const char *d, const char *f)
{
    event("mount:%s,%s,%s;", p, d, f);
    if (child_mode) return mount_fail ? VFS_ERR_IO : VFS_OK;
    if (!strcmp(p, "/")) return root_fail ? VFS_ERR_IO : VFS_OK;
    CHECK(!strcmp(p, "/host") && !strcmp(d, "hostdrv") && !strcmp(f, "hostdrv"));
    return mount_fail ? VFS_ERR_IO : VFS_OK;
}
static void rtc_init(void) { event("rtc;"); }
static void kcg_init(void) { event("kcg;"); }
static void pc98_boot_pci_resources(void) { event("resources;"); }
static int lgy98_init(void) { event("lgy;"); return -1; }
#include "boot_platform_source.inc"
#include "pc98_boot_source.inc"
#include "kernel_mount_source.inc"

static void expect(const char *s) { if (strcmp(events, s)) fprintf(stderr, "got: %s\nwant: %s\n", events, s); CHECK(!strcmp(events, s)); used=0; events[0]=0; }
int main(void)
{
    struct boot_context ctx;
    char number[64];
    char *end = tv_cat_dec(number, -2147483647 - 1);
    CHECK(end == number + strlen(number) && !strcmp(number, "-2147483648"));
    tv_cat_hex2(number, 0x0f); CHECK(!strcmp(number, "0f"));
    render_tvram(TVRAM_COLS - 1, 0, "AB", TATTR_GREEN);
    CHECK(chars[(TVRAM_COLS-1)*TVRAM_CELL_BYTES] == 'A');
    CHECK(chars[TVRAM_BPR] == 'B' && attrs[TVRAM_BPR] == TATTR_GREEN);
    render_tvram(TVRAM_COLS - 1, TVRAM_ROWS - 1, "CD", TATTR_RED);
    CHECK(chars[(TVRAM_ROWS-1)*TVRAM_BPR+(TVRAM_COLS-1)*TVRAM_CELL_BYTES] == 'C');
    pc98_boot_bind(&ctx, BOOT_DRIVE_FDD_144);
    expect("bind-d2;bind-disk;bind-names;");
    CHECK(ctx.drive == BOOT_DRIVE_FDD_144);
    for (int i=0; i<BOOT_POINT_COUNT; i++) CHECK(boot_ops->step[i]);
    pc98_boot_bind(&ctx, BOOT_DRIVE_FDD_144);
    expect("bind-d2;bind-disk;bind-names;");
    boot_platform_run(BOOT_DMA, &ctx); expect("dma;");
    irq_on=1;
    boot_platform_run(BOOT_INPUT, &ctx);
    expect("T48,1,129:KBD...;kbd;T54,1,225:OK;mouse;");
    media_drive=ctx.drive;
    for (failing=0; failing<5; failing++) {
        char want[1024];
        boot_platform_run(BOOT_FLOPPY, &ctx);
        snprintf(want, sizeof(want), "T60,1,%u:FDC...;enable;fdc;T66,1,%u:%s;fdc-status;T0,%d,%u:FDC rc=%d st0=12 0439h=34->56;L%u:[fdc] FDC rc=%d st0=12 0439h=34->56\n;media;inp;T69,1,%u:144;T72,1,%u:M;", TATTR_GREEN, failing?TATTR_RED:TATTR_WHITE, failing?"ER":"OK", TVRAM_ROWS-1, failing?TATTR_YELLOW:TATTR_WHITE, failing?-1:0, TATTR_WHITE, failing?-1:0, TATTR_GREEN, TATTR_WHITE);
        expect(want);
        boot_platform_run(BOOT_DEV_FS, &ctx);
        snprintf(want, sizeof(want), "T58,1,%u:DEV...;dev;loop;path;ext2;fat;hostfs;serialfs;validator;T64,1,%u:%s;%s", TATTR_GREEN, TATTR_WHITE, failing?"NG":"OK", failing?"L225:VFS: filesystem registration failed\n;":"");
        expect(want);
    }
    boot_platform_run(BOOT_PCI_ENUM, &ctx); expect("pci;");
    disk_count=1;
    boot_platform_run(BOOT_DISK, &ctx);
    expect("T0,3,129:IDE...;ide;T5,3,225:OK;present0;hdd0;present1;present2;hdd2;present3;");
    disk_count=0;
    boot_platform_run(BOOT_DISK, &ctx); expect("T0,3,129:IDE...;ide;T5,3,161:no drive;");
    boot_platform_run(BOOT_AUTO_MOUNT, &ctx); expect("host-detect;T32,3,161:HDRV --;");
    boot_platform_run(BOOT_CLOCK, &ctx); expect("T26,3,129:RTC...;rtc;T32,3,225:OK;");
    boot_platform_run(BOOT_FONT, &ctx); expect("kcg;");
    boot_platform_run(BOOT_PCI_RESOURCES, &ctx); expect("resources;");
    boot_platform_run(BOOT_NIC_LEGACY, &ctx); expect("lgy;");
    /* Loader drive is preserved; root uses exact equality, display uses type. */
    for (unsigned drive=0; drive<256; drive++) {
        pc98_boot_bind(&ctx, drive);
        expect("bind-d2;bind-disk;bind-names;");
        int fd = drive == 0x90 || drive == 0x30;
        CHECK(ctx.drive == drive);
        CHECK(!strcmp(ctx.root_dev, fd ? "fd0" : "hd0"));
        CHECK(!strcmp(ctx.root_fs, fd ? "fat" : "ext2"));
        media_drive=drive;
        failing=0;
        for (media_144=0; media_144<2; media_144++) {
            boot_platform_run(BOOT_FLOPPY, &ctx);
            int show = (drive & 0xf0) == 0x30 || (drive & 0xf0) == 0xb0;
            CHECK((strstr(events, "inp;") != NULL) == show);
            if (show) CHECK(strstr(events, media_144 ? ":M;" : ":?;"));
            CHECK(strstr(events, "media;"));
            used=0; events[0]=0;
        }
        for (host_present=0; host_present<2; host_present++) {
            for (mount_fail=0; mount_fail<2; mount_fail++) {
                host_root_mount(ctx);
                char want[256];
                snprintf(want, sizeof(want), "mount:/,%s,%s;T23,3,225:root OK;host-detect;%sT32,3,%u:HDRV %s;",
                         ctx.root_dev, ctx.root_fs,
                         host_present ? "mount:/host,hostdrv,hostdrv;" : "",
                         host_present ? (mount_fail ? TATTR_RED : TATTR_WHITE) : TATTR_CYAN,
                         host_present ? (mount_fail ? "NG" : "OK") : "--");
                expect(want);
            }
        }
        root_fail=1; halted=0;
        if (!setjmp(stop)) { host_root_mount(ctx); CHECK(0); }
        CHECK(halted == 1 && !strstr(events, "host-detect;"));
        CHECK(strstr(events, "root panic"));
        used=0; events[0]=0; root_fail=0;
    }
    child_mode=1;
    mount_fail=1;
    kernel_mount_children("hd0");
    expect("mount:/fd0,fd0,ext2;mount:/fd0,fd0,iso9660;mount:/fd0,fd0,fat;mount:/cd0,cd0,iso9660;");
    kernel_mount_children("fd0");
    expect("mount:/hd0,hd0,ext2;mount:/hd0,hd0,iso9660;mount:/hd0,hd0,fat;mount:/cd0,cd0,iso9660;");
    mount_fail=0;
    kernel_mount_children("fd0");
    expect("mount:/hd0,hd0,ext2;mount:/cd0,cd0,iso9660;");
    puts("PASS D3c B/C real boot/root/children: all 256 drives, failures, order, display, IF");
    return 0;
}
