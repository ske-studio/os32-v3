/*---------------------------------------------------------------------------/
/  FatFs Configuration for OS32 Kernel
/  PC-9801 32-bit Bare-Metal OS — DOS/Win95 FAT16 互換ドライバ
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80286	/* Revision ID (R0.15) */

/*---------------------------------------------------------------------------/
/ 機能構成
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
/* R/W有効 — FDD書き込み + HDD FAT16書き込み対応 */

#define FF_FS_MINIMIZE	0
/* 全基本機能有効 (stat, getfree, unlink, mkdir, truncate, rename) */

#define FF_USE_FIND		0
/* f_findfirst/f_findnext 無効 */

#define FF_USE_MKFS		1
/* f_mkfs 有効 — FATフォーマット機能 */

#define FF_USE_FASTSEEK	0
/* 高速シーク無効 */

#define FF_USE_EXPAND	0
/* f_expand 無効 */

#define FF_USE_CHMOD	0
/* f_chmod/f_utime 無効 */

#define FF_USE_LABEL	0
/* ボリュームラベル操作 無効 */

#define FF_USE_FORWARD	0
/* f_forward 無効 */

#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0
/* 文字列関数 (f_gets, f_puts, f_printf) 無効 */


/*---------------------------------------------------------------------------/
/ ロケール・名前空間構成
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437
/* コードページ: US ASCII (最小サイズ) */

#define FF_USE_LFN		0
#define FF_MAX_LFN		255
/* LFN無効 — DOS SFN 8.3 のみ */

#define FF_LFN_UNICODE	0
/* ANSI/OEM (LFN無効時は影響なし) */

#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
/* LFN無効時は影響なし */

#define FF_FS_RPATH		0
/* 相対パス無効 (VFSレイヤーが管理) */


/*---------------------------------------------------------------------------/
/ ドライブ・ボリューム構成
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		2
/* 物理ドライブ数: 0=FDD, 1=HDD */

#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"FDD","HDD"
/* 文字列ボリュームID 無効 (数値のみ) */

#define FF_MULTI_PARTITION	0
/* マルチパーティション無効 (1物理ドライブ=1ボリューム) */

#define FF_MIN_SS		512
#define FF_MAX_SS		1024
/* セクタサイズ範囲: IDE HDD=512B, PC-98 FDD=1024B
/  FF_MIN_SS != FF_MAX_SS のため、disk_ioctl(GET_SECTOR_SIZE) 必須 */

#define FF_LBA64		0
/* 64bit LBA 無効 */

#define FF_MIN_GPT		0x10000000
/* GPT閾値 (LBA64無効時は影響なし) */

#define FF_USE_TRIM		0
/* TRIM 無効 */


/*---------------------------------------------------------------------------/
/ システム構成
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		1
/* Tiny構成: ファイルオブジェクトからセクタバッファを排除
/  FATFSオブジェクトの共有バッファを使用 → メモリ節約 */

#define FF_FS_EXFAT		0
/* exFAT 無効 — C89互換維持 */

#define FF_FS_NORTC		0
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026
/* RTC有効: get_fattime() で rtc_read() から時刻取得 */

#define FF_FS_NOFSINFO	0
/* FSINFOセクタ信頼 (FAT32用) */

#define FF_FS_LOCK		0
/* ファイルロック無効 (シングルタスクOS) */

#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000
/* リエントラント無効 (シングルタスクOS) */


/*--- End of configuration options ---*/
