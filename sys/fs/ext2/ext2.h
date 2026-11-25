/* ======================================================================== */
/*  EXT2.H — ext2ファイルシステムドライバ (読み書き対応)                      */
/*                                                                          */
/*  マルチインスタンス対応: 全関数が Ext2Ctx* を第1引数に取る。              */
/*  1KBブロック, 512Bセクタ対応。複数同時マウント可能。                      */
/* ======================================================================== */

#ifndef EXT2_H
#define EXT2_H

#include "types.h"

/* ext2定数 */
#define EXT2_SUPER_MAGIC  0xEF53
#define EXT2_ROOT_INO     2
#define EXT2_BLOCK_SIZE   1024
#define EXT2_NAME_LEN     255
#define EXT2_NDIR_BLOCKS  12     /* 直接ブロック数 */
#define EXT2_IND_BLOCK    12     /* 単一間接ブロック */
#define EXT2_DIND_BLOCK   13     /* 二重間接ブロック */
#define EXT2_TIND_BLOCK   14     /* 三重間接ブロック */
#define EXT2_N_BLOCKS     15     /* ブロックポインタ総数 */

/* 1KBブロックの場合、間接ブロック内のポインタ数 */
#define EXT2_ADDR_PER_BLOCK  (EXT2_BLOCK_SIZE / EXT2_BLOCK_PTR_SIZE)  /* 256 */

/* Fixed OS32 ext2 geometry and wire layouts (1 KiB blocks). */
#define EXT2_SECTOR_SIZE 512
#define EXT2_SECTORS_PER_BLOCK (EXT2_BLOCK_SIZE / EXT2_SECTOR_SIZE)
#define EXT2_SUPER_LBA EXT2_SECTORS_PER_BLOCK
#define EXT2_MIN_PART_SECTORS (EXT2_SUPER_LBA + EXT2_SECTORS_PER_BLOCK)
#define EXT2_GDT_FIRST_BLOCK 2
#define EXT2_BLOCKS_PER_GROUP_MAX (EXT2_BLOCK_SIZE * BITS_PER_BYTE)
#define EXT2L_INODE_SIZE 128UL
#define EXT2L_GD_SIZE 32UL
#define EXT2_BLOCK_PTR_SIZE 4
#define EXT2_INODE_OSD2_SIZE 12
#define EXT2_VOLUME_NAME_LEN 16
#define EXT2_RESERVED_INODES 10
#define EXT2_FIRST_NONRESERVED_INO (EXT2_RESERVED_INODES + 1)
#define EXT2_DIRENT_RECLEN_OFF 4
#define EXT2_DIRENT_NAMELEN_OFF 6
#define EXT2_DIRENT_TYPE_OFF 7
#define EXT2_DIRENT_NAME_OFF 8
#define EXT2_DIRENT_HEADER_SIZE EXT2_DIRENT_NAME_OFF
#define EXT2_DIRENT_ALIGN 4
#define EXT2_DIRENT_SIZE(n) ((EXT2_DIRENT_HEADER_SIZE + (n) + EXT2_DIRENT_ALIGN - 1) & ~(EXT2_DIRENT_ALIGN - 1))
#define EXT2_DOT_RECLEN EXT2_DIRENT_SIZE(1)
#define EXT2_INODE_MODE_OFF 0
#define EXT2_INODE_UID_OFF 2
#define EXT2_INODE_SIZE_OFF 4
#define EXT2_INODE_ATIME_OFF 8
#define EXT2_INODE_CTIME_OFF 12
#define EXT2_INODE_MTIME_OFF 16
#define EXT2_INODE_DTIME_OFF 20
#define EXT2_INODE_GID_OFF 24
#define EXT2_INODE_LINKS_COUNT_OFF 26
#define EXT2_INODE_BLOCKS_OFF 28
#define EXT2_INODE_FLAGS_OFF 32
#define EXT2_INODE_OSD1_OFF 36
#define EXT2_INODE_BLOCK_OFF 40
#define EXT2_INODE_GENERATION_OFF 100
#define EXT2_INODE_FILE_ACL_OFF 104
#define EXT2_INODE_DIR_ACL_OFF 108
#define EXT2_INODE_FADDR_OFF 112
#define EXT2_INODE_OSD2_OFF 116
#define EXT2_SB_INODES_COUNT_OFF 0
#define EXT2_SB_BLOCKS_COUNT_OFF 4
#define EXT2_SB_R_BLOCKS_COUNT_OFF 8
#define EXT2_SB_FREE_BLOCKS_COUNT_OFF 12
#define EXT2_SB_FREE_INODES_COUNT_OFF 16
#define EXT2_SB_FIRST_DATA_BLOCK_OFF 20
#define EXT2_SB_LOG_BLOCK_SIZE_OFF 24
#define EXT2_SB_LOG_FRAG_SIZE_OFF 28
#define EXT2_SB_BLOCKS_PER_GROUP_OFF 32
#define EXT2_SB_FRAGS_PER_GROUP_OFF 36
#define EXT2_SB_INODES_PER_GROUP_OFF 40
#define EXT2_SB_MTIME_OFF 44
#define EXT2_SB_WTIME_OFF 48
#define EXT2_SB_MNT_COUNT_OFF 52
#define EXT2_SB_MAX_MNT_COUNT_OFF 54
#define EXT2_SB_MAGIC_OFF 56
#define EXT2_SB_MINOR_REV_LEVEL_OFF 62
#define EXT2_SB_LASTCHECK_OFF 64
#define EXT2_SB_CHECKINTERVAL_OFF 68
#define EXT2_SB_CREATOR_OS_OFF 72
#define EXT2_SB_REV_LEVEL_OFF 76
#define EXT2_SB_DEF_RESUID_OFF 80
#define EXT2_SB_DEF_RESGID_OFF 82
#define EXT2_SB_FIRST_INO_OFF 84
#define EXT2_SB_INODE_SIZE_OFF 88
#define EXT2_SB_BLOCK_GROUP_NR_OFF 90
#define EXT2_SB_FEATURE_INCOMPAT_OFF 96
#define EXT2_SB_FEATURE_RO_COMPAT_OFF 100
#define EXT2_SB_VOLUME_NAME_OFF 120
#define EXT2_GD_BLOCK_BITMAP_OFF 0
#define EXT2_GD_INODE_BITMAP_OFF 4
#define EXT2_GD_INODE_TABLE_OFF 8
#define EXT2_GD_FREE_BLOCKS_OFF 12
#define EXT2_GD_FREE_INODES_OFF 14
#define EXT2_GD_USED_DIRS_OFF 16

