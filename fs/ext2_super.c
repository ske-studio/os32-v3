/* ======================================================================== */
/*  EXT2_SUPER.C — ext2ファイルシステムドライバ (読み書き対応)                */
/*                                                                          */
/*  マルチインスタンス対応: 全関数が Ext2Ctx* を受け取り、                   */
/*  グローバル状態ではなくコンテキスト経由でインスタンスを識別する。          */
/*                                                                          */
/*  ブロック→セクタ変換:                                                    */
/*    1 ext2ブロック (1024B) = 2 IDEセクタ (512B)                           */
/*    ext2ブロックN → IDEセクタ (N*2)                                      */
/* ======================================================================== */

#include "ext2_priv.h"
#include "ide.h"    /* ide_drive_present, ide_get_info — 存在と総セクタ数 */
#include "kprintf.h"
#include "bootinfo.h" /* bootinfo_part_geom — 区画表の CHS の幾何 */
#include "pc98pt.h"   /* 区画表の共有部 (票 TASK_HDD_INSTALL 段 1-4) */

/* 共有静的バッファ (スタックオーバーフロー防止)
 * シングルタスクOSのため全インスタンスで共有可能。
 * ext2_g_blk: inode/super/GD読み書き用
 * ext2_g_aux: ディレクトリ走査/ビットマップ/間接ブロック用
 * ext2_g_dat: ext2_write_stream のデータ読み書き専用 (ext2_g_aux と競合回避) */
u8 ext2_g_blk[EXT2_BLOCK_SIZE];
u8 ext2_g_aux[EXT2_BLOCK_SIZE];
u8 ext2_g_dat[EXT2_BLOCK_SIZE];

/* ======================================================================== */
/*  ブロック読み書き基盤                                                     */
/* ======================================================================== */

/* ブロック block_num (1KB = 2 セクタ) が区画の中か。part_len は区画の長さ
 * (セクタ)。媒体から読んだブロック番号は壊れていることがあるので、掛け算の
 * 前に割り算の側で比べる (block_num * 2 の桁あふれで範囲内に化けない)。 */
static int ext2_block_in_part(const Ext2Ctx *ctx, u32 block_num)
{
    return (block_num < ctx->part_len / 2U) ? 1 : 0;
}

/* ---- 生の 1KB ブロック I/O (エラー状態に触らない) ---- */
static int ext2_raw_read_block(Ext2Ctx *ctx, u32 block_num, void *buf)
{
    u32 sector;
    u8 *dst = (u8 *)buf;
    int ret;

    if (!ext2_block_in_part(ctx, block_num)) return EXT2_ERR_IO;
    sector = ctx->base_lba + block_num * 2;

    /* セクタ0 → buf[0..511] */
    ret = dev_blk_read_lba(ctx->dev, sector, 1, dst);
    if (ret != 0) return ret;

    /* セクタ1 → buf[512..1023] */
    ret = dev_blk_read_lba(ctx->dev, sector + 1, 1, dst + 512);
    return ret;
}

static int ext2_raw_write_block(Ext2Ctx *ctx, u32 block_num, const void *buf)
{
    u32 sector;
    int ret;

    if (!ext2_block_in_part(ctx, block_num)) return EXT2_ERR_IO;
    sector = ctx->base_lba + block_num * 2;
    ret = dev_blk_write_lba(ctx->dev, sector, 1, buf);
    if (ret != 0) return ret;
    ret = dev_blk_write_lba(ctx->dev, sector + 1, 1, (const u8 *)buf + 512);
    return ret;
}

/* **メタデータ**の I/O。失敗したらエラー状態にする (票 B8 往復 5 / 決裁 2)。
 *
 * 既定をメタデータ側に置いた — データブロックの I/O は ext2_file.c の数か所
 * だけで、そこは明示的に _data 版を呼ぶ。逆 (既定をデータ側) にすると、
 * メタデータの呼び手を 1 か所見落としただけでエラー状態が立たなくなる。 */
int ext2_read_block(Ext2Ctx *ctx, u32 block_num, void *buf)
{
    int ret = ext2_raw_read_block(ctx, block_num, buf);
    if (ret != 0) ext2_fs_error(ctx);
    return ret;
}

int ext2_write_block(Ext2Ctx *ctx, u32 block_num, const void *buf)
{
    int ret = ext2_raw_write_block(ctx, block_num, buf);
    if (ret != 0) ext2_fs_error(ctx);
    return ret;
}

