/* ======================================================================== */
/*  DEV.C — デバイス抽象化層 実装                                           */
/*                                                                          */
/*  CHS / LBA 2系統デバイス:                                                */
/*    fd0/fd1 — FDD (CHS ネイティブ via fdc_read_sector)                   */
/*    hd0-3  — IDE HDD (LBA via ide_read_sectors、方式は ide_addr.c)       */
/*    cd0    — ATAPI CD (LBA ネイティブ via atapi_read_sectors)             */
/*    lo0-3  — ループバック (loop_dev.c が独自に登録)                       */
/*                                                                          */
/*  dev_blk_read_lba(): CHS デバイスには LBA→CHS 変換、                    */
/*                      LBA デバイスには直接委譲する汎用ラッパー             */
/* ======================================================================== */

#include "dev.h"
#include "disk.h"
#include "ide.h"
#include "atapi.h"
#include "fdc.h"   /* fdc_read_sector, fdc_write_sector */

/* ======== デバイステーブル ======== */
static Device *dev_table[MAX_DEVICES];
static int     dev_num = 0;

/* ======== ユーティリティ ======== */
static int dev_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* ======================================================================== */
/*  FDD ドライバ (CHS ネイティブ)                                          */
/* ======================================================================== */

static int fdd0_read_chs(Device *self, u16 cyl, u8 head, u8 sect,
                          void *buf)
{
    (void)self;
    return fdc_read_sector(0, (int)cyl, (int)head, (int)sect, buf);
}

static int fdd0_write_chs(Device *self, u16 cyl, u8 head, u8 sect,
                           const void *buf)
{
    (void)self;
    return fdc_write_sector(0, (int)cyl, (int)head, (int)sect, buf);
}

static int fdd1_read_chs(Device *self, u16 cyl, u8 head, u8 sect,
                          void *buf)
{
    (void)self;
    return fdc_read_sector(1, (int)cyl, (int)head, (int)sect, buf);
}

static int fdd1_write_chs(Device *self, u16 cyl, u8 head, u8 sect,
                           const void *buf)
{
    (void)self;
    return fdc_write_sector(1, (int)cyl, (int)head, (int)sect, buf);
}

/* 下の 2 つは 2HD 1232KB の値で初期化してある。**dev_init() が
 * fdc_get_geom() から書き直す** — 1.44MB で起動したときに 1024B/8セクタの
 * ままだと、マウントはできても LBA が全部ずれる (票 FD144)。 */
static Device fdd0_dev = {
    "fd0",
    DEV_BLOCK,
    DEV_BUS_FDC, 0,         /* bus_type=FDC, bus_id=0 */
    DISK_SECT_SZ,           /* sect_size */
    DISK_TOTAL_SEC,         /* total_sects */
    0,                      /* blk_read (LBA) = NULL — CHS デバイス */
    0,                      /* blk_write (LBA) = NULL */
    0, 0,                   /* chr_read, chr_write */
    0,                      /* ioctl */
    0,                      /* priv */
    fdd0_read_chs,          /* blk_read_chs */
    fdd0_write_chs,         /* blk_write_chs */
    FDC_CYLINDERS,          /* cyls */
    FDC_HEADS,              /* heads */
    FDC_SPT                 /* spt */
};

static Device fdd1_dev = {
    "fd1",
    DEV_BLOCK,
    DEV_BUS_FDC, 1,         /* bus_type=FDC, bus_id=1 */
    DISK_SECT_SZ,
    DISK_TOTAL_SEC,
    0, 0,                   /* blk_read/write (LBA) = NULL */
    0, 0,                   /* chr_read, chr_write */
    0, 0,                   /* ioctl, priv */
    fdd1_read_chs,
    fdd1_write_chs,
    FDC_CYLINDERS, FDC_HEADS, FDC_SPT
};

/* ======================================================================== */
/*  HDD ドライバ (IDE — LBA の API へ委譲)                                  */
/*                                                                          */
/*  以前はここで IDENTIFY の**既定**幾何を使って LBA → CHS にしていた        */
/*  (ide.c にも同じ変換があった)。BIOS がドライブの現在の変換を変えていると  */
/*  別の物理セクタを指す (票 TASK_HDD_INSTALL F8 / F13)。変換は ide.c の     */
/*  1 か所 (drivers/ide_addr.c の方式選択: LBA28 / 現在の CHS / 既定の CHS)  */
/*  に寄せ、ここは LBA をそのまま渡す。範囲外は ide.c が 1 セクタも出さずに  */
/*  断る (IDE_ERR_RANGE)。                                                  */
/* ======================================================================== */

