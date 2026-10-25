#include "ide_limits.h"
#include "ext2_priv.h"
#include "ext2_layout.h"   /* 大きさの固定点 (票 TASK_HDD_INSTALL N7) */
#include "ide.h"
#include "bootinfo.h"   /* BOOTINFO_GEOM_BIOS */
#include "kprintf.h"

/*  ext2_format — ディスクにext2ファイルシステムを作成                       */
/*                                                                          */
/*  マルチブロックグループ対応、1KBブロック、128バイトinode。                 */
/*  SPARSE_SUPER対応: Linux mkfs.ext2/e2fsck互換のレイアウトを生成する。     */
/*                                                                          */
/*  マルチインスタンス対応:                                                  */
/*    フォーマットは一時的なExt2Ctxを使用 (既存マウントとは独立)。           */
/*    呼び出し側でExt2Ctxをスタック上に確保するか、kmalloc/kfreeする。       */
/*                                                                          */
/*  スパースグループ規則:                                                    */
/*    グループ 0, 1, 3^n, 5^n, 7^n にのみSBバックアップ+GDTコピーを配置。  */
/*    それ以外のグループはビットマップから開始する。                          */
/*                                                                          */
/*  各グループのレイアウト:                                                  */
/*    スパースグループ:                                                      */
/*      SBバックアップ(1) + GDTコピー(N) + bmap(1) + imap(1) + itable       */
/*    非スパースグループ:                                                    */
/*      bmap(1) + imap(1) + itable                                          */
/* ======================================================================== */

/* グループごとのメタデータ情報 */
typedef struct {
    u32 group_start;    /* グループの最初のブロック番号 (first_data_blockからの絶対値) */
    u32 block_bitmap;   /* ブロックビットマップのブロック番号 */
    u32 inode_bitmap;   /* inodeビットマップのブロック番号 */
    u32 inode_table;    /* inodeテーブル開始ブロック番号 */
    u32 data_start;     /* データブロック開始位置 */
    u32 blocks_in_group;/* このグループのブロック数 */
    u32 inodes_in_group;/* このグループのinode数 */
    u32 inode_tbl_blocks;/* inodeテーブルのブロック数 */
} GroupLayout;

#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER  0x0001

/* SPARSE_SUPER規則: 0, 1, 3^n, 5^n, 7^n のグループにSBバックアップを配置
 * (規則の正典は fs/ext2_layout.c — 必要量の計算と同じ関数を使う) */
#define is_sparse_group(g)  ext2_layout_is_sparse(g)

/* ===== Block 1: スーパーブロック =====
 * lay (ext2_layout_plan の結果) から SB を組んで block 1 に書く。
 * ext2_write_block の結果をそのまま返し、EXT2_ERR_IO への変換は呼び手がする。
 * s_free_blocks_count はここでは仮値 0 (ext2_format_range が後で埋める)。 */