/* ファイルタイプ (dirent) */
#define EXT2_FT_UNKNOWN   0
#define EXT2_FT_REG_FILE  1
#define EXT2_FT_DIR       2
#define EXT2_FT_CHRDEV    3
#define EXT2_FT_BLKDEV    4
#define EXT2_FT_FIFO      5
#define EXT2_FT_SOCK      6
#define EXT2_FT_SYMLINK   7

/* iノードモード (POSIX) */
#define EXT2_S_IFMT     0170000
#define EXT2_S_IFSOCK   0140000
#define EXT2_S_IFLNK    0120000
#define EXT2_S_IFREG    0100000
#define EXT2_S_IFBLK    0060000
#define EXT2_S_IFDIR    0040000
#define EXT2_S_IFCHR    0020000
#define EXT2_S_IFIFO    0010000

/* パーミッション */
#define EXT2_S_IRWXU    0000700
#define EXT2_S_IRUSR    0000400
#define EXT2_S_IWUSR    0000200
#define EXT2_S_IXUSR    0000100
#define EXT2_S_IRWXG    0000070
#define EXT2_S_IRGRP    0000040
#define EXT2_S_IWGRP    0000020
#define EXT2_S_IXGRP    0000010
#define EXT2_S_IRWXO    0000007
#define EXT2_S_IROTH    0000004
#define EXT2_S_IWOTH    0000002
#define EXT2_S_IXOTH    0000001

/* スーパーブロック (メモリ上の要約) */
typedef struct {
    u32 total_inodes;
    u32 total_blocks;
    u32 block_size;
    u32 blocks_per_group;
    u32 inodes_per_group;
    u32 first_data_block;
    u16 inode_size;
    u16 magic;
    u32 first_ino;
    u32 free_blocks_count;
    u32 free_inodes_count;
    char volume_name[EXT2_VOLUME_NAME_LEN + 1];
} Ext2Super;

/* グループディスクリプタ */
typedef struct {
    u32 block_bitmap;
    u32 inode_bitmap;
    u32 inode_table;
    u16 free_blocks;
    u16 free_inodes;
    u16 used_dirs;
} Ext2GroupDesc;

/* iノード (ディスク上) */
typedef struct {
    u16 mode;
    u16 uid;
    u32 size;
    u32 atime;
    u32 ctime;
    u32 mtime;
    u32 dtime;
    u16 gid;
    u16 links_count;
    u32 blocks;     /* 512B単位 */
    u32 flags;
    u32 osd1;
    u32 block[EXT2_N_BLOCKS];
    u32 generation;
    u32 file_acl;
    u32 dir_acl;
    u32 faddr;
    u8  osd2[EXT2_INODE_OSD2_SIZE];
} Ext2Inode;

/* ディレクトリエントリ */
typedef struct {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    char name[EXT2_NAME_LEN + 1];
} Ext2DirEntry;

