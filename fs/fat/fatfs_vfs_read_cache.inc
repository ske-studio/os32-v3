/* Per-mount read-only FIL lifetime; included before FatFsCtx.
 * Contract: docs/design/fs/fat/fatfs_vfs_read_cache.md */
#include "fdc.h"

#if FF_FS_LOCK != 0
#error "fatfs_vfs_read_cache requires FF_FS_LOCK=0"
#endif

#define FAT_READ_CACHE_SLOTS 4

typedef struct {
    FIL fil;
    char path[VFS_MAX_PATH];
    int valid;
    int rank; /* 0 = most recently used; bounded by slot count */
} FatReadSlot;

typedef struct {
    FatReadSlot slots[FAT_READ_CACHE_SLOTS];
    u32 media_gen;
} FatReadCache;

static void rc_drop(FatReadCache *rc, FatReadSlot *slot)
{
    if (slot->valid) {
        (void)f_close(&slot->fil);
        slot->valid = 0;
        /* Keep distinct compact ranks after dropping an arbitrary failed FIL. */
        for (int i = 0; i < FAT_READ_CACHE_SLOTS; i++) {
            FatReadSlot *other = &rc->slots[i];
            if (other->valid && other->rank > slot->rank)
                other->rank--;
        }
    }
    slot->rank = 0;
}

static void rc_invalidate_all(FatReadCache *rc)
{
    for (int i = 0; i < FAT_READ_CACHE_SLOTS; i++)
        rc_drop(rc, &rc->slots[i]);
}

static void rc_drop_on_umount(FatReadCache *rc)
{
    rc_invalidate_all(rc);
    rc->media_gen = 0;
}

static FRESULT rc_failure(FatReadCache *rc, FatReadSlot *slot, FRESULT fr)
{
    if (fr == FR_DISK_ERR || fr == FR_NOT_READY ||
        fr == FR_INVALID_OBJECT || fr == FR_INT_ERR)
        rc_invalidate_all(rc);
    else
        rc_drop(rc, slot);
    return fr;
}

static void rc_touch(FatReadCache *rc, FatReadSlot *slot)
{
    for (int i = 0; i < FAT_READ_CACHE_SLOTS; i++) {
        FatReadSlot *other = &rc->slots[i];
        if (other->valid && other != slot && other->rank < slot->rank)
            other->rank++;
    }
    slot->rank = 0;
}

static FRESULT rc_read(FatReadCache *rc, int dev_id, const char *fpath,
                       void *buf, UINT size, FSIZE_t offset, UINT *br)
{
    FatReadSlot *slot = 0;
    FRESULT fr;

    *br = 0;
    if (VFS_MOUNT_DEV_TYPE(dev_id) == VFS_DEV_FD) {
        u32 gen = fdc_media_gen(VFS_MOUNT_DEV_ID(dev_id));
        if (gen != rc->media_gen)
            rc_invalidate_all(rc);
        /* Keep the entry generation even if it changes during this read. */
        rc->media_gen = gen;
    }

    for (int i = 0; i < FAT_READ_CACHE_SLOTS; i++) {
        if (rc->slots[i].valid && kstrcmp(rc->slots[i].path, fpath) == 0) {
            slot = &rc->slots[i];
            break;
        }
    }
    if (!slot) {
        slot = &rc->slots[0];
        for (int i = 0; i < FAT_READ_CACHE_SLOTS; i++) {
            FatReadSlot *candidate = &rc->slots[i];
            if (!candidate->valid) {
                slot = candidate;
                break;
            }
            if (candidate->rank > slot->rank) slot = candidate;
        }
        rc_drop(rc, slot);
        fr = f_open(&slot->fil, fpath, FA_READ);
        if (fr != FR_OK) return rc_failure(rc, slot, fr);
        kstrncpy(slot->path, fpath, sizeof(slot->path));
        slot->valid = 1;
        slot->rank = FAT_READ_CACHE_SLOTS;
    }
    rc_touch(rc, slot);
    if (f_tell(&slot->fil) != offset) {
        fr = f_lseek(&slot->fil, offset);
        if (fr != FR_OK) return rc_failure(rc, slot, fr);
    }
    fr = f_read(&slot->fil, buf, size, br);
    if (fr != FR_OK) return rc_failure(rc, slot, fr);
    return FR_OK;
}
