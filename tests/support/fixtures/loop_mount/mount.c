/* The disk and clock are synthetic. Both filesystems, the VFS descriptors
 * retained by losetup, and the PC-98 geometry resolver are the product code. */
#include "boot_names.h"
#include "bootinfo.h"
#include "dev.h"
#include "ext2.h"
#include "ext2_medium_contract.h"
#include "loop_contract.h"
#include "os32_kapi_shared.h"
#include "platform_device.h"
#include "kstring.h"
#include "kmalloc.h"
#include "vfs.h"
#include "vfs_stdin.h"
#include "fd_redirect.h"
#include "pc98pt.h"
#include "isofs/iso9660.h"

#define SECTORS 4096
static u8 disk[SECTORS][512];
static u8 backing[320 * 512];
static u8 payload[17 * EXT2_BLOCK_SIZE + 37];
static u8 readback[sizeof(payload)];
static u32 heap[KHEAP_SIZE / sizeof(u32)];
volatile u32 tick_count;
extern void write_gui_raw(const u32 *value);

static int cd_read(Device *dev, int lba, int count, void *buf)
{
    u8 *p = buf;
    if (lba != ISO_PVD_LBA || count != 1) return -1;
    kmemset(p, 0, ISO_SECTOR_SIZE);
    p[0] = ISO_PVD_TYPE_PRIMARY;
    kmemcpy(p + 1, ISO_PVD_MAGIC, 5);
    p[6] = ISO_PVD_VERSION;
    p[ISO_PVD_VOLUME_SIZE] = 32;
    p[ISO_PVD_BLOCK_SIZE + 1] = ISO_SECTOR_SIZE >> 8;
    p[ISO_PVD_ROOT_REC] = ISO_ROOT_REC_LEN;
    p[ISO_PVD_ROOT_REC + ISO_DR_EXTENT] = 20;
    p[ISO_PVD_ROOT_REC + ISO_DR_DATA_LEN + 1] = ISO_SECTOR_SIZE >> 8;
    p[ISO_PVD_ROOT_REC + ISO_DR_FLAGS] = ISO_FLAG_DIRECTORY;
    return 0;
}

static int trace_hd;
static void hd_request(int write, int lba, int count)
{
    if (!trace_hd) return;
    u32 request[3] = {write, lba, count};
    for (int i = 0; i < 3; i++) write_gui_raw(&request[i]);
}

static int disk_read(Device *d, int lba, int n, void *dst)
{
    if (lba < 0 || n < 0 || n > SECTORS - lba) return -1;
    hd_request(0, lba, n);
    kmemcpy(dst, disk[lba], n * 512);
    return 0;
}

static int disk_write(Device *d, int lba, int n, const void *src)
{
    if (lba < 0 || n < 0 || n > SECTORS - lba) return -1;
    hd_request(1, lba, n);
    kmemcpy(disk[lba], src, n * 512);
    return 0;
}

int platform_ide_drive_present(int drv) { return drv == 0; }
int platform_ide_range_ok(int drv, u32 lba, u32 count)
{
    return drv == 0 && lba < SECTORS && count <= SECTORS - lba;
}
int platform_disk_geometry_get(int drv, struct platform_disk_geometry *out)
{
    out->total_sectors = SECTORS;
    return drv == 0 ? 0 : -1;
}
int bootinfo_part_geom(int drv, u16 *heads, u16 *spt)
{
    *heads = 4;
    *spt = 8;
    return BOOTINFO_GEOM_BIOS;
}
u32 ext2_current_time(void) { return 12345; }
void kprintf(u8 attr, const char *fmt, ...) {}

extern int shell_loop_lifecycle(char *image);

int probe(void)
{
    static Device hd = {.name = "hd0", .blk_read = disk_read, .blk_write = disk_write};
    static Device cd = {.name = "cd0", .sect_size = ISO_SECTOR_SIZE,
                        .total_sects = 32, .blk_read = cd_read};
    int type, unit;
    char name[] = "lo0", prefix[] = "/lo0", path[] = "/hd0/img0.img";
    char file[] = "/lo0/new.txt", value[] = "loop0", result[sizeof(value)];
    kmalloc_init(heap, sizeof(heap));
    disk[1][PC98PT_OFF_MID] = PC98PT_MID_BOOTABLE;
    disk[1][PC98PT_OFF_SID] = PC98PT_SID_OS32;
    disk[1][PC98PT_OFF_SCYL] = 1;
    disk[1][PC98PT_OFF_ECYL] = 126;
    pc98_boot_names_bind();
    dev_register(&hd);
    loop_dev_init();
    ext2_medium_register(EXT2_MEDIUM_HD, &ext2_medium_ide_ops);
    ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops);
#ifdef HD_TRACE
    trace_hd = 1;
#endif
    if (ext2_init() || ext2_format(0, 4000) || vfs_mount("/hd0", "hd0", "ext2")) return 1;
