/* D88/FDI/HDI/NHD/RAW の形式別アタッチとジオメトリ取得。 */

/* ======================================================================== */
/*  attach_d88 — D88 イメージのアタッチ処理                                 */
/* ======================================================================== */
static int attach_d88(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr_small[D88_FIXED_HDR_BYTES];
    u8  tbl_buf[D88_MAX_TRACKS * D88_TRACK_ENTRY_BYTES];
    u8  sec0[D88_SECT_HDR_SIZE];
    int i, rd;

    if (fsize < D88_HDR_SIZE) return LOOP_ERR_FORMAT;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr_small, D88_FIXED_HDR_BYTES);
    if (rd < D88_FIXED_HDR_BYTES) return LOOP_ERR_IO;

    s->media     = hdr_small[D88_OFF_MEDIA];
    s->file_size = le_read32(hdr_small + D88_OFF_SIZE);
    if (s->file_size > fsize)
        s->file_size = fsize;

    /* トラックオフセットテーブル */
    vfs_seek(fd, D88_OFF_TRACKS, 0);
    rd = vfs_read_fd(fd, tbl_buf, D88_MAX_TRACKS * D88_TRACK_ENTRY_BYTES);
    if (rd < (int)(D88_MAX_TRACKS * D88_TRACK_ENTRY_BYTES)) return LOOP_ERR_IO;
    for (i = 0; i < D88_MAX_TRACKS; i++)
        s->track_offset[i] = le_read32(tbl_buf + i * D88_TRACK_ENTRY_BYTES);

    /* media → cyls/heads */
    switch (s->media) {
    case D88_MEDIA_2D:   s->cyls = 40; s->heads = 2; break;
    case D88_MEDIA_2DD:  s->cyls = 80; s->heads = 2; break;
    case D88_MEDIA_2HD:  s->cyls = 77; s->heads = 2; break;
    case D88_MEDIA_1D:   s->cyls = 40; s->heads = 1; break;
    case D88_MEDIA_1DD:  s->cyls = 80; s->heads = 1; break;
    default: return LOOP_ERR_FORMAT;
    }

    /* トラック0のセクタヘッダから N/SPT を取得 */
    if (s->track_offset[0] == 0 || s->track_offset[0] >= s->file_size)
        return LOOP_ERR_FORMAT;
    vfs_seek(fd, (int)s->track_offset[0], 0);
    rd = vfs_read_fd(fd, sec0, D88_SECT_HDR_SIZE);
    if (rd < D88_SECT_HDR_SIZE) return LOOP_ERR_IO;

    {
        u8  n_val   = sec0[D88_SH_N_OFF];
        u16 hdr_spt = le_read16(sec0 + D88_SH_SPT_OFF);
        if (hdr_spt == 0 || hdr_spt > LOOP_FDD_SPT_MAX || n_val > D88_SECTOR_N_MAX)
            return LOOP_ERR_FORMAT;
        s->spt          = (u8)hdr_spt;
        s->lba_sect_size = (u16)(D88_SECTOR_BASE_BYTES << n_val);
    }

    s->total_lba = (u32)s->cyls * (u32)s->heads * (u32)s->spt;
    s->fmt = LOOP_FMT_D88;
    s->dev.blk_read = d88_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_fdi — FDI イメージのアタッチ処理                                 */
