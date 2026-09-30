;; ============================================================
;; bootinfo.inc — ブート情報域 (0x7E00) の番地とオフセット (NASM 側の写し)
;;
;; **正典は include/memmap.h (番地) と include/bootinfo.h (形式)。**
;; NASM は C のヘッダを読めないので値を写して持つ。
;;   番地の一致        tools/gen_memmap.py --check (MIRRORS)
;;   名前ごとの値の一致 tools/tests/test_bootinfo.py (make check-bootinfo-host)
;; 片方だけ直すと、ローダとカーネルが別の場所を読んで静かにずれる。
;; 票: docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 0
;; ============================================================

MEM_BOOTINFO_BASE EQU 0x7E00

BOOTINFO_MAGIC      EQU 0x49544F42
BOOTINFO_VERSION    EQU 2
BOOTINFO_CHECK      EQU 0xB6ABB0BF
BOOTINFO_NDRIVES    EQU 2
BOOTINFO_SRC_FD     EQU 1
BOOTINFO_SRC_HDD    EQU 2
BOOTINFO_SECLEN     EQU 512
BOOTINFO_WIRE_SIZE  EQU 0x40
BOOTINFO_IMG_KEY    EQU 0x43474D49

BI_OFF_MAGIC        EQU 0x00
BI_OFF_VERSION      EQU 0x04
BI_OFF_SOURCE       EQU 0x06
BI_OFF_NDRIVES      EQU 0x07
BI_OFF_DRIVE0       EQU 0x08
BI_DRIVE_SIZE       EQU 0x10
BI_OFF_SUM          EQU 0x28
BI_OFF_CHECK        EQU 0x2C
BI_OFF_IMG_CRC      EQU 0x30
BI_OFF_IMG_SIZE     EQU 0x34
BI_OFF_IMG_CHECK    EQU 0x38

BI_DRV_DA           EQU 0x00
BI_DRV_VALID        EQU 0x01
BI_DRV_CF           EQU 0x02
BI_DRV_AH           EQU 0x03
BI_DRV_BX           EQU 0x04
BI_DRV_CX           EQU 0x06
BI_DRV_DH           EQU 0x08
BI_DRV_DL           EQU 0x09
BI_DRV_QUERIED      EQU 0x0A
