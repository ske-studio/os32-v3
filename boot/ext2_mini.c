/* ======================================================================== */
/*  EXT2_MINI.C — ブートローダー用 ext2 最小リーダー                         */
/*                                                                          */
/*  カーネルの fs/ext2_*.c から読み取り機能のみを抽出。                      */
/*  ブロックサイズ 1024B 固定。直接 + 単一間接 + 二重間接ブロック対応。       */
/*  読み取り専用。BSS/ヒープ使用は静的バッファのみ。                         */
/* ======================================================================== */

#include "boot_defs.h"

/* 内部状態 */
static u32 s_base_lba;         /* パーティション開始LBA */
static u32 s_blocks_per_group;
static u32 s_inodes_per_group;
static u16 s_inode_size;

/* 静的バッファ (BSS, ASMがゼロクリア) */
static u8 blk_buf[EXT2_BLOCK_SIZE];
static u8 aux_buf[EXT2_BLOCK_SIZE];

/* ---------------------------------------------------------------- */
/*  ブロック読み込み (1ブロック = 2セクタ)                           */
/* ---------------------------------------------------------------- */
static void read_block(u32 block_num, u8 *buf)
{
    u32 sector = s_base_lba + block_num * 2;
    boot_read_sector_asm(sector,     buf);
    boot_read_sector_asm(sector + 1, buf + 512);
}

/* ---------------------------------------------------------------- */
/*  文字列比較 (name_len 長)                                        */
/* ---------------------------------------------------------------- */
static int name_eq(const char *a, const char *b, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return (a[len] == '\0' || a[len] == '/');
}

/* ---------------------------------------------------------------- */
/*  グループディスクリプタから inode_table ブロック番号を取得         */
/* ---------------------------------------------------------------- */
static u32 get_inode_table(u32 group)
{
    u32 gd_block = 2 + (group * 32) / EXT2_BLOCK_SIZE;
    u32 offset   = (group * 32) % EXT2_BLOCK_SIZE;
    read_block(gd_block, blk_buf);
    return *(u32 *)&blk_buf[offset + 8];  /* bg_inode_table */
}

/* ---------------------------------------------------------------- */
/*  inode 読み込み (128B分をblk_bufからコピー)                       */
/* ---------------------------------------------------------------- */
static void read_inode(u32 ino, u8 *out_inode)
{
    u32 group = (ino - 1) / s_inodes_per_group;
    u32 index = (ino - 1) % s_inodes_per_group;
    u32 itab  = get_inode_table(group);
    u32 blk   = itab + (index * s_inode_size) / EXT2_BLOCK_SIZE;
    u32 off   = (index * s_inode_size) % EXT2_BLOCK_SIZE;
    int i;

    read_block(blk, blk_buf);
    for (i = 0; i < (int)s_inode_size && i < 128; i++)
        out_inode[i] = blk_buf[off + i];
}

/* ---------------------------------------------------------------- */
/*  bmap: 論理ブロック番号 → 物理ブロック番号                       */
/*  inode_blocks は inode の i_block[15] 配列へのポインタ            */
/* ---------------------------------------------------------------- */
static u32 bmap(u32 *iblocks, u32 fblock)
{
    u32 ptrs_per_block = EXT2_BLOCK_SIZE / 4;  /* 256 */

    /* 直接ブロック */
    if (fblock < EXT2_NDIR_BLOCKS)
        return iblocks[fblock];

    fblock -= EXT2_NDIR_BLOCKS;

    /* 単一間接 */
    if (fblock < ptrs_per_block) {
        if (iblocks[EXT2_IND_BLOCK] == 0) return 0;
        read_block(iblocks[EXT2_IND_BLOCK], aux_buf);
        return ((u32 *)aux_buf)[fblock];
    }

    fblock -= ptrs_per_block;

    /* 二重間接 */
    {
        u32 idx1 = fblock / ptrs_per_block;
        u32 idx2 = fblock % ptrs_per_block;
        u32 ind_blk;
        if (iblocks[EXT2_DIND_BLOCK] == 0) return 0;
        read_block(iblocks[EXT2_DIND_BLOCK], aux_buf);
        ind_blk = ((u32 *)aux_buf)[idx1];
        if (ind_blk == 0) return 0;
        read_block(ind_blk, aux_buf);
        return ((u32 *)aux_buf)[idx2];
    }
}

