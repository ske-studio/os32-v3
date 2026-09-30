/* ======================================================================== */
/*  BOOT_MAIN.C — HDD新ローダー メインロジック (C言語)                      */
/*                                                                          */
/*  ASM (loader_hdd_new.asm) から呼び出される。                              */
/*  1. PC-98パーティションテーブルからext2パーティションを特定               */
/*  2. ext2から /boot/vmkernel.lz4 を読み込み                               */
/*  3. VK32 v2 の検査 (長さ・範囲・CRC32) + LZ4展開 (vk32_boot.c)          */
/*  4. 正常終了時は呼び出し元ASMに戻り、ASMがカーネルにジャンプ              */
/* ======================================================================== */

#include "boot_defs.h"
#include "pc98pt.h"   /* 区画表の共有部 (drivers/pc98pt.c、票 TASK_HDD_INSTALL 段 1-4) */

/* ================================================================ */
/*  パーティション検索 (PC-98 区画表: LBA 1、標準配置)               */
/*                                                                  */
/*  CHS → LBA の幾何は IPL から受けた heads / SPT (= インストーラが  */
/*  IPL に焼いた BIOS 幾何。HDD ローダは AH=84h と食い違えば止まる)。 */
/*  **見つからなければ失敗** — 以前は LBA 1088 を仮定して読みに行った */
/*  (票 TASK_HDD_INSTALL 段 1-5)。総数は知らないので 0 (見ない)。     */
/* ================================================================ */
static int find_partition(u32 *out_lba)
{
    u8 pt_buf[PC98PT_SECTOR_SIZE];
    unsigned long start, len;

    boot_read_sector_asm(PC98PT_LBA, pt_buf);
    if (pc98pt_find_os32(pt_buf, param_heads, param_spt, 0,
                         (int *)0, &start, &len) != PC98PT_OK)
        return -1;
    *out_lba = start;
    return 0;
}

/* ================================================================ */
/*  ブート情報域のイメージ欄 (正典 include/bootinfo.h、写しは boot_defs.h) */
/* ================================================================ */
static void wr32_low(u32 addr, u32 v)
{
    *(volatile u32 *)addr = v;
}

static void bootinfo_set_image(u32 crc, u32 size)
{
    wr32_low(BOOTINFO_BASE + BI_OFF_IMG_CRC, crc);
    wr32_low(BOOTINFO_BASE + BI_OFF_IMG_SIZE, size);
    /* **最後に**チェック語 — ここまでで止まった欄は無効に見える */
    wr32_low(BOOTINFO_BASE + BI_OFF_IMG_CHECK, crc ^ size ^ BOOTINFO_IMG_KEY);
}

/* ================================================================ */
/*  boot_main — ローダーメイン (ASMから呼ばれる)                     */
/*  戻り値: 0=成功, 負数=エラー                                     */
/* ================================================================ */
int boot_main(void)
{
    u32 part_lba;
    u32 ino;
    int file_size;
    u8 *file_base;
    u32 img_crc = 0;
    int rc;

    /* 1. パーティション検索 */
    boot_print_asm(0xA0000 + 160, "Finding partition...");
    if (find_partition(&part_lba) != 0) {
        boot_print_asm(0xA0000 + 320, "No OS32 partition in LBA 1 (PC-98 table)!");
        return -7;
    }

    /* 2. ext2初期化 */
    if (ext2m_init(part_lba) != 0) {
        boot_print_asm(0xA0000 + 320, "ext2 mount FAIL!");
        return -1;
    }

    /* 3. /boot/vmkernel.lz4 を検索 */
    boot_print_asm(0xA0000 + 320, "Loading vmkernel.lz4...");
    ino = ext2m_lookup("/boot/vmkernel.lz4");
    if (ino == 0) {
        boot_print_asm(0xA0000 + 480, "/boot/vmkernel.lz4 NOT FOUND!");
        return -2;
    }

    /* 4. ファイルを LOAD_BUF (0x10000) に読み込み */
    file_base = (u8 *)LOAD_BUF;
    file_size = ext2m_read_file(ino, file_base, MAX_IMAGE_SIZE);
    if (file_size == EXT2M_ERR_TOO_BIG) {
        /* 上限 (0x10000〜0x8EFFF、ブート時スタックの手前) を超えるイメージは
         * 切り詰めて展開せずに止まる (N8)。 */
        boot_print_asm(0xA0000 + 480, "vmkernel.lz4 too large (> 508KiB)");
        return -6;
    }
    if (file_size <= 0) {
        boot_print_asm(0xA0000 + 480, "File read FAIL!");
        return -3;
    }

    /* 5. VK32 v2 の検査と展開 (boot/vk32_boot.c)。外れたら画面に出して止まる
     *    (完全長・entry_count・範囲・decoded == raw_size・CRC32、部品 A-4)。 */
    boot_print_asm(0xA0000 + 480, "Checking + decompressing...");
    rc = vk32_boot(file_base, (u32)file_size, (u8 *)VK32_LOAD_MIN, &img_crc);
    if (rc != VK32_OK) {
        /* 5 行目 (+800)。4 行目 (+640) は ASM が戻り値の表示で上書きする */
        boot_print_asm(0xA0000 + 800, vk32_strerror(rc));
        return -5;
    }

    /* 6. 起動したイメージの CRC をブート情報域へ (include/bootinfo.h の
     *    イメージ欄)。主部は実モードで封じ済み — イメージ欄は自分の
     *    チェック語を**最後に**書く。 */
    bootinfo_set_image(img_crc, (u32)file_size);

    boot_print_asm(0xA0000 + 640, "Kernel loaded. Booting...");
    return 0;
}
