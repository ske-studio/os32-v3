/* FDD の D88 セクタ読み取りと READ / VERIFY。 */

/* D88 の 1 セクタ読み。
 *
 * 物理トラックを (phys_cyl, head) で選び、その上で ID (id_c, id_h, id_r) を
 * 照合する。見つかったセクタの実データ長ぶんを buf へ読む。
 *
 * loop_dev_read_chs() は使えない。あれは「物理=論理」を仮定するうえ、
 * 読み出し長をイメージ全体の bps (= トラック 0 の値。Ys.D88 では 256) で
 * 頭打ちにするため、1024 バイトセクタが 256 バイトしか転送されない。
 *
 * セクタ長 (= コマンドの N) も照合する。**ここを緩めてはいけない。**
 * 緩めた実装で実測したところ、ゲストが N=3 (1024B) を要求した読みに
 * トラック 0 の 256B セクタを 3 個返して「成功」と答えてしまい、
 * ロード先の 3/4 が未初期化のまま残ってゲストがそこへ飛んで #UD になった。
 * 見つからないなら見つからないと答えるほうがゲストは正しく振る舞える。
 * NP21/W も searchsector_d88() で c/h/r に加えて n を比較している。
 *
 * 戻り値: 読めたバイト数、-1 = セクタ不在 / 長さ不一致 / I/O エラー */
static int d88_read_sector(u32 phys_cyl, u32 head,
                           u32 id_c, u32 id_h, u32 id_r, u32 seclen,
                           u8 *buf, u32 buflen, u16 *out_spt)
{
    u32 off, data_len = 0, to_read;
    u16 spt = 0;
    int fd;

    off = loop_dev_seek_d88(cur_fdd_slot,
                            (u8)phys_cyl, (u8)head,
                            (u8)id_c, (u8)id_h, (u8)id_r,
                            &data_len, &spt);
    if (off == 0 || data_len == 0 || data_len != seclen) {
        return -1;
    }
    fd = loop_dev_get_fd(cur_fdd_slot);
    if (fd < 0) {
        return -1;
    }
    to_read = data_len;
    if (to_read > buflen) {
        to_read = buflen;
    }
    vfs_seek(fd, (int)off, 0);
    if (vfs_read_fd(fd, buf, to_read) < (int)to_read) {
        return -1;
    }
    if (out_spt) {
        *out_spt = spt;
    }
    return (int)to_read;
}

/* AH=06h READ DATA / AH=01h VERIFY — CHS を進めながら BX バイト転送する。
 *
 * transfer=0 ならゲストのバッファへは書かずに読めるかどうかだけ見る
 * (ベリファイ)。実機も同じ作りで、bios1b.c の case 0x01 と case 0x06 は
 * `MEML_WRITES()` の有無しか違わない。 */
