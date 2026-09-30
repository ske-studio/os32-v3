/* ======================================================================== */
/*  HDPREP_PLAN.C — `hdprep` の判定と計画 (純粋関数)                         */
/*                                                                          */
/*  規則は hdprep_plan.h。区画表の中身は drivers/pc98pt.c (共有部) で読む。 */
/*  32 ビットの桁あふれに頼る計算はしない (ホスト試験は 64 ビットで回る)。   */
/* ======================================================================== */

#include "hdprep_plan.h"
#include "drivers/pc98pt.h"   /* PROGRAM_FLAGS の -I. で引く */

int hdprep_check_geom(const HdprepGeom *g)
{
    if (!g) return HDPREP_E_ARG;
    if (!g->ata_present) return HDPREP_E_NO_ATA;
    /* 総数が分からなければ書く範囲の上限が決まらない。ext2_format_at も断るので、
     * 探りを書く前 (計画の段階) で断る (Codex C3) */
    if (g->ata_total == 0) return HDPREP_E_NO_TOTAL;
    if (!g->bios_queried || !g->bios_valid) return HDPREP_E_NO_BIOS;
    if (g->bios_seclen != HDPREP_SECLEN) return HDPREP_E_SECLEN;
    /* 規則の二重化: bios_valid が立っていても 0 は割り算を壊す */
    if (g->bios_heads == 0 || g->bios_spt == 0 || g->bios_cyl == 0)
        return HDPREP_E_NO_BIOS;
    if (g->bios_heads > 255 || g->bios_spt > 255) return HDPREP_E_NO_BIOS;
    /* I/O は LBA28 か現在の CHS でだけ書く。既定の CHS はドライブの現在の
     * 変換と一致する保証が無い (F13) */
    if (g->addr_mode != HDPREP_AMODE_LBA28 &&
        g->addr_mode != HDPREP_AMODE_CHS_CUR)
        return HDPREP_E_ADDR;
    return HDPREP_OK;
}

int hdprep_check_disk(const unsigned char *lba0, const unsigned char *lba1)
{
    if (!lba0 || !lba1) return HDPREP_E_ARG;
    /* 区画項目が 1 つでもあれば断る (空のディスク専用、N4)。--force も無い */
    if (pc98pt_count_used(lba1) != 0) return HDPREP_E_PT_USED;
    /* LBA 0 の 55AA = IPL か IBM 形式の MBR がある = 空ではない */
    if (lba0[510] == 0x55 && lba0[511] == 0xAA) return HDPREP_E_MBR_SIG;
    return HDPREP_OK;
}

int hdprep_check_mounts(int mount_count, int root_is_hd0)
{
    if (root_is_hd0) return HDPREP_E_ROOT;
    if (mount_count < 0) return HDPREP_E_ARG;
    if (mount_count > 0) return HDPREP_NEED_UMOUNT;
    return HDPREP_OK;
}

int hdprep_parse_mb(const char *s, unsigned long *out_mb)
{
    unsigned long v = 0;
    int n = 0;

    if (!out_mb) return HDPREP_E_ARG;
    if (!s || !*s) {
        *out_mb = HDPREP_DEFAULT_MB;
        return HDPREP_OK;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10UL + (unsigned long)(*s - '0');
        if (v > HDPREP_MAX_MB) return HDPREP_E_SIZE;   /* 桁が増える前に止める */
        s++;
        n++;
    }
    if (*s != '\0' || n == 0) return HDPREP_E_SIZE;
    if (v < HDPREP_MIN_MB || v > HDPREP_MAX_MB) return HDPREP_E_SIZE;
    *out_mb = v;
    return HDPREP_OK;
}

int hdprep_plan(const HdprepGeom *g, unsigned long mb, HdprepPlan *out)
{
    unsigned long cyl, start, limit, room, want, len, ncyl;
    int rc;

    if (!g || !out) return HDPREP_E_ARG;
    rc = hdprep_check_geom(g);
    if (rc != HDPREP_OK) return rc;
    if (mb < HDPREP_MIN_MB || mb > HDPREP_MAX_MB) return HDPREP_E_SIZE;

    out->heads = g->bios_heads;
    out->spt = g->bios_spt;
    cyl = g->bios_heads * g->bios_spt;                 /* ≤ 255 × 255 */
    out->cyl_sectors = cyl;

    /* 開始 = 1632 以上の最初のシリンダ境界 */
    start = ((HDPREP_BOOT_RESERVE_LBA + cyl - 1UL) / cyl) * cyl;

    /* 上限 = BIOS が指せる範囲 (CX シリンダ) と IDENTIFY の総数の小さい方。
     * CX がシリンダ数でも最大番号でも、ここは小さめ (安全側) に出る。
     * bios_cyl ≤ 65535 なので積は 2^32 未満。 */
    limit = g->bios_cyl * cyl;
    if (g->ata_total < limit) limit = g->ata_total;
    /* ATA の指定の方式で指せる上限 (drivers/ide_addr.c と同じ規則。Codex C4) */
    if (g->addr_mode == HDPREP_AMODE_LBA28) {
        if (limit > HDPREP_LBA28_LIMIT) limit = HDPREP_LBA28_LIMIT;
    } else {
        unsigned long chs = g->ata_cur_cyl * g->ata_cur_heads * g->ata_cur_spt;
        if (chs < limit) limit = chs;
    }
    out->disk_limit = limit;
    if (limit <= start) return HDPREP_E_TOO_SMALL;
    room = limit - start;

    want = mb * HDPREP_SECTORS_PER_MB;                 /* ≤ 524288 */
    if (want > room) want = room;
    ncyl = want / cyl;                                 /* シリンダ単位へ切り下げ */
    len = ncyl * cyl;
    if (len < HDPREP_MIN_MB * HDPREP_SECTORS_PER_MB) return HDPREP_E_TOO_SMALL;

    out->end_cyl = start / cyl + ncyl - 1UL;
    if (out->end_cyl > PC98PT_MAX_CYL) return HDPREP_E_CYL;

    out->start = start;
    out->len = len;
    out->probe_lba = start + len - 1UL;
    return HDPREP_OK;
}

const char *hdprep_reason(int code)
{
    switch (code) {
    case HDPREP_OK:              return "ok";
    case HDPREP_NEED_UMOUNT:     return "hd0 is mounted (will umount first)";
    case HDPREP_E_NO_ATA:        return "hd0 does not answer IDENTIFY";
    case HDPREP_E_NO_BIOS:       return "no BIOS geometry (INT 1Bh AH=84h) for DA 80h";
    case HDPREP_E_SECLEN:        return "BIOS sector length (BX) is not 512";
    case HDPREP_E_ADDR:          return "drive has neither LBA nor a current CHS translation";
    case HDPREP_E_PT_USED:       return "LBA 1 has a partition entry (disk is not empty)";
    case HDPREP_E_MBR_SIG:       return "LBA 0 ends with 55AA (disk is not empty)";
    case HDPREP_E_ROOT:          return "hd0 is the root file system";
    case HDPREP_E_SIZE:          return "size must be 8..256 (MiB)";
    case HDPREP_E_TOO_SMALL:     return "disk is too small for the area";
    case HDPREP_E_CYL:           return "end cylinder does not fit in 16 bits";
    case HDPREP_E_STILL_MOUNTED: return "hd0 is still mounted after umount";
    case HDPREP_E_NO_TOTAL:      return "IDENTIFY reports no total sector count";
    default:                     return "bad argument";
    }
}
