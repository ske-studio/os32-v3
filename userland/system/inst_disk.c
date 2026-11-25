/* Shared installer capacity estimates and destination path checks. */

#include "inst_disk.h"
#include "sys/fs/ext2/ext2_layout.h"

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
