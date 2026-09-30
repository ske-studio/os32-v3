/* ======================================================================== */
/*  DISKIO.C — FatFs ディスクI/Oブリッジ (OS32カーネル用)                    */
/*                                                                          */
/*  FatFsモジュールとOS32のディスクドライバ (FDC/IDE) を接続する。           */
/*                                                                          */
/*  物理ドライブマッピング:                                                 */
/*    pdrv=0 : FDD (µPD765A FDC, 1024B/sector, disk.c経由)                 */
/*    pdrv=1 : HDD (ATA/IDE PIO, 512B/sector, ide.c経由)                   */
/*                                                                          */
/*  出典: FatFs R0.15 Application Note (elm-chan.org)                       */
/* ======================================================================== */

#include "ff.h"
#include "diskio.h"

/* disk.h は CHS版 disk_read/disk_write を宣言しており、
 * FatFSの diskio.h の disk_read/disk_write と名前が衝突する。
 * FDD は fdc_read_sector/fdc_write_sector を直接使用する。 */

#include "dev.h"
#include "ide.h"    /* ide_drive_present, ide_get_info — 初期化/ioctl用 */
#include "rtc.h"
#include "fdc.h"
#include "fdc_track.h"  /* FD の読みをトラック単位に束ねる (純粋な層) */
#include "diskio_os32.h"
#include "kstring.h"
#include "kprintf.h"

/* 物理ドライブ番号 */
#define DRV_FDD   0
#define DRV_HDD   1

/* ドライブ状態 */
static DSTATUS fdd_status = STA_NOINIT;
static DSTATUS hdd_status = STA_NOINIT;

/* FDDドライブ番号 (OS32側: 通常0) */
static int fdd_drive = 0;
/* HDDドライブ番号 (OS32側: 通常0) */
static int hdd_drive = 0;
/* HDDパーティションオフセット (LBA, IDE物理セクタ単位) — PC-98パーティション対応 */
static u32 hdd_part_offset = 0;
/* HDD論理セクタサイズ (PC-98 DOSでは1024、通常は512) */
static u16 hdd_sector_size = 512;
/* HDD IDE物理セクタサイズ (SASI HDI=256, IDE=512)
 * NP21/WはSASI形式HDIをIDE経由で提供するが、内部LBAは256B単位。
 * IDE転送は常に512Bだが、連続セクタでオーバーラップが発生するため
 * 256B時はLBAを2刻みで進める必要がある。 */
static u16 hdd_ide_phys_size = 512;
/* Device API ポインタ (diskio_set_hdd_drive時に取得) */
static Device *hdd_dev = 0;

/* ======================================================================== */
/*  FD のトラック単位の読み出し (2026-09-24)                                */
/*                                                                          */
/*  1 セクタずつ SEEK + READ DATA を出していたころは 1 セクタごとにほぼ      */
/*  1 回転待ち、実機でフォント (188KB) の読み込みが 1 分以上止まった。       */
/*  要求をトラックの境目で区切り、要求したセクタからトラックの終わりまでを  */
/*  1 回で読んで持っておく (drivers/fdc_track.h の注記)。持つのは           */
/*  FDC_TRACK_SLOTS 本 — FAT のトラックとデータのトラックの分。             */
/*  受け皿は fdc.c の静的な領域から借りる (fdc_track_slot)。                */
/*                                                                          */
/*  **捨てる時**: disk_initialize (媒体の入れ替えを Ready 変化無しで済ませ  */
/*  る経路もあるので明示的に。diskio_set_fdd_drive もここを通らせる)。書き込みは */
/*  fdc_media_gen() の世代で外れる (FatFs 以外の書き込みも同じ)。          */
/*  メディアの種類が変われば世代とジオメトリの両方で外れる。               */
/* ======================================================================== */
static struct fdc_track_cache fdd_track;
static int fdd_track_ready = 0;

