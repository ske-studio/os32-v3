/* ======================================================================== */
/*  IDE_ADDR.C — ATA のセクタ指定の選択と範囲検査 (純粋関数)                 */
/*                                                                          */
/*  規則は ide_addr.h。drivers/ide.c がレジスタに書く値は全部ここで作る。     */
/*  試験: tools/tests/test_hdd_stage1.py (記録 tools/tests/hdd_stage1_tdd.md)  */
/* ======================================================================== */

#include "ide_addr.h"

int ide_addr_mode(const IdeGeom *g, u16 *out_heads, u16 *out_spt,
                  u32 *out_limit)
{
    u32 heads, spt, cyl, limit;
    int mode;

    if (!g || !g->valid) return IDE_AMODE_NONE;

    if (g->w49 & 0x0200) {
        /* LBA28。総数 (word 60-61) が 0 の申告は信じず 2^28 を上限にする */
        mode  = IDE_AMODE_LBA28;
        heads = 0;
        spt   = 0;
        limit = g->total;
        if (limit == 0 || limit > IDE_LBA28_LIMIT) limit = IDE_LBA28_LIMIT;
    } else if ((g->w53 & 0x0001) &&
               g->cur_heads >= 1 && g->cur_heads <= IDE_CHS_MAX_HEADS &&
               g->cur_spt >= 1 && g->cur_spt <= 255 && g->cur_cyl >= 1) {
        mode  = IDE_AMODE_CHS_CUR;
        heads = g->cur_heads;
        spt   = g->cur_spt;
        cyl   = g->cur_cyl;
        limit = cyl * heads * spt;         /* 65535 × 16 × 255 < 2^28 */
    } else {
        mode  = IDE_AMODE_CHS_DEF;
        heads = g->def_heads ? g->def_heads : IDE_FALLBACK_HEADS;
        spt   = g->def_spt ? g->def_spt : IDE_FALLBACK_SPT;
        if (heads > IDE_CHS_MAX_HEADS || spt > 255) return IDE_AMODE_NONE;
        cyl   = g->def_cyl ? g->def_cyl : (IDE_CHS_MAX_CYL + 1UL);
        limit = cyl * heads * spt;
    }

    if (out_heads) *out_heads = (u16)heads;
    if (out_spt)   *out_spt   = (u16)spt;
    if (out_limit) *out_limit = limit;
    return mode;
}

int ide_addr_range_ok(const IdeGeom *g, u32 lba, u32 count)
{
    u32 limit;

    if (ide_addr_mode(g, (u16 *)0, (u16 *)0, &limit) == IDE_AMODE_NONE)
        return 0;
    if (count == 0) return 1;
    /* lba + count <= limit を、足し算をせずに (桁あふれしない形で) */
    if (lba >= limit) return 0;
    if (count > limit - lba) return 0;
    return 1;
}

int ide_addr_make(const IdeGeom *g, int drive, u32 lba, IdeAddr *out)
{
    u16 heads, spt;
    u32 limit, track, cyl, head, sect;
    u8 slave;
    int mode;

    mode = ide_addr_mode(g, &heads, &spt, &limit);
    if (mode == IDE_AMODE_NONE) return IDE_ERR_NO_DRIVE;
    if (!out) return IDE_ERR_RANGE;
    if (lba >= limit) return IDE_ERR_RANGE;

    slave = (u8)((drive & 1) ? IDE_DRVHEAD_SLAVE : 0);
    out->mode = (u8)mode;

    if (mode == IDE_AMODE_LBA28) {
        out->sect_num = (u8)(lba & 0xFFUL);
        out->cyl_lo   = (u8)((lba >> 8) & 0xFFUL);
        out->cyl_hi   = (u8)((lba >> 16) & 0xFFUL);
        out->drv_head = (u8)(IDE_DRVHEAD_BASE | IDE_DRVHEAD_LBA | slave |
                             (u8)((lba >> 24) & 0x0FUL));
        return IDE_OK;
    }

    sect  = (lba % spt) + 1UL;            /* 1 始まり */
    track = lba / spt;
    head  = track % heads;
    cyl   = track / heads;
    if (cyl > IDE_CHS_MAX_CYL) return IDE_ERR_RANGE;

    out->sect_num = (u8)sect;
    out->cyl_lo   = (u8)(cyl & 0xFFUL);
    out->cyl_hi   = (u8)((cyl >> 8) & 0xFFUL);
    out->drv_head = (u8)(IDE_DRVHEAD_BASE | slave | (u8)(head & 0x0FUL));
    return IDE_OK;
}
