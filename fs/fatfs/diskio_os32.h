/* ======================================================================== */
/*  DISKIO_OS32.H — fs/fatfs/diskio.c の OS32 側の口                       */
/*                                                                          */
/*  diskio.h は FatFs (ChaN) の配布物で、ff.h の型 (BYTE / LBA_t など) を   */
/*  前提にする。FatFs の型を持ち込まずに呼びたい側 (kernel/kernel.c、      */
/*  fs/fatfs_vfs.c) のために、OS32 が足した関数だけをここで宣言する。       */
/*  (2026-09-24 ラリー 2 の Fable: kernel.c の extern をヘッダへ)          */
/* ======================================================================== */
#ifndef DISKIO_OS32_H
#define DISKIO_OS32_H

#include "types.h"

void diskio_set_fdd_drive(int drv);
void diskio_set_hdd_drive(int drv);
void diskio_set_hdd_partition(u32 offset);
void diskio_set_hdd_sector_size(u16 sz);
void diskio_set_hdd_ide_phys_size(u16 sz);

/* FD の先読みとセクタキャッシュの数を 1 行で出す (起動時)。
 * 何も読んでいなければ出さない。 */
void diskio_print_fdd_cache(const char *tag);

#endif /* DISKIO_OS32_H */
