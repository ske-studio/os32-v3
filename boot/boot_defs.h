/* ======================================================================== */
/*  BOOT_DEFS.H — ブートローダー共通定義                                     */
/*  カーネルヘッダに依存せず、ローダー内で完結する型と定数を定義。            */
/* ======================================================================== */

#ifndef BOOT_DEFS_H
#define BOOT_DEFS_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

#define NULL ((void *)0)

/* ext2 定数 */
#define EXT2_BLOCK_SIZE    1024
#define EXT2_SUPER_MAGIC   0xEF53
#define EXT2_ROOT_INO      2
#define EXT2_FT_DIR        2
#define EXT2_FT_REG_FILE   1
#define EXT2_NDIR_BLOCKS   12
#define EXT2_IND_BLOCK     12
#define EXT2_DIND_BLOCK    13

/* VK32 ヘッダ (v2、票 TASK_SERIAL_HOSTFS 部品 A-4)
 *
 *   0x00          magic 'VK32'
 *   0x04          header_size = VK32_HEADER_SIZE(n) = 16 + 20n + 8
 *   0x08          version = VK32_VERSION (2)
 *   0x0C          entry_count n (1〜VK32_MAX_ENTRIES)
 *   0x10          entry[n] (16B: load_addr / raw_size / data_offset / compressed_size)
 *   0x10 + 16n    entry_crc[n] (展開後のデータの CRC32)
 *   0x10 + 20n    image_size   (ファイル全体の長さ = 完全長)
 *   0x14 + 20n    image_crc    (ファイル全体の CRC32。**この欄を 0 として**計算)
 *   header_size〜 LZ4 データ
 *
 * CRC32 は IEEE 802.3 (zlib.crc32 と同じ、lib/crc32_core.inc)。
 * 生成は tools/mkvmkernel.py、検査は boot/vk32_boot.c (HDD) と
 * boot/loader_fat_new.asm の pm_vk32_boot (FD)。両者は同じ VK32_ERR_* を返す。
 * **NASM 側の写し** (loader_fat_new.asm の VK32_* / MAX_IMAGE_SIZE の EQU) との
 * 一致は tools/tests/test_vk32_crc.py が名前ごとに照合する。 */
#define VK32_MAGIC         0x32334B56UL
#define VK32_VERSION       2UL
#define VK32_MAX_ENTRIES   4
#define VK32_COMMON_SIZE   16UL
#define VK32_ENTRY_SIZE    16UL
#define VK32_HEADER_SIZE(n) (VK32_COMMON_SIZE + (u32)(n) * (VK32_ENTRY_SIZE + 4UL) + 8UL)
#define VK32_OFF_ENTRY_CRC(n) (VK32_COMMON_SIZE + (u32)(n) * VK32_ENTRY_SIZE)
#define VK32_OFF_IMAGE_SIZE(n) (VK32_COMMON_SIZE + (u32)(n) * (VK32_ENTRY_SIZE + 4UL))
#define VK32_OFF_IMAGE_CRC(n)  (VK32_OFF_IMAGE_SIZE(n) + 4UL)

/* 展開先として許す帯 [VK32_LOAD_MIN, VK32_LOAD_END)。
 * 正典は include/memmap.h (KERNEL_LOAD_ADDR / MEM_DMA_POOL_BASE)。
 * ローダはカーネルのヘッダを読まないので写しを持ち、一致は
 * tools/gen_memmap.py --check (MIRRORS) が見る。
 * 上端を DMA プール (その上はカーネルスタック) で切る — SQLite の帯の終わり。 */
#define VK32_LOAD_MIN      0x100000UL
#define VK32_LOAD_END      0x2E8000UL

