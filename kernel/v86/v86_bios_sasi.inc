/* SASI/IDE HDD のアドレス計算、転送、INT 1Bh コマンド処理。 */

/* ======================================================================== */
/*  INT 1Bh — SASI/IDE HDD (DA=0x80 / 旧表記 0x00)                          */
/*                                                                          */
/*  **FDD と共用しない。** レジスタ規約 (CX/DL/BX)、エラーコード             */
/*  (0xD0/0x40 系)、転送モデル (LBA 線形 vs トラック内)、コマンド表          */
/*  (READ ID の成否が逆) のすべてが違う。if を差し込むと 08_dos5.md §2 の    */
/*  「偶然動く」型の事故が再発する。                                          */
/*                                                                          */
/*  正本: np21w-src/src/bios/sxsibios.c (sxsi_pos / sasifunc / sense)。      */
/*  実測: docs/archive/v21/v86v2/10_dos_hdd.md §2 (2026-08-11 の 154 呼び出し)。   */
/*                                                                          */
/*  レジスタ規約 (FDD と違う点だけ):                                         */
/*    CHS モード (AL bit7=1): CX=16bit シリンダ全体 / DH=ヘッド /            */
/*                            DL=セクタ (**0 起算**)                          */
/*    LBA モード (AL bit7=0): pos = (DL<<16) | CX                            */
/*    BX = バイト数。**0 は 0x10000 (64KB)** — FDD の「0 なら何もしない」と逆 */
/*    転送は pos++ の線形でトラックもシリンダも自由に跨ぐ                     */
/*  戻り: AH >= 0x20 のときだけ CF=1 (0x0F は CF=0 の成功扱い)               */
/* ======================================================================== */

/* 戻り AH と CF をまとめて書く。CF の閾値 0x20 は bios1b.c の
 * `if (ret_ah >= 0x20) flag += 1;` と np2sysp_sasi の両方に一致。 */
static void sasi_done(u32 *frame, u32 ret)
{
    bios_set_ah(frame, ret);
    bios_set_cf(frame, ret >= BIOS_DISK_STATUS_ERROR_MIN);
}

/* アドレッシング。sxsi_pos() の移植。範囲外は 0xD0。 */
static u32 sasi_pos(u32 *frame, u16 cyls, u8 heads, u8 spt, u32 total_lba,
                    u32 *out_pos)
{
    u32 al = frame[V86F_EAX] & U8_MAX;
    u32 cx = frame[V86F_ECX] & U16_MAX;
    u32 dh = (frame[V86F_EDX] >> 8) & U8_MAX;
    u32 dl = frame[V86F_EDX] & U8_MAX;
    u32 pos;

    if (al & BIOS_DISK_CMD_CHS) {
        /* CHS: DL は 0 起算。範囲照合してから線形位置へ落とす。 */
        if (dl >= (u32)spt || dh >= (u32)heads || cx >= (u32)cyls) {
            return V86_DISK_ST_IO;
        }
        pos = (cx * (u32)heads + dh) * (u32)spt + dl;
    } else {
        /* LBA: 総セクタ数 <= 0xFFFFFF (u32 ファイルサイズの上限から
         * 4GB/512B = 8.4M セクタ < 16.7M) なので (DL<<16)|CX で足りる。 */
        pos = (dl << 16) | cx;
    }
    if (pos >= total_lba) {
        return V86_DISK_ST_IO;
    }
    *out_pos = pos;
    return 0;
}

/* READ (06) / WRITE (05)。バウンスバッファ 1 セクタで pos++ の線形転送。
 * ゲスト側アドレスは ES:BP からリニアに進み、セグメントの繰り上がりは
 * 気にしない (実機の MEML_READS/WRITES も物理線形)。 */
static u8 sasi_bounce[V86_SASI_BOUNCE_BYTES];

static u32 sasi_rw(u32 *frame, int slot, int is_write,
                   u16 bps, u32 pos, u32 total_lba)
{
    u32 size = frame[V86F_EBX] & U16_MAX;
    u32 addr = ((frame[V86F_ES] & U16_MAX) << X86_REAL_SEG_SHIFT)
             + (frame[V86F_EBP] & U16_MAX);
    u32 r;

    if (size == 0) {
        size = V86_BIOS_COUNT_ZERO_BYTES;         /* BX=0 は 64KB。FDD と逆 (sxsibios.c) */
    }

    if (is_write && !loop_dev_is_writable(slot)) {
        return V86_DISK_ST_WRITE_PROTECT;            /* ライトプロテクト。黙って捨てない */
    }

    while (size) {
        r = size;
        if (r > (u32)bps) {
            r = bps;
        }
        if (pos >= total_lba) {
            return V86_DISK_ST_IO;        /* 転送中に末尾を越えた */
        }
        if (!guest_range_ok(addr, r)) {
            v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_RANGE;
            v86_disk_fail_dst = addr;
            return V86_DISK_ST_RANGE;        /* 直接低位 (V86_LOW) の外。書くと カーネルが壊れる */
        }
        if (is_write) {
            /* 端数はセクタを読んでから前半だけ差し替える (RMW)。 */
            if (r < (u32)bps) {
                if (loop_dev_read_lba(slot, pos, 1, sasi_bounce) != 0) {
                    return V86_DISK_ST_IO;
                }
            }
            kmemcpy(sasi_bounce, (const void *)addr, r);
            if (loop_dev_write_lba(slot, pos, 1, sasi_bounce) != 0) {
                return V86_DISK_ST_IO;
            }
        } else {
            if (loop_dev_read_lba(slot, pos, 1, sasi_bounce) != 0) {
                return V86_DISK_ST_IO;
            }
            kmemcpy((void *)addr, sasi_bounce, r);
        }
        addr += r;
        size -= r;
        pos++;
        xfer_done += r;
    }
    return 0;
}

