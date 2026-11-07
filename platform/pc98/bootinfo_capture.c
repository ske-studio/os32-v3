#include "bootinfo.h"
#include "bootinfo_native.h"
#include "../../init/boot_disk.h"
#include "memmap.h"
#include "kprintf.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "build_id.h"
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
/* 域はブート情報域の予約 (256B) に収まる。 */
STATIC_ASSERT(BOOTINFO_WIRE_SIZE <= MEM_BOOTINFO_SIZE, bi_fits);

static struct bootinfo s_bootinfo = { .status = BOOTINFO_ERR_NONE };
static u8 s_raw[BOOTINFO_WIRE_SIZE];    /* 生の写し (kselftest・調査用) */
static int s_captured;

void bootinfo_capture(void)
{
    volatile u8 *low = (volatile u8 *)P2V_IO(MEM_BOOTINFO_BASE);
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
    struct boot_common info = {
        s_bootinfo.status == BOOTINFO_OK && s_bootinfo.img_valid,
        s_bootinfo.img_crc, s_bootinfo.img_size
    };
    boot_common_set(&info, s_bootinfo.status == BOOTINFO_OK ? s_bootinfo.source : 0);
    s_captured = 1;
}

const struct bootinfo *bootinfo_get(void)
{
    return &s_bootinfo;
}

