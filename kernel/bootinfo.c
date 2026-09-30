/* ======================================================================== */
/*  BOOTINFO.C — ブート情報域 (0x7E00) を写して保存する                      */
/*                                                                          */
/*  kernel_main の**最初**で bootinfo_capture() を 1 回だけ呼ぶ。0x7E00 は   */
/*  フォントキャッシュ (MEM_FONT_CACHE_BASE = 0x1000〜) の内側にあり、        */
/*  フォント初期化・その他の低位の再利用で上書きされる。以後は写しだけを使う。 */
/*  検証は kernel/bootinfo_check.c (純粋関数、ホスト試験の対象)。             */
/*  票: docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 0                           */
/* ======================================================================== */

#include "bootinfo.h"
#include "memmap.h"
#include "ide.h"
#include "kprintf.h"
#include "ide_addr.h"
#include "kstring.h"
#include "os32_kapi_shared.h"   /* HddGeom (KAPI v64) / BootImageInfo (v65) */
#include "build_id.h"           /* os32_build_commit (生成物 build/out/build_id.c) */

/* 構造体の並びが NASM 側 (boot/bootinfo.inc) と同じオフセットか。
 * ホストでは u32 が 64bit なのでここ (i386 のカーネルビルド) でだけ見る。 */
#define BI_OFFSETOF(t, m) __builtin_offsetof(t, m)
STATIC_ASSERT(sizeof(struct bootinfo_wire) == BOOTINFO_WIRE_SIZE, bi_wire_size);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, magic)   == BI_OFF_MAGIC,   bi_off_magic);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, version) == BI_OFF_VERSION, bi_off_version);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, source)  == BI_OFF_SOURCE,  bi_off_source);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, ndrives) == BI_OFF_NDRIVES, bi_off_ndrives);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, drive)   == BI_OFF_DRIVE0,  bi_off_drive0);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, sum)     == BI_OFF_SUM,     bi_off_sum);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, check)   == BI_OFF_CHECK,   bi_off_check);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, img_crc)   == BI_OFF_IMG_CRC,   bi_off_img_crc);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, img_size)  == BI_OFF_IMG_SIZE,  bi_off_img_size);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire, img_check) == BI_OFF_IMG_CHECK, bi_off_img_check);
STATIC_ASSERT(sizeof(struct bootinfo_wire_drive) == BI_DRIVE_SIZE, bi_drive_size);
/* boot_image_info の出力は KAPI の契約 (40 バイト、os32_kapi_shared.h) */
STATIC_ASSERT(sizeof(BootImageInfo) == 40, boot_image_info_size);
STATIC_ASSERT(BI_OFFSETOF(BootImageInfo, commit) == 12, boot_image_info_commit);
STATIC_ASSERT(BOOT_IMAGE_COMMIT_MAX == BUILD_COMMIT_MAX, boot_image_commit_max);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire_drive, bx) == BI_DRV_BX, bi_drv_bx);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire_drive, cx) == BI_DRV_CX, bi_drv_cx);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire_drive, dh) == BI_DRV_DH, bi_drv_dh);
STATIC_ASSERT(BI_OFFSETOF(struct bootinfo_wire_drive, queried) == BI_DRV_QUERIED,
              bi_drv_queried);
/* hdd_geom_info の出力は KAPI の契約 (32 バイト、os32_kapi_shared.h) */
STATIC_ASSERT(sizeof(HddGeom) == 32, hdd_geom_size);
STATIC_ASSERT(BI_OFFSETOF(HddGeom, ata_total) == 24, hdd_geom_total);
STATIC_ASSERT(HDD_AMODE_LBA28 == IDE_AMODE_LBA28 &&
              HDD_AMODE_CHS_CUR == IDE_AMODE_CHS_CUR &&
              HDD_AMODE_CHS_DEF == IDE_AMODE_CHS_DEF, hdd_amode_same);
/* 域はブート情報域の予約 (256B) に収まる。 */
STATIC_ASSERT(BOOTINFO_WIRE_SIZE <= MEM_BOOTINFO_SIZE, bi_fits);

