/* ======================================================================== */
/*  IDE.C — PC-98 IDE/ATA PIOドライバ実装                                   */
/*                                                                          */
/*  PIOモードによるセクタ読み書き。LBA の API (ide_read_sector ほか) は      */
/*  drivers/ide_addr.c が IDENTIFY から選んだ方式 (LBA28 / 現在の CHS /      */
/*  既定の CHS) でレジスタ値を作る (票 TASK_HDD_INSTALL 段 1、F8 / F13)。     */
/*  drivers/dev.c の hd0-3 もこの LBA の API を通る。                        */
/*  NP21/W + DOSBox-X + FreeBSD/pc98 wdc を参考に実装。                     */
/* ======================================================================== */

#include "ide.h"
#include "ide_addr.h"
#include "io.h"
#include "pc98.h"
#include "kprintf.h"

/* 外部: irq_disable (kernel/idt.c で定義)。drivers/ は -Ikernel を
 * 持たないため、kbd.c の irq_enable と同じ扱いでここに宣言する。 */
extern void irq_disable(unsigned int irq);

/* ドライブ検出フラグ (4ドライブ対応)
 * drive 0 = バンク0 Master (NP21/W IDE #0)
 * drive 1 = バンク0 Slave  (NP21/W IDE #1)
 * drive 2 = バンク1 Master (NP21/W IDE #2)
 * drive 3 = バンク1 Slave  (NP21/W IDE #3)
 */
#define IDE_MAX_DRIVES 4
static int drive_present[IDE_MAX_DRIVES] = { 0, 0, 0, 0 };
static IdeInfo drive_info[IDE_MAX_DRIVES];
/* IdeInfo に無い IDENTIFY の語 (word 53-56 ほか)。IdeInfo は KAPI の並びなので
 * 足さずに別に持つ (票 TASK_HDD_INSTALL 段 0)。 */
static IdeGeom drive_geom[IDE_MAX_DRIVES];

/* ---- 内部ヘルパー ---- */

/* バンク選択 (0=プライマリ, 無効化しない) */
static void ide_select_bank(int bank)
{
    /* NP21/Wではgetidedev()がセカンダリバンク(IDE_BANK1)を参照するため、
       明示的に書き込む必要がある。値0x00=プライマリ, 0x01=セカンダリ */
    outp(IDE_BANK1, (unsigned)(bank ? 0x01 : 0x00));
}

/* ステータス待ち (BSY=0になるまで) */
static int ide_wait_bsy(void)
{
    int timeout = IDE_TIMEOUT_BSY;
    while (timeout-- > 0) {
        u8 st = (u8)inp(IDE_ALT_STATUS);
        if (!(st & IDE_ST_BSY)) return IDE_OK;
    }
    return IDE_ERR_TIMEOUT;
}

/* DRQ待ち: BSYが落ちてDRQが立つまで */
static int ide_wait_drq(void)
{
    int timeout = IDE_TIMEOUT_LOOP;
    u8 st;

    /* まずBSYが落ちるのを待つ */
    while (timeout-- > 0) {
        st = (u8)inp(IDE_ALT_STATUS);
        if (!(st & IDE_ST_BSY)) break;
    }
    if (timeout <= 0) return IDE_ERR_TIMEOUT;

    /* DRQチェック */
    st = (u8)inp(IDE_ALT_STATUS);
    if (st & IDE_ST_ERR) return IDE_ERR_IO;
    if (st & IDE_ST_DRQ) return IDE_OK;

    /* DRQが来るまで追加待ち */
    timeout = IDE_TIMEOUT_BSY;
    while (timeout-- > 0) {
        st = (u8)inp(IDE_ALT_STATUS);
        if (st & IDE_ST_DRQ) return IDE_OK;
        if (st & IDE_ST_ERR) return IDE_ERR_IO;
    }
    return IDE_ERR_TIMEOUT;
}

/* DRDY待ち */
static int ide_wait_ready(void)
{
    int timeout = IDE_TIMEOUT_BSY;
    while (timeout-- > 0) {
        u8 st = (u8)inp(IDE_ALT_STATUS);
        if (!(st & IDE_ST_BSY) && (st & IDE_ST_DRDY)) return IDE_OK;
    }
    return IDE_ERR_TIMEOUT;
}

/* ドライブ選択 (Master/Slave)
 * PC-98では IDE_DRV_HEAD の bit4 で Master(0)/Slave(1) を選択する。
 * またI/Oバンク (0 or 1) も同時に切り替える。 */
