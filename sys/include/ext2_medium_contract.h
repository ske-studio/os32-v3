#ifndef EXT2_MEDIUM_CONTRACT_H
#define EXT2_MEDIUM_CONTRACT_H

#include "types.h"

struct _Device;
struct ext2_medium_ops {                 /* 引数 drv は KAPI から来た値そのまま (切り出さない) */
    int  (*present)(int drv);                           /* 1/0。今の platform_ide_drive_present の位置で呼ぶ */
    int  (*total_sectors)(int drv, u32 *out);           /* 0=成功。format_at だけが、今の geometry 取得の位置で呼ぶ */
    int  (*find_part)(int drv, u32 *start, u32 *len);   /* EXT2_OK / EXT2_ERR_*。format だけが呼ぶ。BIOS の幾何の検査と kprintf を含む */
    int  (*range_ok)(int drv, u32 lba, u32 count);      /* 1/0。今の 2 か所 (:176, :261) で呼ぶ */
    struct _Device *(*dev)(int drv);                    /* ext2 の I/O が使う装置 */
    void (*after_write)(void);                          /* 0 可。書込 1 回ごとの mount 側の後始末 (HD: 読みのキャッシュの破棄) */
    u32  min_lba;                                       /* format_at の開始の下限 (HD = EXT2_FMT_MIN_LBA) */
};
#define EXT2_MEDIUM_HD   0
#define EXT2_MEDIUM_LOOP 1
int ext2_medium_register(int kind, const struct ext2_medium_ops *ops);   /* 0 / -1 (範囲外・二重) */
extern const struct ext2_medium_ops ext2_medium_ide_ops;                 /* sys/platform/pc98/storage/ext2_medium_ide.c */

extern const struct ext2_medium_ops ext2_medium_loop_ops;

#endif