#ifdef HD_TRACE
    for (unsigned i = 0; i < sizeof(payload); i++) payload[i] = (u8)i;
    if (vfs_write("/hd0/data", payload, sizeof(payload)) < 0 ||
        vfs_umount_checked("/hd0") || vfs_mount("/hd0", "hd0", "ext2")) return 20;
    int fd = vfs_open("/hd0/data", 0);
    if (fd < 0 || vfs_seek(fd, 13 * EXT2_BLOCK_SIZE + 13, 0) < 0 ||
        vfs_read_fd(fd, readback, 91) != 91 ||
        memcmp(readback, payload + 13 * EXT2_BLOCK_SIZE + 13, 91))
        return 21;
    vfs_close(fd);
    if (vfs_umount_checked("/hd0") || vfs_mount("/hd0", "hd0", "ext2")) return 20;
    if (vfs_read("/hd0/data", readback, sizeof(payload)) != sizeof(payload) ||
        memcmp(readback, payload, sizeof(payload))) return 21;
    return vfs_umount_checked("/hd0") != 0;
#endif
    if (vfs_dev_parse("lo4", &type, &unit) != VFS_ERR_INVAL ||
        vfs_dev_parse("lo00", &type, &unit) != VFS_ERR_INVAL) return 2;
    for (int i = 0; i < 4; i++) {
        name[2] = prefix[3] = path[8] = '0' + i;
        if (vfs_dev_parse(name, &type, &unit) || type != VFS_DEV_LOOP || unit != i) return 2;
        if (vfs_dev_mount_count(OS32_DRV_LOOP(i)) != 0) return 3;
        if (vfs_write(path, backing, sizeof(backing)) < 0) return 4;
        if (i == 0) {
            int rc = shell_loop_lifecycle(path);
            if (rc) return rc;
        }
        if (loop_dev_attach(path, i)) return 4;
        if (i == 0 && loop_dev_attach(path, 1) != LOOP_ERR_BUSY) return 11;
        if (vfs_umount_checked("/hd0") != VFS_ERR_BUSY) return 12;
        if (ext2_format(OS32_DRV_LOOP(i), 2880) || vfs_mount(prefix, name, "ext2")) return 5;
        if (vfs_dev_mount_count(OS32_DRV_LOOP(i)) != 1 || vfs_dev_mount_count(0) != 1) return 3;
        if (ext2_format(OS32_DRV_LOOP(i), 2880) != EXT2_ERR_BUSY ||
            ext2_format_at(OS32_DRV_LOOP(i), 0, 320) != EXT2_ERR_BUSY) return 13;
        loop_dev_detach(i);
        if (!loop_dev_status(i, 0, 0)) return 14;
        if (vfs_mount("/alias", name, "ext2") != VFS_ERR_EXIST) return 6;
    }
    dev_register(&cd);
    u32 before_iso = kmalloc_free();
    if (vfs_register_fs(&iso9660_ops) || vfs_mount("/cd", "cd0", "iso9660")) return 15;
    u32 available = kmalloc_free();
    if (before_iso - available < ISO_RA_BYTES + sizeof(Iso9660Ctx)) return 15;
    write_gui_raw(&available);
    /* Cross direct/indirect and partial-block boundaries on a nested write. */
    for (unsigned i = 0; i < sizeof(payload); i++) payload[i] = (u8)(i + i / 256);
    if (vfs_write("/lo2/data", payload, sizeof(payload)) < 0) return 7;
    for (int i = 0; i < 4; i++) {
        file[3] = value[4] = '0' + i;
        if (vfs_write(file, value, sizeof(value)) < 0) return 7;
    }
    for (int i = 0; i < 4; i++) {
        name[2] = prefix[3] = file[3] = value[4] = '0' + i;
        if (vfs_umount_checked(prefix) || vfs_dev_mount_count(OS32_DRV_LOOP(i)) != 0) return 3;
        if (vfs_mount(prefix, name, "ext2") || vfs_read(file, result, sizeof(result)) != sizeof(value) ||
            kstrncmp(result, value, sizeof(value))) return 8;
        if (i == 2 && (vfs_read("/lo2/data", readback, sizeof(readback)) != sizeof(payload) ||
                       memcmp(readback, payload, sizeof(payload)))) return 8;
        if (vfs_umount_checked(prefix)) return 9;
        loop_dev_detach(i);
        if (vfs_mount(prefix, name, "ext2") != VFS_ERR_IO) return 10;
    }
    return vfs_umount_checked("/hd0") != VFS_OK;
}

/* A regular file descriptor must never dispatch to terminal I/O. */
int res_owner_get(void) { return 1; }
int input_getchar_kapi(void) { __builtin_trap(); }
int fd_is_redirected(int fd) { __builtin_trap(); }
int fd_redirect_read(int fd, void *buf, u32 size) { __builtin_trap(); }
int fd_redirect_write(int fd, const void *buf, u32 size) { __builtin_trap(); }
void console_write(const char *buf, u32 size, u8 color) { __builtin_trap(); }
VfsStdinRead *exec_stdin_state(void) { __builtin_trap(); }
