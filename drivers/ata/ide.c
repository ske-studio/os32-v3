/* ======================================================================== */
/*  IDE.C — PC-98 IDE/ATA PIOドライバ実装                                   */
/*                                                                          */
/*  PIOモードによるセクタ読み書き。LBA の API (ide_read_sector ほか) は      */
/*  drivers/ata/ide_addr.c が IDENTIFY から選んだ方式 (LBA28 / 現在の CHS /      */
/*  既定の CHS) でレジスタ値を作る (票 TASK_HDD_INSTALL 段 1、F8 / F13)。     */
/*  drivers/base/dev.c の hd0-3 もこの LBA の API を通る。                        */
/*  NP21/W + DOSBox-X + FreeBSD/pc98 wdc を参考に実装。                     */
/* ======================================================================== */

#include "ide.h"
#include "ide_addr.h"
#include "ide_bus.h"
#include "kprintf.h"
#include "memmap.h"
#include "tick_clock.h"
#include "io.h"

/* ドライブ検出フラグ (4ドライブ対応)
 * drive 0 = バンク0 Master (NP21/W IDE #0)
 * drive 1 = バンク0 Slave  (NP21/W IDE #1)
 * drive 2 = バンク1 Master (NP21/W IDE #2)
 * drive 3 = バンク1 Slave  (NP21/W IDE #3)
 */
#define IDE_SASI_SPT          33
#define IDE_SASI_HEADS_SMALL  4
#define IDE_SASI_HEADS_MEDIUM 6
#define IDE_SASI_HEADS_LARGE  8
#if OS32_FEATURE_IDE
static char identify_serial[IDE_MAX_DRIVES][IDE_SERIAL_LEN];
#endif
static int drive_present[IDE_MAX_DRIVES] = { 0, 0, 0, 0 };
static IdeInfo drive_info[IDE_MAX_DRIVES];
/* IdeInfo に無い IDENTIFY の語 (word 53-56 ほか)。IdeInfo は KAPI の並びなので
 * 足さずに別に持つ (票 TASK_HDD_INSTALL 段 0)。 */
static IdeGeom drive_geom[IDE_MAX_DRIVES];

/* ---- 内部ヘルパー ---- */

/* バンク選択 (0=プライマリ, 無効化しない) */
static void ide_select_bank(int bank)
{
    ide_bus_select(bank);
}

/* NetBSD ata_wdc.c ATA_DELAY: 10 seconds for drive I/O. */
typedef struct {
    u32 start_ms;
    u32 early_polls;
} IdeWait;

static IdeWait ide_wait_start(u32 early_polls)
{
    u32 start = tick_clock_ms();
    return (IdeWait){ start, (start && _irq_enabled()) ? 0 : early_polls + 1 };
}

static int ide_wait_pending(IdeWait *wait)
{
    u32 elapsed = tick_clock_ms() - wait->start_ms;
    /* Boot or IRQ-masked I/O can see a stationary clock. Once the
     * first tick arrives only elapsed time bounds this wait. */
    if (wait->early_polls && elapsed == 0) return --wait->early_polls != 0;
    return elapsed < IDE_IO_TIMEOUT_MS;
}

/* ステータス待ち (BSY=0になるまで) */
static int ide_wait_bsy(void)
{
    IdeWait wait = ide_wait_start(IDE_EARLY_BSY_POLLS);
    while (ide_wait_pending(&wait)) {
        u8 st = ide_bus_alt();
        if (!(st & IDE_ST_BSY)) return IDE_OK;
    }
    return IDE_ERR_TIMEOUT;
}

