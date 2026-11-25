/* Odd DRQs consume a final word; bytes beyond the response/buffer are discarded.
 * Bus stream functions preserve the byte order on both machines. */
static u32 atapi_pio_read(u8 *p, u32 buf_size, u32 total_read, u16 xfer_size)
{
    u32 words;
    u32 end;
    u32 k;
    words = ((u32)xfer_size + sizeof(u16) - 1) / sizeof(u16);
    end = total_read + xfer_size;
    if (!(xfer_size & 1) && end <= buf_size) {
#if ATAPI_NATIVE_LE_WORDS
        ide_unaligned_word *dst = (ide_unaligned_word *)(p + total_read);
        for (k = 0; k < words; k++) dst[k] = ide_bus_data_word();
#else
        ide_bus_data_read(p + total_read, words);
#endif
    } else {
        for (k = 0; k < words; k++) {
            u16 w = ide_bus_data_word();
            u32 b = total_read + k * sizeof(u16);
            if (b < end && b < buf_size) p[b] = (u8)(w & 0xFF);
            if (b + 1 < end && b + 1 < buf_size) p[b + 1] = (u8)(w >> 8);
        }
    }
    return end;
}
