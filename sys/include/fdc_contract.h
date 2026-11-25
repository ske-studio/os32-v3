#ifndef FDC_CONTRACT_H
#define FDC_CONTRACT_H
/* 共通 (fs/fat の読みのキャッシュ) と FD のドライバ (sys/dev/block/fdc.c) の間の口。 */
#include "types.h"

/* ドライブの中身の世代。**書き込み (fdc_write_sector_geom を通るもの全部 —
 * FatFs、dev.c の fd0/fd1、KAPI の dev_blk_write)、Ready 変化 (媒体の
 * 入れ替え)、fdc_set_media のたびに増える**。先読み (sys/dev/block/fdc_track.c)
 * はスロットを埋めたときの世代と比べ、違えば当てない。 */
u32 fdc_media_gen(int drv);

#endif