/* ======================================================================== */
static int attach_fdi(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr[FDI_HDR_FIELDS];
    u32 hdr_size, fdd_size, sect_size, spt, surfaces, cyls;
    int rd;

    if (fsize < FDI_HDR_SIZE) return LOOP_ERR_FORMAT;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr, FDI_HDR_FIELDS);
    if (rd < FDI_HDR_FIELDS) return LOOP_ERR_IO;

    /* FDI ヘッダ解析 (全フィールド LE32) */
    hdr_size  = le_read32(hdr + FDI_OFF_HDRSIZE);
    fdd_size  = le_read32(hdr + FDI_OFF_DATASIZE);
    sect_size = le_read32(hdr + FDI_OFF_BPS);
    spt       = le_read32(hdr + FDI_OFF_SPT);
    surfaces  = le_read32(hdr + FDI_OFF_HEADS);
    cyls      = le_read32(hdr + FDI_OFF_CYLS);

    /* バリデーション */
    if (hdr_size < FDI_HDR_FIELDS || hdr_size > LOOP_HEADER_BYTES_MAX) return LOOP_ERR_FORMAT;
    if (sect_size == 0 || sect_size > LOOP_SECTOR_BYTES_MAX) return LOOP_ERR_FORMAT;
    if (spt == 0 || spt > LOOP_FDD_SPT_MAX) return LOOP_ERR_FORMAT;
    if (surfaces == 0 || surfaces > 2) return LOOP_ERR_FORMAT;
    if (cyls == 0 || cyls > 1024) return LOOP_ERR_FORMAT;
    if (hdr_size + fdd_size > fsize) return LOOP_ERR_FORMAT;

    s->data_offset   = hdr_size;
    s->file_size     = fsize;
    s->cyls          = (u16)cyls;
    s->heads         = (u8)surfaces;
    s->spt           = (u8)spt;
    s->lba_sect_size = (u16)sect_size;
    s->total_lba     = cyls * surfaces * spt;
    s->fmt           = LOOP_FMT_FDI;
    s->dev.blk_read  = raw_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_hdi — HDI (Anex86 HDD) イメージのアタッチ処理                    */
/*                                                                          */
/*  FDI と同一ヘッダ構造 (4096B ヘッダ + RAW データ)。                       */
/*  拡張子 .hdi で区別する (マジックナンバーなし)。                          */
/* ======================================================================== */
static int attach_hdi(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr[FDI_HDR_FIELDS];
    u32 hdr_size, data_size, sect_size, spt, surfaces, cyls;
    int rd;

    if (fsize < FDI_HDR_SIZE) return LOOP_ERR_FORMAT;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr, FDI_HDR_FIELDS);
    if (rd < FDI_HDR_FIELDS) return LOOP_ERR_IO;

    /* HDI ヘッダ解析 (FDI と同一レイアウト、全フィールド LE32) */
    hdr_size  = le_read32(hdr + FDI_OFF_HDRSIZE);
    data_size = le_read32(hdr + FDI_OFF_DATASIZE);
    sect_size = le_read32(hdr + FDI_OFF_BPS);
    spt       = le_read32(hdr + FDI_OFF_SPT);
    surfaces  = le_read32(hdr + FDI_OFF_HEADS);
    cyls      = le_read32(hdr + FDI_OFF_CYLS);

    /* バリデーション */
    if (hdr_size < FDI_HDR_FIELDS || hdr_size > LOOP_HEADER_BYTES_MAX) return LOOP_ERR_FORMAT;
    if (sect_size == 0 || sect_size > LOOP_SECTOR_BYTES_MAX) return LOOP_ERR_FORMAT;
    if (spt == 0 || spt > 255) return LOOP_ERR_FORMAT;
    if (surfaces == 0 || surfaces > 255) return LOOP_ERR_FORMAT;
    if (cyls == 0 || cyls > LOOP_HDD_CYL_MAX) return LOOP_ERR_FORMAT;
    if (hdr_size + data_size > fsize) return LOOP_ERR_FORMAT;

    s->data_offset   = hdr_size;
    s->file_size     = fsize;
    s->cyls          = (u16)cyls;
    s->heads         = (u8)surfaces;
    s->spt           = (u8)spt;
    s->lba_sect_size = (u16)sect_size;
    s->total_lba     = cyls * surfaces * spt;
    s->fmt           = LOOP_FMT_HDI;
    s->dev.blk_read  = raw_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_nhd — NHD (T98-Next r0) イメージのアタッチ処理                    */