/* DRQ待ち: BSYが落ちてDRQが立つまで */
static int ide_wait_drq(void)
{
    IdeWait wait = ide_wait_start(IDE_EARLY_PHASE_POLLS);
    u8 st = IDE_ST_BSY;

    /* まずBSYが落ちるのを待つ */
    while (ide_wait_pending(&wait)) {
        st = ide_bus_alt();
        if (!(st & IDE_ST_BSY)) break;
    }
    if (st & IDE_ST_BSY) return IDE_ERR_TIMEOUT;

    /* DRQチェック */
    st = ide_bus_alt();
    if (st & IDE_ST_ERR) return IDE_ERR_IO;
    if (st & IDE_ST_DRQ) return IDE_OK;

    /* DRQが来るまで追加待ち */
    wait = ide_wait_start(IDE_EARLY_BSY_POLLS);
    while (ide_wait_pending(&wait)) {
        st = ide_bus_alt();
        if (st & IDE_ST_DRQ) return IDE_OK;
        if (st & IDE_ST_ERR) return IDE_ERR_IO;
    }
    return IDE_ERR_TIMEOUT;
}

/* ドライブ選択 (Master/Slave)
 * PC-98では IDE_DRV_HEAD の bit4 で Master(0)/Slave(1) を選択する。
 * またI/Oバンク (0 or 1) も同時に切り替える。 */
static void ide_select_drive(int drive)
{
    int bank = drive / IDE_DRIVES_PER_BANK;        /* 0 or 1 */
    int slave = drive % IDE_DRIVES_PER_BANK;       /* 0=Master, 1=Slave */
    ide_select_bank(bank);
    ide_bus_out(IDE_REG_DRV_HEAD, IDE_DRVHEAD_BASE | (slave ? IDE_DRVHEAD_SLAVE : 0x00));
    /* ドライブ選択後、400ns待ち（ダミーリード） */
    {
        int i;
        for (i = 0; i < IDE_SEL_SETTLE; i++) ide_bus_alt();
    }
}


/* セクタ指定のレジスタを書く。DRV/HEAD は ide_addr_make が作った値
 * (LBA28 なら bit6 と LBA[27:24]、CHS ならヘッド) をそのまま出す。 */
static void ide_set_addr(const IdeAddr *a, u8 count)
{
    ide_bus_out(IDE_REG_SECT_CNT, count);
    ide_bus_out(IDE_REG_SECT_NUM, a->sect_num);
    ide_bus_out(IDE_REG_CYL_LO, a->cyl_lo);
    ide_bus_out(IDE_REG_CYL_HI, a->cyl_hi);
    ide_bus_out(IDE_REG_DRV_HEAD, a->drv_head);
}

/* CHS をそのまま IdeAddr にする (ide_read_sector_chs 系の入口) */
static void ide_chs_addr(int drive, u16 cyl, u8 head, u8 sect, IdeAddr *a)
{
    a->mode     = IDE_AMODE_CHS_DEF;
    a->sect_num = sect;
    a->cyl_lo   = (u8)(cyl & 0xFF);
    a->cyl_hi   = (u8)((cyl >> 8) & 0xFF);
    a->drv_head = (u8)(IDE_DRVHEAD_BASE | (head & IDE_DRVHEAD_ADDR_MASK)
                       | ((drive % IDE_DRIVES_PER_BANK) ? IDE_DRVHEAD_SLAVE : 0x00));
}

static void ide_pio_settle(void);
static int ide_pio_finish(void);
#include "ide_multiple.inc"
#include "ide_pio.inc"

/* ============================================================ */
/*  公開API                                                      */
/* ============================================================ */