/* ================================================================ */
/*  ext2m_init — スーパーブロック読み込み                            */
/* ================================================================ */
int ext2m_init(u32 part_lba)
{
    u16 magic;
    s_base_lba = part_lba;

    /* スーパーブロックはブロック1 (オフセット1024B) */
    read_block(1, blk_buf);

    magic = *(u16 *)&blk_buf[56];
    if (magic != EXT2_SUPER_MAGIC)
        return -1;

    s_blocks_per_group = *(u32 *)&blk_buf[32];
    s_inodes_per_group = *(u32 *)&blk_buf[40];
    s_inode_size       = *(u16 *)&blk_buf[88];
    if (s_inode_size == 0) s_inode_size = 128;

    return 0;
}

/* ================================================================ */
/*  ext2m_lookup — パス解決 ("/boot/vmkernel.lz4" → inode番号)      */
/* ================================================================ */
u32 ext2m_lookup(const char *path)
{
    u32 cur_ino = EXT2_ROOT_INO;
    const char *p = path;
    u8 inode_raw[128];

    /* 先頭の '/' をスキップ */
    if (*p == '/') p++;

    while (*p) {
        /* 次のパスコンポーネントを取得 */
        const char *comp = p;
        int comp_len = 0;
        u32 found_ino = 0;
        u32 offset;

        while (*p && *p != '/') { p++; comp_len++; }
        if (*p == '/') p++;

        if (comp_len == 0) continue;

        /* 現在のディレクトリ inode を読む */
        read_inode(cur_ino, inode_raw);

        /* ディレクトリエントリを走査 */
        {
            u32 dir_size = *(u32 *)&inode_raw[4]; /* i_size */
            u32 *blocks  = (u32 *)&inode_raw[40]; /* i_block */
            u32 fblk;
            int found = 0;

            for (fblk = 0; fblk * EXT2_BLOCK_SIZE < dir_size; fblk++) {
                u32 pblk = bmap(blocks, fblk);
                if (pblk == 0) break;
                read_block(pblk, blk_buf);

                offset = 0;
                while (offset < EXT2_BLOCK_SIZE) {
                    u32 d_ino    = *(u32 *)&blk_buf[offset];
                    u16 d_reclen = *(u16 *)&blk_buf[offset + 4];
                    u8  d_namlen = blk_buf[offset + 6];
                    char *d_name = (char *)&blk_buf[offset + 8];

                    if (d_reclen == 0) break;
                    if (d_ino != 0 && d_namlen == (u8)comp_len) {
                        if (name_eq(comp, d_name, comp_len)) {
                            found_ino = d_ino;
                            found = 1;
                            break;
                        }
                    }
                    offset += d_reclen;
                }
                if (found) break;
            }
        }

        if (found_ino == 0) return 0; /* 見つからない */
        cur_ino = found_ino;
    }

    return cur_ino;
}

/* ================================================================ */
/*  ext2m_read_file — ファイル全体読み込み                           */
/*  戻り値: 読み込みバイト数 (負=エラー)                             */
/*    EXT2M_ERR_TOO_BIG  i_size が max_size を超える (**切り詰めない**) */
/*    EXT2M_ERR_SHORT    ブロックの穴で最後まで読めなかった           */
/*  以前は max_size で黙って切り詰め、途中までのイメージを展開して     */
/*  いた (票 TASK_HDD_INSTALL N8)。                                    */
/* ================================================================ */
int ext2m_read_file(u32 ino, u8 *buf, u32 max_size)
{
    u8 inode_raw[128];
    u32 file_size;
    u32 *blocks;
    u32 fblk;
    u32 remain;
    u8 *dst = buf;
    int i;

    read_inode(ino, inode_raw);
    file_size = *(u32 *)&inode_raw[4]; /* i_size */
    blocks    = (u32 *)&inode_raw[40]; /* i_block */

    if (file_size > max_size) return EXT2M_ERR_TOO_BIG;
    remain = file_size;

    for (fblk = 0; remain > 0; fblk++) {
        u32 pblk = bmap(blocks, fblk);
        u32 copy_len = (remain > EXT2_BLOCK_SIZE) ? EXT2_BLOCK_SIZE : remain;

        if (pblk == 0) break;
        read_block(pblk, blk_buf);

        for (i = 0; i < (int)copy_len; i++)
            dst[i] = blk_buf[i];

        dst    += copy_len;
        remain -= copy_len;
    }

    if (remain != 0) return EXT2M_ERR_SHORT;
    return (int)file_size;
}
