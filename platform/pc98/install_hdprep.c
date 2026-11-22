#include "install_geometry.h"
#include "fs/pc98pt.h"

#define HDPREP_SECT          512
#define HDPREP_DUMP_BYTES    64      /* LBA 0 / 1 の先頭を何バイト見せるか */
static u8 hp_lba0[HDPREP_SECT];
static u8 hp_lba1[HDPREP_SECT];
static u8 hp_sect[HDPREP_SECT];
static u8 hp_back[HDPREP_SECT];

static int disk_memeq(const u8 *a, const u8 *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

void platform_hdprep_refuse(KernelAPI *api, int code)
{
    api->kprintf(ATTR_RED, "hdprep: refused: %s (code %d). Nothing was written.\n",
                   hdprep_reason(code), code);
}

void platform_hdprep_incomplete(KernelAPI *api, const char *what, int rc)
{
    api->kprintf(ATTR_RED, "hdprep: INCOMPLETE: %s (rc=%d)\n", what, rc);
    api->kprintf(ATTR_RED, "%s",
                   "  The area is not usable. Run hdprep again only after checking "
                   "LBA 0/1 (dd hd0 lba=1).\n");
}

static void hp_dump(KernelAPI *api, const char *label, const u8 *b)
{
    int i, nz = 0;

    for (i = 0; i < HDPREP_SECT; i++) if (b[i]) nz++;
    api->kprintf(ATTR_WHITE, "  %s: nonzero=%d sig=%02x%02x\n",
                   label, nz, (u32)b[510], (u32)b[511]);
    for (i = 0; i < HDPREP_DUMP_BYTES; i++) {
        if ((i % 16) == 0) api->kprintf(ATTR_WHITE, "    %03x:", (u32)i);
        api->kprintf(ATTR_WHITE, " %02x", (u32)b[i]);
        if ((i % 16) == 15) api->kprintf(ATTR_WHITE, "%s", "\n");
    }
}

static const char *hp_amode_name(int m)
{
    switch (m) {
    case HDPREP_AMODE_LBA28:   return "lba28";
    case HDPREP_AMODE_CHS_CUR: return "chs-current";
    case HDPREP_AMODE_CHS_DEF: return "chs-default";
    default:                   return "none";
    }
}

int platform_hdprep_check(KernelAPI *api, HdprepGeom *geometry)
{
    HddGeom hg;
    int rc;

    /* ---- 1. 幾何 ---- */
    if (api->hdd_geom_info(HDPREP_DRIVE, &hg) != 0) {
        platform_hdprep_refuse(api, HDPREP_E_ARG);
        return -1;
    }
    platform_install_geometry(&hg, geometry);

    api->kprintf(ATTR_CYAN, "%s", "hdprep: target hd0 (IDE drive 0 = BIOS DA 80h)\n");
    api->kprintf(ATTR_WHITE,
                   "  BIOS (AH=84h): queried=%d valid=%d C/H/S=%u/%u/%u len=%u\n",
                   (int)hg.bios_queried, (int)hg.bios_valid, (u32)hg.bios_cyl,
                   (u32)hg.bios_heads, (u32)hg.bios_spt, (u32)hg.bios_seclen);
    api->kprintf(ATTR_WHITE,
                   "  ATA: present=%d def=%u/%u/%u cur=%u/%u/%u(%s) lba=%d total=%u io=%s\n",
                   (int)hg.ata_present, (u32)hg.ata_def_cyl, (u32)hg.ata_def_heads,
                   (u32)hg.ata_def_spt, (u32)hg.ata_cur_cyl, (u32)hg.ata_cur_heads,
                   (u32)hg.ata_cur_spt, (hg.ata_w53 & 1) ? "valid" : "invalid",
                   (hg.ata_w49 & 0x0200) ? 1 : 0, hg.ata_total,
                   hp_amode_name(hg.addr_mode));

    rc = hdprep_check_geom(geometry);
    if (rc != HDPREP_OK) { platform_hdprep_refuse(api, rc); return -1; }

    /* ---- 1. ディスクが空か (LBA 0 / 1 の生の中身も見せる) ---- */
    rc = api->ide_read_sector(HDPREP_DRIVE, 0, hp_lba0);
    if (rc != 0) {
        api->kprintf(ATTR_RED, "hdprep: refused: cannot read LBA 0 (rc=%d). Nothing was written.\n", rc);
        return -1;
    }
    rc = api->ide_read_sector(HDPREP_DRIVE, PC98PT_LBA, hp_lba1);
    if (rc != 0) {
        api->kprintf(ATTR_RED, "hdprep: refused: cannot read LBA 1 (rc=%d). Nothing was written.\n", rc);
        return -1;
    }
    hp_dump(api, "LBA 0", hp_lba0);
    hp_dump(api, "LBA 1", hp_lba1);
    rc = hdprep_check_disk(hp_lba0, hp_lba1);
    if (rc != HDPREP_OK) { platform_hdprep_refuse(api, rc); return -1; }

    return 0;
}

void platform_hdprep_describe(KernelAPI *api, const HdprepPlan *plan)
{
    api->kprintf(ATTR_WHITE,
                   "  area: LBA %u..%u (%u sectors = %u KiB), cyl %u..%u of %u/%u\n",
                   plan->start, plan->start + plan->len - 1UL, plan->len, plan->len / 2UL,
                   plan->start / plan->cyl_sectors, plan->end_cyl, plan->heads, plan->spt);
    api->kprintf(ATTR_WHITE,
                   "  writes: probe LBA %u, ext2 in the area, partition table LBA 1\n",
                   plan->probe_lba);
}

int platform_hdprep_probe(KernelAPI *api, const HdprepPlan *plan)
{
    int i, rc;

    /* ---- 5. 探り: 予定域の最終セクタへ書いて読み戻す ---- */
    for (i = 0; i < HDPREP_SECT; i++) hp_sect[i] = (u8)(0xA5 ^ i);
    rc = api->ide_write_sector(HDPREP_DRIVE, plan->probe_lba, hp_sect);
    if (rc == 0) rc = api->ide_read_sector(HDPREP_DRIVE, plan->probe_lba, hp_back);
    if (rc != 0 || !disk_memeq(hp_sect, hp_back, HDPREP_SECT)) {
        api->kprintf(ATTR_RED,
                       "hdprep: probe write/readback at LBA %u failed (rc=%d). "
                       "Only that sector was written; partition table untouched.\n",
                       plan->probe_lba, rc);
        return -1;
    }

    return 0;
}

int platform_hdprep_partition(KernelAPI *api, const HdprepPlan *plan)
{
    int rc;

    /* ---- 7. 区画表 → 読み戻し比較 ---- */
    rc = inst_build_pt(hp_sect, plan);
    if (rc != PC98PT_OK) {
        platform_hdprep_incomplete(api, "cannot build partition entry (partition table not written)", rc);
        return -1;
    }
    rc = api->ide_write_sector(HDPREP_DRIVE, PC98PT_LBA, hp_sect);
    if (rc == 0) rc = api->ide_read_sector(HDPREP_DRIVE, PC98PT_LBA, hp_back);
    if (rc != 0 || !disk_memeq(hp_sect, hp_back, HDPREP_SECT)) {
        platform_hdprep_incomplete(api, "partition table write/readback failed", rc);
        return -1;
    }

    return 0;
}
