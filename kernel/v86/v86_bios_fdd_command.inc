/* FDD の WRITE / READ ID と INT 1Bh コマンド処理。 */

/* AH=05h WRITE — FDD 規約の書き込み。正本は bios1b.c case 0x05。
 *
 * READ と同じレジスタ規約・同じ「トラックを跨がない」歩き方で、
 * 方向だけが逆。FDI/RAW のみ対応 (D88 の ID ずらしイメージへ書く用途は
 * 無い — Ys 等は読み専用。来たら正直に AH=0x40 で返す)。
 * エラーコードは実機と同じ: 0x70 ライトプロテクト / 0x20 64KB 境界 /
 * 0xC0 転送しきれず。 */
static void disk_write(u32 *frame)
{
    u32 cyl   = (frame[V86F_ECX] >> 0) & U8_MAX;     /* CL */
    u32 n     = (frame[V86F_ECX] >> 8) & U8_MAX;     /* CH: セクタ長コード */
    u32 head  = (frame[V86F_EDX] >> 8) & U8_MAX;     /* DH */
    u32 sect  = (frame[V86F_EDX] >> 0) & U8_MAX;     /* DL (1 起算) */
    u32 count = frame[V86F_EBX] & U16_MAX;          /* BX: バイト数 */
    u32 src   = ((frame[V86F_ES] & U16_MAX) << X86_REAL_SEG_SHIFT) +
                (frame[V86F_EBP] & U16_MAX);
    u16 cyls; u8 heads, spt; u16 bps; u32 total;
    u32 seclen;
    u32 ah    = (frame[V86F_EAX] >> 8) & U8_MAX;
    u32 phys_head;
    static u8 secbuf[V86_SECTOR_BUFFER_BYTES];

    if (ah & BIOS_DISK_CMD_SEEK) {
        cur_cyl = (u8)cyl;
    }
    phys_head = V86_FDC_HEAD(head, frame[V86F_EAX] & U8_MAX);

    v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
    v86_disk_fail_len = (n << 16) | count;
    v86_disk_fail_dst = src;
    xfer_done = 0;

    if (!disk_attached) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_DEVICE;
        bios_set_ah(frame, V86_DISK_ST_NOT_READY);
        bios_set_cf(frame, 1);
        return;
    }
    if (count == 0) {
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        return;
    }
    if ((src & U16_MAX) > ((src + count - 1) & U16_MAX)) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_RANGE;
        bios_set_ah(frame, V86_DISK_ST_DMA_BOUNDARY);
        bios_set_cf(frame, 1);
        return;
    }
    if (!guest_range_ok(src, count)) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_RANGE;
        bios_set_ah(frame, V86_DISK_ST_RANGE);
        bios_set_cf(frame, 1);
        return;
    }
    if (loop_dev_get_geometry(cur_fdd_slot, &cyls, &heads, &spt,
                              &bps, &total) != 0 || spt == 0) {
        bios_set_ah(frame, V86_DISK_ST_NOT_READY);
        bios_set_cf(frame, 1);
        return;
    }
    if (!loop_dev_is_writable(cur_fdd_slot)) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_RANGE;
        bios_set_ah(frame, V86_DISK_ST_WRITE_PROTECT);       /* ライトプロテクト */
        bios_set_cf(frame, 1);
        return;
    }
    if (loop_dev_get_format(cur_fdd_slot) == LOOP_FMT_D88) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_SECTOR_SIZE;
        bios_set_ah(frame, V86_DISK_ST_RANGE);
        bios_set_cf(frame, 1);
        return;
    }
    seclen = (unsigned long)BIOS_SECTOR_BASE_BYTES << (n & BIOS_SECTOR_N_MASK);
    if (seclen != (u32)bps || seclen > sizeof(secbuf)) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_SECTOR_SIZE;
        bios_set_ah(frame, V86_DISK_ST_RANGE);
        bios_set_cf(frame, 1);
        return;
    }

    while (count) {
        u32 i;
        u32 xfer = (count > seclen) ? seclen : count;

        /* 端数セクタは読んでから前半だけ差し替える (RMW)。
         * 残りを 0 で埋めて書くと隣のデータを消してしまう。 */
        if (xfer < seclen) {
            if (loop_dev_read_chs(cur_fdd_slot, (u16)cyl, (u8)head,
                                  (u8)sect, secbuf) != 0) {
                v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_SECTOR;
                bios_set_ah(frame, V86_DISK_ST_RANGE);
                bios_set_cf(frame, 1);
                return;
            }
        }
        for (i = 0; i < xfer; i++) {
            secbuf[i] = ((volatile u8 *)src)[i];
        }
        if (loop_dev_write_chs(cur_fdd_slot, (u16)cyl, (u8)head,
                               (u8)sect, secbuf) != 0) {
            v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_SECTOR;
            v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
            bios_set_ah(frame, V86_DISK_ST_RANGE);
            bios_set_cf(frame, 1);
            return;
        }
        xfer_done += xfer;
        src   += xfer;
        count -= xfer;

        /* READ と同じ: トラックは跨がない。AH bit7 + 表面なら裏面へ。 */
        if (sect >= spt) {
            if ((ah & V86_DISK_CMD_MULTI_TRACK) && phys_head == 0 && heads > 1) {
                head      = 1;
                phys_head = 1;
                sect      = 1;
            } else {
                break;
            }
        } else {
            sect++;
        }
    }

    if (count) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_SECTOR;
        bios_set_ah(frame, V86_DISK_ST_INCOMPLETE);
        bios_set_cf(frame, 1);
        return;
    }

    bios_set_ah(frame, 0x00);
    bios_set_cf(frame, 0);
}

