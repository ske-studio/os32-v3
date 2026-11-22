#ifndef KERNEL_MOUNT_CHILDREN_H
#define KERNEL_MOUNT_CHILDREN_H

#include "dev.h"
#include "vfs.h"

/* ルート以外のブロックデバイスを "/<名前>" にマウントする。ルートは起動情報で選んだ名前
 * を受け取り除く。CD は iso9660 だけ、他は ext2 → iso9660 → fat の
 * 順に試し、失敗しても起動は続ける。 */
static inline void kernel_mount_children(const char *root_dev)
{
    {
        const char *names[MAX_DEVICES];
        int num = dev_get_names(names, MAX_DEVICES);
        /* 登録済みの部品を含めてサブマウント (ルート以外) */
        for (int i = 0; i < num; i++) {
            const char *dname = names[i];
            char mnt[VFS_MNTPATH_MAX];
            int j;
            Device *d = dev_find(dname);

            if (!d || d->type != DEV_BLOCK) continue;
            if (!d->sect_size || !d->total_sects) continue;

            /* ルートと一致する場合はスキップ */
            {
                int match = 1;
                for (j = 0; root_dev[j] || dname[j]; j++) {
                    if (root_dev[j] != dname[j]) { match = 0; break; }
                }
                if (match) continue;
            }

            /* パス生成: "/dname/" */
            mnt[0] = '/';
            for (j = 0; dname[j] && j < VFS_MNTPATH_MAX - 4; j++) {
                mnt[1 + j] = dname[j];
            }
            mnt[1 + j] = '\0';

            /* フォールバックしながらマウント試行 */
            {
                int mounted = 0;
                /* CDデバイスにはiso9660のみ試行 */
                if (dname[0] == 'c' && dname[1] == 'd') {
                    if (vfs_mount(mnt, dname, "iso9660") == VFS_OK) mounted = 1;
                } else {
                    if (vfs_mount(mnt, dname, "ext2") == VFS_OK) mounted = 1;
                    if (!mounted) {
                        if (vfs_mount(mnt, dname, "iso9660") == VFS_OK) mounted = 1;
                    }
                    if (!mounted) {
                        vfs_mount(mnt, dname, "fat");
                    }
                }
            }
        }
    }
}

#endif