/*                                                                          */
/*  512B ヘッダ + RAW データ。HDI と違いマジックがあるので必ず照合する。      */
/*  ジオメトリはヘッダの C/H/S/BPS をそのまま使う (docs/NHD_FORMAT.md)。     */
/* ======================================================================== */
static int attach_nhd(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr[NHD_OFF_BPS + 2];
    u32 hdr_size, cyls, heads, spt, bps;
    int rd, i;
    static const char sig[] = NHD_SIG;

    if (fsize < NHD_HDR_SIZE) return LOOP_ERR_FORMAT;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr, sizeof(hdr));
    if (rd < (int)sizeof(hdr)) return LOOP_ERR_IO;

    for (i = 0; sig[i]; i++) {
        if (hdr[i] != (u8)sig[i]) return LOOP_ERR_FORMAT;
    }

    hdr_size = le_read32(hdr + NHD_OFF_HDRSIZE);
    cyls     = le_read32(hdr + NHD_OFF_CYLS);
    heads    = le_read16(hdr + NHD_OFF_HEADS);
    spt      = le_read16(hdr + NHD_OFF_SPT);
    bps      = le_read16(hdr + NHD_OFF_BPS);

    /* バリデーション (LoopSlot.cyls は u16 なので上限 65535)。
     * C*H*S*BPS は u32 を溢れ得るので段階的に上限を確かめる。
     * ファイルサイズが u32 なのでイメージは 4GB 未満しか扱えない。 */
    if (hdr_size < NHD_HDR_SIZE || hdr_size > LOOP_HEADER_BYTES_MAX) return LOOP_ERR_FORMAT;
    if (hdr_size >= fsize) return LOOP_ERR_FORMAT;
    if (bps == 0 || bps > LOOP_SECTOR_BYTES_MAX) return LOOP_ERR_FORMAT;
    if (spt == 0 || spt > 255) return LOOP_ERR_FORMAT;
    if (heads == 0 || heads > 255) return LOOP_ERR_FORMAT;
    if (cyls == 0 || cyls > LOOP_HDD_CYL_MAX) return LOOP_ERR_FORMAT;
    {
        u32 per_cyl = heads * spt;              /* <= 255*255, 溢れない */
        if (cyls * per_cyl > U32_MAX / bps) return LOOP_ERR_FORMAT;
        if (cyls * per_cyl * bps > fsize - hdr_size) return LOOP_ERR_FORMAT;
    }

    s->data_offset   = hdr_size;
    s->file_size     = fsize;
    s->cyls          = (u16)cyls;
    s->heads         = (u8)heads;
    s->spt           = (u8)spt;
    s->lba_sect_size = (u16)bps;
    s->total_lba     = cyls * heads * spt;
    s->fmt           = LOOP_FMT_NHD;
    s->dev.blk_read  = raw_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_raw — RAW ディスクイメージのアタッチ処理                              */
/*                                                                          */
/*  ヘッダなし。ファイルサイズの完全一致で既知容量テーブルから C/H/S/BPS を決定。 */
/*  未知サイズの場合は拒否する (誤判定防止)。                              */
/* ======================================================================== */
static int attach_raw(LoopSlot *s, u32 fsize)
{
    /* 既知容量テーブル */
    static const struct {
        u32 size;
        u16 cyls;
        u8  heads;
        u8  spt;
        u16 bps;
    } known[] = {
        { 1261568, 77, 2, 8,  1024 }, /* 2HD 1232KB (PC-98) */
        {  655360, 80, 2, 8,   512 }, /* 2DD 640KB */
        {  737280, 80, 2, 9,   512 }, /* 2DD 720KB */
        { 1474560, 80, 2, 18,  512 }, /* 2HD 1.44MB (IBM) */
        {  327680, 40, 2, 8,   512 }, /* 2D 320KB */
        {  163840, 40, 1, 8,   512 }, /* 1D 160KB */
        { 0, 0, 0, 0, 0 }
    };
    int i;

    for (i = 0; known[i].size != 0; i++) {
        if (fsize == known[i].size) {
            s->data_offset   = 0;
            s->file_size     = fsize;
            s->cyls          = known[i].cyls;
            s->heads         = known[i].heads;
            s->spt           = known[i].spt;
            s->lba_sect_size = known[i].bps;
            s->total_lba     = (u32)s->cyls * (u32)s->heads * (u32)s->spt;
            s->fmt           = LOOP_FMT_RAW;
            s->dev.blk_read  = raw_blk_read;
            return 0;
        }
    }
    return LOOP_ERR_FORMAT;  /* 未知サイズ: 拒否 */
}

