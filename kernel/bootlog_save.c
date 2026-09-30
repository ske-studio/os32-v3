/* ======================================================================== */
/*  BOOTLOG_SAVE.C — 起動ログの書き出し (VFS と版情報への結線)              */
/*                                                                          */
/*  kernel.c がルートのマウント後、常駐シェルを exec する直前に 1 回だけ     */
/*  bootlog_save() を呼ぶ。手順そのものは kernel/bootlog.c の               */
/*  bootlog_save_with (偽の VFS でホスト試験済み)。ここは本物の vfs_* と     */
/*  ヘッダの材料 (Build / Commit / Image CRC / tick) を渡すだけ。           */
/*  どこで失敗しても起動は続け、kprintf で 1 行だけ出す。                    */
/* ======================================================================== */

#include "bootlog.h"
#include "vfs.h"
#include "kprintf.h"
#include "bootinfo.h"
#include "build_id.h"
#include "idt.h"                /* tick_count */

extern void kapi_sys_get_build_info(char *buf, int size);   /* kapi/kapi_sys.c */

/* vfs_write の戻りは FS ごとに違う (ext2 は 0、FAT はバイト数、負は失敗)。
 * 種別を知る bootlog_save_with が bootlog_write_ok で揃えるので、ここは
 * 生のまま差す (include/bootlog.h の BootlogFsOps)。 */
static const BootlogFsOps g_bootlog_vfs_ops = {
    vfs_mkdir, vfs_rm, vfs_rename, vfs_write, vfs_sync
};

void bootlog_save(void)
{
    const char *fs;
    const char *data;
    int kind, stage, rc = 0;
    u32 len;
    char build[32];
    char header[BOOTLOG_HDR_MAX];
    BootImageInfo bi;
    BootlogHeaderInfo hi;

    /* ここより後の出力 (下の 1 行を含む) は溜めない */
    bootlog_stop();

    fs = vfs_fstype("/");
    kind = bootlog_plan(fs);
    if (kind == BOOTLOG_FS_SKIP) {
        kprintf(0x07, "[bootlog] not saved (root is %s)\n", fs ? fs : "unmounted");
        return;
    }

    kapi_sys_get_build_info(build, (int)sizeof(build));
    boot_image_info(&bi);
    hi.build      = build;
    hi.commit     = os32_build_commit;
    hi.crc_valid  = bi.crc_valid;
    hi.image_crc  = bi.image_crc;
    hi.image_size = bi.image_size;
    hi.ticks      = tick_count;
    bootlog_format_header(header, (u32)sizeof(header), &hi);
    data = bootlog_compose(header, &len);

    stage = bootlog_save_with(&g_bootlog_vfs_ops, kind, data, len, &rc);
    if (stage != BOOTLOG_ST_OK) {
        kprintf(0xC1, "[bootlog] %s failed rc=%d (boot continues)\n",
                bootlog_stage_name(stage), rc);
    } else {
        kprintf(0x07, "[bootlog] %s %u bytes (dropped %u)\n", SYS_BOOTLOG_FILE,
                (unsigned)len, (unsigned)bootlog_dropped());
    }
}
