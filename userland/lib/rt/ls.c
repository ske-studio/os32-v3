#include "ls.h"
extern KernelAPI *kapi;

static __attribute__((noinline)) int ls_windows(const char *path, DirCallback cb,
                                                void *ctx, int *use_legacy)
{
    OS32_LsPacket packet;
    char snapshot[OS32_MAX_PATH + 1];
    u32 skip = 0;
    u32 j = 0;
    /* Match the legacy CPL3 shim: callbacks may modify the supplied path. */
    if (path) {
        for (; j < OS32_MAX_PATH; j++) {
            snapshot[j] = path[j];
            if (!snapshot[j]) break;
        }
    }
    snapshot[j] = 0;
    for (;;) {
        u32 i;
        int rc = kapi->sys_ls_window(snapshot, skip, &packet);
        /* TRUSTED (resident shell) cannot use the USER-only value slot. */
        if (rc == OS32_ERR_INVAL && skip == 0) {
            *use_legacy = 1;
            return rc;
        }
        if (rc < 0) return rc;
        if (packet.count > OS32_LS_BATCH) return OS32_ERR_INVAL;
        for (i = 0; i < packet.count; i++) {
            DirEntry_Ext entry;
            unsigned int j;
            for (j = 0; j < sizeof(entry.name); j++)
                entry.name[j] = packet.entries[i].name[j];
            entry.size = packet.entries[i].size;
            entry.type = packet.entries[i].type;
            cb(&entry, ctx);
        }
        /* A backend error can accompany a full window: deliver the
         * remaining collected entries before returning it, as slot 12 does. */
        if (packet.done) return packet.result;
        if (!packet.count || skip > ~(u32)0 - packet.count)
            return OS32_ERR_INVAL;
        skip += packet.count;
    }
}

int os32_ls(const char *path, DirCallback cb, void *ctx)
{
    int use_legacy = 0;
    int rc = ls_windows(path, cb, ctx, &use_legacy);
    /* Release the large window frame before slot 12: the resident CPL0
     * shell has only a 40KB stack, shared with legacy FS and callbacks. */
    if (use_legacy) return kapi->sys_ls(path, (void *)cb, ctx);
    return rc;
}