static void ide_select_drive(int drive)
{
    int bank = drive / 2;        /* 0 or 1 */
    int slave = drive % 2;       /* 0=Master, 1=Slave */
    ide_select_bank(bank);
    outp(IDE_DRV_HEAD, IDE_DRV_SEL_CHS | (slave ? 0x10 : 0x00));
    /* ドライブ選択後、400ns待ち（ダミーリード） */
    {
        int i;
        for (i = 0; i < IDE_SEL_SETTLE; i++) inp(IDE_ALT_STATUS);
    }
}


/* セクタ指定のレジスタを書く。DRV/HEAD は ide_addr_make が作った値
 * (LBA28 なら bit6 と LBA[27:24]、CHS ならヘッド) をそのまま出す。 */
static void ide_set_addr(const IdeAddr *a, u8 count)
{
    outp(IDE_SECT_CNT, (unsigned)count);
    outp(IDE_SECT_NUM, (unsigned)a->sect_num);
    outp(IDE_CYL_LO,   (unsigned)a->cyl_lo);
    outp(IDE_CYL_HI,   (unsigned)a->cyl_hi);
    outp(IDE_DRV_HEAD, (unsigned)a->drv_head);
}

/* CHS をそのまま IdeAddr にする (ide_read_sector_chs 系の入口) */
static void ide_chs_addr(int drive, u16 cyl, u8 head, u8 sect, IdeAddr *a)
{
    a->mode     = IDE_AMODE_CHS_DEF;
    a->sect_num = sect;
    a->cyl_lo   = (u8)(cyl & 0xFF);
    a->cyl_hi   = (u8)((cyl >> 8) & 0xFF);
    a->drv_head = (u8)(IDE_DRV_SEL_CHS | (head & 0x0F)
                       | ((drive % 2) ? 0x10 : 0x00));
}

/* 1 セクタ読み / 書き (指定は呼び手が作る) */
static int ide_pio_read(int drive, const IdeAddr *a, void *buf)
{
    int ret;

    if (!drive_present[drive & 3]) return IDE_ERR_NO_DRIVE;

    ide_select_drive(drive);
    ret = ide_wait_bsy();
    if (ret != IDE_OK) return ret;

    ide_set_addr(a, 1);

    outp(IDE_COMMAND, IDE_CMD_READ);

    ret = ide_wait_drq();
    if (ret != IDE_OK) return ret;

    {
        u16 *dst = (u16 *)buf;
        int w;
        for (w = 0; w < 256; w++) {
            dst[w] = (u16)inpw(IDE_DATA);
        }
    }

    { u8 st = (u8)inp(IDE_STATUS); (void)st; }
    ide_wait_bsy();

    return IDE_OK;
}

static int ide_pio_write(int drive, const IdeAddr *a, const void *buf)
{
    int ret;
    const u16 *data = (const u16 *)buf;
    int i;

    if (!drive_present[drive & 3]) return IDE_ERR_NO_DRIVE;

    ide_select_drive(drive);
    ret = ide_wait_bsy();
    if (ret != IDE_OK) return ret;

    ide_set_addr(a, 1);

    outp(IDE_COMMAND, IDE_CMD_WRITE);

    ret = ide_wait_drq();
    if (ret != IDE_OK) return ret;

    for (i = 0; i < 256; i++) {
        outpw(IDE_DATA, (unsigned)data[i]);
    }

    ret = ide_wait_bsy();
    if (ret != IDE_OK) return ret;

    {
        u8 st = (u8)inp(IDE_STATUS);
        if (st & IDE_ST_ERR) return IDE_ERR_IO;
    }

    return IDE_OK;
}

/* ============================================================ */
/*  公開API                                                      */
/* ============================================================ */

