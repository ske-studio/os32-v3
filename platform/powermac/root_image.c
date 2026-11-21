#include "root_boot.h"
#include "bootinfo.h"
#include "crc32.h"
#include "endian_le.h"
#include "lz4.h"
#include "pgalloc.h"
#include "memmap.h"

extern const u8 __root_record_start[], __root_image_start[];

int pm_root_image(struct boot_root_image *image, u32 frame_bytes)
{
    struct boot_common info = {0};
    u32 packed = le32_rd(__root_record_start + 8);
    u32 length = le32_rd(__root_record_start + 12);
    u32 pfn;
    *image = (struct boot_root_image){0};
    if (!packed || packed > frame_bytes || !length || length > 0x7fffffff)
        return -1;
    u32 pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    if (!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, pages, MEM_POOL_BASE / PAGE_SIZE,
                               pgalloc_limit_pfn(), LEDGER_TOP_DOWN, &pfn))
        return -2;
    u8 *dst = P2V(pfn * PAGE_SIZE);
    int rc = -3;
    if (lz4_decode(__root_image_start, packed, dst, length) == (int)length) {
        u32 crc = crc32_calc(dst, length);
        if (crc == le32_rd(__root_record_start + 4)) {
            info.image_valid = 1;
            info.image_crc = crc;
            info.image_size = length;
        }
        rc = -4;
    }
    boot_common_set(&info, BOOTINFO_SRC_NONE);
    if (!info.image_valid) {
        pgalloc_free_n_owner(LEDGER_OWNER_BOOT, pfn, pages);
        return rc;
    }
    *image = (struct boot_root_image){dst, length};
    return 0;
}

int pm_root_release(const struct boot_root_image *image, int registered,
                    u32 frame_pfn, u32 pages, u32 *before, u32 *after)
{
    if (image->base && !registered &&
        !pgalloc_free_n_owner(LEDGER_OWNER_BOOT, V2P(image->base) / PAGE_SIZE,
                              (image->bytes + PAGE_SIZE - 1) / PAGE_SIZE))
        return -1;
    *before = pgalloc_free_pages();
    if (!pgalloc_free_n_owner(LEDGER_OWNER_BOOT, frame_pfn, pages))
        return -1;
    *after = pgalloc_free_pages();
    return *after - *before == pages ? 0 : -1;
}