/* **データブロック**の I/O。失敗しても I/O エラーを返すだけ (Linux ext2 と同じ)。
 *   読み取り: 構造には何も起きていない。
 *   書き込み: ブロックの中身が古いか半分だけ新しいだけで、どの参照も正しい
 *             ブロックを指している (参照を書くのはメタデータ側の段)。
 * どちらも「以後の書き込みが構造を壊す」原因にならないので止めない。 */
int ext2_read_data_block(Ext2Ctx *ctx, u32 block_num, void *buf)
{
    return ext2_raw_read_block(ctx, block_num, buf);
}

int ext2_write_data_block(Ext2Ctx *ctx, u32 block_num, const void *buf)
{
    return ext2_raw_write_block(ctx, block_num, buf);
}

/* ---- セクタ 1 本だけの I/O (票 H2 §2-2-1) ----
 *
 * ext2_read_block / ext2_write_block は 1KB ブロックを 512B x 2 回で読み書き
 * するので、「1 回の書き込みが失敗した = ブロックは未変更」は成り立たない。
 * 変更したフィールドを含む**セクタ 1 本だけ**を書きたい経路 (ext2_dir.c の
 * 置き換え rename の公開処理) のために、ブロック内のセクタ番号を指定できる
 * 口を出す。sect は 0 か 1。
 *
 * **エラー状態 (ext2_fs_error) は立てない。** 公開処理は失敗したセクタを
 * 読み直して「公開済み / 未公開 / 不明」の 3 値を決めるので、どちらへ倒すか
 * (そして書き込み禁止にするか) は呼び手が決める。 */
int ext2_read_sector(Ext2Ctx *ctx, u32 block_num, u32 sect, void *buf)
{
    if (!ext2_block_in_part(ctx, block_num) || sect > 1) return EXT2_ERR_IO;
    return dev_blk_read_lba(ctx->dev, ctx->base_lba + block_num * 2 + sect,
                            1, buf);
}

int ext2_write_sector(Ext2Ctx *ctx, u32 block_num, u32 sect, const void *buf)
{
    if (!ext2_block_in_part(ctx, block_num) || sect > 1) return EXT2_ERR_IO;
    return dev_blk_write_lba(ctx->dev, ctx->base_lba + block_num * 2 + sect,
                             1, buf);
}

/* ======================================================================== */
/*  エラー状態 (票 B8 往復 5 / ユーザー決裁 2)                               */
/* ======================================================================== */

/* s_state を書くための専用バッファ。**共有バッファ (g_blk / g_aux / g_dat) を
 * 使ってはいけない** — ext2_fs_error は操作の途中 (例えば二重間接表を g_blk に
 * 載せたまま内側の表の読み取りが落ちた瞬間) に呼ばれるので、共有バッファを
 * 潰すと呼び手が走査中の表が化ける (gotcha §4-24 と同じ壊れ方)。 */
static u8 ext2_g_err_sect[512];

void ext2_fs_error(Ext2Ctx *ctx)
{
    u32 sector;
    u16 state;

    if (!ctx) return;
    if (ctx->fs_error) return;          /* 2 回目以降は何もしない */
    ctx->fs_error = 1;                  /* **先に**立てる (下の I/O の失敗で再入しない) */

    /* フォーマット中の一時 ctx など、マウントしていない ctx は媒体に書かない */
    if (!ctx->mounted) return;

    kprintf(0x0C, "[EXT2] I/O error on metadata: writes disabled until remount "
                  "(run e2fsck)\n");

    /* スーパーブロック = ブロック 1 = セクタ base+2 の先頭 512B に s_state が居る。
     * **そのセクタだけ**を読み直して 1 フィールドだけ変えて書く。空き数などの
     * メモリ上の値は書かない (操作の途中で、ビットマップと合っている保証が無い)。
     * 失敗しても何もしない — メモリ上のエラー状態は立っている。 */
    sector = ctx->base_lba + 2;
    if (dev_blk_read_lba(ctx->dev, sector, 1, ext2_g_err_sect) != 0) return;
    state = le16_rd(&ext2_g_err_sect[EXT2_SB_STATE_OFF]);
    if (state & EXT2_ERROR_FS) return;  /* 既に立っている */
    le16_wr(&ext2_g_err_sect[EXT2_SB_STATE_OFF], (u16)(state | EXT2_ERROR_FS));
    (void)dev_blk_write_lba(ctx->dev, sector, 1, ext2_g_err_sect);
}