static struct bootinfo s_bootinfo = { BOOTINFO_ERR_NONE };
static u8 s_raw[BOOTINFO_WIRE_SIZE];    /* 生の写し (kselftest・調査用) */
static int s_captured;

void bootinfo_capture(void)
{
    volatile u8 *low = (volatile u8 *)MEM_BOOTINFO_BASE;
    unsigned int i;

    if (s_captured) return;
    for (i = 0; i < (unsigned int)BOOTINFO_WIRE_SIZE; i++) {
        s_raw[i] = low[i];
    }
    (void)bootinfo_parse(s_raw, (unsigned int)BOOTINFO_WIRE_SIZE, &s_bootinfo);

    /* 写したら低位の magic を消す。次の起動で古いローダ (情報域を書かない)
     * から来ても、この起動の値を「今回の値」と読まないように (N2)。 */
    for (i = 0; i < 4; i++) {
        low[BI_OFF_MAGIC + i] = 0;
    }
    s_captured = 1;
}

const struct bootinfo *bootinfo_get(void)
{
    return &s_bootinfo;
}

int bootinfo_hdd_geom(int da, u16 *cyl, u8 *heads, u8 *spt, u16 *seclen)
{
    int i;

    if (s_bootinfo.status != BOOTINFO_OK) return s_bootinfo.status;
    for (i = 0; i < (int)BOOTINFO_NDRIVES; i++) {
        const struct bootinfo_drive *d = &s_bootinfo.drive[i];
        if (!d->queried || (int)d->da != da) continue;
        if (!d->valid) return -1;
        if (cyl)    *cyl = d->cyl;
        if (heads)  *heads = d->heads;
        if (spt)    *spt = d->spt;
        if (seclen) *seclen = d->seclen;
        return 0;
    }
    return -1;
}

void bootinfo_report(void)
{
    char line[BOOTINFO_LINE_MAX];
    int i;

    if (s_bootinfo.status != BOOTINFO_OK) {
        bootinfo_format_bios(&s_bootinfo, 0, line, (int)sizeof(line));
        kprintf(0x07, "%s\n", line);
    } else {
        for (i = 0; i < (int)BOOTINFO_NDRIVES; i++) {
            if (!s_bootinfo.drive[i].queried) continue;
            bootinfo_format_bios(&s_bootinfo, i, line, (int)sizeof(line));
            kprintf(0x07, "%s\n", line);
        }
    }

    /* IDENTIFY の値。hd0 / hd1 (バンク0 のマスタ・スレーブ) だけ。 */
    for (i = 0; i < 2; i++) {
        IdeGeom g;
        struct bootinfo_ata a;

        if (ide_get_geom(i, &g) != IDE_OK) continue;
        a.def_cyl   = g.def_cyl;
        a.def_heads = g.def_heads;
        a.def_spt   = g.def_spt;
        a.cur_cyl   = g.cur_cyl;
        a.cur_heads = g.cur_heads;
        a.cur_spt   = g.cur_spt;
        a.cur_valid = (u8)((g.w53 & 0x0001) ? 1 : 0);
        a.lba       = (u8)((g.w49 & 0x0200) ? 1 : 0);
        a.total     = g.total;
        bootinfo_format_ata(&a, i, line, (int)sizeof(line));
        kprintf(0x07, "%s\n", line);
    }
}

/* ======================================================================== */
/*  区画表の幾何 (票 TASK_HDD_INSTALL 段 1)                                  */
/* ======================================================================== */

/* IDE ドライブ番号 → BIOS の DA。バンク 0 のマスタ = 80h、スレーブ = 81h。
 * バンク 1 (ドライブ 2/3) は BIOS の HDD として数えない (0 = 無し)。 */
static int bootinfo_da_of(int ide_drive)
{
    if (ide_drive == 0) return BOOTINFO_DA_HDD0;
    if (ide_drive == 1) return BOOTINFO_DA_HDD0 + 1;
    return 0;
}