int ide_init(void)
{
    int found = 0;
    int bank, drv;

    ide_bus_init();

    ide_multiple_init();
    for (drv = 0; drv < IDE_MAX_DRIVES; drv++) {
        drive_present[drv] = 0;
        drive_geom[drv].valid = 0;
    }

    /* 全バンクの全ドライブをスキャン */
    for (bank = 0; bank < IDE_BANK_COUNT; bank++) {
        u8 probe;

        ide_select_bank(bank);

        /* フローティングバス検出: 存在しないバンクでは全ビット1 (0xFF)
         * が返る → 即スキップ
         * 注: 0x00 はスレーブドライブ選択前の正常値なのでスキップしない */
        probe = ide_bus_alt();
        kprintf(KLOG_ATTR, "[ide] bank%d probe=0x%02x\n", bank, probe);
        if (probe == IDE_BUS_OPEN8) continue;

        /* 割り込み無効 (nIEN=1) — ポーリングモード */
        ide_bus_ctl(IDE_NIEN);

        for (drv = 0; drv < IDE_DRIVES_PER_BANK; drv++) {
            int idx = bank * IDE_DRIVES_PER_BANK + drv; /* 0-3 */
            int ret;

            /* IDENTIFYコマンドで直接検出。ステータスだけでは
             * スレーブドライブを見逃す場合がある */
            ret = ide_identify(idx, &drive_info[idx]);
            kprintf(KLOG_ATTR, "[ide] drive%d identify=%d\n", idx, ret);
            if (ret == IDE_OK) {
                drive_present[idx] = 1;
                found++;
            }
        }
        /* Detect both devices before a rejected/busy C6h can stop this bank. */
        for (drv = 0; drv < IDE_DRIVES_PER_BANK; drv++) {
            int idx = bank * IDE_DRIVES_PER_BANK + drv;
            if (!drive_present[idx]) continue;
            ide_select_drive(idx);
            if (ide_wait_bsy() != IDE_OK) break;
            if (ide_multiple_prepare(idx) != IDE_OK) break;
        }
    }

    /* バンク0のマスタードライブを再選択（後続アクセスのため） */
    ide_select_drive(0);

    return found;
}

