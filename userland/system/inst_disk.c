/* ======================================================================== */
/*  INST_DISK.C — インストーラの判定 (純粋関数)                              */
/*                                                                          */
/*  規則は inst_disk.h。区画表は drivers/pc98pt.c、幾何と計画は            */
/*  userland/shell/hdprep_plan.c、ext2 の配置は fs/ext2_layout.c (どれも    */
/*  カーネル・hdprep と同じソースを組む — 写さない)。                       */
/*  32 ビットの桁あふれに頼る計算はしない (ホスト試験は 64 ビットで回る)。   */
/* ======================================================================== */

#include "inst_disk.h"
#include "drivers/pc98pt.h"
#include "fs/ext2_layout.h"

static unsigned long inst_rd16(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

/* 名前欄が "OS32" + 空白 (か NUL) か。書き手は空白で埋める
 * (pc98pt_make_os32 / 旧 cdinst / 旧 install / tools/pc98pt.py)。 */
static int name_is_os32(const unsigned char *e)
{
    const char *nm = PC98PT_NAME_OS32;
    int i;

    for (i = 0; nm[i]; i++)
        if (e[PC98PT_OFF_NAME + i] != (unsigned char)nm[i]) return 0;
    for (; i < PC98PT_NAME_LEN; i++) {
        unsigned char c = e[PC98PT_OFF_NAME + i];
        if (c != ' ' && c != 0) return 0;
    }
    return 1;
}

int inst_classify(const unsigned char *lba0, const unsigned char *lba1,
                  unsigned long heads, unsigned long spt,
                  unsigned long disk_total, unsigned long expect_start,
                  int *out_mode)
{
    PC98PartEntry e;
    const unsigned char *raw;
    unsigned long st, ln;
    int used, i, idx = -1;

    if (!lba0 || !lba1 || !out_mode) return INST_E_ARG;

    used = pc98pt_count_used(lba1);
    if (used == 0) {
        /* 空のディスク。hdprep と同じく LBA 0 の 55AA (何かの起動域) は断る */
        if (lba0[510] == 0x55 && lba0[511] == 0xAA) return HDPREP_E_MBR_SIG;
        *out_mode = INST_MODE_EMPTY;
        return 0;
    }
    if (used != 1) return INST_E_MULTI;

    for (i = 0; i < PC98PT_MAX_ENTRIES; i++) {
        (void)pc98pt_get(lba1, i, &e);
        if (!pc98pt_entry_empty(&e)) { idx = i; break; }
    }
    if (idx < 0) return INST_E_ARG;               /* count_used と食い違う (来ない) */
    raw = lba1 + idx * PC98PT_ENTRY_SIZE;

    /* OS32 が作ったもの = sid 0xE2 かつ名前 "OS32" */
    if (e.sys_id != PC98PT_SID_OS32 || !name_is_os32(raw)) return INST_E_FOREIGN;

    /* 標準配置で読めるなら開始を比べる */
    if (pc98pt_entry_range(&e, heads, spt, disk_total, &st, &ln) == PC98PT_OK) {
        if (st != expect_start) return INST_E_START;
        *out_mode = INST_MODE_RECREATE;
        return 0;
    }

    /* 旧配置 (+6 開始セクタ / +7 開始ヘッド / +8-9 開始シリンダ)。旧 cdinst と
     * 旧 install は 8/17 でシリンダ 12 (= LBA 1632) を書いた。BIOS 幾何で同じ
     * LBA になるとき (8/17 のディスク) だけ作り直す — 16/63 ではシリンダ 12 は
     * LBA 12,096 で、期待値 2016 と違うので断る */
    if (pc98pt_os32_is_legacy(lba1, heads, spt, disk_total)) {
        if (pc98pt_chs_to_lba(inst_rd16(raw + 8), raw[7], raw[6], heads, spt, &st)
            != PC98PT_OK)
            return INST_E_BROKEN;
        if (st != expect_start) return INST_E_START;
        *out_mode = INST_MODE_RECREATE_OLD;
        return 0;
    }
    return INST_E_BROKEN;
}

int inst_check_boot_files(unsigned long ipl_len, unsigned long loader_len,
                          unsigned long kernel_len)
{
    if (ipl_len == 0 || ipl_len > INST_IPL_MAX) return INST_E_IPL_SIZE;
    if (loader_len == 0 || loader_len > INST_LOADER_MAX) return INST_E_LOADER_SIZE;
    if (kernel_len == 0 || kernel_len > INST_KERNEL_MAX) return INST_E_KERNEL_SIZE;
    return 0;
}

void inst_need_init(InstNeed *n)
{
    if (!n) return;
    n->files = 0;
    n->dirs = 0;
    n->blocks = 0;
    n->too_big = 0;
}

/* 1KiB ブロック: 直接 12、単一間接 256、二重間接 256 × 256 */
unsigned long inst_file_blocks(unsigned long size)
{
    unsigned long data = size / EXT2L_BLOCK_SIZE + ((size % EXT2L_BLOCK_SIZE) ? 1UL : 0UL);
    unsigned long meta = 0;
    unsigned long per = EXT2L_BLOCK_SIZE / 4UL;          /* 256 */

    if (data > 12UL) meta += 1UL;                        /* 単一間接 */
    if (data > 12UL + per) {
        unsigned long rest = data - 12UL - per;
        meta += 1UL + rest / per + ((rest % per) ? 1UL : 0UL);  /* 二重間接 */
    }
    return data + meta;
}

void inst_need_file(InstNeed *n, unsigned long size)
{
    if (!n) return;
    n->files++;
    if (size > INST_EXT2_MAX_FILE) n->too_big++;
    n->blocks += inst_file_blocks(size);
}

void inst_need_dir(InstNeed *n)
{
    if (!n) return;
    n->dirs++;
    n->blocks += 1UL;
}

int inst_check_space(unsigned long part_sectors, const InstNeed *n, InstRoom *out)
{
    Ext2Layout l;
    unsigned long meta = 0, g, free_blocks, free_inodes, need_blocks, need_inodes;
    unsigned long entries;

    if (out) {
        out->free_blocks = 0;
        out->free_inodes = 0;
        out->need_blocks = 0;
        out->need_inodes = 0;
    }
    if (!n) return INST_E_ARG;
    if (n->too_big) return INST_E_FILE_SIZE;
    if (ext2_layout_plan((u32)part_sectors, (u32)INST_EXT2_MAX_GROUPS, &l) != EXT2L_OK)
        return INST_E_LAYOUT;

    for (g = 0; g < l.num_groups; g++)
        meta += ext2_layout_group_overhead(&l, (u32)g);
    meta += EXT2L_FIRST_DATA_BLOCK + EXT2L_ROOT_DATA_BLOCKS;
    free_blocks = (l.total_blocks > meta) ? l.total_blocks - meta : 0UL;
    free_inodes = (l.inodes_count > INST_EXT2_RESERVED_INODES)
                  ? l.inodes_count - INST_EXT2_RESERVED_INODES : 0UL;

    /* ディレクトリ項目 (8 B + 名前、4 B 揃え) は 1 ブロックに 16 件と多めに数え、
     * 各ディレクトリの 1 ブロックに加える */
    entries = n->files + n->dirs;
    need_blocks = n->blocks + entries / 16UL + 1UL + INST_SPACE_MARGIN_BLOCKS;
    need_inodes = n->files + n->dirs + INST_SPACE_MARGIN_INODES;

    if (out) {
        out->free_blocks = free_blocks;
        out->free_inodes = free_inodes;
        out->need_blocks = need_blocks;
        out->need_inodes = need_inodes;
    }
    if (need_blocks > free_blocks) return INST_E_SPACE;
    if (need_inodes > free_inodes) return INST_E_INODES;
    return 0;
}

int inst_check_path(const char *path)
{
    unsigned long depth = 0;
    const char *p, *c;
    unsigned long len;

    if (!path || path[0] != '/') return INST_E_PATH;
    p = path + 1;
    for (;;) {
        c = p;
        while (*p && *p != '/') p++;
        len = (unsigned long)(p - c);
        if (len == 0) return INST_E_PATH;                       /* "//"・末尾 "/"・"/" だけ */
        if (len == 1 && c[0] == '.') return INST_E_PATH;
        if (len == 2 && c[0] == '.' && c[1] == '.') return INST_E_PATH;
        depth++;
        if (depth + INST_PREFIX_DEPTH > INST_VFS_MAX_DEPTH) return INST_E_DEPTH;
        if (*p == '\0') break;
        p++;
    }
    return 0;
}

void inst_patch_ipl(unsigned char *ipl, unsigned long heads, unsigned long spt)
{
    if (!ipl) return;
    ipl[INST_IPL_OFF_HEADS] = (unsigned char)heads;
    ipl[INST_IPL_OFF_SPT] = (unsigned char)spt;
    ipl[510] = 0x55;
    ipl[511] = 0xAA;
}

int inst_build_pt(unsigned char *sect, const HdprepPlan *p)
{
    PC98PartEntry e;
    int i, rc;

    if (!sect || !p) return PC98PT_ERR_ARG;
    for (i = 0; i < PC98PT_SECTOR_SIZE; i++) sect[i] = 0;
    rc = pc98pt_make_os32(&e, p->start, p->len, p->heads, p->spt);
    if (rc != PC98PT_OK) return rc;
    return pc98pt_put(sect, 0, &e);
}

const char *inst_reason(int code)
{
    switch (code) {
    case INST_E_FOREIGN:     return "hd0 has a partition that OS32 did not create";
    case INST_E_MULTI:       return "hd0 has two or more partitions";
    case INST_E_START:       return "the OS32 partition does not start where the installer would put it";
    case INST_E_BROKEN:      return "the OS32 partition entry is broken";
    case INST_E_IPL_SIZE:    return "IPL (boot_hdd.bin) is empty or larger than 512 bytes";
    case INST_E_LOADER_SIZE: return "loader (loader_hdd.bin) is empty or larger than 8192 bytes";
    case INST_E_KERNEL_SIZE: return "vmkernel.lz4 is empty or larger than 508 KiB";
    case INST_E_SPACE:       return "the files do not fit in the OS32 area (blocks)";
    case INST_E_INODES:      return "the files do not fit in the OS32 area (inodes)";
    case INST_E_LAYOUT:      return "no ext2 layout fits in the OS32 area";
    case INST_E_ARG:         return "bad argument";
    case INST_E_PATH:        return "a package path is not absolute or has an empty, '.' or '..' part";
    case INST_E_DEPTH:       return "a package path is deeper than the VFS allows (32 parts with /hd0)";
    case INST_E_FILE_SIZE:   return "a file is larger than ext2 can hold here (67,383,296 bytes)";
    case INST_E_GEOM:        return "the kernel did not return the geometry of hd0 (hdd_geom_info)";
    default:                 return hdprep_reason(code);
    }
}

const char *inst_mode_name(int mode)
{
    switch (mode) {
    case INST_MODE_EMPTY:        return "empty disk: create the OS32 area";
    case INST_MODE_RECREATE:     return "re-create the existing OS32 area";
    case INST_MODE_RECREATE_OLD: return "re-create the existing OS32 area (old table layout)";
    default:                     return "?";
    }
}