static int hd_read(Device *self, int lba, int count, void *buf)
{
    if (count < 0) return -1;
    return ide_read_sectors((int)self->bus_id, (u32)lba, (u32)count, buf);
}

static int hd_write(Device *self, int lba, int count, const void *buf)
{
    if (count < 0) return -1;
    return ide_write_sectors((int)self->bus_id, (u32)lba, (u32)count, buf);
}

/* cyls/heads/spt は情報用 (dev_register_hdd が IDENTIFY の既定値で埋める)。
 * I/O には使わない — blk_read / blk_write があるので dev_blk_read_lba は
 * CHS の経路を通らない。 */
static Device hd0_dev = {
    "hd0", DEV_BLOCK, DEV_BUS_IDE, 0, 512, 0,
    hd_read, hd_write, 0, 0, 0, 0,   /* blk_read/write(LBA), chr, ioctl, priv */
    0, 0,                            /* blk_read_chs/write_chs = NULL */
    0, 0, 0                          /* cyls/heads/spt: dev_register_hdd で設定 */
};

static Device hd1_dev = {
    "hd1", DEV_BLOCK, DEV_BUS_IDE, 1, 512, 0,
    hd_read, hd_write, 0, 0, 0, 0,
    0, 0,
    0, 0, 0
};

static Device hd2_dev = {
    "hd2", DEV_BLOCK, DEV_BUS_IDE, 2, 512, 0,
    hd_read, hd_write, 0, 0, 0, 0,
    0, 0,
    0, 0, 0
};

static Device hd3_dev = {
    "hd3", DEV_BLOCK, DEV_BUS_IDE, 3, 512, 0,
    hd_read, hd_write, 0, 0, 0, 0,
    0, 0,
    0, 0, 0
};

/* HDDを登録するユーティリティ (4ドライブ対応) */
static Device *hd_devs[4] = { &hd0_dev, &hd1_dev, &hd2_dev, &hd3_dev };

void dev_register_hdd(int drive)
{
    IdeInfo info;
    if (drive < 0 || drive > 3) return;
    if (ide_get_info(drive, &info) == IDE_OK) {
        hd_devs[drive]->total_sects = info.total_sectors;
        hd_devs[drive]->cyls  = info.cylinders;
        hd_devs[drive]->heads = info.heads;
        hd_devs[drive]->spt   = info.sectors;
        dev_register(hd_devs[drive]);
    }
}

/* ======================================================================== */
/*  CD-ROM ドライバ (ATAPI — LBA ネイティブ)                                */
/* ======================================================================== */

static int cd0_read(Device *self, int lba, int count, void *buf)
{
    (void)self;
    return atapi_read_sectors((u32)lba, (u32)count, buf);
}

static Device cd0_dev = {
    "cd0", DEV_BLOCK,
    DEV_BUS_ATAPI, 0,       /* bus_type=ATAPI, bus_id=0 */
    ATAPI_SECTOR_SIZE,      /* sect_size 2048 */
    0,                      /* total_sects (初期化時に設定) */
    cd0_read,               /* blk_read (LBA ネイティブ) */
    0,                      /* blk_write = NULL (読み取り専用) */
    0, 0, 0, 0,             /* chr_read, chr_write, ioctl, priv */
    0, 0,                   /* blk_read_chs/write_chs = NULL (LBA デバイス) */
    0, 0, 0                 /* cyls/heads/spt = 0 (CHS ジオメトリなし) */
};

/* CD-ROMを登録するユーティリティ */
void dev_register_cdrom(void)
{
    AtapiCapacity cap;
    if (atapi_read_capacity(&cap) == ATAPI_OK) {
        cd0_dev.total_sects = cap.total_sectors;
    }
    dev_register(&cd0_dev);
}

/* SCSIデバイスの登録 (将来用スタブ — WD33C93ドライバ完成後に実装予定) */
void dev_register_scsi(int scsi_id)
{
    (void)scsi_id;
}

/* ======================================================================== */
/*  汎用 LBA ラッパー                                                       */
/*                                                                          */
/*  CHS デバイス: ジオメトリから LBA→CHS 変換 → blk_read_chs              */
/*  LBA デバイス: blk_read を直接呼び出し                                   */
/* ======================================================================== */

/* [lba, lba+count) が 32 ビットで桁あふれしないか。count < 0 も断る。
 * 各デバイスの上限 (総数・LBA28) は下の層 (ide.c / atapi.c / loop) が見る —
 * ここは「足したら 0 付近へ戻る」要求を下へ渡さないためだけ。 */