/* 受け皿を fdc.c から借りる (最初の 1 回だけ)。 */
static void fdd_track_setup(void)
{
    int i;

    if (fdd_track_ready) return;
    for (i = 0; i < FDC_TRACK_SLOTS; i++) {
        fdc_track_init(&fdd_track, i, fdc_track_slot(i));
    }
    for (i = 0; i < FDC_SECTOR_SLOTS; i++) {
        fdc_track_init_sector(&fdd_track, i, fdc_sector_slot(i));
    }
    fdd_track_ready = 1;
}

/* 先読みとセクタキャッシュの数を 1 行で出す (起動時、kernel/kernel.c)。 */
void diskio_print_fdd_cache(const char *tag)
{
    if (fdd_track.sec_hits + fdd_track.trk_hits + fdd_track.fills == 0) return;
    kprintf(0x07, "[fdc] %s: cache sec_hit=%u trk_hit=%u fill=%u idle=%u\n",
            tag, (unsigned int)fdd_track.sec_hits,
            (unsigned int)fdd_track.trk_hits, (unsigned int)fdd_track.fills,
            (unsigned int)fdd_track.idle_drops);
}

static int fdd_ops_read_multi(void *ctx, int drv, int cyl, int head, int sect,
                              int count, const struct fdc_geom *g, void *buf)
{
    (void)ctx;
    return fdc_read_sectors(drv, cyl, head, sect, count, g, buf);
}

static int fdd_ops_read_one(void *ctx, int drv, int cyl, int head, int sect,
                            const struct fdc_geom *g, void *buf)
{
    (void)ctx;
    return fdc_read_sector_geom(drv, cyl, head, sect, g, buf);
}

static void fdd_ops_copy(void *dst, const void *src, u32 n)
{
    kmemcpy(dst, src, n);
}

static u32 fdd_ops_gen(void *ctx, int drv)
{
    (void)ctx;
    return fdc_media_gen(drv);
}

/* 2 秒規則の時計 (drivers/fdc_track.h の FDC_TRACK_IDLE_TICKS)。 */
extern volatile u32 tick_count;
static u32 fdd_ops_now(void *ctx)
{
    (void)ctx;
    return tick_count;
}

static const struct fdc_track_ops fdd_track_ops = {
    fdd_ops_read_multi, fdd_ops_read_one, fdd_ops_copy, fdd_ops_gen,
    fdd_ops_now, 0
};

/* ドライブ番号設定 API (fatfs_vfs.c から呼ばれる)
 * ドライブ変更時はstatusとオフセットをリセットし、f_mountでの再初期化を促す */
/* 先読みはここでは捨てない: STA_NOINIT にするので、次の読みの前に必ず
 * disk_initialize (そこで捨てる) を通る (disk_read は NOINIT なら
 * RES_NOTRDY を返し、FatFs は disk_status を見て初期化し直す)。 */
void diskio_set_fdd_drive(int drv) { fdd_drive = drv; fdd_status = STA_NOINIT; }
void diskio_set_hdd_drive(int drv) {
    char devname[8];
    hdd_drive = drv; hdd_status = STA_NOINIT;
    hdd_part_offset = 0; hdd_sector_size = 512; hdd_ide_phys_size = 512;
    devname[0] = 'h'; devname[1] = 'd';
    devname[2] = '0' + (char)drv; devname[3] = '\0';
    hdd_dev = dev_find(devname);
}
void diskio_set_hdd_partition(u32 offset) { hdd_part_offset = offset; hdd_status = STA_NOINIT; }
void diskio_set_hdd_sector_size(u16 sz) { hdd_sector_size = sz; }
void diskio_set_hdd_ide_phys_size(u16 sz) { hdd_ide_phys_size = sz; }


/* ======================================================================== */
/*  disk_status — ドライブ状態取得                                          */
/* ======================================================================== */

DSTATUS disk_status(BYTE pdrv)
{
    switch (pdrv) {
    case DRV_FDD:
        return fdd_status;
    case DRV_HDD:
        return hdd_status;
    }
    return STA_NOINIT;
}


/* ======================================================================== */
/*  disk_initialize — ドライブ初期化                                        */
/* ======================================================================== */