int ide_identify(int drive, IdeInfo *info)
{
    u16 buf[IDE_IDENTIFY_WORDS];
    int i, ret;

    /* drive_geom は I/O の方式 (ide_addr.c) の元なので、**成功したときだけ**
     * 書き換える。KAPI の ide_identify (シェルの `ide N`) が一時的に失敗しても、
     * 起動時に得た値で読み書きが続けられる。起動時の初期値は 0 (valid = 0)。 */
    ide_select_drive(drive & (IDE_MAX_DRIVES - 1));
    ret = ide_wait_bsy();
    if (ret != IDE_OK) return ret;

    /* IDENTIFYコマンド発行 */
    ide_bus_out(IDE_REG_STATUS, IDE_CMD_IDENTIFY);

    /* DRQ待ち */
    ret = ide_wait_drq();
    if (ret != IDE_OK) return IDE_ERR_NO_DRIVE;

    /* 256ワード読み出し (個別inpwループ) */
    {
        int w;
        for (w = 0; w < IDE_IDENTIFY_WORDS; w++) {
            buf[w] = ide_bus_data_word();
        }
    }

    /* IRQクリア: Statusレジスタ読み出し */
    { u8 st = ide_bus_in(IDE_REG_STATUS); (void)st; }
    ide_wait_bsy();

#if OS32_FEATURE_IDE
    /* ATA IDENTIFY word 106. The PIO path transfers 256 words per sector.
     * FreeBSD sys/cam/ata/ata_all.c, ata_logical_sector_size(). */
    if ((buf[106] & 0xd000) == 0x5000) return IDE_ERR_RANGE;
#endif
    /* 幾何の生の語。I/O の指定の方式 (LBA28 / 現在の CHS / 既定の CHS) は
     * drivers/ata/ide_addr.c がこの値から決める (票 TASK_HDD_INSTALL 段 1)。 */
    {
        IdeGeom *g = &drive_geom[drive & (IDE_MAX_DRIVES - 1)];
        g->def_cyl   = buf[IDE_ID_DEF_CYL_WORD];
        g->def_heads = buf[IDE_ID_DEF_HEADS_WORD];
        g->def_spt   = buf[IDE_ID_DEF_SPT_WORD];
        g->w49       = buf[IDE_ID_CAP_WORD];
        g->w53       = buf[IDE_ID_VALID_WORD];
        g->cur_cyl   = buf[IDE_ID_CUR_CYL_WORD];
        g->cur_heads = buf[IDE_ID_CUR_HEADS_WORD];
        g->cur_spt   = buf[IDE_ID_CUR_SPT_WORD];
        g->total     = (u32)buf[IDE_ID_TOTAL_WORD] | ((u32)buf[IDE_ID_TOTAL_WORD + 1] << 16);
        g->valid     = 1;
    }

    /* 情報抽出 */
    info->cylinders = buf[IDE_ID_DEF_CYL_WORD];
    info->heads = buf[IDE_ID_DEF_HEADS_WORD];
    info->sectors = buf[IDE_ID_DEF_SPT_WORD];

    /* LBAサポート確認 (word 49 bit 9) */
    info->lba_supported = (buf[IDE_ID_CAP_WORD] & IDE_ID_CAP_LBA) ? 1 : 0;

    /* LBA総セクタ数 (word 60-61) */
    info->total_sectors = (u32)buf[IDE_ID_TOTAL_WORD] | ((u32)buf[IDE_ID_TOTAL_WORD + 1] << 16);

    /* SASI標準ジオメトリ判定 (NP21/W sasihdd[]テーブルと一致)
     * SPT=33, Heads∈{4,6,8} → SASI 256B/セクタ形式 (HDI)
     * NP21/WはSASI形式HDIをIDE経由で提供するが、
     * 内部LBAは256Bセクタ単位のまま */
#if OS32_FEATURE_IDE
    info->phys_sector_size = SYS_HDD_SECTOR_SIZE;
#else
    if (info->sectors == IDE_SASI_SPT &&
        (info->heads == IDE_SASI_HEADS_SMALL || info->heads == IDE_SASI_HEADS_MEDIUM || info->heads == IDE_SASI_HEADS_LARGE)) {
        info->phys_sector_size = SASI_SECTOR_BYTES;
    } else {
        info->phys_sector_size = SYS_HDD_SECTOR_SIZE;
    }

#endif
    ide_multiple_identify(drive & (IDE_MAX_DRIVES - 1), buf,
                          info->phys_sector_size);

    /* サイズ(MB) = 総セクタ数 / (1MB あたりのセクタ数)。
     * 総セクタ数 × セクタ長を先に掛けると 4GB 超 (実機の 8GB = 16,514,063
     * セクタ) で 32bit が溢れる。1MB はどちらのセクタ長でも割り切れる。 */
    info->size_mb = info->total_sectors
                    / ((KIB * KIB) / (u32)info->phys_sector_size);

    /* モデル名 (word 27-46): ATA文字列はバイトスワップ */
    for (i = 0; i < IDE_MODEL_LEN / (int)sizeof(u16); i++) {
        info->model[i * 2]     = (char)(buf[IDE_ID_MODEL_WORD + i] >> 8);
        info->model[i * 2 + 1] = (char)(buf[IDE_ID_MODEL_WORD + i] & 0xFF);
    }
    info->model[IDE_MODEL_LEN] = '\0';
    /* 末尾空白除去 */
    for (i = IDE_MODEL_LEN - 1; i >= 0 && info->model[i] == ' '; i--) {
        info->model[i] = '\0';
    }

    /* シリアル番号 (word 10-19) */
    for (i = 0; i < IDE_SERIAL_LEN / (int)sizeof(u16); i++) {
        info->serial[i * 2]     = (char)(buf[IDE_ID_SERIAL_WORD + i] >> 8);
        info->serial[i * 2 + 1] = (char)(buf[IDE_ID_SERIAL_WORD + i] & 0xFF);
    }
#if OS32_FEATURE_IDE
    __builtin_memcpy(identify_serial[drive & (IDE_MAX_DRIVES-1)], info->serial, IDE_SERIAL_LEN);
#endif
    info->serial[IDE_SERIAL_LEN] = '\0';
    for (i = IDE_SERIAL_LEN - 1; i >= 0 && info->serial[i] == ' '; i--) {
        info->serial[i] = '\0';
    }

    /* ファームウェア (word 23-26) */
    for (i = 0; i < IDE_FIRMWARE_LEN / (int)sizeof(u16); i++) {
        info->firmware[i * 2]     = (char)(buf[IDE_ID_FIRMWARE_WORD + i] >> 8);
        info->firmware[i * 2 + 1] = (char)(buf[IDE_ID_FIRMWARE_WORD + i] & 0xFF);
    }
    info->firmware[IDE_FIRMWARE_LEN] = '\0';
    for (i = IDE_FIRMWARE_LEN - 1; i >= 0 && info->firmware[i] == ' '; i--) {
        info->firmware[i] = '\0';
    }

    return IDE_OK;
}