int ext2_check_writable(Ext2Ctx *ctx)
{
    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    if (ctx->fs_error) return EXT2_ERR_ROFS;
    return EXT2_OK;
}

/* ======================================================================== */
/*  ヘルパー                                                                */
/* ======================================================================== */

/* ext2_mem_copy / ext2_mem_zero / ext2_str_len / ext2_str_ncmp は
 * ext2_priv.h のマクロで kstring 関数 (ASM最適化済み) に転送済み */

/* ======================================================================== */
/*  メタデータの汚れ / 解決済み経路の記憶                                    */
/* ======================================================================== */

/* スーパーブロックかグループ記述子を動かした側が呼ぶ。
 * 次の ext2_sync() が実際にディスクへ書く。 */
void ext2_meta_touch(Ext2Ctx *ctx)
{
    ctx->meta_dirty = 1;
}

/* 名前空間を動かした側が呼ぶ (ext2_add_entry / ext2_delete_entry)。
 * 世代が進むと記憶は全部まとめて無効になる。 */
void ext2_ns_touch(Ext2Ctx *ctx)
{
    ctx->ns_gen++;
    if (ctx->ns_gen == 0) {
        /* 一周した。同じ世代番号の古い記憶と衝突しないよう全部捨てる */
        ext2_path_memo_reset(ctx);
        ctx->ns_gen = 1;
    }
}

void ext2_path_memo_reset(Ext2Ctx *ctx)
{
    int i;
    for (i = 0; i < EXT2_PATH_MEMO_N; i++) ctx->memo[i].gen = 0;
    ctx->memo_next = 0;
}

int ext2_path_memo_get(Ext2Ctx *ctx, const char *path, u32 *out_ino)
{
    int i;
    for (i = 0; i < EXT2_PATH_MEMO_N; i++) {
        if (ctx->memo[i].gen == ctx->ns_gen &&
            kstrcmp(ctx->memo[i].path, path) == 0) {
            *out_ino = ctx->memo[i].ino;
            return EXT2_OK;
        }
    }
    return EXT2_ERR_NOTFOUND;
}

void ext2_path_memo_put(Ext2Ctx *ctx, const char *path, u32 ino)
{
    int i;

    /* 収まらない経路は覚えない。kstrncpy は黙って切り詰めるので、
     * 覚えてしまうと別の経路に化けて当たる */
    if (kstrlen(path) >= OS32_MAX_PATH) return;

    for (i = 0; i < EXT2_PATH_MEMO_N; i++) {
        if (ctx->memo[i].gen == ctx->ns_gen &&
            kstrcmp(ctx->memo[i].path, path) == 0) {
            ctx->memo[i].ino = ino;
            return;
        }
    }

    i = ctx->memo_next;
    ctx->memo_next = (i + 1) % EXT2_PATH_MEMO_N;
    kstrncpy(ctx->memo[i].path, path, OS32_MAX_PATH);
    ctx->memo[i].ino = ino;
    ctx->memo[i].gen = ctx->ns_gen;
}

/* ======================================================================== */
/*  タイムスタンプ                                                           */
/* ======================================================================== */

u32 ext2_current_time(void)
{
    return 0x67E8E800UL;  /* 2025-01-01 00:00:00 UTC 近似 */
}

/* ======================================================================== */
/*  スーパーブロック / グループディスクリプタ 書き戻し                       */
/* ======================================================================== */