static void disk_read_common(u32 *frame, int transfer)
{
    u32 cyl   = (frame[V86F_ECX] >> 0) & U8_MAX;     /* CL */
    u32 n     = (frame[V86F_ECX] >> 8) & U8_MAX;     /* CH: セクタ長コード */
    u32 head  = (frame[V86F_EDX] >> 8) & U8_MAX;     /* DH */
    u32 sect  = (frame[V86F_EDX] >> 0) & U8_MAX;     /* DL (1 起算) */
    u32 count = frame[V86F_EBX] & U16_MAX;          /* BX: バイト数 */
    u32 dst   = ((frame[V86F_ES] & U16_MAX) << X86_REAL_SEG_SHIFT) +
                (frame[V86F_EBP] & U16_MAX);
    u16 cyls; u8 heads, spt; u16 bps; u32 total;
    u32 seclen;
    u32 ah    = (frame[V86F_EAX] >> 8) & U8_MAX;
    u32 al    = frame[V86F_EAX] & U8_MAX;            /* DA/UA */
    u32 phys_cyl;
    u32 phys_head = V86_FDC_HEAD(head, al);
    int is_d88 = (loop_dev_get_format(cur_fdd_slot) == LOOP_FMT_D88);
    /* PC-98 のセクタ長は最大 1024 バイト (N=3)。 */
    static u8 secbuf[V86_SECTOR_BUFFER_BYTES];

    /* AH bit4 が立っていたら READ 自身がシークする (bios1b.c:375
     * `if (CPU_AH & 0x10) biosfd_seek(CPU_CL, ...)`)。
     * 立っていなければ直前の SEEK が決めた位置のまま読む。 */
    if (ah & BIOS_DISK_CMD_SEEK) {
        cur_cyl[cur_fdd_slot] = (u8)cyl;
    }
    phys_cyl = cur_cyl[cur_fdd_slot];

    v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
    v86_disk_fail_len = (n << 16) | count;
    v86_disk_fail_dst = dst;

    if (count == 0) {
        /* 実機は BX=0 を「何もせず成功」として返す (while(size) に入らない) */
        bios_set_ah(frame, 0x00);
        bios_set_cf(frame, 0);
        return;
    }
    /* 実機は 64KB 境界を跨ぐ転送をエラーにする (bios1b.c: AH=0x20)。
     * ES:BP のオフセットは 16bit なので、跨いだ瞬間にゲストが意図した
     * 場所と違うところへ落ちる。素通しにすると壊れ方が静かになる。 */
    if ((dst & U16_MAX) > ((dst + count - 1) & U16_MAX)) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_RANGE;
        bios_set_ah(frame, V86_DISK_ST_DMA_BOUNDARY);
        bios_set_cf(frame, 1);
        return;
    }
    if (transfer && !guest_range_ok(dst, count)) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_RANGE;
        bios_set_ah(frame, V86_DISK_ST_RANGE);       /* パラメータ異常 */
        bios_set_cf(frame, 1);
        return;
    }
    if (loop_dev_get_geometry(cur_fdd_slot, &cyls, &heads, &spt,
                              &bps, &total) != 0 || spt == 0) {
        bios_set_ah(frame, V86_DISK_ST_NOT_READY);
        bios_set_cf(frame, 1);
        return;
    }

    /* 転送単位はゲストの CH (セクタ長コード) に従う。実機の FDC と同じで、
     * 何バイトのセクタを読むかは呼び出し側が指定する。
     *
     * loop_dev が報告する bps を使ってはいけない。D88 はトラックごとに
     * セクタ長が違い得るからで、実際 Ys.D88 はトラック 0 が 256B x 16、
     * 以降が 1024B x 5 になっている。bps (=先頭トラックの値) で弾くと、
     * IPL の次段が読めずに INT 1Bh を無限リトライする。
     *
     * ただし loop_dev はセクタの実長ぶんを書き込むので、ゲストのバッファへ
     * 直接読ませるとサイズが食い違ったときに溢れる。いったんカーネル側の
     * バウンスバッファへ受けてから、ゲストが要求した長さだけ渡す。 */
    seclen = (unsigned long)BIOS_SECTOR_BASE_BYTES << (n & BIOS_SECTOR_N_MASK);
    while (count) {
        u32 i;
        u32 got;                    /* セクタから読めたバイト数 */
        u32 xfer;                   /* うちゲストへ渡す長さ */
        u16 trk_spt = 0;

        /* 実機は最後の 1 セクタだけ端数を転送する
         * (bios1b.c: `accesssize = min(size, secsize)`)。
         * **端数を捨てて成功を返してはいけない。** 以前は
         * `while (count >= seclen)` で回していたので、BX がセクタ長の
         * 倍数でないとき残りを黙って捨てて CF=0 を返していた。
         * ゲストは読めたつもりで未初期化領域を使うことになる。 */
        xfer = (count > seclen) ? seclen : count;

        if (is_d88) {
            /* まず SEEK で決まった物理トラックを見る (実機の FDC と同じ)。 */
            int r = d88_read_sector(phys_cyl, phys_head, cyl, head, sect,
                                    seclen, secbuf, seclen, &trk_spt);
            if (r < 0) {
                /* そこに無ければ「物理=論理」でもう一度探す。
                 * ゲストが SEEK を省いて BIOS の暗黙シークに頼る
                 * 場合があるため。どちらで読めたかは実測できるよう
                 * カウンタを分けてある。 */
                r = d88_read_sector(cyl, head, cyl, head, sect,
                                    seclen, secbuf, seclen, &trk_spt);
                if (r < 0) {
                    v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_SECTOR;
                    v86_disk_fail_chs = (phys_cyl << 24) | (phys_head << 28) |
                                        (cyl << 16) | (head << 8) | sect;
                    bios_set_ah(frame, V86_DISK_ST_RANGE);   /* セクタ不在 */
                    bios_set_cf(frame, 1);
                    return;
                }
                phys_cyl  = cyl;
                phys_head = head;
                v86_disk_ident_n++;
            } else if (phys_cyl != cyl || phys_head != head) {
                v86_disk_shift_n++;
            }
            got = (u32)r;
            if (trk_spt != 0) {
                spt = (u8)trk_spt;      /* トラックごとに違い得る */
            }
        } else {
            /* RAW/FDI はイメージ全体で 1 つのセクタ長しか持たない。
             * ゲストが違う長さを要求してきたら、実機の FDC が ID 照合で
             * 空振りするのと同じく「セクタ不在」で返す。
             *
             * **ここを素通りさせてはいけない。** N を見ていなかった頃は、
             * MS-DOS が SASI 規約で投げた「128B / セクタ 0」の読みに
             * FAT 領域のゴミを返して成功と答えていた
             * (docs/archive/v21/v86v2/08_dos5.md §2)。
             * セクタ長を照合しておけば secbuf の溢れも同時に閉じる。 */
            if (seclen != (u32)bps) {
                v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_SECTOR_SIZE;
                v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
                bios_set_ah(frame, V86_DISK_ST_RANGE);
                bios_set_cf(frame, 1);
                return;
            }
            if (loop_dev_read_chs(cur_fdd_slot, (u16)cyl, (u8)head,
                                  (u8)sect, secbuf) != 0) {
                v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_SECTOR;
                v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
                bios_set_ah(frame, V86_DISK_ST_RANGE);   /* セクタ不在 */
                bios_set_cf(frame, 1);
                return;
            }
            got = seclen;
        }

        if (got > xfer) {
            got = xfer;                 /* 端数セクタは要求ぶんだけ渡す */
        }
        if (transfer) {
            for (i = 0; i < got; i++) {
                ((volatile u8 *)dst)[i] = secbuf[i];
            }
        }
        xfer_done += got;
        dst   += xfer;
        count -= xfer;

        /* 次のセクタへ。**実機はトラックを跨がない。**
         *
         * bios1b.c は最終セクタ (R == para) を読んだ時点で
         *   - AH bit7 が立っていて物理ヘッドが 0 なら → 裏面へ続ける
         *   - そうでなければ → ループを抜ける (残っていればエラー)
         * となっていて、**シリンダは絶対に跨がない**。
         * 以前はここでヘッドもシリンダも自由に繰り上げていたので、
         * 実機ならエラーになる要求が「成功」として通ってしまい、
         * 隣のトラックの中身が返っていた。
         *
         * 物理位置も同じ歩幅で動かす。ID のずれは連続領域では一定なので、
         * 差を保ったまま動かせば整合が取れる。 */
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
        v86_disk_fail_chs = (cyl << 16) | (head << 8) | sect;
        bios_set_ah(frame, V86_DISK_ST_INCOMPLETE);       /* 転送しきれなかった */
        bios_set_cf(frame, 1);
        return;
    }

    bios_set_ah(frame, 0x00);
    bios_set_cf(frame, 0);
}

static void disk_read(u32 *frame)   { disk_read_common(frame, 1); }
static void disk_verify(u32 *frame) { disk_read_common(frame, 0); }