/* SENSE (nibble=4)。AH=84h のときだけジオメトリをレジスタで返す。
 * 戻り AH は SASI 標準ジオメトリ (SPT=33) なら容量クラス、それ以外は
 * 0x0F (CF=0)。NHD/HDI (SPT=17 等) は常に 0x0F — 実測どおり。 */
static u32 sasi_sense(u32 *frame, u16 cyls, u8 heads, u8 spt, u16 bps)
{
    u32 ah = (frame[V86F_EAX] >> 8) & U8_MAX;

    if (ah == BIOS_DISK_SENSE) {
        frame[V86F_EBX] = (frame[V86F_EBX] & V86_REG_AX_KEEP_MASK) | bps;
        frame[V86F_ECX] = (frame[V86F_ECX] & V86_REG_AX_KEEP_MASK) | cyls;
        frame[V86F_EDX] = (frame[V86F_EDX] & V86_REG_AX_KEEP_MASK)
                        | ((u32)heads << 8) | spt;
    }
    if (spt == 33 && heads <= 8) {
        u32 bytes = (u32)cyls * heads * spt * bps;
        if (bytes < 10475520U)  return 0;   /* < 10MB */
        if (bytes < 15713280U)  return 1;   /* < 15MB */
        if (bytes < 20275200U)  return 2;   /* < 20MB */
        if (bytes < 41564160U)  return 3;   /* < 40MB */
        if (bytes == 41564160U) return 4;   /* = 40MB */
    }
    return V86_DISK_ST_DONE;
}

/* INIT (nibble=3)。実機は BDA 0x55C の SASI ビットを接続台数で立て直す
 * (sxsibios.c sasibios_init)。装置の有無は見ずに成功する。 */
static u32 sasi_init(void)
{
    u8 *guest = (u8 *)0;
    u16 equip = *(u16 *)(guest + BIOS_WORK_DISK_EQUIP);
    int i;

    equip &= BIOS_DISK_SASI_MASK;
    for (i = 0; i < V86_DRIVE_MAX; i++) {
        if (v86_drives[i].in_use &&
            (v86_drives[i].daua & BIOS_DA_TYPE_MASK) == V86_DAUA_HDD) {
            equip |= (u16)(BIOS_DISK_SASI_BIT0 << (v86_drives[i].daua & BIOS_DA_UNIT_MASK));
        }
    }
    *(u16 *)(guest + BIOS_WORK_DISK_EQUIP) = equip;
    return 0;
}

static void bios_sasi_int1b(u32 *frame)
{
    u32 cmd = (frame[V86F_EAX] >> 8) & BIOS_DISK_CMD_MASK;
    u32 ax_in = frame[V86F_EAX] & U16_MAX;
    u32 fails_before = v86_disk_fail_n;
    u32 ret;
    u32 pos = 0;
    int slot;
    u16 cyls = 0, bps = 0;
    u8 heads = 0, spt = 0;
    u32 total_lba = 0;

    xfer_done = 0;

    /* INIT 以外は装置の有無を先に見る (sasibios_operate と同じ)。 */
    slot = daua_to_slot(ax_in & U8_MAX);
    if (cmd == V86_DISK_INIT) {
        ret = sasi_init();
        sasi_done(frame, ret);
        disklog(frame, ax_in, 0, 0, 0);
        return;
    }
    if (slot < 0 || !loop_dev_is_hdd(slot)) {
        v86_disk_fail_n++; v86_disk_fail_why = V86_DISK_FAIL_NO_DEVICE;
        sasi_done(frame, V86_DISK_ST_NOT_READY);
        disklog(frame, ax_in, 0, 0, v86_disk_fail_why);
        return;
    }
    loop_dev_get_geometry(slot, &cyls, &heads, &spt, &bps, &total_lba);

    switch (cmd) {
    case V86_DISK_VERIFY:               /* 実機も実読みしない */
    case V86_DISK_RECAL:
    case V86_SASI_RETRACT2:
        ret = 0;
        break;

    case V86_DISK_SENSE:
        ret = sasi_sense(frame, cyls, heads, spt, bps);
        break;

    case V86_DISK_READ:
    case V86_DISK_WRITE:
        ret = sasi_pos(frame, cyls, heads, spt, total_lba, &pos);
        if (ret == 0) {
            ret = sasi_rw(frame, slot, (cmd == V86_DISK_WRITE),
                          bps, pos, total_lba);
        }
        break;

    default:
        /* SEEK(0)/READ_DIAG(2)/READID(A)/FORMAT(D)/密度(E) その他。
         * SASI では失敗が正 — 実測でも AH=8E が飛んできて 0x40 で
         * 流している (10_dos_hdd.md §2-2)。 */
        ret = V86_DISK_ST_RANGE;
        break;
    }

    sasi_done(frame, ret);
    disklog(frame, ax_in, pos & 0xFFFFFFU, xfer_done,
            (v86_disk_fail_n != fails_before) ? v86_disk_fail_why
          : ((ret >= BIOS_DISK_STATUS_ERROR_MIN) ? V86_DISKLOG_CF_ERROR : 0U));
}