/* AH=0Ah READ ID — ヘッドの下を次に通るセクタの ID を返す。
 *
 * **ゲストはこれでディスクのフォーマットを知る。** MS-DOS 5 は
 * RECALIBRATE → SENSE → READ ID の順に呼んで、返ってきた N (セクタ長コード)
 * と R からメディアが 2HD (1024B×8) か 2DD (512B×8/9) かを判定する。
 * 未実装 (AH=0x86) で返していた頃は、ここで諦めて
 * 「MSDOS.SYS 読み込み時にエラー」になっていた。
 *
 * 実機は `fdc.crcn` にトラック内のセクタ番号を持ち、呼ばれるたびに
 * 1 つ進めて末尾で 0 に戻す (bios1b.c case 0x0a → fdd_readid_*)。
 * 返すのは物理位置の C/H と、そのセクタが実際に持っている ID の R/N。
 *
 * 実機との差: 実機は crcn が一周したときトラックも進める。
 * こちらは進めない — READ ID がシーク位置を動かす副作用は、
 * この後の READ の物理位置を狂わせるほうが害が大きい。
 * (フォーマット判定に使う限り一周する前に答えが出る) */
static void disk_readid(u32 *frame)
{
    u32 ah  = (frame[V86F_EAX] >> 8) & U8_MAX;
    u32 al  = frame[V86F_EAX] & U8_MAX;
    u32 cyl = frame[V86F_ECX] & U8_MAX;              /* CL */
    u32 head;
    u16 cyls; u8 heads, spt; u16 bps; u32 total;
    u8  id_c, id_h, id_r, id_n;
    u16 trk_spt = 0;

    if (ah & BIOS_DISK_CMD_SEEK) { /* AH bit4 = シーク付き */
        cur_cyl = (u8)cyl;
    }
    head = V86_FDC_HEAD((frame[V86F_EDX] >> 8) & U8_MAX, al);

    if (loop_dev_get_geometry(cur_fdd_slot, &cyls, &heads, &spt,
                              &bps, &total) != 0 || spt == 0) {
        bios_set_ah(frame, V86_DISK_ST_NOT_READY);
        bios_set_cf(frame, 1);
        return;
    }

    if (loop_dev_get_format(V86_DISK_SLOT) == LOOP_FMT_D88) {
        if (loop_dev_track_id_d88(cur_fdd_slot, cur_cyl, (u8)head,
                                  (u16)cur_id_idx,
                                  &id_c, &id_h, &id_r, &id_n,
                                  &trk_spt) != 0) {
            cur_id_idx = 0;
            bios_set_ah(frame, V86_DISK_ST_ID_NOT_FOUND); /* ID が見つからない */
            bios_set_cf(frame, 1);
            return;
        }
        spt = (u8)trk_spt;
    } else {
        /* RAW/FDI は全トラック同じ書式。ID は物理位置そのもの。 */
        if (cur_cyl >= (u8)cyls || head >= (u32)heads) {
            bios_set_ah(frame, V86_DISK_ST_ID_NOT_FOUND);
            bios_set_cf(frame, 1);
            return;
        }
        id_c = cur_cyl;
        id_h = (u8)head;
        id_r = (u8)(cur_id_idx + 1);
        id_n = loop_dev_get_sec_n(cur_fdd_slot);
    }

    if (++cur_id_idx >= spt) {
        cur_id_idx = 0;
    }

    /* CL=C / DH=H / DL=R / CH=N を返す (bios1b.c case 0x0a の末尾と同じ) */
    frame[V86F_ECX] = (frame[V86F_ECX] & V86_REG_AX_KEEP_MASK) |
                      ((u32)id_n << 8) | (u32)id_c;
    frame[V86F_EDX] = (frame[V86F_EDX] & V86_REG_AX_KEEP_MASK) |
                      ((u32)id_h << 8) | (u32)id_r;
    bios_set_ah(frame, 0x00);
    bios_set_cf(frame, 0);
}