static inline int fmt_write_super(Ext2Ctx *ctx, const Ext2Layout *lay)
{
    ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
    le32_wr(&ext2_g_blk[EXT2_SB_INODES_COUNT_OFF], lay->inodes_count);         /* s_inodes_count */
    le32_wr(&ext2_g_blk[EXT2_SB_BLOCKS_COUNT_OFF], lay->total_blocks);         /* s_blocks_count */
    le32_wr(&ext2_g_blk[EXT2_SB_R_BLOCKS_COUNT_OFF], 0);                         /* s_r_blocks_count */
    /* s_free_blocks_count は後で計算 (仮値) */
    le32_wr(&ext2_g_blk[EXT2_SB_FREE_BLOCKS_COUNT_OFF], 0);
    le32_wr(&ext2_g_blk[EXT2_SB_FREE_INODES_COUNT_OFF], lay->inodes_count - EXT2_RESERVED_INODES);   /* s_free_inodes_count (予約inode 1-10) */
    le32_wr(&ext2_g_blk[EXT2_SB_FIRST_DATA_BLOCK_OFF], 1);                        /* s_first_data_block (1 for 1KB block) */
    le32_wr(&ext2_g_blk[EXT2_SB_LOG_BLOCK_SIZE_OFF], 0);                        /* s_log_block_size (0 = 1KB) */
    le32_wr(&ext2_g_blk[EXT2_SB_LOG_FRAG_SIZE_OFF], 0);                        /* s_log_frag_size */
    le32_wr(&ext2_g_blk[EXT2_SB_BLOCKS_PER_GROUP_OFF], EXT2_BLOCKS_PER_GROUP_MAX); /* s_blocks_per_group */
    le32_wr(&ext2_g_blk[EXT2_SB_FRAGS_PER_GROUP_OFF], EXT2_BLOCKS_PER_GROUP_MAX); /* s_frags_per_group */
    le32_wr(&ext2_g_blk[EXT2_SB_INODES_PER_GROUP_OFF], lay->inodes_per_group);    /* s_inodes_per_group */
    le32_wr(&ext2_g_blk[EXT2_SB_MTIME_OFF], 0);                        /* s_mtime */
    le32_wr(&ext2_g_blk[EXT2_SB_WTIME_OFF], ext2_current_time());      /* s_wtime */
    le16_wr(&ext2_g_blk[EXT2_SB_MNT_COUNT_OFF], 0);                        /* s_mnt_count */
    le16_wr(&ext2_g_blk[EXT2_SB_MAX_MNT_COUNT_OFF], (u16)0xFFFF);              /* s_max_mnt_count */
    le16_wr(&ext2_g_blk[EXT2_SB_MAGIC_OFF], EXT2_SUPER_MAGIC);         /* s_magic */
    le16_wr(&ext2_g_blk[EXT2_SB_STATE_OFF], EXT2_VALID_FS); /* s_state */
    /* s_errors = RO (票 B8 往復 5)。OS32 自身は値に関わらずメタデータの I/O エラーで
     * 以後の書き込みを止める (ext2_fs_error)。以前の CONTINUE はその振る舞いと
     * 食い違い、ホストの Linux がこの像をマウントしたときにもエラー後に書き続けた。 */
    le16_wr(&ext2_g_blk[EXT2_SB_ERRORS_OFF], EXT2_ERRORS_RO);
    le16_wr(&ext2_g_blk[EXT2_SB_MINOR_REV_LEVEL_OFF], 0);                        /* s_minor_rev_level */
    le32_wr(&ext2_g_blk[EXT2_SB_LASTCHECK_OFF], 0);                        /* s_lastcheck */
    le32_wr(&ext2_g_blk[EXT2_SB_CHECKINTERVAL_OFF], 0);                        /* s_checkinterval */
    le32_wr(&ext2_g_blk[EXT2_SB_CREATOR_OS_OFF], 0);                        /* s_creator_os = LINUX */
    le32_wr(&ext2_g_blk[EXT2_SB_REV_LEVEL_OFF], 1);                        /* s_rev_level = DYNAMIC_REV */
    le16_wr(&ext2_g_blk[EXT2_SB_DEF_RESUID_OFF], 0);                        /* s_def_resuid */
    le16_wr(&ext2_g_blk[EXT2_SB_DEF_RESGID_OFF], 0);                        /* s_def_resgid */
    le32_wr(&ext2_g_blk[EXT2_SB_FIRST_INO_OFF], EXT2_FIRST_NONRESERVED_INO);                       /* s_first_ino */
    le16_wr(&ext2_g_blk[EXT2_SB_INODE_SIZE_OFF], EXT2L_INODE_SIZE);                      /* s_inode_size */
    le16_wr(&ext2_g_blk[EXT2_SB_BLOCK_GROUP_NR_OFF], 0);                        /* s_block_group_nr */
    le32_wr(&ext2_g_blk[EXT2_SB_FEATURE_INCOMPAT_OFF], 0x0002);                   /* s_feature_incompat = FILETYPE */
    le32_wr(&ext2_g_blk[EXT2_SB_FEATURE_RO_COMPAT_OFF], EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER);
    /* s_volume_name at offset 120 */
    ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF] = 'O'; ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF + 1] = 'S'; ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF + 2] = '3'; ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF + 3] = '2';
    ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF + 4] = '_'; ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF + 5] = 'H'; ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF + 6] = 'D'; ext2_g_blk[EXT2_SB_VOLUME_NAME_OFF + 7] = 'D';

    return ext2_write_block(ctx, 1, ext2_g_blk);
}