int ide_init(void)
{
    int found = 0;
    int bank, drv;

    /* PC-98スレーブPICでIRQ9をマスク。
       IRQ9 = スレーブPIC bit1。未処理IRQによるシステム破壊を防止。
       手書きの IMR RMW は割り込みに分断されると危険なので共通 API を使う。 */
    irq_disable(9);

    /* 全バンクの全ドライブをスキャン */
    for (bank = 0; bank < 2; bank++) {
        u8 probe;

        ide_select_bank(bank);

        /* フローティングバス検出: 存在しないバンクでは全ビット1 (0xFF)
         * が返る → 即スキップ
         * 注: 0x00 はスレーブドライブ選択前の正常値なのでスキップしない */
        probe = (u8)inp(IDE_ALT_STATUS);
        kprintf(0x07, "[ide] bank%d probe=0x%02x\n", bank, probe);
        if (probe == 0xFF) continue;

        /* 割り込み無効 (nIEN=1) — ポーリングモード */
        outp(IDE_DEV_CTRL, IDE_NIEN);

        for (drv = 0; drv < 2; drv++) {
            int idx = bank * 2 + drv; /* 0-3 */
            int ret;

            /* IDENTIFYコマンドで直接検出。ステータスだけでは
             * スレーブドライブを見逃す場合がある */
            ret = ide_identify(idx, &drive_info[idx]);
            kprintf(0x07, "[ide] drive%d identify=%d\n", idx, ret);
            if (ret == IDE_OK) {
                drive_present[idx] = 1;
                found++;
            }
        }
    }

    /* バンク0のマスタードライブを再選択（後続アクセスのため） */
    ide_select_drive(0);

    return found;
}

int ide_identify(int drive, IdeInfo *info)
{
    u16 buf[256];
    int i, ret;

    /* drive_geom は I/O の方式 (ide_addr.c) の元なので、**成功したときだけ**
     * 書き換える。KAPI の ide_identify (シェルの `ide N`) が一時的に失敗しても、
     * 起動時に得た値で読み書きが続けられる。起動時の初期値は 0 (valid = 0)。 */
    ide_select_drive(drive & 3);
    ret = ide_wait_bsy();
    if (ret != IDE_OK) return ret;

    /* IDENTIFYコマンド発行 */
    outp(IDE_COMMAND, IDE_CMD_IDENTIFY);

    /* DRQ待ち */
    ret = ide_wait_drq();
    if (ret != IDE_OK) return IDE_ERR_NO_DRIVE;

    /* 256ワード読み出し (個別inpwループ) */
    {
        int w;
        for (w = 0; w < 256; w++) {
            buf[w] = (u16)inpw(IDE_DATA);
        }
    }

    /* IRQクリア: Statusレジスタ読み出し */
    { u8 st = (u8)inp(IDE_STATUS); (void)st; }
    ide_wait_bsy();

    /* 幾何の生の語。I/O の指定の方式 (LBA28 / 現在の CHS / 既定の CHS) は
     * drivers/ide_addr.c がこの値から決める (票 TASK_HDD_INSTALL 段 1)。 */
    {
        IdeGeom *g = &drive_geom[drive & 3];
        g->def_cyl   = buf[1];
        g->def_heads = buf[3];
        g->def_spt   = buf[6];
        g->w49       = buf[49];
        g->w53       = buf[53];
        g->cur_cyl   = buf[54];
        g->cur_heads = buf[55];
        g->cur_spt   = buf[56];
        g->total     = (u32)buf[60] | ((u32)buf[61] << 16);
        g->valid     = 1;
    }

    /* 情報抽出 */
    info->cylinders = buf[1];
    info->heads = buf[3];
    info->sectors = buf[6];

    /* LBAサポート確認 (word 49 bit 9) */
    info->lba_supported = (buf[49] & 0x0200) ? 1 : 0;

    /* LBA総セクタ数 (word 60-61) */
    info->total_sectors = (u32)buf[60] | ((u32)buf[61] << 16);

    /* SASI標準ジオメトリ判定 (NP21/W sasihdd[]テーブルと一致)
     * SPT=33, Heads∈{4,6,8} → SASI 256B/セクタ形式 (HDI)
     * NP21/WはSASI形式HDIをIDE経由で提供するが、
     * 内部LBAは256Bセクタ単位のまま */
    if (info->sectors == 33 &&
        (info->heads == 4 || info->heads == 6 || info->heads == 8)) {
        info->phys_sector_size = 256;
    } else {
        info->phys_sector_size = 512;
    }

    /* サイズ(MB) = 総セクタ数 / (1MB あたりのセクタ数)。
     * 総セクタ数 × セクタ長を先に掛けると 4GB 超 (実機の 8GB = 16,514,063
     * セクタ) で 32bit が溢れる。1MB はどちらのセクタ長でも割り切れる。 */
    info->size_mb = info->total_sectors
                    / ((1024UL * 1024UL) / (u32)info->phys_sector_size);

    /* モデル名 (word 27-46): ATA文字列はバイトスワップ */
    for (i = 0; i < 20; i++) {
        info->model[i * 2]     = (char)(buf[27 + i] >> 8);
        info->model[i * 2 + 1] = (char)(buf[27 + i] & 0xFF);
    }
    info->model[40] = '\0';
    /* 末尾空白除去 */
    for (i = 39; i >= 0 && info->model[i] == ' '; i--) {
        info->model[i] = '\0';
    }

    /* シリアル番号 (word 10-19) */
    for (i = 0; i < 10; i++) {
        info->serial[i * 2]     = (char)(buf[10 + i] >> 8);
        info->serial[i * 2 + 1] = (char)(buf[10 + i] & 0xFF);
    }
    info->serial[20] = '\0';
    for (i = 19; i >= 0 && info->serial[i] == ' '; i--) {
        info->serial[i] = '\0';
    }

    /* ファームウェア (word 23-26) */
    for (i = 0; i < 4; i++) {
        info->firmware[i * 2]     = (char)(buf[23 + i] >> 8);
        info->firmware[i * 2 + 1] = (char)(buf[23 + i] & 0xFF);
    }
    info->firmware[8] = '\0';
    for (i = 7; i >= 0 && info->firmware[i] == ' '; i--) {
        info->firmware[i] = '\0';
    }

    return IDE_OK;
}