int ext2_write_super_raw(Ext2Ctx *ctx)
{
    int ret;
    ret = ext2_read_block(ctx, 1, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;
    le32_wr(&ext2_g_blk[12], ctx->sb_info.free_blocks_count);
    le32_wr(&ext2_g_blk[16], ctx->sb_info.free_inodes_count);
    return ext2_write_block(ctx, 1, ext2_g_blk);
}

int ext2_write_gd_raw(Ext2Ctx *ctx)
{
    /* GDTはブロック2から連続配置。1エントリ32バイト。
     * 1KBブロックに32エントリ収まる → MAX_GROUPS=32なら1ブロックで足りる */
    int ret;
    u32 g, gd_block, offset;

    /* GDTが占めるブロック数を計算 (32B × num_groups) */
    for (g = 0; g < ctx->num_groups; g++) {
        gd_block = 2 + (g * 32) / EXT2_BLOCK_SIZE;
        offset = (g * 32) % EXT2_BLOCK_SIZE;

        /* ブロックの先頭エントリの場合のみ読み込み */
        if (offset == 0) {
            ret = ext2_read_block(ctx, gd_block, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
        }

        le32_wr(&ext2_g_blk[offset + 0], ctx->gd_table[g].block_bitmap);
        le32_wr(&ext2_g_blk[offset + 4], ctx->gd_table[g].inode_bitmap);
        le32_wr(&ext2_g_blk[offset + 8], ctx->gd_table[g].inode_table);
        le16_wr(&ext2_g_blk[offset + 12], ctx->gd_table[g].free_blocks);
        le16_wr(&ext2_g_blk[offset + 14], ctx->gd_table[g].free_inodes);
        le16_wr(&ext2_g_blk[offset + 16], ctx->gd_table[g].used_dirs);

        /* ブロック末尾のエントリ or 最後のグループの場合に書き込み */
        if (offset + 32 >= EXT2_BLOCK_SIZE || g == ctx->num_groups - 1) {
            ret = ext2_write_block(ctx, gd_block, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
        }
    }
    return EXT2_OK;
}

/* ======================================================================== */
/*  パーティションテーブル解析                                               */
/* ======================================================================== */

Device *ext2_dev_for(int ide_drive)
{
    char devname[8];
    devname[0] = 'h';
    devname[1] = 'd';
    devname[2] = (char)('0' + (ide_drive & 0xFF));
    devname[3] = '\0';
    return dev_find(devname);
}

/* 区画表を読む共通部。geom_src (NULL 可) にどちらの幾何で CHS を LBA にしたか
 * (BOOTINFO_GEOM_BIOS / BOOTINFO_GEOM_IDENTIFY) を返す。legacy (NULL 可) は
 * NOPART のとき「旧配置の OS32 項目があった」なら 1。 */
static int ext2_find_partition_ex(int ide_drive, u32 *out_start, u32 *out_len,
                                  int *geom_src, int *legacy)
{
    u8 pt_sect[PC98PT_SECTOR_SIZE];
    u16 heads, spt;
    unsigned long start, len;
    IdeInfo info;
    Device *dev;
    int src;

    if (legacy) *legacy = 0;
    if (!out_start || !out_len) return EXT2_ERR_INVAL;
    if (ide_get_info(ide_drive, &info) != IDE_OK) return EXT2_ERR_IO;

    dev = ext2_dev_for(ide_drive);
    if (!dev) return EXT2_ERR_IO;

    /* 区画表 (LBA 1)。**読めなければ失敗** — 以前はここで LBA 1088 を仮定し、
     * format はその位置から書き始めた (F12)。 */
    if (dev_blk_read_lba(dev, PC98PT_LBA, 1, pt_sect) != 0) return EXT2_ERR_IO;

    /* 区画表の CHS は BIOS 幾何で書かれている (IPL と同じ)。ATA の IDENTIFY と
     * 違う機械がある (実機 8GB: BIOS 未測定、IDENTIFY 16/63。F4)。 */
    src = bootinfo_part_geom(ide_drive, &heads, &spt);
    if (src < 0) return EXT2_ERR_NOPART;
    if (geom_src) *geom_src = src;

    if (pc98pt_find_os32(pt_sect, heads, spt, info.total_sectors,
                         (int *)0, &start, &len) != PC98PT_OK) {
        if (legacy)
            *legacy = pc98pt_os32_is_legacy(pt_sect, heads, spt, info.total_sectors);
        return EXT2_ERR_NOPART;
    }

    *out_start = (u32)start;
    *out_len = (u32)len;
    return EXT2_OK;
}

int ext2_find_partition(int ide_drive, u32 *out_start, u32 *out_len)
{
    return ext2_find_partition_ex(ide_drive, out_start, out_len, (int *)0, (int *)0);
}

int ext2_find_partition_src(int ide_drive, u32 *out_start, u32 *out_len,
                            int *geom_src)
{
    return ext2_find_partition_ex(ide_drive, out_start, out_len, geom_src, (int *)0);
}

/* ======================================================================== */
/*  マウント / アンマウント                                                  */
/* ======================================================================== */

int ext2_mount(Ext2Ctx *ctx, int ide_drive)
{
    int ret, i;

    if (ctx->mounted) ext2_unmount(ctx);
    if (!ide_drive_present(ide_drive)) return EXT2_ERR_IO;
    ctx->drive_num = ide_drive;

    /* Device API ポインタを取得 */
    ctx->dev = ext2_dev_for(ide_drive);
    if (!ctx->dev) return EXT2_ERR_IO;

    /* 区画表から (開始, 長さ) を得る。見つからない / 読めないならマウントしない */
    {
        int src = 0, legacy = 0;
        ret = ext2_find_partition_ex(ide_drive, &ctx->base_lba, &ctx->part_len,
                                     &src, &legacy);
        if (ret == EXT2_ERR_NOPART && legacy) {
            /* v63 までの OS32 が書いた旧配置。v64 は読まない (票 段 1-4) ので、
             * NOPART だけでは直し方が分からない — 何をすればよいかを出す (Opus M2) */
            kprintf(0x0C, "[EXT2] hd%d: partition table is in the pre-v64 OS32 layout; "
                          "migrate it (host: make nhd-migrate-pt) or reinstall\n",
                    ide_drive & 3);
        }
        if (ret != EXT2_OK) return ret;
        /* どちらの幾何で区画の位置を決めたか (BIOS が無いと IDENTIFY に落ちる。m3) */
        kprintf(0x07, "[EXT2] hd%d: partition LBA %u +%u (CHS geometry from %s)\n",
                ide_drive & 3, ctx->base_lba, ctx->part_len,
                src == BOOTINFO_GEOM_BIOS ? "BIOS" : "IDENTIFY");
    }
    if (ctx->part_len < 4) return EXT2_ERR_NOPART;   /* ブロック 0〜1 も無い */

    /* スーパーブロック読み込み: Device API 経由 */
    {
        u8 sb_sect[512];
        ret = dev_blk_read_lba(ctx->dev, ctx->base_lba + 2, 1, sb_sect);
        if (ret != 0) return EXT2_ERR_IO;
        kmemcpy(ext2_g_blk, sb_sect, 512);
        
        ret = dev_blk_read_lba(ctx->dev, ctx->base_lba + 3, 1, sb_sect);
        if (ret != 0) return EXT2_ERR_IO;
        kmemcpy(ext2_g_blk + 512, sb_sect, 512);
    }

    {
        u16 magic = le16_rd(&ext2_g_blk[56]);
        if (magic != EXT2_SUPER_MAGIC) return EXT2_ERR_MAGIC;
    }

    ctx->sb_info.total_inodes     = le32_rd(&ext2_g_blk[0]);
    ctx->sb_info.total_blocks     = le32_rd(&ext2_g_blk[4]);
    ctx->sb_info.free_blocks_count = le32_rd(&ext2_g_blk[12]);
    ctx->sb_info.free_inodes_count = le32_rd(&ext2_g_blk[16]);
    ctx->sb_info.first_data_block = le32_rd(&ext2_g_blk[20]);
    ctx->sb_info.block_size       = 1024U << le32_rd(&ext2_g_blk[24]);
    ctx->sb_info.blocks_per_group = le32_rd(&ext2_g_blk[32]);
    ctx->sb_info.inodes_per_group = le32_rd(&ext2_g_blk[40]);
    ctx->sb_info.magic            = le16_rd(&ext2_g_blk[56]);
    ctx->sb_info.first_ino        = le32_rd(&ext2_g_blk[84]);
    /* ファイルシステムが区画より大きい = 区画表と中身が食い違っている。
     * マウントすると区画の外 (次の区画・ディスクの外) を割り当てに使う。 */
    if (ctx->sb_info.total_blocks > ctx->part_len / 2U) {
        kprintf(0x0C, "[EXT2] fs (%u blocks) exceeds partition (%u sectors)\n",
                ctx->sb_info.total_blocks, ctx->part_len);
        return EXT2_ERR_NOPART;
    }
    /* 媒体にエラーの印が残っていても**読み書きでマウントする** (Linux と同じ、
     * ユーザー決裁 2 の条件)。警告だけ出す。印は e2fsck だけが消す。 */
    ctx->fs_error = 0;
    ctx->mounted_with_errors =
        (le16_rd(&ext2_g_blk[EXT2_SB_STATE_OFF]) & EXT2_ERROR_FS) ? 1 : 0;
    if (ctx->mounted_with_errors) {
        kprintf(0x0E, "[EXT2] warning: mounting fs with errors, "
                      "running e2fsck is recommended\n");
    }
    ctx->sb_info.inode_size       = le16_rd(&ext2_g_blk[88]);
    if (ctx->sb_info.inode_size == 0) ctx->sb_info.inode_size = 128;

    for (i = 0; i < 16; i++) {
        ctx->sb_info.volume_name[i] = (char)ext2_g_blk[120 + i];
    }
    ctx->sb_info.volume_name[16] = '\0';

    /* グループ数を計算 */
    ctx->num_groups = (ctx->sb_info.total_blocks - ctx->sb_info.first_data_block
                       + ctx->sb_info.blocks_per_group - 1)
                      / ctx->sb_info.blocks_per_group;
    if (ctx->num_groups == 0) ctx->num_groups = 1;
    if (ctx->num_groups > EXT2_MAX_GROUPS) return EXT2_ERR_IO;

    /* グループディスクリプタテーブル全体を読み込み — g_blk再利用 */
    {
        u32 g, gd_block, offset;
        for (g = 0; g < ctx->num_groups; g++) {
            gd_block = 2 + (g * 32) / EXT2_BLOCK_SIZE;
            offset = (g * 32) % EXT2_BLOCK_SIZE;

            /* ブロックの先頭エントリの場合のみ読み込み */
            if (offset == 0) {
                ret = ext2_read_block(ctx, gd_block, ext2_g_blk);
                if (ret != 0) return EXT2_ERR_IO;
            }

            ctx->gd_table[g].block_bitmap = le32_rd(&ext2_g_blk[offset + 0]);
            ctx->gd_table[g].inode_bitmap = le32_rd(&ext2_g_blk[offset + 4]);
            ctx->gd_table[g].inode_table  = le32_rd(&ext2_g_blk[offset + 8]);
            ctx->gd_table[g].free_blocks  = le16_rd(&ext2_g_blk[offset + 12]);
            ctx->gd_table[g].free_inodes  = le16_rd(&ext2_g_blk[offset + 14]);
            ctx->gd_table[g].used_dirs    = le16_rd(&ext2_g_blk[offset + 16]);
        }
    }

    ctx->mounted = 1;
    /* 読み込んだばかり = ディスクと一致。経路の記憶も持ち越さない
     * (ctx は使い回されることがある) */
    ctx->meta_dirty = 0;
    ctx->ns_gen = 1;
    ext2_path_memo_reset(ctx);

    return EXT2_OK;
}

void ext2_unmount(Ext2Ctx *ctx)
{
    if (ctx->mounted) ext2_sync(ctx);
    ctx->mounted = 0;
}

int ext2_is_mounted_ctx(Ext2Ctx *ctx) { return ctx->mounted; }

const Ext2Super *ext2_get_super_ctx(Ext2Ctx *ctx)
{
    return ctx->mounted ? &ctx->sb_info : (const Ext2Super *)0;
}

int ext2_sync(Ext2Ctx *ctx)
{
    int ret;
    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    /* エラー状態では空き数も書き戻さない (票 B8 往復 5 / 決裁 2)。メモリ上の
     * 数は成功したビットマップの書き込みだけを数えているが、エラーの後の媒体を
     * これ以上動かさない方針を優先する (数のずれは e2fsck が直す)。
     * 書き戻すべきものが残っていたなら「戻った時点でディスクが正しい」とは
     * 言えないので失敗を返す。
     *
     * **返すのは ROFS ではなく IO**。ROFS は「操作の入口で断った = 何もしていない」
     * (ext2_check_writable) にだけ使う。ここへ来るのは操作が走った後で、
     * 原因はその操作の途中の I/O エラーなので IO が正しい (呼び手の操作が
     * 自分の IO / 漏れを返す前に sync の ROFS で上書きしないため)。 */
    if (ctx->fs_error) return ctx->meta_dirty ? EXT2_ERR_IO : EXT2_OK;

    /* 空きブロック数・空き inode 数・グループ記述子が前回の書き戻しから
     * 一つも動いていなければ、書き戻すものは無い。write-through の契約は
     * 変わらない (戻った時点でディスクは正しい) — 同じ中身を read-modify-
     * write し直す 4 ブロック (= 8 セクタ) を出さないだけ。票 S6-P: 追記
     * 1 回あたり 26 セクタのうち 8 セクタがこれだった。 */
    if (!ctx->meta_dirty) return EXT2_OK;

    ret = ext2_write_super_raw(ctx);
    if (ret != 0) return ret;
    ret = ext2_write_gd_raw(ctx);
    if (ret != 0) return ret;
    ctx->meta_dirty = 0;
    return EXT2_OK;
}

/* ======================================================================== */
