/* L2 word transfers, unchanged. Wait/status/reset/DRQ settle stay in atapi.c. */
/* 呼び手のバッファや奇数長 DRQ の次の位置は、ワード境界とは限らない。
 * x86 の非整列 16bit 格納を使い、元のバッファの型とも別名参照できるようにする。 */
typedef u16 AtapiPioWord __attribute__((aligned(1), may_alias));

static void atapi_pio_send_cdb(const u8 *cdb)
{
    int i;
    for (i = 0; i < ATAPI_CDB_BYTES / (int)sizeof(u16); i++) {
        u16 w = (u16)cdb[i * sizeof(u16)] | ((u16)cdb[i * sizeof(u16) + 1] << 8);
        outpw(IDE_DATA, (unsigned)w);
    }
}

/* Odd DRQs consume a final word; bytes beyond the response/buffer are discarded.
 * The packed alias word keeps the L2 even-length path valid for unaligned dst. */
static u32 atapi_pio_read(u8 *p, u32 buf_size, u32 total_read, u16 xfer_size)
{
    u32 words;
    u32 end;
    u32 k;
    words = ((u32)xfer_size + sizeof(u16) - 1) / sizeof(u16);
    end = total_read + xfer_size;
    if (!(xfer_size & 1) && end <= buf_size) {
        AtapiPioWord *dst = (AtapiPioWord *)(p + total_read);
        for (k = 0; k < words; k++) dst[k] = (u16)inpw(IDE_DATA);
    } else {
        for (k = 0; k < words; k++) {
            u16 w = (u16)inpw(IDE_DATA);
            u32 b = total_read + k * sizeof(u16);
            if (b < end && b < buf_size) p[b] = (u8)(w & 0xFF);
            if (b + 1 < end && b + 1 < buf_size) p[b + 1] = (u8)(w >> 8);
        }
    }
    return end;
}