int ide_drive_present(int drive)
{
    return drive_present[drive & (IDE_MAX_DRIVES - 1)];
}

int ide_get_geom(int drive, IdeGeom *out)
{
    const IdeGeom *g;

    if (drive < 0 || drive >= IDE_MAX_DRIVES) return IDE_ERR_NO_DRIVE;
    g = &drive_geom[drive];
    if (!g->valid) return IDE_ERR_NO_DRIVE;
    if (out) *out = *g;
    return IDE_OK;
}

int ide_get_info(int drive, IdeInfo *info)
{
    if (!drive_present[drive & (IDE_MAX_DRIVES - 1)]) return IDE_ERR_NO_DRIVE;
    if (info) {
        int i;
        u8 *dst = (u8 *)info;
        u8 *src = (u8 *)&drive_info[drive & (IDE_MAX_DRIVES - 1)];
        for (i = 0; i < sizeof(IdeInfo); i++) {
            dst[i] = src[i];
        }
    }
    return IDE_OK;
}

/* ======================================================================== */
/*  LBA の読み書き (KAPI の ide_read_sector 系と dev.c の hd0-3)             */
/*                                                                          */
/*  指定の方式と範囲は drivers/ata/ide_addr.c が決める。**範囲外は 1 セクタも    */
/*  出さない** — 複数セクタは先に全体の範囲を見てから回す (前半だけ書いて    */
/*  後半で断ると、呼び手には失敗なのに媒体は変わっている)。                   */
/* ======================================================================== */

int ide_addr_mode_of(int drive)
{
    if (drive < 0 || drive >= IDE_MAX_DRIVES) return IDE_AMODE_NONE;
    if (!drive_present[drive]) return IDE_AMODE_NONE;
    return ide_addr_mode(&drive_geom[drive], (u16 *)0, (u16 *)0, (u32 *)0);
}

int ide_range_ok(int drive, u32 lba, u32 count)
{
    if (drive < 0 || drive >= IDE_MAX_DRIVES) return 0;
    if (!drive_present[drive]) return 0;
    return ide_addr_range_ok(&drive_geom[drive], lba, count);
}

#include "ide_request.inc"

#if OS32_FEATURE_IDE
/* Reservation callbacks have already passed admission. Do not enter public
 * dispatch again while the unit is being prepared or recovered. */
int ide_install_identify(u32 unit, IdeInfo *info, char serial[IDE_SERIAL_LEN])
{
    if (unit >= IDE_MAX_DRIVES) return IDE_ERR_RANGE;
    int rc = ide_identify(unit, info);
    if (!rc) __builtin_memcpy(serial, identify_serial[unit], IDE_SERIAL_LEN);
    return rc;
}

int ide_install_read(u32 unit, u32 lba, void *sector)
{
    if (unit >= IDE_MAX_DRIVES) return IDE_ERR_RANGE;
    return ide_read_one(unit, lba, sector);
}

int ide_install_read_many(u32 unit, u32 lba, u32 count, void *sectors)
{
    if (unit >= IDE_MAX_DRIVES) return IDE_ERR_RANGE;
    return ide_request(unit, lba, count, sectors, 0);
}

int ide_install_write(u32 unit, u32 lba, u32 count, const void *sectors)
{
    if (unit >= IDE_MAX_DRIVES) return IDE_ERR_RANGE;
    return ide_request(unit, lba, count, 0, sectors);
}

int ide_install_flush(u32 unit)
{
    if (unit >= IDE_MAX_DRIVES || !drive_present[unit]) return IDE_ERR_NO_DRIVE;
    ide_select_drive(unit);
    int rc = ide_wait_bsy();
    if (rc) return rc;
    /* Seagate SV35 Series Product Manual 100415205A, supported commands.
     * FreeBSD sys/cam/ata/ata_da.c, BIO_FLUSH. */
    ide_bus_out(IDE_REG_STATUS, IDE_CMD_FLUSH_CACHE);
    ide_pio_settle();
    return ide_pio_finish();
}
#endif