/* エラーコード */
#define EXT2_OK           0
#define EXT2_ERR_IO      -1
#define EXT2_ERR_MAGIC   -2
#define EXT2_ERR_NOTFOUND -3
#define EXT2_ERR_NOMOUNT -4
#define EXT2_ERR_NOSPC   -5
#define EXT2_ERR_EXIST   -6
#define EXT2_ERR_NOTDIR  -7
#define EXT2_ERR_NOTEMPTY -8
#define EXT2_ERR_ISDIR   -9
#define EXT2_ERR_INVAL   -10  /* 不正操作 (ディレクトリを自身の配下へ rename 等) */
/* このマウントはエラー状態で、書き込み系操作を受け付けない (票 B8 往復 5 /
 * ユーザー決裁 2: Linux ext2 の errors=remount-ro 相当)。VFS へは
 * OS32_ERR_ROFS に写す。 */
#define EXT2_ERR_ROFS    -11
/* links_count が EXT2_LINK_MAX に達していて増やせない (Linux の EMLINK)。
 * VFS へは OS32_ERR_FULL (資源が満杯) に写す。 */
#define EXT2_ERR_MLINK   -12
/* 区画表に OS32 の区画が無い / 項目の範囲が壊れている (票 TASK_HDD_INSTALL
 * 段 1-5)。以前は LBA 1088 へフォールバックしてそこを読み書きした。 */
#define EXT2_ERR_NOPART  -13

/* スーパーブロックの s_state / s_errors (ext2 の仕様どおり、offset 58 / 60)。
 *   s_state  … EXT2_VALID_FS (1) / EXT2_ERROR_FS (2) のビット
 *   s_errors … エラー検出時の既定動作。OS32 は値に関わらず常に「以後の書き込みを
 *              止める」(RO 相当) ので、フォーマット時は RO を書く (ext2_fmt.c)。 */
#define EXT2_SB_STATE_OFF     58
#define EXT2_SB_ERRORS_OFF    60
#define EXT2_VALID_FS         0x0001
#define EXT2_ERROR_FS         0x0002
#define EXT2_ERRORS_CONTINUE  1
#define EXT2_ERRORS_RO        2
#define EXT2_ERRORS_PANIC     3

/* links_count (u16) の上限。Linux ext2 の EXT2_LINK_MAX と同じ値。
 * ディレクトリの子を増やす操作 (mkdir / 別の親への rename) がこれを越える
 * なら断る (票 B8 往復 5、レビュー非 blocker)。 */
#define EXT2_LINK_MAX         32000

/* 空のディレクトリの links_count ("." と、親の中の名前の 2 本)。
 * 空なのにこれより多いなら、名前を持たない子 (孤児) の ".." がまだ数えられて
 * いる — rmdir はその inode を返さない (票 B8 往復 6、ユーザー決裁)。 */
#define EXT2_EMPTY_DIR_LINKS  2

/* rename の循環検査で ".." を辿る上限 (壊れた FS でのループ防止) */
#define EXT2_RENAME_MAX_DEPTH 64

/* 前方宣言: Ext2Ctx (ext2_ctx.h で定義) */
struct Ext2Ctx_tag;
typedef struct Ext2Ctx_tag Ext2Ctx;

/* ---- ディレクトリコールバック ---- */
typedef void (*ext2_dir_callback)(const Ext2DirEntry *entry, void *ctx);

/* ---- マウント/アンマウント ---- */
int ext2_mount(Ext2Ctx *ctx, int ide_drive);
void ext2_unmount(Ext2Ctx *ctx);
int ext2_is_mounted_ctx(Ext2Ctx *ctx);

/* ---- 同期 ---- */
int ext2_sync(Ext2Ctx *ctx);

/* ---- フォーマット (コンテキスト不要: 一時CTXを内部で使用) ---- */
/* 区画表の OS32 区画の先頭から total_sectors (区画の長さで頭打ち) に作る。
 * 区画が無ければ EXT2_ERR_NOPART。 */
int ext2_format(int ide_drive, u32 total_sectors);
/* 区画表を**読まずに** [start_lba, start_lba + length) だけに作る (KAPI v64、
 * hdprep / インストーラ用、票 TASK_HDD_INSTALL §1-v3 N5)。範囲がディスクの
 * 総数を超える・桁あふれ・LBA 0〜EXT2_FMT_MIN_LBA-1 (IPL / 区画表 / ローダ) に
 * 掛かるなら 1 バイトも書かずに EXT2_ERR_INVAL。大きさは ext2_layout_plan の
 * 固定点 (最終グループに全メタデータが収まる長さへ切り下げ)。 */
#define EXT2_FMT_MIN_LBA  18   /* LBA 0 = IPL、1 = 区画表、2〜17 = ローダ (8KB) */
int ext2_format_at(int ide_drive, u32 start_lba, u32 length);

/* ---- VFS登録 ---- */
int ext2_init(void);

#endif /* EXT2_H */
