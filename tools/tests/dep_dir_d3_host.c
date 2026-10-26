/* A/F: real VFS and platform names, with the existing fake ext2 device edge. */
#define main vfs_mount_fixture_main
#include "vfs_mount_dev_host.c"
#undef main

static int legacy(const char *name, int *type, int *id)
{
    if ((name[0] == 'h' || name[0] == 'f' || name[0] == 'c') &&
        name[1] == 'd' && name[2] >= '0' && name[2] <= '3') {
        *type = name[0] == 'h' ? VFS_DEV_HD :
                name[0] == 'f' ? VFS_DEV_FD : VFS_DEV_CD;
        *id = name[2] - '0';
        return VFS_OK;
    }
    if (!strncmp(name, "COM", 3)) {
        *type = VFS_DEV_SERIAL; *id = 1; return VFS_OK;
    }
    if (!strcmp(name, "hostdrv")) {
        *type = VFS_DEV_HOSTDRV; *id = 0; return VFS_OK;
    }
    return VFS_ERR_INVAL;
}

static void compare(const char *name)
{
    int ot = -99, oi = -98, nt = ot, ni = oi;
    int rc = legacy(name, &ot, &oi);
    CHECK(vfs_dev_parse(name, &nt, &ni) == rc);
    CHECK(ot == nt && oi == ni); /* Failure leaves both outputs untouched. */
}

int main(void)
{
    const char *prefixes[] = {"hd", "fd", "cd", "HD", "FD", "CD", "lo"};
    const char *tails[] = {"", "suffix", "0", "/part"};
    const char *special[] = {"", "h", "f", "c", "C", "CO", "hd", "fd", "cd",
        "hd-1", "fd-1", "cd-1", "COM", "COM0", "COM1", "COM9", "COMxyz",
        "com1", "Com1", "hostdrv", "hostdrv0", "HOSTDRV", "hostdrv/", "loop0"};
    int type = -99, id = -98;
    CHECK(VFS_DEV_UNIT_MAX == 4);
    /* Count limits are independent of registration and device presence (F11). */
    CHECK(vfs_dev_mount_count(-1) == VFS_ERR_INVAL);
    CHECK(vfs_dev_mount_count(4) == VFS_ERR_INVAL);
    for (int i = 0; i < 4; i++) CHECK(vfs_dev_mount_count(i) == 0);
    CHECK(vfs_dev_parse("hd0", &type, &id) == VFS_ERR_INVAL);
    CHECK(type == -99 && id == -98);
    pc98_boot_names_bind();
    for (unsigned p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++)
        for (int digit = 0; digit <= 9; digit++)
            for (unsigned t = 0; t < sizeof(tails) / sizeof(tails[0]); t++) {
                char name[32];
                snprintf(name, sizeof(name), "%s%d%s", prefixes[p], digit, tails[t]);
                compare(name);
            }
    for (unsigned i = 0; i < sizeof(special) / sizeof(special[0]); i++) compare(special[i]);
    reset(); /* Ordinary FS fixture initialization must preserve names. */
    CHECK(vfs_mount("/", "hd0suffix", "ext2") == VFS_OK);
    CHECK(vfs_mount("/again", "hd0", "ext2") == VFS_ERR_EXIST);
    CHECK(vfs_dev_mount_count(0) == 1 && vfs_dev_mount_count(3) == 0);
    pc98_boot_names_bind(); /* Same resident table, no state reset. */
    CHECK(vfs_dev_mount_count(0) == 1);
    CHECK(vfs_mount("/disk1", "hd1", "ext2") == VFS_OK);
    CHECK(vfs_dev_mount_count(1) == 1);
    vfs_umount("/disk1");
    CHECK(vfs_dev_mount_count(1) == 0);
    compare("hd3suffix"); compare("fd3"); compare("cd3");
    puts("PASS D3a A/F: legacy names, lifetime, missing table, rebind, duplicate/count");
    return 0;
}
