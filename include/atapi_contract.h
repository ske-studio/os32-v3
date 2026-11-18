#ifndef ATAPI_CONTRACT_H
#define ATAPI_CONTRACT_H
/* 共通 (fs/isofs) と ATAPI のドライバ (drivers/ata/atapi.c) の間の口。 */
#include "types.h"

/* ======== CD-ROM セクタサイズ ======== */
#define ATAPI_SECTOR_SIZE    2048

/* 1 回の READ(10) で読む最大のセクタ数 (説明は drivers/ata/atapi.h)。 */
#ifndef ATAPI_READ_MAX_SECTORS
#define ATAPI_READ_MAX_SECTORS  16
#endif

/* 媒体の世代。上の層は覚えた値と違えばキャッシュを捨てる。 */
u32 atapi_media_gen(void);

#endif
