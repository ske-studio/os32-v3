/* D88 セクタ検索・ID 取得と全フォーマットの CHS 入出力。 */

/* ======================================================================== */
/*  CHS コア API                                                            */
/* ======================================================================== */

/* ------------------------------------------------------------------------ */
/*  loop_dev_seek_d88 — D88 FDCエミュレーション用 CHS セクタ検索            */
/*                                                                          */
/*  トラック選択 (trk_cyl/trk_head) とセクタID照合 (id_c/id_h/id_r) を分離。*/
/*  Ys 等のコピープロテクション対応:                                         */
/*    SEEK cyl=1 → READ C=0, H=1 のように物理トラックと論理IDが異なる。     */
/*                                                                          */
/*  戻り値: セクタデータのファイルオフセット (0=未発見)                       */
/* ------------------------------------------------------------------------ */
u32 loop_dev_seek_d88(int slot,
                      u8 trk_cyl, u8 trk_head,
                      u8 id_c, u8 id_h, u8 id_r,
                      u32 *out_data_len, u16 *out_spt)
{
    LoopSlot *s;
    u32 track_idx, trk_off, pos;
    u8  shdr[D88_SECT_HDR_SIZE];
    u16 track_spt, data_len;
    int si, rd;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;
    s = &loop_slots[slot];
    if (!s->in_use || s->fmt != LOOP_FMT_D88) return 0;

    /* トラック選択: trk_cyl * heads + trk_head */
    track_idx = (u32)trk_cyl * (u32)s->heads + (u32)trk_head;
    if (track_idx >= D88_MAX_TRACKS) return 0;

    trk_off = s->track_offset[track_idx];
    if (trk_off == 0 || trk_off >= s->file_size) return 0;

    /* トラック先頭セクタヘッダから SPT 取得 */
    vfs_seek(s->fd, (int)trk_off, 0);
    rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
    if (rd < D88_SECT_HDR_SIZE) return 0;
    track_spt = le_read16(shdr + D88_SH_SPT_OFF);

    if (track_spt == 0 || track_spt > LOOP_FDD_SPT_MAX) return 0;
    if (out_spt) *out_spt = track_spt;

    /* セクタヘッダ走査: id_c/id_h/id_r で照合 */
    pos = trk_off;
    for (si = 0; si < (int)track_spt; si++) {
        vfs_seek(s->fd, (int)pos, 0);
        rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
        if (rd < D88_SECT_HDR_SIZE) return 0;

        data_len = le_read16(shdr + D88_SH_DATA_LEN_OFF);
        if (data_len == 0) {
            pos += D88_SECT_HDR_SIZE;
            continue;
        }

        if (shdr[0] == id_c &&
            shdr[1] == id_h &&
            shdr[D88_SH_R_OFF] == id_r) {
            if (out_data_len) *out_data_len = (u32)data_len;
            return pos + D88_SECT_HDR_SIZE;  /* データ部の先頭 */
        }

        pos += D88_SECT_HDR_SIZE + (u32)data_len;
    }
    return 0;  /* 未発見 */
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_track_id_d88 — D88 の物理トラック上の index 番目のセクタ ID     */
/*                                                                          */
/*  FDC の READ ID コマンド用。ヘッドの下を通る順にセクタ ID が並んでいて、  */
/*  READ ID はそれを 1 つずつ返す。ゲストはこれでフォーマットを判定する。    */
/*  (np21w-src/src/diskimage/fd/fdd_d88.c `fdd_readid_d88()` と同じ走査)     */
/*                                                                          */
/*  戻り値: 0=成功, -1=トラック/セクタ不在                                   */
/* ------------------------------------------------------------------------ */
int loop_dev_track_id_d88(int slot, u8 trk_cyl, u8 trk_head, u16 index,
                          u8 *out_c, u8 *out_h, u8 *out_r, u8 *out_n,
                          u16 *out_spt)
{
    LoopSlot *s;
    u32 track_idx, trk_off, pos;
    u8  shdr[D88_SECT_HDR_SIZE];
    u16 track_spt, data_len;
    int si, rd;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    s = &loop_slots[slot];
    if (!s->in_use || s->fmt != LOOP_FMT_D88) return -1;

    track_idx = (u32)trk_cyl * (u32)s->heads + (u32)trk_head;
    if (track_idx >= D88_MAX_TRACKS) return -1;

    trk_off = s->track_offset[track_idx];
    if (trk_off == 0 || trk_off >= s->file_size) return -1;

    vfs_seek(s->fd, (int)trk_off, 0);
    rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
    if (rd < D88_SECT_HDR_SIZE) return -1;
    track_spt = le_read16(shdr + D88_SH_SPT_OFF);
    if (track_spt == 0 || track_spt > LOOP_FDD_SPT_MAX) return -1;
    if (out_spt) *out_spt = track_spt;
    if (index >= track_spt) return -1;

    pos = trk_off;
    for (si = 0; si < (int)track_spt; si++) {
        vfs_seek(s->fd, (int)pos, 0);
        rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
        if (rd < D88_SECT_HDR_SIZE) return -1;
        data_len = le_read16(shdr + D88_SH_DATA_LEN_OFF);

        if (si == (int)index) {
            if (out_c) *out_c = shdr[0];
            if (out_h) *out_h = shdr[1];
            if (out_r) *out_r = shdr[D88_SH_R_OFF];
            if (out_n) *out_n = shdr[D88_SH_N_OFF];
            return 0;
        }
        pos += D88_SECT_HDR_SIZE + (u32)data_len;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_read_chs — CHS セクタ読み出し (全フォーマット共通)              */
/*                                                                          */
/*  D88: loop_dev_seek_d88 → vfs_read                                      */
/*  FDI/RAW: オフセット計算 → vfs_read                                     */
/*  sect は 1-based (ATA/FDC 準拠)                                          */
/* ------------------------------------------------------------------------ */
/* FDI/HDI/RAW のオフセット計算に入れてよい CHS か。
 *
 * **sect は 1 起算。0 を素通ししてはいけない。**
 * オフセット計算の `(u32)(sect - 1)` が 0xFFFFFFFF に化け、
 * セクタ長を掛けた時点で桁溢れしてイメージ先頭付近へ回り込む。
 * 「offset + セクタ長 がファイル末尾を超えないか」の境界チェックも
 * 通ってしまうので、**範囲外の要求に無関係なセクタを返して成功と
 * 答える**という、いちばん質の悪い壊れ方になる。
 *
 * 実測: MS-DOS 5 が SASI 規約で投げた C=0/H=0/R=0 の読みに対して、
 * FAT 領域の中身を「成功」として返していた。
 * → docs/archive/v21/v86v2/08_dos5.md §2 */
static int raw_chs_ok(const LoopSlot *s, u16 cyl, u8 head, u8 sect)
{
    if (sect == 0 || (u32)sect > (u32)s->spt)   return 0;
    if ((u32)head >= (u32)s->heads)             return 0;
    if ((u32)cyl  >= (u32)s->cyls)              return 0;
    return 1;
}

int loop_dev_read_chs(int slot, u16 cyl, u8 head, u8 sect, void *buf)
{
    LoopSlot *s;
    int rd;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    s = &loop_slots[slot];
    if (!s->in_use) return -1;

    if (s->fmt == LOOP_FMT_D88) {
        /* D88: 標準 CHS アクセス (物理=論理) */
        u32 data_len = 0;
        u32 off;
        u32 to_read;

        off = loop_dev_seek_d88(slot,
            (u8)cyl, head, (u8)cyl, head, sect,
            &data_len, 0);
        if (off == 0) return -1;
        to_read = data_len;
        if (to_read > (u32)s->lba_sect_size)
            to_read = (u32)s->lba_sect_size;
        vfs_seek(s->fd, (int)off, 0);
        rd = vfs_read_fd(s->fd, buf, to_read);
        if (rd < (int)to_read) return -1;
        /* BPS に満たない場合はゼロ埋め */
        if (to_read < (u32)s->lba_sect_size)
            zero_fill((u8 *)buf + to_read,
                      (u32)s->lba_sect_size - to_read);
        return 0;
    } else {
        /* FDI/HDI/RAW: オフセット計算 */
        u32 offset;

        if (!raw_chs_ok(s, cyl, head, sect)) return -1;

        offset = s->data_offset
            + (((u32)cyl * (u32)s->heads + (u32)head)
               * (u32)s->spt + (u32)(sect - 1))
            * (u32)s->lba_sect_size;
        if (offset + (u32)s->lba_sect_size > s->file_size)
            return -1;
        if (vfs_seek(s->fd, (int)offset, 0) < 0) return -1;
        rd = vfs_read_fd(s->fd, buf, (u32)s->lba_sect_size);
        if (rd < (int)s->lba_sect_size) return -1;
        return 0;
    }
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_write_chs — CHS セクタ書き込み (全フォーマット共通)             */
/*                                                                          */
/*  D88: loop_dev_seek_d88 → vfs_write                                     */
/*  FDI/HDI/RAW: オフセット計算 → vfs_write                                */
/*  sect は 1-based (ATA/FDC 準拠)                                          */
/* ------------------------------------------------------------------------ */
int loop_dev_write_chs(int slot, u16 cyl, u8 head, u8 sect,
                        const void *buf)
{
    LoopSlot *s;
    int wr;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    s = &loop_slots[slot];
    if (!s->in_use) return -1;

    if (s->fmt == LOOP_FMT_D88) {
        /* D88: seek_d88 でセクタデータ位置を取得 → 書き込み */
        u32 data_len = 0;
        u32 off;
        u32 to_write;

        off = loop_dev_seek_d88(slot,
            (u8)cyl, head, (u8)cyl, head, sect,
            &data_len, 0);
        if (off == 0) return -1;
        to_write = data_len;
        if (to_write > (u32)s->lba_sect_size)
            to_write = (u32)s->lba_sect_size;
        vfs_seek(s->fd, (int)off, 0);
        wr = vfs_write_fd(s->fd, buf, to_write);
        if (wr < (int)to_write) return -1;
        return 0;
    } else {
        /* FDI/HDI/RAW: オフセット計算 */
        u32 offset;

        if (!raw_chs_ok(s, cyl, head, sect)) return -1;

        offset = s->data_offset
            + (((u32)cyl * (u32)s->heads + (u32)head)
               * (u32)s->spt + (u32)(sect - 1))
            * (u32)s->lba_sect_size;
        if (offset + (u32)s->lba_sect_size > s->file_size)
            return -1;
        if (vfs_seek(s->fd, (int)offset, 0) < 0) return -1;
        wr = vfs_write_fd(s->fd, buf, (u32)s->lba_sect_size);
        if (wr < (int)s->lba_sect_size) return -1;
        return 0;
    }
}

