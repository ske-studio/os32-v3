#ifndef PLATFORM_INSTALL_LAYOUT_H
#define PLATFORM_INSTALL_LAYOUT_H

#define PLATFORM_HAS_INSTALL_TARGET 1

typedef struct {
    int drive;
    int           ata_present;
    int           addr_mode;       /* HDPREP_AMODE_* */
    unsigned long ata_total;       /* IDENTIFY word 60-61 (0 = 申告なし → 断る) */
    unsigned long ata_cur_cyl;     /* word 54-56 (現在の CHS。CHS_CUR の上限に使う) */
    unsigned long ata_cur_heads;
    unsigned long ata_cur_spt;
    int           bios_queried;
    int           bios_valid;      /* カーネルの規則 (CF=0・BX=512・CX/DH/DL≠0) */
    unsigned long bios_cyl;        /* CX */
    unsigned long bios_heads;      /* DH */
    unsigned long bios_spt;        /* DL */
    unsigned long bios_seclen;     /* BX */
} HdprepGeom;

typedef struct {
    int drive;
    unsigned long heads, spt;      /* 区画表の CHS に使う BIOS 幾何 */
    unsigned long cyl_sectors;     /* heads × spt */
    unsigned long start;           /* 区画の開始 LBA (シリンダ境界) */
    unsigned long len;             /* 区画の長さ (シリンダの倍数) */
    unsigned long end_cyl;         /* 終了シリンダ (含む) */
    unsigned long probe_lba;       /* 書き込みの探り = 区画の最終セクタ */
    unsigned long disk_limit;      /* 置ける上限 (BIOS 幾何・IDENTIFY の総数・ATA の方式の上限の最小) */
} HdprepPlan;

#define INST_IPL_MAX        512UL
#define INST_LOADER_LBA     2UL
/* boot/boot_hdd.asm は LBA 2 から 16 セクタ読む (tools/nhd_deploy.py の
 * LOADER_MAX_SECTORS と同じ) */
#define INST_LOADER_MAX     8192UL
/* boot/boot_defs.h MAX_IMAGE_SIZE (0x10000〜0x8EFFF)。試験が突き合わせる */
#define INST_KERNEL_MAX     (508UL * 1024UL)
/* IPL の中の幾何 (boot/boot_hdd.asm の geo_heads / geo_spt) */
#define INST_IPL_OFF_HEADS  8
#define INST_IPL_OFF_SPT    9

/* BOOT.PKG の中の名前 (build/packages.yaml の boot:) */
#define BOOT_IPL_NAME    "boot_hdd.bin"
#define BOOT_LOADER_NAME "loader_hdd.bin"


#define SRC_BOOT_HDD    "/sys/boot_hdd.bin"
#define SRC_LOADER_H    "/sys/loader_h.bin"

#endif
