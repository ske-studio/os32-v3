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

    vfs_seek(s->fd, (int)offset, 0);
    rd = vfs_read_fd(s->fd, buf, total_bytes);
    if (rd < (int)total_bytes) {
        /* 読み切れなかった分はゼロ埋め */
        if (rd > 0)
            zero_fill((u8 *)buf + rd, total_bytes - (u32)rd);
        else
            zero_fill((u8 *)buf, total_bytes);
    }

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
    int sec_i;

    s = (LoopSlot *)self->priv;
    if (!s || !s->in_use) return -1;
    if (lba < 0 || (u32)lba >= s->total_lba) return -1;
    if (count <= 0) return -1;
    if ((u32)(lba + count) > s->total_lba) return -1;

    dst = (u8 *)buf;

    for (sec_i = 0; sec_i < count; sec_i++) {
        u32 cur_lba, track, cyl, head, r_1based;
        u32 track_idx, trk_off, pos;
        u8  shdr[D88_SECT_HDR_SIZE];
        u16 track_spt, data_len;
        int found, si, rd;
        u32 to_read, pad;

        cur_lba = (u32)lba + (u32)sec_i;

        /* LBA → CHS 変換 */
        track    = cur_lba / (u32)s->spt;
        cyl      = track / (u32)s->heads;
        head     = track % (u32)s->heads;
        r_1based = (cur_lba % (u32)s->spt) + 1;

        track_idx = cyl * (u32)s->heads + head;
        if (track_idx >= D88_MAX_TRACKS) return -1;

        trk_off = s->track_offset[track_idx];

        /* 不在トラック → エラー (実FDC準拠) */
        if (trk_off == 0 || trk_off >= s->file_size) {
            return -1;
        }

        /* トラック先頭セクタヘッダから SPT 取得 */
        vfs_seek(s->fd, (int)trk_off, 0);
        rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
        if (rd < D88_SECT_HDR_SIZE) return -1;
        track_spt = le_read16(shdr + D88_SH_SPT_OFF);

        if (track_spt == 0 || track_spt > LOOP_FDD_SPT_MAX) return -1;

        /* セクタヘッダ走査: C/H/R 厳密一致 */
        found = 0;
        pos = trk_off;
        for (si = 0; si < (int)track_spt; si++) {
            vfs_seek(s->fd, (int)pos, 0);
            rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
            if (rd < D88_SECT_HDR_SIZE) return -1;

            data_len = le_read16(shdr + D88_SH_DATA_LEN_OFF);

            if (data_len == 0) {
                pos += D88_SECT_HDR_SIZE;
                continue;
            }

            if (shdr[0] == (u8)cyl &&
                shdr[1] == (u8)head &&
                shdr[D88_SH_R_OFF] == (u8)r_1based) {
                to_read = (u32)data_len;
                if (to_read > (u32)s->lba_sect_size)
                    to_read = (u32)s->lba_sect_size;

                rd = vfs_read_fd(s->fd, dst, to_read);
                if (rd < (int)to_read) return -1;

                pad = (u32)s->lba_sect_size - to_read;
                if (pad > 0)
                    zero_fill(dst + to_read, pad);

                found = 1;
                break;
            }

            pos += D88_SECT_HDR_SIZE + (u32)data_len;
        }

        /* 未発見セクタ → エラー (実FDC準拠: No Data) */
        if (!found)
            return -1;

        dst += s->lba_sect_size;
    }

    return 0;
}

