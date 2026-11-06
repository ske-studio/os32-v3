/* Device media boundary (NET_HOSTSVC_DESIGN #9/#10).
 * D5 connects these hooks to ISO operations. No ATAPI-global generation here. */
#include "iso9660.h"
#include "iso9660_media.h"
#include "dev.h"
#include "endian_le.h"

static void media_drop_caches(Iso9660Ctx *ctx)
{
    ctx->pc_valid = 0;
    ctx->ra_valid = 0;
    for (int i = 0; i < ISO_SCACHE_SLOTS; i++) ctx->scache[i].valid = 0;
    ctx->touched = 0;
    ctx->stats.drops++;
}

void iso9660_media_drop(void *raw)
{
    Iso9660Ctx *ctx = raw;
    if (!ctx) return;
    media_drop_caches(ctx);
    ctx->root_lba = 0;
    ctx->root_size = 0;
    ctx->volume_size = 0;
    /* A valid PVD always has a nonzero block size. Zero means refresh pending,
     * keeping the declared ctx layout and its outer lifetime unchanged. */
    ctx->block_size = 0;
}

int iso9660_resource_check(void *raw, u32 *generation)
{
    Iso9660Ctx *ctx = raw;
    Device *dev;
    u32 gen;
    int rc;

    if (!ctx) return VFS_ERR_NOMOUNT;
    dev = dev_find(ctx->devname);
    if (!dev) return VFS_ERR_IO;
    rc = dev_media_check(dev, &gen);
    if (rc) {
        if (rc == OS32_ERR_STALE) {
            vfs_fd_stale_mount(ctx);
            iso9660_media_drop(ctx);
        } else {
            media_drop_caches(ctx);
        }
        return rc;
    }
    if (generation) *generation = gen;
    if (gen != ctx->media_gen) {
        vfs_fd_stale_mount(ctx);
        iso9660_media_drop(ctx);
        ctx->media_gen = gen;
        return OS32_ERR_STALE;
    }
    return ctx->block_size ? 0 : OS32_ERR_STALE;
}

/* ISO sectors -> Device sectors. The shared Device wrapper owns u32/legacy
 * dispatch (#9); do not select ATAPI/netcd here. Used for rebuilding the PVD. */
static int media_read(Device *dev, u32 lba, u32 count, void *buf)
{
    u32 ratio;
    if (dev->sect_size <= 0 || dev->sect_size > ISO_SECTOR_SIZE ||
        ISO_SECTOR_SIZE % dev->sect_size) return VFS_ERR_IO;
    ratio = ISO_SECTOR_SIZE / (u32)dev->sect_size;
    if (lba > U32_MAX / ratio || count > (u32)S32_MAX / ratio ||
        (dev->bus_type != DEV_BUS_ATAPI &&
         (lba > dev->total_sects / ratio ||
          count > dev->total_sects / ratio - lba))) return VFS_ERR_IO;
    return dev_blk_read_lba(dev, lba * ratio, (int)(count * ratio), buf);
}

int iso9660_resource_refresh(void *raw)
{
    Iso9660Ctx *ctx = raw;
    Device *dev;
    u8 pvd[ISO_SECTOR_SIZE];
    u32 before, after, volume, root, size, sectors;
    int rc, checked;

    if (!ctx) return VFS_ERR_NOMOUNT;
    dev = dev_find(ctx->devname);
    if (!dev) return VFS_ERR_IO;
    /* Refresh replaces only internals. Old FDs must stay stale even when a
     * caller explicitly requests refresh before observing the new generation. */
    vfs_fd_stale_mount(ctx);
    iso9660_media_drop(ctx);
    rc = dev_media_check(dev, &before);
    if (rc) return rc;
    rc = media_read(dev, ISO_PVD_LBA, 1, pvd);
    checked = dev_media_check(dev, &after);
    if (checked) return checked;
    if (after != before) return OS32_ERR_STALE;
    if (rc) return rc;

    volume = le32_rd(pvd + ISO_PVD_VOLUME_SIZE);
    root = le32_rd(pvd + ISO_PVD_ROOT_REC + ISO_DR_EXTENT);
    size = le32_rd(pvd + ISO_PVD_ROOT_REC + ISO_DR_DATA_LEN);
    sectors = size / ISO_SECTOR_SIZE + (size % ISO_SECTOR_SIZE != 0);
    if (pvd[0] != ISO_PVD_TYPE_PRIMARY ||
        pvd[1] != ISO_PVD_MAGIC[0] || pvd[2] != ISO_PVD_MAGIC[1] ||
        pvd[3] != ISO_PVD_MAGIC[2] || pvd[4] != ISO_PVD_MAGIC[3] ||
        pvd[5] != ISO_PVD_MAGIC[4] || pvd[6] != ISO_PVD_VERSION ||
        le16_rd(pvd + ISO_PVD_BLOCK_SIZE) != ISO_SECTOR_SIZE ||
        volume <= ISO_PVD_LBA ||
        (dev->bus_type != DEV_BUS_ATAPI &&
         volume > dev->total_sects / (ISO_SECTOR_SIZE / dev->sect_size)) ||
        pvd[ISO_PVD_ROOT_REC] < ISO_ROOT_REC_LEN ||
        !(pvd[ISO_PVD_ROOT_REC + ISO_DR_FLAGS] & ISO_FLAG_DIRECTORY) ||
        !size || root >= volume || sectors > volume - root) return VFS_ERR_IO;

    ctx->root_lba = root;
    ctx->root_size = size;
    ctx->volume_size = volume;
    ctx->block_size = ISO_SECTOR_SIZE;
    ctx->media_gen = before;
    return 0;
}
