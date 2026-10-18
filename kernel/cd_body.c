/* ATAPI and ISO9660 are one body: no cross-module media-generation pointer. */
#include "cd_module.h"
#include "atapi.h"
#include "iso9660.h"
#include "kprintf.h"

static int cd_read(Device *self, int lba, int count, void *buf)
{
    (void)self;
    return atapi_read_sectors((u32)lba, (u32)count, buf);
}

static Device cd_device = {
    .name = "cd0", .type = DEV_BLOCK, .bus_type = DEV_BUS_ATAPI,
    .sect_size = ATAPI_SECTOR_SIZE, .blk_read = cd_read
};

int cd_module_init(CdModuleApi *out)
{
    AtapiCapacity cap;
    out->fs = &iso9660_ops;
    out->device = 0;
    if (atapi_init() <= 0) {
        kprintf(0x07, "[cd] no ATAPI drive; root retained\n");
        return 0;
    }
    int rc = atapi_read_capacity(&cap);
    /* Keep the driver's spin-up/UA retries. NP21/W's empty tray reports
     * one sector; it cannot contain even the ISO primary descriptor. */
    if (!rc && (cap.total_sectors <= ISO_PVD_LBA || cap.sector_size != ATAPI_SECTOR_SIZE))
        rc = ATAPI_ERR_NO_MEDIA;
    if (rc) {
        kprintf(0x07, "[cd] medium unavailable rc=%d; root retained\n", rc);
    }
    /* The drive survives an empty tray. ATAPI reads do not use this bound;
     * zero only suppresses the boot-time automatic mount. */
    cd_device.total_sects = rc ? 0 : cap.total_sectors;
    cd_device.bus_id = (u8)atapi_drive_index();
    out->device = &cd_device;
    return 0;
}

const struct {
    u32 version;
    int (*initialize)(CdModuleApi *);
} cd_module_ops = {CD_MODULE_OPS_VERSION, cd_module_init};
