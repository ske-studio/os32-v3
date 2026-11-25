/* Positive: current record length; zero: next sector; negative: extent end. */
static int iso_dir_record_length(const u8 *sector, u32 dir_size, u32 *offset)
{
    u32 sect_off = *offset % ISO_SECTOR_SIZE;
    u8 rec_len = sector[sect_off];

    if (rec_len != 0) {
        if (rec_len > dir_size - *offset) return -1;
        if (sect_off + rec_len <= ISO_SECTOR_SIZE) return rec_len;
    }
    *offset = ((*offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
    return 0;
}
