/* ======================================================================== */
/*  hsync_bootold.inc — `/boot/vmkernel.old` を作ってよいかの純規則          */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md §1-v3「`.old` の識別は        */
/*  カーネルイメージの CRC」。hsync は `/boot/vmkernel.lz4` を置き換える前に、*/
/*  **ディスク上の今の vmkernel.lz4 の CRC** と**起動したイメージの CRC**     */
/*  (KAPI v65 boot_image_info、ローダが検査して記録した値) を比べ、一致した  */
/*  ときだけ `.old` を作る (複製 → 検証 → rename)。一致しない (FD 起動・前回 */
/*  の更新の後に未起動・記録が無い) ときは**断る** — 一度も起動していない版 */
/*  を `.old` に残すと、戻したときに起動する保証が無い。`--no-backup` を     */
/*  明示したときだけ `.old` を作らずに進む。                                */
/*                                                                          */
/*  CRC は VK32 v2 の**ファイル全体の CRC32 を image_crc 欄を 0 として**     */
/*  求めたもの (zlib.crc32 と同じ)。欄の位置は entry_count から決まる:       */
/*    image_crc = 16 + 20n + 4  (n = entry_count、boot/boot_defs.h の         */
/*    VK32_OFF_IMAGE_CRC)。ユーザーランドは boot/ を見ないので値を写して     */
/*    持つ — tools/tests/test_serialfs.py が boot_defs.h と照合する。        */
/*                                                                          */
/*  依存は crc32_core.inc (includer が先に include する) と u8 / u32 だけ。  */
/*  [C1] C89 / GNU89。                                                      */
/* ======================================================================== */
#ifndef HSYNC_BOOTOLD_INC
#define HSYNC_BOOTOLD_INC

#define HBO_VK32_MAGIC        0x32334B56UL   /* 'VK32' (VK32_MAGIC) */
#define HBO_VK32_VERSION      2UL            /* VK32_VERSION */
#define HBO_VK32_MAX_ENTRIES  4UL            /* VK32_MAX_ENTRIES */
#define HBO_VK32_COMMON_SIZE  16UL           /* VK32_COMMON_SIZE */
#define HBO_VK32_ENTRY_SIZE   16UL           /* VK32_ENTRY_SIZE */

/* 判定の結果 */
#define HBO_MAKE        0   /* 起動したイメージと一致 → .old を作る */
#define HBO_NO_BACKUP   1   /* --no-backup: 作らずに進む */
#define HBO_REFUSE_INFO 2   /* 起動したイメージの記録が無い → 断る */
#define HBO_REFUSE_DISK 3   /* ディスクの vmkernel.lz4 が読めない / VK32 v2 でない → 断る */
#define HBO_REFUSE_DIFF 4   /* 一致しない (起動していない版) → 断る */
#define HBO_REFUSE_CORRUPT 5 /* ファイルの image_crc 欄と中身が合わない (欄が壊れている) → 断る */

static u32 hbo_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

/* 先頭 n バイトから image_crc 欄の位置を出す。0 = *off に位置 / -1 = VK32
 * v2 として読めない */
static int hbo_crc_offset(const u8 *head, u32 n, u32 *off)
{
    u32 cnt;

    if (n < HBO_VK32_COMMON_SIZE) return -1;
    if (hbo_le32(head) != HBO_VK32_MAGIC) return -1;
    if (hbo_le32(head + 8) != HBO_VK32_VERSION) return -1;
    cnt = hbo_le32(head + 12);
    if (cnt == 0 || cnt > HBO_VK32_MAX_ENTRIES) return -1;
    *off = HBO_VK32_COMMON_SIZE + cnt * (HBO_VK32_ENTRY_SIZE + 4UL) + 4UL;
    return 0;
}

/* 内部状態 state に chunk (ファイル上の位置 pos から len バイト) を送る。
 * [crc_off, crc_off + 4) のバイトは 0 として送る。 */
static u32 hbo_crc_update(u32 state, const u8 *chunk, u32 len, u32 pos,
                          u32 crc_off)
{
    static const u8 zero4[4] = { 0, 0, 0, 0 };
    u32 i = 0;

    while (i < len) {
        u32 at = pos + i;
        if (at >= crc_off && at < crc_off + 4UL) {
            u32 k = crc_off + 4UL - at;       /* 欄の残り */
            if (k > len - i) k = len - i;
            state = crc32_core_update(state, zero4, k);
            i += k;
        } else {
            u32 k = len - i;
            if (at < crc_off && crc_off - at < k) k = crc_off - at;
            state = crc32_core_update(state, chunk + i, k);
            i += k;
        }
    }
    return state;
}

/* have_info = 起動したイメージの記録がある (crc_valid)、disk_ok = ディスクの
 * CRC が求まった、disk_crc = 欄を 0 として求めた CRC、stored_crc = ファイルの
 * image_crc 欄に**書いてある**値。
 * **欄も検査する** (レビュー往復 1、Codex 2): 中身は起動した版と同じで欄だけが
 * 壊れたファイルは、ローダが VK32_ERR_FILE_CRC で断る = 戻しても起動しない。
 * 同じ関数を**公開する一時ファイル (.old の複製)** にも掛ける。 */
static int hbo_decide(int no_backup, int have_info, u32 booted_crc,
                      int disk_ok, u32 disk_crc, u32 stored_crc)
{
    if (no_backup) return HBO_NO_BACKUP;
    if (!have_info) return HBO_REFUSE_INFO;
    if (!disk_ok) return HBO_REFUSE_DISK;
    if (stored_crc != disk_crc) return HBO_REFUSE_CORRUPT;
    if (booted_crc != disk_crc) return HBO_REFUSE_DIFF;
    return HBO_MAKE;
}

#endif /* HSYNC_BOOTOLD_INC */