int bootinfo_part_geom(int ide_drive, u16 *heads, u16 *spt)
{
    u8 h, s;
    int da = bootinfo_da_of(ide_drive);
    IdeInfo info;

    if (da != 0 && bootinfo_hdd_geom(da, (u16 *)0, &h, &s, (u16 *)0) == 0) {
        if (heads) *heads = h;
        if (spt)   *spt = s;
        return BOOTINFO_GEOM_BIOS;
    }
    /* BIOS 幾何が無い (FD 起動で問い合わせなかった / 規則に外れる) ときは
     * IDENTIFY の既定。NP21/W の NHD はどちらも同じ値なので従来どおり。 */
    if (ide_get_info(ide_drive, &info) != IDE_OK) return -1;
    if (info.heads == 0 || info.sectors == 0) return -1;
    if (heads) *heads = info.heads;
    if (spt)   *spt = info.sectors;
    return BOOTINFO_GEOM_IDENTIFY;
}

int hdd_geom_info(int drive, void *out_v)
{
    HddGeom *out = (HddGeom *)out_v;
    IdeGeom g;
    int da, i;

    if (!out) return OS32_ERR_INVAL;
    if (drive < 0 || drive > 3) return OS32_ERR_INVAL;
    kmemset(out, 0, sizeof(*out));

    da = bootinfo_da_of(drive);
    out->bios_da = (u8)da;
    if (da != 0 && s_bootinfo.status == BOOTINFO_OK) {
        for (i = 0; i < (int)BOOTINFO_NDRIVES; i++) {
            const struct bootinfo_drive *d = &s_bootinfo.drive[i];
            if (!d->queried || (int)d->da != da) continue;
            out->bios_queried = 1;
            out->bios_valid   = d->valid;
            out->bios_heads   = d->heads;
            out->bios_spt     = d->spt;
            out->bios_cyl     = d->cyl;
            out->bios_seclen  = d->seclen;
            break;
        }
    }

    if (ide_get_geom(drive, &g) == IDE_OK && ide_drive_present(drive)) {
        out->ata_present   = 1;
        out->ata_def_cyl   = g.def_cyl;
        out->ata_def_heads = g.def_heads;
        out->ata_def_spt   = g.def_spt;
        out->ata_cur_cyl   = g.cur_cyl;
        out->ata_cur_heads = g.cur_heads;
        out->ata_cur_spt   = g.cur_spt;
        out->ata_w49       = g.w49;
        out->ata_w53       = g.w53;
        out->ata_total     = g.total;
        out->addr_mode     = (u8)ide_addr_mode_of(drive);
    }
    return 0;
}

/* ======================================================================== */
/*  起動したイメージ (票 TASK_SERIAL_HOSTFS 部品 A-4)                        */
/* ======================================================================== */

/* kapi/kapi_sys.c (__DATE__ __TIME__ はそこだけが毎回組み直される) */
extern void kapi_sys_get_build_info(char *buf, int size);

int boot_image_info(void *out_v)
{
    BootImageInfo *out = (BootImageInfo *)out_v;

    if (!out) return OS32_ERR_INVAL;
    kmemset(out, 0, sizeof(*out));
    if (s_bootinfo.status == BOOTINFO_OK) {
        out->source = s_bootinfo.source;
        if (s_bootinfo.img_valid) {
            out->crc_valid  = 1;
            out->image_crc  = s_bootinfo.img_crc;
            out->image_size = s_bootinfo.img_size;
        }
    }
    kstrncpy(out->commit, os32_build_commit, (u32)sizeof(out->commit));
    out->commit[sizeof(out->commit) - 1] = '\0';
    return 0;
}

void bootinfo_report_image(void)
{
    char build[32];

    kapi_sys_get_build_info(build, (int)sizeof(build));
    kprintf(0x07, "[boot] Build: %s  Commit: %s\n", build, os32_build_commit);
    if (s_bootinfo.status == BOOTINFO_OK && s_bootinfo.img_valid) {
        kprintf(0x07, "[boot] Image CRC: %08x (%u bytes, src=%s)\n",
                s_bootinfo.img_crc, s_bootinfo.img_size,
                s_bootinfo.source == BOOTINFO_SRC_HDD ? "hdd" :
                s_bootinfo.source == BOOTINFO_SRC_FD ? "fd" : "?");
    } else {
        kprintf(0x07, "[boot] Image CRC: none (loader did not record)\n");
    }
}
