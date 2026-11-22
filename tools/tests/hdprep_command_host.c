/* Exercise the registered resident command with a memory-backed disk. */
#include <assert.h>
#include <string.h>
#include "userland/shell/shell.h"

static KernelAPI api;
KernelAPI *g_api = &api;
static CmdHandler command;
static const char *answer;
static int corrupt_probe;
static char trace[64];
static int events;
static unsigned char disk[20000][512], original[sizeof(disk)], file[1024];
static u32 probe;

void shell_register_cmds(const ShellCmd *cmds)
{
    assert(strcmp(cmds[0].name, "hdprep") == 0);
    command = cmds[0].handler;
}
void shell_print_help(const char *name) { (void)name; }
static void print(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
static void put(char c, u8 attr) { (void)c; (void)attr; }
static int key(void) { assert(*answer); return *answer++; }
static void record(char c) { assert(events < 63); trace[events++] = c; trace[events] = 0; }
static int geometry(int drv, HddGeom *g)
{
    assert(drv == 0);
    memset(g, 0, sizeof(*g));
    g->ata_present = g->bios_queried = g->bios_valid = 1;
    g->addr_mode = HDD_AMODE_LBA28;
    g->ata_total = 20000;
    g->bios_cyl = 147;
    g->bios_heads = 8;
    g->bios_spt = 17;
    g->bios_seclen = 512;
    return 0;
}
static int read_sector(int drv, u32 lba, void *buf)
{
    assert(drv == 0 && lba < 20000);
    record(lba == 0 ? '0' : lba == 1 ? '1' : 'r');
    memcpy(buf, disk[lba], 512);
    if (lba > 1 && corrupt_probe) ((u8 *)buf)[511] ^= 1;
    return 0;
}
static int write_sector(int drv, u32 lba, const void *buf)
{
    assert(drv == 0 && lba < 20000);
    record(lba == 1 ? 'T' : 'P');
    if (lba != 1) probe = lba;
    memcpy(disk[lba], buf, 512);
    return 0;
}
static int format(int drv, u32 start, u32 len)
{
    assert(drv == 0 && start == 1632 && len && probe == start + len - 1);
    record('F');
    return 0;
}
static const char *device(const char *path) { assert(strcmp(path, "/") == 0); return "fd0"; }
static int mounts(int drv) { assert(drv == 0); return 0; }
static int mount_fs(const char *path, const char *dev, const char *fs)
{
    assert(!strcmp(path, "/hd0") && !strcmp(dev, "hd0") && !strcmp(fs, "ext2"));
    record('M');
    return 0;
}
static int open_file(const char *path, int mode)
{
    (void)mode;
    assert(!strcmp(path, "/hd0/.hdprep"));
    return 3;
}
static int write_file(int fd, const void *buf, u32 len)
{
    assert(fd == 3 && len == sizeof(file));
    memcpy(file, buf, len);
    record('W');
    return len;
}
static int read_file(int fd, void *buf, u32 len)
{
    assert(fd == 3 && len == sizeof(file));
    memcpy(buf, file, len);
    record('R');
    return len;
}
static void close_file(int fd) { assert(fd == 3); }
static int unlink_file(const char *path)
{
    assert(!strcmp(path, "/hd0/.hdprep"));
    record('U');
    return 0;
}
static void run(const char *input, int status, const char *expected)
{
    char *argv[] = {"hdprep"};
    answer = input;
    events = 0;
    trace[0] = 0;
    assert(command(1, argv) == status);
    assert(!strcmp(trace, expected));
}

int main(void)
{
    api.kprintf = print;
    api.shell_putchar = put;
    api.kbd_getchar = key;
    api.hdd_geom_info = geometry;
    api.ide_read_sector = read_sector;
    api.ide_write_sector = write_sector;
    api.vfs_devname = device;
    api.dev_mount_count = mounts;
    api.ext2_format_at = format;
    api.sys_mount = mount_fs;
    api.sys_open = open_file;
    api.sys_write = write_file;
    api.sys_read = read_file;
    api.sys_close = close_file;
    api.sys_unlink = unlink_file;
    shell_cmd_hdprep_init();
    disk[0][510] = 0x55;
    disk[0][511] = 0xaa;
    memcpy(original, disk, sizeof(disk));
    run("", SH_STATUS_ERROR, "01");
    assert(!memcmp(original, disk, sizeof(disk)));
    disk[0][510] = disk[0][511] = 0;
    memcpy(original, disk, sizeof(disk));
    run("no\n", SH_STATUS_USAGE, "01");
    assert(!memcmp(original, disk, sizeof(disk)));
    corrupt_probe = 1;
    run("yes\n", SH_STATUS_ERROR, "01Pr");
    assert(probe < 20000 && !memcmp(disk[1], original + 512, 512));
    corrupt_probe = 0;
    run("yes\n", SH_STATUS_OK, "01PrFT1MWRU");
    return 0;
}