/* vk32_boot / pm_vk32_boot の戻り値 (0 = 成功)。FD の ASM も同じ番号 */
#define VK32_OK             0
#define VK32_ERR_SIZE      (-1)   /* ファイルが共通部より短い / 上限を超える */
#define VK32_ERR_MAGIC     (-2)
#define VK32_ERR_VERSION   (-3)
#define VK32_ERR_COUNT     (-4)   /* entry_count が 0 か VK32_MAX_ENTRIES 超 */
#define VK32_ERR_HEADER    (-5)   /* header_size が n から決まる値でない / ファイルより長い */
#define VK32_ERR_LENGTH    (-6)   /* image_size != 読んだ長さ (完全長) */
#define VK32_ERR_FILE_CRC  (-7)   /* ファイル全体の CRC32 */
#define VK32_ERR_SRC       (-8)   /* data_offset / compressed_size がファイルの外 */
#define VK32_ERR_DST       (-9)   /* 展開先が帯の外・raw_size 0・エントリ同士の重なり */
#define VK32_ERR_DECODE    (-10)  /* LZ4 の展開に失敗 (入力・出力の境界) */
#define VK32_ERR_RAW_SIZE  (-11)  /* decoded != raw_size */
#define VK32_ERR_ENTRY_CRC (-12)  /* 展開後のデータの CRC32 */
#define VK32_ERR_MIN       (-12)

/* ヘッダ・エントリはバイト列を**オフセットで**読む (vk32_boot.c)。
 * 構造体は旧 v1 の説明用に残さない。 */

/* ブート情報域 (0x7E00) のイメージ欄。**正典は include/bootinfo.h**。
 * ローダ (C) はカーネルのヘッダを読まないので写しを持つ。一致は
 * tools/tests/test_vk32_crc.py が名前ごとに照合する。 */
#define BOOTINFO_BASE          0x7E00UL
#define BI_OFF_IMG_CRC         0x30
#define BI_OFF_IMG_SIZE        0x34
#define BI_OFF_IMG_CHECK       0x38
#define BOOTINFO_IMG_KEY       0x43474D49UL  /* 'I','M','G','C' (LE) */

/* メモリアドレス */
#define LOAD_BUF           0x10000UL   /* vmkernel.lz4 一時読み込み先 */
/* ステージング先の上限は **ブート時スタック (0x9FFFC)** の手前。
 * カーネルスタックを 0x1FC000 へ移した後もローダーは低位スタックを
 * 使い続けるので、ここは広げられない。 */
#define MAX_IMAGE_SIZE     (508*1024)  /* 0x10000-0x8EFFF */

/* ASMから呼ばれるセクタ読み込み関数 (loader_hdd_new.asm で定義) */
extern void boot_read_sector_asm(u32 lba, u8 *buf);

/* ASMのTVRAM表示関数 */
extern void boot_print_asm(u32 tvram_addr, const char *msg);

/* ASMのジオメトリパラメータ */
extern u8 param_da;
extern u8 param_heads;
extern u8 param_spt;

/* ext2 ミニリーダー API */
int  ext2m_init(u32 part_lba);
u32  ext2m_lookup(const char *path);
int  ext2m_read_file(u32 ino, u8 *buf, u32 max_size);
#define EXT2M_ERR_TOO_BIG  (-2)   /* i_size > max_size (切り詰めない、N8) */
#define EXT2M_ERR_SHORT    (-3)   /* 穴で最後まで読めない */

/* LZ4 デコーダ */
int  boot_lz4_decode(const u8 *src, int compressed_size,
                     u8 *dst, int dst_capacity);

/* VK32 イメージの検査と展開 (boot/vk32_boot.c)。
 *   file[0..file_size) を検査し、各エントリを window + (load_addr - VK32_LOAD_MIN)
 *   へ展開する。ローダでは window = (u8 *)VK32_LOAD_MIN。ホスト試験は大きな
 *   配列を渡す (展開先を帯の中の相対位置で受ける)。
 *   成功で 0 と *out_crc = ファイル全体の CRC32、失敗で VK32_ERR_*。 */
int  vk32_boot(const u8 *file, u32 file_size, u8 *window, u32 *out_crc);
const char *vk32_strerror(int rc);

#endif /* BOOT_DEFS_H */