/* ===== グループ0: inode 2 (ルートディレクトリ) =====
 * inode テーブルの inode 2 を読み書きして root を作り、直後のデータブロックに
 * "." と ".." を書く。ext2_g_blk を作業域に使う。I/O が失敗したら EXT2_ERR_IO、
 * 成功で EXT2_OK。 */
static inline int fmt_write_root(Ext2Ctx *ctx, u32 gdt_blocks,
                                 u32 inode_tbl_blocks_per_group)
{
    int ret;
    u32 itable_g0, ino_block, ino_offset, root_data_blk;

    itable_g0 = 1 + 1 + gdt_blocks + 1 + 1; /* boot+SB+GDT+bmap+imap */
    ino_block = itable_g0 + (1 * EXT2L_INODE_SIZE) / EXT2_BLOCK_SIZE;  /* inode 2 = index 1 */
    ino_offset = (1 * EXT2L_INODE_SIZE) % EXT2_BLOCK_SIZE;
    root_data_blk = itable_g0 + inode_tbl_blocks_per_group; /* オーバーヘッド直後 */

    ret = ext2_read_block(ctx, ino_block, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;

    le16_wr(&ext2_g_blk[ino_offset + EXT2_INODE_MODE_OFF], (u16)(EXT2_S_IFDIR | OS32_DEFAULT_DIR_PERM));
    le16_wr(&ext2_g_blk[ino_offset + EXT2_INODE_UID_OFF], 0);
    le32_wr(&ext2_g_blk[ino_offset + EXT2_INODE_SIZE_OFF], EXT2_BLOCK_SIZE);
    {
        u32 now = ext2_current_time();
        le32_wr(&ext2_g_blk[ino_offset + EXT2_INODE_ATIME_OFF], now);
        le32_wr(&ext2_g_blk[ino_offset + EXT2_INODE_CTIME_OFF], now);
        le32_wr(&ext2_g_blk[ino_offset + EXT2_INODE_MTIME_OFF], now);
    }
    le16_wr(&ext2_g_blk[ino_offset + EXT2_INODE_LINKS_COUNT_OFF], EXT2_EMPTY_DIR_LINKS);   /* links_count */
    le32_wr(&ext2_g_blk[ino_offset + EXT2_INODE_BLOCKS_OFF], EXT2_SECTORS_PER_BLOCK);   /* blocks (512B単位) */
    le32_wr(&ext2_g_blk[ino_offset + EXT2_INODE_BLOCK_OFF], root_data_blk); /* block[0] */

    ret = ext2_write_block(ctx, ino_block, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;

    /* ルートディレクトリデータブロック */
    ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
    le32_wr(&ext2_g_blk[0], EXT2_ROOT_INO);                 /* "." → inode 2 */
    le16_wr(&ext2_g_blk[EXT2_DIRENT_RECLEN_OFF], EXT2_DOT_RECLEN);
    ext2_g_blk[EXT2_DIRENT_NAMELEN_OFF] = 1;
    ext2_g_blk[EXT2_DIRENT_TYPE_OFF] = EXT2_FT_DIR;
    ext2_g_blk[EXT2_DIRENT_NAME_OFF] = '.';
    le32_wr(&ext2_g_blk[EXT2_DOT_RECLEN], EXT2_ROOT_INO);                /* ".." → inode 2 */
    le16_wr(&ext2_g_blk[EXT2_DOT_RECLEN + EXT2_DIRENT_RECLEN_OFF], (u16)(EXT2_BLOCK_SIZE - EXT2_DOT_RECLEN));
    ext2_g_blk[EXT2_DOT_RECLEN + EXT2_DIRENT_NAMELEN_OFF] = 2;
    ext2_g_blk[EXT2_DOT_RECLEN + EXT2_DIRENT_TYPE_OFF] = EXT2_FT_DIR;
    ext2_g_blk[EXT2_DOT_RECLEN + EXT2_DIRENT_NAME_OFF] = '.'; ext2_g_blk[EXT2_DOT_RECLEN + EXT2_DIRENT_NAME_OFF + 1] = '.';

    ret = ext2_write_block(ctx, root_data_blk, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;

    return EXT2_OK;
}

/* [base_lba, base_lba + part_len) の中に sectors 分の ext2 を作る。
 * 呼び手 (ext2_format / ext2_format_at) が範囲を検証済みであること。
 * 大きさは ext2_layout_plan の固定点 — 最終グループに全メタデータが
 * 収まらなければそのグループを落として計算し直す (N7 / F12)。 */
static int ext2_format_range(int ide_drive, u32 base_lba, u32 part_len,
                             u32 sectors)
{
    u32 total_blocks, num_groups;
    u32 inodes_per_group, inode_tbl_blocks_per_group;
    u32 gdt_blocks;
    u32 g, i;
    int ret;
    GroupLayout gl;
    Ext2Layout lay;
    /* フォーマット用一時コンテキスト。
     * static — Ext2Ctx は解決済み経路の記憶 (票 S6-P) で 1.7KB 余りあり、
     * 16KB のカーネルスタックへ丸ごと積みたくない。シングルタスクなので
     * フォーマットが同時に 2 本走ることはない。 */
    static Ext2Ctx fmt_ctx;

    if (!ide_drive_present(ide_drive)) return EXT2_ERR_IO;
    if (sectors > part_len) sectors = part_len;

    if (ext2_layout_plan(sectors, EXT2_MAX_GROUPS, &lay) != EXT2L_OK)
        return EXT2_ERR_NOSPC;
    /* 書く範囲 (固定点で決まった大きさ) を ATA の指定の方式の上限と照合する。
     * 入口 (ext2_format / ext2_format_at) に関係なく、最初の書き込みの前に
     * **この 1 か所**で見る (Codex ラリー 2 の 3: ext2_format は 32 グループへの
     * 頭打ちで、以前は大きすぎて断っていた入力もここへ来る)。 */
    if (!ide_range_ok(ide_drive, base_lba, lay.total_blocks * (unsigned long)EXT2_SECTORS_PER_BLOCK))
        return EXT2_ERR_INVAL;

    /* 一時コンテキストを初期化
     * ext2_read_block/ext2_write_block は ctx->dev (Device API) 経由で
     * I/O するため、dev を必ず解決してから使うこと。part_len で区画の
     * 外への書き込みはブロック I/O の層でも断る。 */
    ext2_mem_zero(&fmt_ctx, sizeof(fmt_ctx));
    fmt_ctx.drive_num = ide_drive;
    fmt_ctx.dev = ext2_dev_for(ide_drive);
    if (!fmt_ctx.dev) return EXT2_ERR_IO;
    fmt_ctx.base_lba = base_lba;
    fmt_ctx.part_len = part_len;

    total_blocks = lay.total_blocks;
    num_groups = lay.num_groups;
    inodes_per_group = lay.inodes_per_group;
    inode_tbl_blocks_per_group = lay.itable_blocks;
    gdt_blocks = lay.gdt_blocks;

    /* ===== Block 0: ブートブロック (ゼロクリア) ===== */
    ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
    ret = ext2_write_block(&fmt_ctx, 0, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;

    ret = fmt_write_super(&fmt_ctx, &lay);
    if (ret != 0) return EXT2_ERR_IO;

    /* ===== GDT + 各グループのメタデータ初期化 ===== */
    {
        u32 total_free_blocks = 0;

        /* まずGDTブロックをゼロクリアして書き込み準備 */
        ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);

        /* 各グループのレイアウトを計算してGDTに書き込み */
        for (g = 0; g < num_groups; g++) {
            u32 gd_offset = (g * EXT2L_GD_SIZE) % EXT2_BLOCK_SIZE;
            u32 overhead;

            /* このグループのブロック範囲 */
            gl.group_start = 1 + g * EXT2_BLOCKS_PER_GROUP_MAX; /* first_data_block=1 */
            gl.blocks_in_group = EXT2_BLOCKS_PER_GROUP_MAX;
            if (g == num_groups - 1) {
                /* 最終グループ: 残りブロック */
                gl.blocks_in_group = total_blocks - gl.group_start;
            }
            gl.inodes_in_group = inodes_per_group;
            gl.inode_tbl_blocks = inode_tbl_blocks_per_group;

            if (is_sparse_group(g)) {
                /* スパースグループ: SB(1) + GDT(gdt_blocks) + bmap + imap + itable */
                gl.block_bitmap = gl.group_start + 1 + gdt_blocks;
                gl.inode_bitmap = gl.block_bitmap + 1;
                gl.inode_table  = gl.inode_bitmap + 1;
                gl.data_start   = gl.inode_table + gl.inode_tbl_blocks;
                overhead = gl.data_start - gl.group_start;
            } else {
                /* 非スパースグループ: bmap + imap + itable のみ */
                gl.block_bitmap = gl.group_start;
                gl.inode_bitmap = gl.block_bitmap + 1;
                gl.inode_table  = gl.inode_bitmap + 1;
                gl.data_start   = gl.inode_table + gl.inode_tbl_blocks;
                overhead = gl.data_start - gl.group_start;
            }

            /* GDTブロックの該当オフセットに書き込み */
            if (gd_offset == 0 && g > 0) {
                /* 新しいGDTブロックの先頭 → 前ブロックを書き込み済み → ゼロクリア */
                ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
            }

            le32_wr(&ext2_g_blk[gd_offset + EXT2_GD_BLOCK_BITMAP_OFF], gl.block_bitmap);
            le32_wr(&ext2_g_blk[gd_offset + EXT2_GD_INODE_BITMAP_OFF], gl.inode_bitmap);
            le32_wr(&ext2_g_blk[gd_offset + EXT2_GD_INODE_TABLE_OFF], gl.inode_table);
            /* free_blocks, free_inodes は後で設定 */
            /* ルートディレクトリはグループ0のデータブロック1つを使用 */
            if (g == 0) {
                le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_BLOCKS_OFF], (u16)(gl.blocks_in_group - overhead - 1));
                le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_INODES_OFF], (u16)(gl.inodes_in_group - EXT2_RESERVED_INODES));
                le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_USED_DIRS_OFF], 1); /* used_dirs (root) */
                total_free_blocks += gl.blocks_in_group - overhead - 1;
            } else {
                le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_BLOCKS_OFF], (u16)(gl.blocks_in_group - overhead));
                le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_INODES_OFF], (u16)gl.inodes_in_group);
                le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_USED_DIRS_OFF], 0);
                total_free_blocks += gl.blocks_in_group - overhead;
            }

            /* GDTブロック末尾 or 最後のグループ → 書き込み */
            if (gd_offset + EXT2L_GD_SIZE >= EXT2_BLOCK_SIZE || g == num_groups - 1) {
                u32 gd_block = EXT2_GDT_FIRST_BLOCK + (g * EXT2L_GD_SIZE) / EXT2_BLOCK_SIZE;
                ret = ext2_write_block(&fmt_ctx, gd_block, ext2_g_blk);
                if (ret != 0) return EXT2_ERR_IO;
            }
        }

        /* スーパーブロックのfree_blocks_countを更新 */
        ret = ext2_read_block(&fmt_ctx, 1, ext2_g_blk);
        if (ret != 0) return EXT2_ERR_IO;
        le32_wr(&ext2_g_blk[EXT2_SB_FREE_BLOCKS_COUNT_OFF], total_free_blocks);
        ret = ext2_write_block(&fmt_ctx, 1, ext2_g_blk);
        if (ret != 0) return EXT2_ERR_IO;

        /* ===== スパースグループへのSBバックアップ + GDTコピー ===== */
        for (g = 1; g < num_groups; g++) {
            u32 gs;
            if (!is_sparse_group(g)) continue;
            gs = 1 + g * EXT2_BLOCKS_PER_GROUP_MAX;

            /* SBバックアップ: プライマリSBを読み、s_block_group_nrを変更 */
            ret = ext2_read_block(&fmt_ctx, 1, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
            le16_wr(&ext2_g_blk[EXT2_SB_BLOCK_GROUP_NR_OFF], (u16)g);           /* s_block_group_nr = g */
            ret = ext2_write_block(&fmt_ctx, gs, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;

            /* GDTコピー: プライマリGDTを各ブロックごとにコピー */
            for (i = 0; i < gdt_blocks; i++) {
                ret = ext2_read_block(&fmt_ctx, EXT2_GDT_FIRST_BLOCK + i, ext2_g_blk);
                if (ret != 0) return EXT2_ERR_IO;
                ret = ext2_write_block(&fmt_ctx, gs + 1 + i, ext2_g_blk);
                if (ret != 0) return EXT2_ERR_IO;
            }
        }

        /* ===== 各グループのビットマップとinodeテーブルを初期化 ===== */
        for (g = 0; g < num_groups; g++) {
            u32 overhead, bmap_blk, imap_blk, itable_blk;

            /* レイアウトを再計算 */
            gl.group_start = 1 + g * EXT2_BLOCKS_PER_GROUP_MAX;
            gl.blocks_in_group = EXT2_BLOCKS_PER_GROUP_MAX;
            if (g == num_groups - 1) {
                gl.blocks_in_group = total_blocks - gl.group_start;
            }

            if (is_sparse_group(g)) {
                /* スパースグループ: SB + GDT + bmap + imap + itable */
                bmap_blk   = gl.group_start + 1 + gdt_blocks;
                imap_blk   = bmap_blk + 1;
                itable_blk = imap_blk + 1;
                overhead = (itable_blk + inode_tbl_blocks_per_group) - gl.group_start;
            } else {
                /* 非スパースグループ: bmap + imap + itable のみ */
                bmap_blk  = gl.group_start;
                imap_blk  = bmap_blk + 1;
                itable_blk = imap_blk + 1;
                overhead = 1 + 1 + inode_tbl_blocks_per_group;
            }

            /* ブロックビットマップ */
            ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
            /* オーバーヘッドブロックをマーク */
            for (i = 0; i < overhead; i++) {
                ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
            }
            /* グループ0: ルートディレクトリ用データブロック1つ */
            if (g == 0) {
                ext2_g_blk[overhead / BITS_PER_BYTE] |= (u8)(1 << (overhead % BITS_PER_BYTE));
            }
            /* グループ内のブロック数以降のビットを全て使用不可にマーク
             * (ext2仕様: ビットマップのパディング部分は1) */
            for (i = gl.blocks_in_group; i < EXT2_BLOCKS_PER_GROUP_MAX; i++) {
                ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
            }
            ret = ext2_write_block(&fmt_ctx, bmap_blk, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;

            /* inodeビットマップ */
            ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
            if (g == 0) {
                /* inode 1-10は予約 (ビット0-9) */
                for (i = 0; i < EXT2_RESERVED_INODES; i++) {
                    ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
                }
            }
            /* inodes_per_group以降のビットを全て使用不可にマーク
             * (ext2仕様: ビットマップのパディング部分は1) */
            for (i = inodes_per_group; i < EXT2_BLOCKS_PER_GROUP_MAX; i++) {
                ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
            }
            ret = ext2_write_block(&fmt_ctx, imap_blk, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;

            /* inodeテーブル (ゼロクリア) */
            ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
            for (i = 0; i < inode_tbl_blocks_per_group; i++) {
                ret = ext2_write_block(&fmt_ctx, itable_blk + i, ext2_g_blk);
                if (ret != 0) return EXT2_ERR_IO;
            }
        }

        ret = fmt_write_root(&fmt_ctx, gdt_blocks, inode_tbl_blocks_per_group);
        if (ret != 0) return EXT2_ERR_IO;
    }

    return EXT2_OK;
}

/* ======================================================================== */
/*  入口                                                                    */
/* ======================================================================== */

int ext2_format(int ide_drive, u32 total_sectors)
{
    u32 start, len;
    int ret;

    if (!ide_drive_present(ide_drive)) return EXT2_ERR_IO;
    /* 区画表の OS32 区画に作る。区画が無ければ作らない (以前は LBA 1088 を
     * 仮定して書き始めた、F12)。長さは区画で頭打ち — インストーラは
     * 「総数 - 1632」を渡すが、区画はシリンダ単位で切り下げてある。 */
    {
        int src = 0;
        ret = ext2_find_partition_src(ide_drive, &start, &len, &src);
        if (ret != EXT2_OK) return ret;
        /* 区画表の位置を IDENTIFY の幾何で読んだだけのときは書かない (m3)。
         * BIOS の幾何と違えば別の場所を指している (F4)。 */
        if (src != BOOTINFO_GEOM_BIOS) {
            kprintf(KLOG_LEGACY_LIGHT_RED, "[EXT2] format: no BIOS geometry for hd%d; "
                          "refusing to write\n", ide_drive & FS_IDE_DRIVE_ID_MASK);
            return EXT2_ERR_INVAL;
        }
    }
    /* 区画が 32 グループ (256MiB) より大きければ、上限の大きさへ頭打ちにする
     * (m2)。format_at は大きすぎる範囲を断る (hdprep は上限の内側で計画する)。 */
    if (total_sectors > EXT2L_MAX_SECTORS(EXT2_MAX_GROUPS))
        total_sectors = EXT2L_MAX_SECTORS(EXT2_MAX_GROUPS);
    return ext2_format_range(ide_drive, start, len, total_sectors);
}

int ext2_format_at(int ide_drive, u32 start_lba, u32 length)
{
    IdeInfo info;

    if (!ide_drive_present(ide_drive)) return EXT2_ERR_IO;
    if (ide_get_info(ide_drive, &info) != IDE_OK) return EXT2_ERR_IO;
    /* 範囲の検査。**どれか 1 つでも外れたら 1 バイトも書かない**。
     *   - 長さ 0 / 開始が IPL・区画表・ローダ (LBA 0〜17) に掛かる
     *   - 総数が分からない / 開始 + 長さがディスクの外 (足し算の桁あふれも) */
    if (length == 0) return EXT2_ERR_INVAL;
    if (start_lba < (u32)EXT2_FMT_MIN_LBA) return EXT2_ERR_INVAL;
    if (info.total_sectors == 0) return EXT2_ERR_INVAL;
    if (start_lba >= info.total_sectors) return EXT2_ERR_INVAL;
    if (length > info.total_sectors - start_lba) return EXT2_ERR_INVAL;
    /* 総数だけでなく、ATA の指定の方式で**本当に指せる**範囲か (LBA28 の上限・
     * 現在の CHS の容量・シリンダ 16 ビット)。最初の書き込みの前に範囲全体で
     * 照合する (Codex C4)。 */
    if (!ide_range_ok(ide_drive, start_lba, length)) return EXT2_ERR_INVAL;
    return ext2_format_range(ide_drive, start_lba, length, length);
}
