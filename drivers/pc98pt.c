/* ======================================================================== */
/*  PC98PT.C — PC-98 区画表の共有部 (純粋関数)                              */
/*                                                                          */
/*  配置・規則・呼び手の一覧は pc98pt.h。                                   */
/*  試験: tools/tests/hdd_stage1_host.c + tools/tests/test_hdd_stage1.py    */
/*  記録: tools/tests/hdd_stage1_tdd.md                                     */
/* ======================================================================== */

#include "pc98pt.h"

static unsigned short rd16(const unsigned char *p)
{
    return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static void wr16(unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

int pc98pt_get(const unsigned char *sect, int idx, PC98PartEntry *out)
{
    const unsigned char *p;
    int i;

    if (!sect || !out || idx < 0 || idx >= PC98PT_MAX_ENTRIES)
        return PC98PT_ERR_ARG;
    p = sect + idx * PC98PT_ENTRY_SIZE;
    out->bootable       = p[PC98PT_OFF_MID];
    out->sys_id         = p[PC98PT_OFF_SID];
    out->reserved1[0]   = p[2];
    out->reserved1[1]   = p[3];
    out->ipl_sector     = p[PC98PT_OFF_IPL_SECT];
    out->ipl_head       = p[PC98PT_OFF_IPL_HEAD];
    out->ipl_cyl        = rd16(p + PC98PT_OFF_IPL_CYL);
    out->start_sector   = p[PC98PT_OFF_SSECT];
    out->start_head     = p[PC98PT_OFF_SHEAD];
    out->start_cyl      = rd16(p + PC98PT_OFF_SCYL);
    out->end_sector     = p[PC98PT_OFF_ESECT];
    out->end_head       = p[PC98PT_OFF_EHEAD];
    out->end_cyl        = rd16(p + PC98PT_OFF_ECYL);
    for (i = 0; i < PC98PT_NAME_LEN; i++)
        out->name[i] = (char)p[PC98PT_OFF_NAME + i];
    return PC98PT_OK;
}

int pc98pt_put(unsigned char *sect, int idx, const PC98PartEntry *in)
{
    unsigned char *p;
    int i;

    if (!sect || !in || idx < 0 || idx >= PC98PT_MAX_ENTRIES)
        return PC98PT_ERR_ARG;
    p = sect + idx * PC98PT_ENTRY_SIZE;
    p[PC98PT_OFF_MID]       = in->bootable;
    p[PC98PT_OFF_SID]       = in->sys_id;
    p[2]                    = in->reserved1[0];
    p[3]                    = in->reserved1[1];
    p[PC98PT_OFF_IPL_SECT]  = in->ipl_sector;
    p[PC98PT_OFF_IPL_HEAD]  = in->ipl_head;
    wr16(p + PC98PT_OFF_IPL_CYL, in->ipl_cyl);
    p[PC98PT_OFF_SSECT]     = in->start_sector;
    p[PC98PT_OFF_SHEAD]     = in->start_head;
    wr16(p + PC98PT_OFF_SCYL, in->start_cyl);
    p[PC98PT_OFF_ESECT]     = in->end_sector;
    p[PC98PT_OFF_EHEAD]     = in->end_head;
    wr16(p + PC98PT_OFF_ECYL, in->end_cyl);
    for (i = 0; i < PC98PT_NAME_LEN; i++)
        p[PC98PT_OFF_NAME + i] = (unsigned char)in->name[i];
    return PC98PT_OK;
}

int pc98pt_entry_empty(const PC98PartEntry *e)
{
    return (e->bootable == 0 && e->sys_id == 0) ? 1 : 0;
}

int pc98pt_count_used(const unsigned char *sect)
{
    PC98PartEntry e;
    int i, n = 0;

    if (!sect) return 0;
    for (i = 0; i < PC98PT_MAX_ENTRIES; i++) {
        (void)pc98pt_get(sect, i, &e);
        if (!pc98pt_entry_empty(&e)) n++;
    }
    return n;
}

/* ヘッド・セクタは 1 バイトの欄なので 1〜255。0 は割り算を壊す。 */
static int geom_ok(unsigned long heads, unsigned long spt)
{
    return (heads >= 1 && heads <= 255 && spt >= 1 && spt <= 255) ? 1 : 0;
}

int pc98pt_chs_to_lba(unsigned long cyl, unsigned long head, unsigned long sect,
                      unsigned long heads, unsigned long spt,
                      unsigned long *out_lba)
{
    if (!out_lba) return PC98PT_ERR_ARG;
    if (!geom_ok(heads, spt)) return PC98PT_ERR_GEOM;
    if (head >= heads || sect >= spt || cyl > PC98PT_MAX_CYL)
        return PC98PT_ERR_RANGE;
    /* 最大 65535 × 255 × 255 + … < 2^32 — 32 ビットに収まる */
    *out_lba = (cyl * heads + head) * spt + sect;
    return PC98PT_OK;
}

int pc98pt_entry_range(const PC98PartEntry *e,
                       unsigned long heads, unsigned long spt,
                       unsigned long disk_total,
                       unsigned long *out_start, unsigned long *out_len)
{
    unsigned long start, end_excl, cylsz;
    int rc;

    if (!e || !out_start || !out_len) return PC98PT_ERR_ARG;
    if (!geom_ok(heads, spt)) return PC98PT_ERR_GEOM;

    rc = pc98pt_chs_to_lba(e->start_cyl, e->start_head, e->start_sector,
                           heads, spt, &start);
    if (rc != PC98PT_OK) return rc;

    /* 終わり (排他) = 終了シリンダの次のシリンダの先頭。
     * (65535 + 1) × 255 × 255 = 4,261,478,400 < 2^32 */
    cylsz = heads * spt;
    end_excl = ((unsigned long)e->end_cyl + 1UL) * cylsz;

    if (end_excl <= start) return PC98PT_ERR_RANGE;
    if (disk_total != 0 && end_excl > disk_total) return PC98PT_ERR_RANGE;

    *out_start = start;
    *out_len = end_excl - start;
    return PC98PT_OK;
}

int pc98pt_find_os32(const unsigned char *sect,
                     unsigned long heads, unsigned long spt,
                     unsigned long disk_total,
                     int *out_idx,
                     unsigned long *out_start, unsigned long *out_len)
{
    PC98PartEntry e;
    int i;

    if (!sect || !out_start || !out_len) return PC98PT_ERR_ARG;
    for (i = 0; i < PC98PT_MAX_ENTRIES; i++) {
        (void)pc98pt_get(sect, i, &e);
        if (e.sys_id != PC98PT_SID_OS32) continue;
        if (out_idx) *out_idx = i;
        return pc98pt_entry_range(&e, heads, spt, disk_total,
                                  out_start, out_len);
    }
    return PC98PT_ERR_NOTFOUND;
}

int pc98pt_os32_is_legacy(const unsigned char *sect,
                          unsigned long heads, unsigned long spt,
                          unsigned long disk_total)
{
    PC98PartEntry e;
    const unsigned char *p;
    unsigned long st, ln, lstart, lend;
    int i;

    if (!sect) return 0;
    for (i = 0; i < PC98PT_MAX_ENTRIES; i++) {
        (void)pc98pt_get(sect, i, &e);
        if (e.sys_id != PC98PT_SID_OS32) continue;
        if (pc98pt_entry_range(&e, heads, spt, disk_total, &st, &ln) == PC98PT_OK)
            return 0;                       /* 標準配置で読める */
        p = sect + i * PC98PT_ENTRY_SIZE;
        /* 旧: +6 開始セクタ、+7 開始ヘッド、+8-9 開始シリンダ、+12-13 終了シリンダ */
        if (pc98pt_chs_to_lba(rd16(p + 8), p[7], p[6], heads, spt, &lstart)
            != PC98PT_OK)
            return 0;
        lend = ((unsigned long)rd16(p + 12) + 1UL) * heads * spt;
        if (lend <= lstart) return 0;
        if (disk_total != 0 && lend > disk_total) return 0;
        return 1;
    }
    return 0;
}

int pc98pt_make_os32(PC98PartEntry *e, unsigned long start, unsigned long len,
                     unsigned long heads, unsigned long spt)
{
    unsigned long cylsz, scyl, ecyl;
    const char *nm = PC98PT_NAME_OS32;
    int i;

    if (!e) return PC98PT_ERR_ARG;
    if (!geom_ok(heads, spt)) return PC98PT_ERR_GEOM;
    if (len == 0) return PC98PT_ERR_RANGE;
    cylsz = heads * spt;
    if ((start % cylsz) != 0 || (len % cylsz) != 0) return PC98PT_ERR_ALIGN;
    scyl = start / cylsz;
    /* 終了シリンダ = (start + len) / cylsz - 1。加算を避けて桁あふれしない形で */
    ecyl = scyl + len / cylsz - 1UL;
    if (scyl > PC98PT_MAX_CYL || len / cylsz > PC98PT_MAX_CYL + 1UL ||
        ecyl > PC98PT_MAX_CYL)
        return PC98PT_ERR_CYL;

    for (i = 0; i < (int)sizeof(*e); i++) ((unsigned char *)e)[i] = 0;
    e->bootable     = PC98PT_MID_BOOTABLE;
    e->sys_id       = PC98PT_SID_OS32;
    e->ipl_sector   = 0;
    e->ipl_head     = 0;
    e->ipl_cyl      = (unsigned short)scyl;
    e->start_sector = 0;
    e->start_head   = 0;
    e->start_cyl    = (unsigned short)scyl;
    e->end_sector   = (unsigned char)(spt - 1UL);
    e->end_head     = (unsigned char)(heads - 1UL);
    e->end_cyl      = (unsigned short)ecyl;
    for (i = 0; i < PC98PT_NAME_LEN; i++) {
        if (nm[i] == '\0') break;
        e->name[i] = nm[i];
    }
    for (; i < PC98PT_NAME_LEN; i++) e->name[i] = ' ';
    return PC98PT_OK;
}