static int dev_span_ok(u32 lba, int count)
{
    if (count < 0) return 0;
    if (count == 0) return 1;
    return ((u32)count - 1U <= 0xFFFFFFFFUL - lba) ? 1 : 0;
}

int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{
    int i;
    u8 *p;

    if (!dev) return -1;
    if (!dev_span_ok(lba, count)) return -1;

    /* LBA ネイティブデバイス (ATAPI CD) */
    if (dev->blk_read)
        return dev->blk_read(dev, (int)lba, count, buf);

    /* CHS デバイス: LBA → CHS 変換 */
    if (!dev->blk_read_chs || dev->spt == 0 || dev->heads == 0)
        return -1;

    p = (u8 *)buf;
    for (i = 0; i < count; i++) {
        u32 cur  = lba + (u32)i;
        u8  sect = (u8)((cur % dev->spt) + 1);  /* 1-based */
        u32 tmp  = cur / dev->spt;
        u8  head = (u8)(tmp % dev->heads);
        u16 cyl  = (u16)(tmp / dev->heads);
        int ret  = dev->blk_read_chs(dev, cyl, head, sect, p);
        if (ret != 0) return ret;
        p += dev->sect_size;
    }
    return 0;
}

int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{
    int i;
    const u8 *p;

    if (!dev) return -1;
    if (!dev_span_ok(lba, count)) return -1;

    /* LBA ネイティブデバイス */
    if (dev->blk_write)
        return dev->blk_write(dev, (int)lba, count, buf);

    /* CHS デバイス: LBA → CHS 変換 */
    if (!dev->blk_write_chs || dev->spt == 0 || dev->heads == 0)
        return -1;

    p = (const u8 *)buf;
    for (i = 0; i < count; i++) {
        u32 cur  = lba + (u32)i;
        u8  sect = (u8)((cur % dev->spt) + 1);
        u32 tmp  = cur / dev->spt;
        u8  head = (u8)(tmp % dev->heads);
        u16 cyl  = (u16)(tmp / dev->heads);
        int ret  = dev->blk_write_chs(dev, cyl, head, sect, p);
        if (ret != 0) return ret;
        p += dev->sect_size;
    }
    return 0;
}

/* ======================================================================== */
/*  公開API                                                                 */
/* ======================================================================== */

/* FDD の記述子をそのドライブのいまのジオメトリに合わせる。
 * fdc_set_media() の後、登録の前に呼ぶ。 */
static void fdd_apply_geom(Device *d, int drv)
{
    const struct fdc_geom *g = fdc_get_geom(drv);
    d->sect_size  = g->bps;
    d->total_sects = (u32)g->cyls * (u32)g->heads * (u32)g->spt;
    d->cyls  = g->cyls;
    d->heads = g->heads;
    d->spt   = g->spt;
}

void dev_init(void)
{
    int i;
    for (i = 0; i < MAX_DEVICES; i++) {
        dev_table[i] = 0;
    }
    dev_num = 0;

    /* 記述子の静的初期値は 2HD。**いまのメディアで上書きする** */
    fdd_apply_geom(&fdd0_dev, 0);
    fdd_apply_geom(&fdd1_dev, 1);

    /* 標準デバイス登録 */
    dev_register(&fdd0_dev);
    dev_register(&fdd1_dev);
}

int dev_register(Device *dev)
{
    if (dev_num >= MAX_DEVICES) return -1;
    dev_table[dev_num] = dev;
    dev_num++;
    return 0;
}

Device *dev_find(const char *name)
{
    int i;
    for (i = 0; i < dev_num; i++) {
        if (dev_table[i] && dev_streq(dev_table[i]->name, name)) {
            return dev_table[i];
        }
    }
    return 0;
}

Device *dev_get(int index)
{
    if (index < 0 || index >= dev_num) return 0;
    return dev_table[index];
}

int dev_count(void)
{
    return dev_num;
}

int dev_get_names(const char **names, int max)
{
    int i, n = 0;
    for (i = 0; i < dev_num && n < max; i++) {
        if (dev_table[i]) {
            names[n++] = dev_table[i]->name;
        }
    }
    return n;
}

int dev_api_get_info(int idx, char *name, int name_max, int *type, u32 *total_sects)
{
    Device *d = dev_get(idx);
    if (!d) return -1;
    {
        int i = 0;
        while (d->name[i] && i < name_max - 1) {
            name[i] = d->name[i];
            i++;
        }
        name[i] = '\0';
    }
    if (type) *type = d->type;
    if (total_sects) *total_sects = d->total_sects;
    return 0;
}