DSTATUS disk_initialize(BYTE pdrv)
{
    switch (pdrv) {
    case DRV_FDD:
        /* FDCはカーネル起動時に fdc_init() で初期化済み。
         * 持っているトラックは捨てる (マウントし直し = 媒体が替わったかも)。 */
        fdd_track_setup();
        fdc_track_invalidate(&fdd_track);
        fdd_status = 0;
        return fdd_status;

    case DRV_HDD:
        /* IDEはカーネル起動時に ide_init() で初期化済み */
        if (ide_drive_present(hdd_drive)) {
            hdd_status = 0;
        } else {
            hdd_status = STA_NOINIT;
        }
        return hdd_status;
    }
    return STA_NOINIT;
}


/* ======================================================================== */
/*  disk_read — セクタ読み込み                                              */
/* ======================================================================== */

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    int rc;

    switch (pdrv) {
    case DRV_FDD:
        if (fdd_status & STA_NOINIT) return RES_NOTRDY;
        /* FDD: トラック単位に束ねて読む (上の fdd_track の注記)。
         * **ジオメトリはドライブから聞く** — FDC_SPT などのマクロは
         * 2HD 1232KB 固定で、1.44MB のディスクをゴミとして読む。 */
        rc = fdc_track_read(&fdd_track, &fdd_track_ops, fdd_drive,
                            fdc_get_geom(fdd_drive), (u32)sector, (u32)count,
                            (u8 *)buff);
        if (rc != 0) return RES_ERROR;
        return RES_OK;

    case DRV_HDD:
        if (hdd_status & STA_NOINIT) return RES_NOTRDY;
        if (!hdd_dev) return RES_ERROR;
        if (hdd_ide_phys_size == 256) {
            /* SASI 256Bセクタ: NP21/WのIDE LBAは256B単位だが転送は512B。
             * 連続LBAでは256Bずつしか進まずオーバーラップするため、
             * LBAを2刻みで進めて非重複の512Bを取得する。 */
            u32 sasi_lba = (u32)sector * (hdd_sector_size / 256)
                         + hdd_part_offset;
            u32 total_256 = (u32)count * (hdd_sector_size / 256);
            u32 ide_reads = total_256 / 2;  /* 512B = 2x256B */
            u32 j;
            for (j = 0; j < ide_reads; j++) {
                rc = dev_blk_read_lba(hdd_dev, sasi_lba + j * 2, 1,
                                      buff + j * 512);
                if (rc != 0) return RES_ERROR;
            }
        } else {
            /* 通常IDE 512Bセクタ */
            u32 phys_sector = (u32)sector * (hdd_sector_size / 512)
                            + hdd_part_offset;
            u32 phys_count  = (u32)count  * (hdd_sector_size / 512);
            rc = dev_blk_read_lba(hdd_dev, phys_sector, (int)phys_count, buff);
            if (rc != 0) return RES_ERROR;
        }
        return RES_OK;
    }
    return RES_PARERR;
}


