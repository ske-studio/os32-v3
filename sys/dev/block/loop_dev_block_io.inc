/* Device 経由の RAW/D88 ブロック読み出しと CHS 読み出しの委譲。 */

/* ======================================================================== */
/*  loop_blk_read_chs — Device 経由の CHS 読み出し                         */
/* ======================================================================== */
static int loop_blk_read_chs(Device *self, u16 cyl, u8 head, u8 sect,
                              void *buf)
{
    LoopSlot *s = (LoopSlot *)self->priv;
    int slot = (int)(s - loop_slots);
    return loop_dev_read_chs(slot, cyl, head, sect, buf);
}

/* ======================================================================== */
/*  raw_blk_read — RAW データ用 blk_read (FDI/NHD/HDI/ISO 等共通)          */
/*                                                                          */
/*  offset = data_offset + lba * lba_sect_size                              */
/* ======================================================================== */
static int raw_blk_read(Device *self, int lba, int count, void *buf)
{
    LoopSlot *s;
    u32 offset, total_bytes;
    int rd;

    s = (LoopSlot *)self->priv;
    if (!s || !s->in_use) return -1;
    if (lba < 0 || (u32)lba >= s->total_lba) return -1;
    if (count <= 0) return -1;
    if ((u32)(lba + count) > s->total_lba) return -1;

    offset = s->data_offset + (u32)lba * (u32)s->lba_sect_size;
    total_bytes = (u32)count * (u32)s->lba_sect_size;

    if (vfs_seek(s->fd, (int)offset, 0) < 0) return -1;
    rd = vfs_read_fd(s->fd, buf, total_bytes);
    if (rd < (int)total_bytes) return -1;

    return 0;
}

/* ======================================================================== */
/*  d88_blk_read — D88仕様書準拠セクタ読み出し                              */
/*                                                                          */
/*  LBA → CHS 変換 → トラックオフセット参照 → セクタヘッダ走査            */
/*  → C/H/R 厳密一致 → データ読み出し                                      */
/* ======================================================================== */
static int d88_blk_read(Device *self, int lba, int count, void *buf)
{
    LoopSlot *s;
    u8 *dst;
    int sec_i, slot;

    s = (LoopSlot *)self->priv;
    if (!s || !s->in_use) return -1;
    if (lba < 0 || (u32)lba >= s->total_lba) return -1;
    if (count <= 0) return -1;
    if ((u32)(lba + count) > s->total_lba) return -1;

    slot = (int)(s - loop_slots);
    dst = (u8 *)buf;

    for (sec_i = 0; sec_i < count; sec_i++) {
        u32 cur_lba, track, cyl, head, r_1based;

        cur_lba = (u32)lba + (u32)sec_i;

        /* LBA → CHS 変換 */
        track    = cur_lba / (u32)s->spt;
        cyl      = track / (u32)s->heads;
        head     = track % (u32)s->heads;
        r_1based = (cur_lba % (u32)s->spt) + 1;

        if (loop_dev_read_chs(slot, (u16)cyl, (u8)head, (u8)r_1based, dst) != 0)
            return -1;

        dst += s->lba_sect_size;
    }

    return 0;
}

