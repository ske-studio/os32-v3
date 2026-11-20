/* Uses the ext2_part_host RAM disk for the backing file. */
#include "loop_contract.h"

static int active_slot, attached, writable, sector_bytes, status_calls;

int loop_dev_status(int slot, u32 *total, int *bps)
{
    status_calls++;
    if (!attached || slot != active_slot) return 0;
    if (total) *total = 320;
    if (bps) *bps = sector_bytes;
    return 1;
}

int loop_dev_lba_writable(int slot)
{
    return slot == active_slot && writable;
}

int loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf)
{
    CHECK(slot == active_slot && lba < 320 && count <= 320 - lba);
    return dev_blk_read_lba(&g_hd0, lba, count, buf);
}

int loop_dev_write_lba(int slot, u32 lba, u32 count, const void *buf)
{
    CHECK(slot == active_slot && lba < 320 && count <= 320 - lba);
    return dev_blk_write_lba(&g_hd0, lba, count, buf);
}

static void check_loop_super(u32 base, u32 length)
{
    Ext2Layout layout;
    const u8 *sb = g_disk + (base + 2) * 512;
    CHECK(ext2_layout_plan(length, EXT2_MAX_GROUPS, &layout) == EXT2L_OK);
    CHECK(le16_rd(sb + EXT2_SB_MAGIC_OFF) == EXT2_SUPER_MAGIC);
    CHECK(le32_rd(sb + EXT2_SB_BLOCKS_COUNT_OFF) == layout.total_blocks);
    CHECK(le32_rd(sb + EXT2_SB_INODES_COUNT_OFF) == layout.inodes_count);
}

static void case_medium(void)
{
    const int invalid[] = {0x05ff, 0x0604, 0x10600, 0x10000, -1};
    CHECK(ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops) == 0);
    attached = writable = 1;
    sector_bytes = 512;
    for (active_slot = 0; active_slot < 4; active_slot++) {
        int drv = 0x0600 + active_slot;
        io_reset();
        kmemset(g_disk, 0xab, 320 * 512);
        CHECK(ext2_format_at(drv, 7, 256) == EXT2_OK);
        CHECK(g_wr_min == 7 && g_wr_max < 263);
        for (int i = 0; i < 512; i++) {
            CHECK(g_disk[6 * 512 + i] == 0xab);
            CHECK(g_disk[263 * 512 + i] == 0xab);
        }
        check_loop_super(7, 256);
        io_reset();
        CHECK(ext2_format(drv, 321) == EXT2_OK);
        CHECK(g_wr_min == 0 && g_wr_max < 320);
        check_loop_super(0, 320);
    }
    active_slot = 0;
    status_calls = 0;
    io_reset();
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(ext2_format(invalid[i], 256) != EXT2_OK);
        CHECK(ext2_format_at(invalid[i], 20, 256) != EXT2_OK);
    }
    CHECK(status_calls == 0 && g_wr == 0);
    writable = 0; /* Both read-only and 512-byte D88 report LBA writes unavailable. */
    CHECK(ext2_format(0x0600, 256) == EXT2_ERR_INVAL);
    CHECK(ext2_format_at(0x0600, 20, 256) == EXT2_ERR_INVAL);
    writable = 1;
    sector_bytes = 1024;
    CHECK(ext2_format(0x0600, 256) == EXT2_ERR_INVAL);
    CHECK(ext2_format_at(0x0600, 20, 256) == EXT2_ERR_INVAL);
    attached = 0;
    CHECK(ext2_format(0x0600, 256) == EXT2_ERR_IO);
    CHECK(ext2_format_at(0x0600, 20, 256) == EXT2_ERR_IO);
    CHECK(g_wr == 0);
}