static void bios_fdd_int1b(u32 *frame)
{
    u32 cmd = (frame[V86F_EAX] >> 8) & BIOS_DISK_CMD_MASK;   /* AH の下位ニブル */
    u32 ax_in = frame[V86F_EAX] & U16_MAX;
    u32 phys_in = ((u32)V86_FDC_HEAD((ax_in >> 8) & U8_MAX, ax_in & U8_MAX) << 8)
                  | cur_cyl;
    u32 fails_before = v86_disk_fail_n;
    int slot;

    xfer_done = 0;      /* SEEK 等でも「今回の転送量」が 0 になるように */

    /* READ ID 以外が来たらトラック内の走査位置を戻す (実機と同じ) */
    if (cmd != V86_DISK_READID) {
        cur_id_idx = 0;
    }

    /* 装置番号 (DA/UA) の判定。こちらが持っていない装置には
     * 「レディでない」で答える。
     *
     * 初期化 (AH=03h) だけは実機も装置の有無を見ずに答えるので通す
     * (bios1b.c: `if ((CPU_AH & 0x0f) != 0x03) { ...ready チェック... }`)。 */
    slot = daua_to_slot(ax_in & U8_MAX);
    if (cmd != V86_DISK_INIT && slot < 0) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_DEVICE;
        bios_set_ah(frame, V86_DISK_ST_NOT_READY);
        bios_set_cf(frame, 1);
        disklog(frame, ax_in, phys_in, 0, v86_disk_fail_why);
        return;
    }
    cur_fdd_slot = (slot >= 0) ? slot : V86_DISK_SLOT;

    switch (cmd) {
    case V86_DISK_SENSE:
        disk_sense(frame);
        break;

    case V86_DISK_READ:
    case V86_DISK_READ_DIAG:
        disk_read(frame);
        break;

    case V86_DISK_WRITE:
        disk_write(frame);
        break;

    case V86_DISK_VERIFY:
        disk_verify(frame);
        break;

    case V86_DISK_READID:
        disk_readid(frame);
        break;

    case V86_DISK_SEEK:
        /* CL/DH の物理位置へヘッドを動かす。イメージ相手なので実体は
         * 無いが、**位置は必ず憶えておくこと**。この後の READ は
         * 「ここに居る物理トラック上で ID を照合する」ためにこれを使う。
         * ヘッドも憶える — シリンダだけだと Ys の 3 番目のロードが
         * 物理トラック 0 を見に行って空振りする。 */
        cur_cyl  = (u8)(frame[V86F_ECX] & U8_MAX);
        /* DH はそのまま憶える。**1 ビットにマスクしてはいけない。**
         * 実測で DH=3 の SEEK が来ることがあり、マスクするとヘッド 1 の
         * 別トラックが「それらしく」読めてしまう。ID は合っているので
         * 成功として返ってしまい、中身だけが違う — いちばん質の悪い壊れ方。
         * マスクしなければ範囲外トラックとして読みが失敗し、ゲストは
         * 自分で DH=0 に直して読み直す。その結果が正しい。 */
        cur_head = (u8)((frame[V86F_EDX] >> 8) & U8_MAX);
        v86_disk_seek_n++;
        v86_disk_seek_cyl = ((u32)cur_head << 8) | cur_cyl;
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        break;

    case V86_DISK_RECAL:
        /* トラック 0 へ戻す */
        cur_cyl  = 0;
        cur_head = 0;
        v86_disk_seek_n++;
        v86_disk_seek_cyl = 0;
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        break;

    case V86_DISK_INIT:
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        break;

    default:
        /* WRITE (05) / FORMAT (0D) / 密度設定 (0E) は未実装。
         * CF=1 / AH=0x86 (機能なし) で返せばゲストは自前の代替手段に進める。 */
        bios_set_ah(frame, BIOS_DISK_STATUS_UNSUPPORTED);
        bios_set_cf(frame, 1);
        break;
    }

    disklog(frame, ax_in, phys_in, xfer_done,
            (v86_disk_fail_n != fails_before) ? v86_disk_fail_why
          : ((frame[V86F_EFLAGS] & 1UL) ? V86_DISKLOG_CF_ERROR : 0U));
}

