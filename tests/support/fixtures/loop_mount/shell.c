/* Use the product commands and bind their KAPI slots to the fixture kernel. */
#include "userland/shell/cmd_mnt.c"
#include "userland/shell/cmd_sys.c"
#include "userland/shell/cmd_fs_shared.c"

extern int vfs_mount(const char *, const char *, const char *);
extern int vfs_is_mounted(const char *);
extern int vfs_umount_checked(const char *);
extern int loop_dev_attach(const char *, int);
extern void loop_dev_detach(int);
extern int loop_dev_status(int, u32 *, int *);
extern int ext2_format(int, u32);

static int busy_shown;
static void record_message(u8 attr, const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    if (!strcmp(fmt, "losetup: lo%d is busy\n"))
        busy_shown = 1;
    else if (!strcmp(fmt, "Format failed: %s (%d)\n"))
        busy_shown = !strcmp(__builtin_va_arg(args, const char *),
                             "device busy or reservation unavailable");
    __builtin_va_end(args);
}

static KernelAPI api = {
    .sys_mount = vfs_mount, .sys_is_mounted = vfs_is_mounted,
    .sys_umount_checked = vfs_umount_checked, .kprintf = record_message,
    .loop_attach = loop_dev_attach, .loop_detach = loop_dev_detach,
    .loop_status = loop_dev_status, .ext2_format = ext2_format
};
KernelAPI *g_api = &api;
void shell_print_help(const char *name) { __builtin_trap(); }

int shell_loop_lifecycle(char *image)
{
    char *attach[] = {"losetup", image, "0"};
    char *alias[] = {"losetup", image, "1"};
    char *format[] = {"format", "lo0"};
    char *mount[] = {"mount", "/lo0", "lo0", "ext2"};
    char *backing[] = {"umount", "/hd0"};
    char *detach[] = {"losetup", "-d", "0"};
    char *umount[] = {"umount", "/lo0"};
    if (cmd_losetup(3, attach)) return 4;
    if (cmd_losetup(3, alias) != SH_STATUS_ERROR) return 11;
    if (cmd_umount(2, backing) != SH_STATUS_ERROR) return 12;
    if (cmd_format(2, format) || cmd_mount(4, mount)) return 5;
    if (cmd_format(2, format) != SH_STATUS_ERROR || !busy_shown) return 13;
    busy_shown = 0;
    if (cmd_losetup(3, detach) != SH_STATUS_ERROR || !busy_shown) return 14;
    if (cmd_umount(2, umount) || cmd_losetup(3, detach)) return 9;
    return 0;
}
