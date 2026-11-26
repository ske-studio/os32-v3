/* ======================================================================== */
/*  HDPREP_PLAN.H — `hdprep` の判定と計画 (純粋関数)                         */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 1-7 / §1-v3 N4・N6・R3-1。   */
/*  `hdprep` は**空のディスク専用**の一時置き場づくり: hd0 (= DA 80h) の     */
/*  LBA 1632 以上の最初の BIOS シリンダ境界から、既定 256MiB 以下の OS32 の  */
/*  ext2 区画を 1 つ作る。書く前に下の条件を全部見て、1 つでも欠けたら       */
/*  1 バイトも書かない。                                                     */
/*                                                                          */
/*  I/O は持たない (cmd_hdprep.c が KAPI で集めた値を渡す)。ホスト試験:      */
/*  test_hdd_stage1.py (旧版 6201b26bccf0) (記録 hdd_stage1_tdd.md (旧版 b895f780947c))。    */
/* ======================================================================== */

#ifndef HDPREP_PLAN_H
#define HDPREP_PLAN_H

/* 置き場の既定と上限 (ext2 32 グループ = 256MiB、F11)。2GB は別票。 */
#define HDPREP_DEFAULT_MB       256UL
#define HDPREP_MAX_MB           256UL
#define HDPREP_MIN_MB           8UL
#define HDPREP_SECTORS_PER_MB   2048UL     /* 1MiB / 512B */
/* ブート予約の下限 (IPL + 区画表 + ローダ + 余白。インストーラの
 * HDD_PARTITION_LBA と同じ 1632 = 8/17 の 12 シリンダ)。区画はここ以上の
 * 最初の BIOS シリンダ境界から (8/17 なら 1632、16/63 なら 2016)。 */
#define HDPREP_BOOT_RESERVE_LBA 1632UL
#define HDPREP_SECLEN           512U
#define HDPREP_DRIVE            0          /* hd0 = DA 80h だけ */

/* I/O の方式 (os32_kapi_shared.h の HDD_AMODE_* と同じ値) */
#define HDPREP_AMODE_NONE       0
#define HDPREP_AMODE_LBA28      1
#define HDPREP_AMODE_CHS_CUR    2
#define HDPREP_AMODE_CHS_DEF    3
/* LBA28 で指せるセクタ数 (sys/dev/ata/ide_addr.h の IDE_LBA28_LIMIT と同じ) */
#define HDPREP_LBA28_LIMIT      0x10000000UL

/* 判定の結果。0 = 可、正 = 追加の手順が要る、負 = 断る */
#define HDPREP_OK               0
#define HDPREP_NEED_UMOUNT      1   /* hd0 がどこかにマウント中 (ルート以外) */
#define HDPREP_E_NO_ATA       (-1)  /* hd0 が IDENTIFY に答えない */
#define HDPREP_E_NO_BIOS      (-2)  /* BIOS 幾何が無い (問い合わせていない / 規則外) */
#define HDPREP_E_SECLEN       (-3)  /* BIOS のセクタ長 (BX) が 512 でない */
#define HDPREP_E_ADDR         (-4)  /* LBA も現在の CHS も無い (既定の CHS では書かない) */
#define HDPREP_E_PT_USED      (-5)  /* LBA 1 に空でない区画項目がある */
#define HDPREP_E_MBR_SIG      (-6)  /* LBA 0 の末尾が 55AA (何かの起動域がある) */
#define HDPREP_E_ROOT         (-7)  /* hd0 がルート (/) にマウントされている */
#define HDPREP_E_SIZE         (-8)  /* 大きさの指定が範囲外 (8〜256MiB) */
#define HDPREP_E_TOO_SMALL    (-9)  /* ディスクに置き場が収まらない */
#define HDPREP_E_CYL          (-10) /* 終了シリンダが 16 ビットを超える */
#define HDPREP_E_STILL_MOUNTED (-11) /* umount の後もまだ hd0 がマウントされている */
#define HDPREP_E_ARG          (-12) /* NULL */
#define HDPREP_E_NO_TOTAL     (-13) /* IDENTIFY の総数 (word 60-61) が 0 = 大きさが分からない */

#include "platform_install_layout.h"

#ifdef PLATFORM_INSTALL_LOGICAL_DRIVE
#define HDPREP_MOUNT_DRIVE(raw) PLATFORM_INSTALL_LOGICAL_DRIVE
#else
#define HDPREP_MOUNT_DRIVE(raw) (raw)
#endif

/* 幾何の条件 (ATA の有無・総数・BIOS 幾何・BX・I/O の方式)。 */
int hdprep_check_geom(const HdprepGeom *g);

/* ディスクが空か。lba0 / lba1 は 512 バイト。 */
int hdprep_check_disk(const unsigned char *lba0, const unsigned char *lba1);

/* マウントの状態。mount_count = dev_mount_count(0)、root_is_hd0 = ルートの
 * デバイスが hd0 か。OK / NEED_UMOUNT / E_ROOT。 */
int hdprep_check_mounts(int mount_count, int root_is_hd0);

/* 大きさの指定 ("" / NULL = 既定)。10 進の MiB、8〜256。 */
int hdprep_parse_mb(const char *s, unsigned long *out_mb);

/* 書く範囲を決める。g は hdprep_check_geom を通っていること。 */
int hdprep_plan(const HdprepGeom *g, unsigned long mb, HdprepPlan *out);

/* 判定の短い説明 (英語 1 行、画面に出す)。 */
const char *hdprep_reason(int code);

int inst_build_pt(unsigned char *sector, const HdprepPlan *plan);

#endif /* HDPREP_PLAN_H */