/* ======================================================================== */
/*  disk_write — セクタ書き込み                                             */
/* ======================================================================== */

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    int rc;
    UINT i;

    switch (pdrv) {
    case DRV_FDD:
        if (fdd_status & STA_NOINIT) return RES_NOTRDY;
        /* FDD: fdc_write_sector (CHS ネイティブ) を直接使用。
         * 読み側と同じくジオメトリはドライブから聞く。
         * 先読みの古い中身は fdc_write_sector_geom が世代を進めて外す
         * (dev.c や KAPI からの書き込みも同じ経路なので、ここで捨てる
         * だけでは足りない)。 */
        {
            const struct fdc_geom *g = fdc_get_geom(fdd_drive);
            for (i = 0; i < count; i++) {
                int lba = (int)(sector + i);
                int sect = (lba % g->spt) + 1;
                int head = (lba / g->spt) % g->heads;
                int cyl  = lba / ((int)g->spt * (int)g->heads);
                rc = fdc_write_sector(fdd_drive, cyl, head, sect,
                                      buff + i * g->bps);
                if (rc != 0) return RES_ERROR;
            }
        }
        return RES_OK;

    case DRV_HDD:
        if (hdd_status & STA_NOINIT) return RES_NOTRDY;
        if (!hdd_dev) return RES_ERROR;
        if (hdd_ide_phys_size == 256) {
            /* SASI 256Bセクタ: LBA 2刻み書き込み */
            u32 sasi_lba = (u32)sector * (hdd_sector_size / 256)
                         + hdd_part_offset;
            u32 total_256 = (u32)count * (hdd_sector_size / 256);
            u32 ide_writes = total_256 / 2;
            u32 j;
            for (j = 0; j < ide_writes; j++) {
                rc = dev_blk_write_lba(hdd_dev, sasi_lba + j * 2, 1,
                                       buff + j * 512);
                if (rc != 0) return RES_ERROR;
            }
        } else {
            /* 通常IDE 512Bセクタ */
            u32 phys_sector = (u32)sector * (hdd_sector_size / 512)
                            + hdd_part_offset;
            u32 phys_count  = (u32)count  * (hdd_sector_size / 512);
            rc = dev_blk_write_lba(hdd_dev, phys_sector, (int)phys_count, buff);
            if (rc != 0) return RES_ERROR;
        }
        return RES_OK;
    }
    return RES_PARERR;
}

#endif /* FF_FS_READONLY == 0 */


/* ======================================================================== */
/*  disk_ioctl — デバイス制御                                               */
/* ======================================================================== */

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    IdeInfo ide_info;

    switch (pdrv) {
    case DRV_FDD:
        if (fdd_status & STA_NOINIT) return RES_NOTRDY;
        switch (cmd) {
        case CTRL_SYNC:
            /* PIO転送: バッファなし → 即完了 */
            return RES_OK;
        case GET_SECTOR_COUNT:
            {
                const struct fdc_geom *g = fdc_get_geom(fdd_drive);
                *(LBA_t *)buff =
                    (LBA_t)((int)g->cyls * (int)g->heads * (int)g->spt);
            }
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = (WORD)fdc_get_geom(fdd_drive)->bps;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1; /* 消去ブロック = 1セクタ */
            return RES_OK;
        }
        return RES_PARERR;

    case DRV_HDD:
        if (hdd_status & STA_NOINIT) return RES_NOTRDY;
        switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            if (ide_get_info(hdd_drive, &ide_info) != 0) return RES_ERROR;
            /* パーティションオフセット分を差し引き、論理セクタ単位に変換 */
            *(LBA_t *)buff = (LBA_t)((ide_info.total_sectors - hdd_part_offset)
                                     / (hdd_sector_size / hdd_ide_phys_size));
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = (WORD)hdd_sector_size;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
        }
        return RES_PARERR;
    }
    return RES_PARERR;
}


/* ======================================================================== */
/*  get_fattime — FAT時刻取得 (RTCから)                                     */
/* ======================================================================== */

DWORD get_fattime(void)
{
    RTC_Time t;
    DWORD fattime;
    int year;

    rtc_read(&t);

    /* RTC_Time.year は 00-99 (2桁) → 2000年代として扱う */
    year = (int)t.year;
    if (year < 80) year += 2000;  /* 00-79 → 2000-2079 */
    else           year += 1900;  /* 80-99 → 1980-1999 */

    /* FAT時刻フォーマット:
     *   bit[31:25] = 年 (1980年起点, 0-127)
     *   bit[24:21] = 月 (1-12)
     *   bit[20:16] = 日 (1-31)
     *   bit[15:11] = 時 (0-23)
     *   bit[10:5]  = 分 (0-59)
     *   bit[4:0]   = 秒/2 (0-29)
     */
    fattime = ((DWORD)(year - 1980) << 25)
            | ((DWORD)t.month << 21)
            | ((DWORD)t.day << 16)
            | ((DWORD)t.hour << 11)
            | ((DWORD)t.min << 5)
            | ((DWORD)(t.sec / 2));

    return fattime;
}