/* ============================================================ */
/*  CHS ネイティブ API (Phase 2)                                 */
/*  LBA→CHS 変換を省略し、CHS を直接レジスタに設定する。       */
/* ============================================================ */

int ide_read_sector_chs(int drive, u16 cyl, u8 head, u8 sect,
                        void *buf)
{
    IdeAddr a;
    ide_chs_addr(drive, cyl, head, sect, &a);
    return ide_pio_read(drive, &a, buf);
}

int ide_write_sector_chs(int drive, u16 cyl, u8 head, u8 sect,
                         const void *buf)
{
    IdeAddr a;
    ide_chs_addr(drive, cyl, head, sect, &a);
    return ide_pio_write(drive, &a, buf);
}

int ide_drive_present(int drive)
{
    return drive_present[drive & 3];
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
    if (!drive_present[drive & 3]) return IDE_ERR_NO_DRIVE;
    if (info) {
        int i;
        u8 *dst = (u8 *)info;
        u8 *src = (u8 *)&drive_info[drive & 3];
        for (i = 0; i < sizeof(IdeInfo); i++) {
            dst[i] = src[i];
        }
    }
    return IDE_OK;
}

/* ======================================================================== */
/*  LBA の読み書き (KAPI の ide_read_sector 系と dev.c の hd0-3)             */
/*                                                                          */
/*  指定の方式と範囲は drivers/ide_addr.c が決める。**範囲外は 1 セクタも    */
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

int ide_read_sector(int drive, u32 lba, void *buf)
{
    IdeAddr a;
    int ret;

    if (!drive_present[drive & 3]) return IDE_ERR_NO_DRIVE;
    ret = ide_addr_make(&drive_geom[drive & 3], drive & 3, lba, &a);
    if (ret != IDE_OK) return ret;
    return ide_pio_read(drive & 3, &a, buf);
}

int ide_write_sector(int drive, u32 lba, const void *buf)
{
    IdeAddr a;
    int ret;

    if (!drive_present[drive & 3]) return IDE_ERR_NO_DRIVE;
    ret = ide_addr_make(&drive_geom[drive & 3], drive & 3, lba, &a);
    if (ret != IDE_OK) return ret;
    return ide_pio_write(drive & 3, &a, buf);
}

int ide_read_sectors(int drive, u32 lba, u32 count, void *buf)
{
    u32 i;
    u8 *dst = (u8 *)buf;

    if (!drive_present[drive & 3]) return IDE_ERR_NO_DRIVE;
    if (!ide_addr_range_ok(&drive_geom[drive & 3], lba, count))
        return IDE_ERR_RANGE;
    for (i = 0; i < count; i++) {
        int ret = ide_read_sector(drive, lba + i, dst + i * 512);
        if (ret != IDE_OK) return ret;
    }
    return IDE_OK;
}

int ide_write_sectors(int drive, u32 lba, u32 count, const void *buf)
{
    u32 i;
    const u8 *src = (const u8 *)buf;

    if (!drive_present[drive & 3]) return IDE_ERR_NO_DRIVE;
    if (!ide_addr_range_ok(&drive_geom[drive & 3], lba, count))
        return IDE_ERR_RANGE;
    for (i = 0; i < count; i++) {
        int ret = ide_write_sector(drive, lba + i, src + i * 512);
        if (ret != IDE_OK) return ret;
    }
    return IDE_OK;
}
