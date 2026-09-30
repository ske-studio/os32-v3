#include "ext2_priv.h"

/*  iノード読み書き — g_blk使用                                             */
/* ======================================================================== */

int ext2_read_inode(Ext2Ctx *ctx, u32 ino, Ext2Inode *inode)
{
    u32 group, index, block_num, offset_in_block;
    int ret, i;
    u8 *src;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    if (ino == 0) return EXT2_ERR_NOTFOUND;

    group = (ino - 1) / ctx->sb_info.inodes_per_group;
    index = (ino - 1) % ctx->sb_info.inodes_per_group;
    if (group >= ctx->num_groups) return EXT2_ERR_NOTFOUND;
    block_num = ctx->gd_table[group].inode_table + (index * ctx->sb_info.inode_size) / EXT2_BLOCK_SIZE;
    offset_in_block = (index * ctx->sb_info.inode_size) % EXT2_BLOCK_SIZE;

    ret = ext2_read_block(ctx, block_num, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;

    src = &ext2_g_blk[offset_in_block];
    inode->mode  = le16_rd(&src[0]);
    inode->uid   = le16_rd(&src[2]);
    inode->size  = le32_rd(&src[4]);
    inode->atime = le32_rd(&src[8]);
    inode->ctime = le32_rd(&src[12]);
    inode->mtime = le32_rd(&src[16]);
    inode->dtime = le32_rd(&src[20]);
    inode->gid   = le16_rd(&src[24]);
    inode->links_count = le16_rd(&src[26]);
    inode->blocks = le32_rd(&src[28]);
    inode->flags  = le32_rd(&src[32]);
    inode->osd1   = le32_rd(&src[36]);
    for (i = 0; i < EXT2_N_BLOCKS; i++) {
        inode->block[i] = le32_rd(&src[40 + i * 4]);
    }
    inode->generation = le32_rd(&src[100]);
    inode->file_acl   = le32_rd(&src[104]);
    inode->dir_acl    = le32_rd(&src[108]);
    inode->faddr      = le32_rd(&src[112]);
    ext2_mem_copy(inode->osd2, &src[116], 12);

    return EXT2_OK;
}

int ext2_write_inode(Ext2Ctx *ctx, u32 ino, const Ext2Inode *inode)
{
    u32 group, index, block_num, offset_in_block;
    int ret, i;
    u8 *dst;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    if (ino == 0) return EXT2_ERR_NOTFOUND;

    group = (ino - 1) / ctx->sb_info.inodes_per_group;
    index = (ino - 1) % ctx->sb_info.inodes_per_group;
    if (group >= ctx->num_groups) return EXT2_ERR_NOTFOUND;
    block_num = ctx->gd_table[group].inode_table + (index * ctx->sb_info.inode_size) / EXT2_BLOCK_SIZE;
    offset_in_block = (index * ctx->sb_info.inode_size) % EXT2_BLOCK_SIZE;

    ret = ext2_read_block(ctx, block_num, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;

    dst = &ext2_g_blk[offset_in_block];
    le16_wr(&dst[0], inode->mode);
    le16_wr(&dst[2], inode->uid);
    le32_wr(&dst[4], inode->size);
    le32_wr(&dst[8], inode->atime);
    le32_wr(&dst[12], inode->ctime);
    le32_wr(&dst[16], inode->mtime);
    le32_wr(&dst[20], inode->dtime);
    le16_wr(&dst[24], inode->gid);
    le16_wr(&dst[26], inode->links_count);
    le32_wr(&dst[28], inode->blocks);
    le32_wr(&dst[32], inode->flags);
    le32_wr(&dst[36], inode->osd1);
    for (i = 0; i < EXT2_N_BLOCKS; i++) {
        le32_wr(&dst[40 + i * 4], inode->block[i]);
    }
    le32_wr(&dst[100], inode->generation);
    le32_wr(&dst[104], inode->file_acl);
    le32_wr(&dst[108], inode->dir_acl);
    le32_wr(&dst[112], inode->faddr);
    ext2_mem_copy(&dst[116], inode->osd2, 12);

    return ext2_write_block(ctx, block_num, ext2_g_blk);
}

/* ======================================================================== */
/*  ビットマップ管理 — g_aux使用                                            */
/* ======================================================================== */

/* 戻り値: 正 = 割り当てたブロック番号 / EXT2_ERR_NOSPC = 空きが無い /
 *         EXT2_ERR_IO = **ビットマップを読めなかった・書けなかった** /
 *         EXT2_ERR_NOMOUNT (票 B8 往復 6、レビュー非 blocker 2)。
 *
 * 以前は失敗をすべて -1 (= EXT2_ERR_IO と同じ値) で返し、呼び手はそれを NOSPC に
 * 読み替えていた。さらに**ビットマップの読み取り失敗で次のグループへ進んで**
 * いたので、エラー状態を立てたあと別のグループに割り当てて操作が最後まで走り、
 * 最後の ext2_sync が IO を返す = **完了しているのに IO** になっていた
 * (媒体は整合するが、実機の `cp` が失敗と言いつつファイルができている)。
 * 1 グループの試験ディスクでは見えず、実 NHD (25 グループ) で起きる。
 *
 * **読めない / 書けないなら次のグループへ進まず、その場で IO を返す。**
 * メタデータの I/O エラーでエラー状態に入った以上、操作は完了させない。 */
int ext2_alloc_block(Ext2Ctx *ctx)
{
    int ret, byte_idx, bit_idx;
    u32 g, block_num, max_bits;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    /* 全グループを走査して空きブロックを探す */
    for (g = 0; g < ctx->num_groups; g++) {
        if (ctx->gd_table[g].free_blocks == 0) continue;

        ret = ext2_read_block(ctx, ctx->gd_table[g].block_bitmap, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;   /* 次のグループへ進まない */

        /* 最終グループはブロック数が端数になる場合がある */
        max_bits = ctx->sb_info.blocks_per_group;
        if (g == ctx->num_groups - 1) {
            u32 remaining = ctx->sb_info.total_blocks
                            - ctx->sb_info.first_data_block
                            - g * ctx->sb_info.blocks_per_group;
            if (remaining < max_bits) max_bits = remaining;
        }

        for (byte_idx = 0; (u32)byte_idx < (max_bits + 7) / 8; byte_idx++) {
            if (ext2_g_aux[byte_idx] == 0xFF) continue;
            for (bit_idx = 0; bit_idx < 8; bit_idx++) {
                if ((u32)(byte_idx * 8 + bit_idx) >= max_bits) break;
                if (!(ext2_g_aux[byte_idx] & (1 << bit_idx))) {
                    block_num = ctx->sb_info.first_data_block
                                + g * ctx->sb_info.blocks_per_group
                                + (u32)(byte_idx * 8 + bit_idx);
                    if (block_num >= ctx->sb_info.total_blocks) return EXT2_ERR_NOSPC;
                    ext2_g_aux[byte_idx] |= (u8)(1 << bit_idx);
                    ret = ext2_write_block(ctx, ctx->gd_table[g].block_bitmap, ext2_g_aux);
                    if (ret != 0) return EXT2_ERR_IO;
                    ctx->gd_table[g].free_blocks--;
                    ctx->sb_info.free_blocks_count--;
                    ext2_meta_touch(ctx);
                    return (int)block_num;
                }
            }
        }
    }
    return EXT2_ERR_NOSPC;
}

/* 戻り値 EXT2_OK = 返した / 負値 = **返せなかった** (票 B8 往復 3、Codex P1-B)。
 *
 * 直す前は void で、ビットマップの読み出し失敗は黙って戻り、**書き込み失敗は
 * 戻り値を捨てたまま空き数を増やしていた** (実測: rc=0 なのにビットは使用中の
 * まま、空き数だけ 1 増える)。
 *
 * **空き数は書き込みが成功したときだけ動かす。**書き込みが途中で失敗すると
 * 媒体上のビットは消えたかもしれないが、そのときは空き数が実際より少なく
 * 見えるだけ (安全側)。逆 (実際より多く見せる) にはしない。
 *
 * 呼び手の扱い: このブロックを指す参照が**もう媒体上に無い**ことを先に
 * 確かめてから呼ぶ (ext2_truncate_blocks の順序)。そうしてあれば、ここが
 * 失敗しても起きるのは**漏れ (使用中だが誰も指さない)** だけで、別ファイル
 * との共有は起きない。 */
int ext2_free_block(Ext2Ctx *ctx, u32 block_num)
{
    int ret;
    u32 group, rel_bit, byte_idx, bit_idx;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    /* 範囲外は壊れた参照。触らない (下の引き算が回り込む) */
    if (block_num < ctx->sb_info.first_data_block ||
        block_num >= ctx->sb_info.total_blocks) return EXT2_ERR_INVAL;

    /* ブロック番号からグループを逆算 */
    group = (block_num - ctx->sb_info.first_data_block) / ctx->sb_info.blocks_per_group;
    rel_bit = (block_num - ctx->sb_info.first_data_block) % ctx->sb_info.blocks_per_group;
    if (group >= ctx->num_groups) return EXT2_ERR_INVAL;

    byte_idx = rel_bit / 8;
    bit_idx  = rel_bit % 8;

    ret = ext2_read_block(ctx, ctx->gd_table[group].block_bitmap, ext2_g_aux);
    if (ret != 0) return EXT2_ERR_IO;
    /* 既に空き: 二重解放。空き数を二重に増やさない
     * (ext2_release_blocks の後始末はこれに頼る — そちらのコメント (2)) */
    if (!(ext2_g_aux[byte_idx] & (1 << bit_idx))) return EXT2_OK;
    ext2_g_aux[byte_idx] &= (u8)~(1 << bit_idx);
    ret = ext2_write_block(ctx, ctx->gd_table[group].block_bitmap, ext2_g_aux);
    if (ret != 0) return EXT2_ERR_IO;
    ctx->gd_table[group].free_blocks++;
    ctx->sb_info.free_blocks_count++;
    ext2_meta_touch(ctx);
    return EXT2_OK;
}

/* 戻り値は ext2_alloc_block と同じ約束 (正 = inode 番号 / NOSPC / IO / NOMOUNT)。
 * ビットマップを読めなければ次のグループへ進まない (票 B8 往復 6)。 */
int ext2_alloc_inode(Ext2Ctx *ctx)
{
    int ret, byte_idx, bit_idx;
    u32 g, ino, max_bits;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    /* 全グループを走査して空きinodeを探す */
    for (g = 0; g < ctx->num_groups; g++) {
        if (ctx->gd_table[g].free_inodes == 0) continue;

        ret = ext2_read_block(ctx, ctx->gd_table[g].inode_bitmap, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;   /* 次のグループへ進まない */

        max_bits = ctx->sb_info.inodes_per_group;

        for (byte_idx = 0; (u32)byte_idx < (max_bits + 7) / 8; byte_idx++) {
            if (ext2_g_aux[byte_idx] == 0xFF) continue;
            for (bit_idx = 0; bit_idx < 8; bit_idx++) {
                if ((u32)(byte_idx * 8 + bit_idx) >= max_bits) break;
                if (!(ext2_g_aux[byte_idx] & (1 << bit_idx))) {
                    ino = g * ctx->sb_info.inodes_per_group
                          + (u32)(byte_idx * 8 + bit_idx) + 1;
                    if (ino > ctx->sb_info.total_inodes) return EXT2_ERR_NOSPC;
                    ext2_g_aux[byte_idx] |= (u8)(1 << bit_idx);
                    ret = ext2_write_block(ctx, ctx->gd_table[g].inode_bitmap, ext2_g_aux);
                    if (ret != 0) return EXT2_ERR_IO;
                    ctx->gd_table[g].free_inodes--;
                    ctx->sb_info.free_inodes_count--;
                    ext2_meta_touch(ctx);
                    return (int)ino;
                }
            }
        }
    }
    return EXT2_ERR_NOSPC;
}

/* ext2_free_block と同じ形 (票 B8 往復 3): 戻り値で返せたかを伝え、
 * 空き数は書き込みが成功したときだけ動かす。 */
int ext2_free_inode(Ext2Ctx *ctx, u32 ino)
{
    int ret;
    u32 group, rel_bit, byte_idx, bit_idx;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    if (ino == 0) return EXT2_ERR_INVAL;

    /* inode番号からグループを逆算 */
    group = (ino - 1) / ctx->sb_info.inodes_per_group;
    rel_bit = (ino - 1) % ctx->sb_info.inodes_per_group;
    if (group >= ctx->num_groups) return EXT2_ERR_INVAL;

    byte_idx = rel_bit / 8;
    bit_idx  = rel_bit % 8;

    ret = ext2_read_block(ctx, ctx->gd_table[group].inode_bitmap, ext2_g_aux);
    if (ret != 0) return EXT2_ERR_IO;
    if (!(ext2_g_aux[byte_idx] & (1 << bit_idx))) return EXT2_OK;
    ext2_g_aux[byte_idx] &= (u8)~(1 << bit_idx);
    ret = ext2_write_block(ctx, ctx->gd_table[group].inode_bitmap, ext2_g_aux);
    if (ret != 0) return EXT2_ERR_IO;
    ctx->gd_table[group].free_inodes++;
    ctx->sb_info.free_inodes_count++;
    ext2_meta_touch(ctx);
    return EXT2_OK;
}

/* ======================================================================== */
/*  間接ブロック: bmap — g_aux使用                                          */
/* ======================================================================== */

/* 「未割当 (0)」と「読めなかった (EXT2_ERR_IO)」を分けて返す。
 * 宣言の上のコメント (fs/ext2_priv.h) が決め方の理由を持つ (票 B8)。 */
int ext2_bmap(Ext2Ctx *ctx, const Ext2Inode *inode, u32 file_block,
              u32 *out_phys)
{
    int ret;

    if (!out_phys) return EXT2_ERR_INVAL;
    *out_phys = 0;

    if (file_block < EXT2_NDIR_BLOCKS) {
        *out_phys = inode->block[file_block];
        return EXT2_OK;
    }

    file_block -= EXT2_NDIR_BLOCKS;
    if (file_block < EXT2_ADDR_PER_BLOCK) {
        if (inode->block[EXT2_IND_BLOCK] == 0) return EXT2_OK;
        ret = ext2_read_block(ctx, inode->block[EXT2_IND_BLOCK], ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;
        *out_phys = le32_rd(&ext2_g_aux[file_block * 4]);
        return EXT2_OK;
    }

    file_block -= EXT2_ADDR_PER_BLOCK;
    if (file_block < EXT2_ADDR_PER_BLOCK * EXT2_ADDR_PER_BLOCK) {
        u32 ind1_idx = file_block / EXT2_ADDR_PER_BLOCK;
        u32 ind2_idx = file_block % EXT2_ADDR_PER_BLOCK;
        u32 ind1_block;

        if (inode->block[EXT2_DIND_BLOCK] == 0) return EXT2_OK;
        ret = ext2_read_block(ctx, inode->block[EXT2_DIND_BLOCK], ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;
        ind1_block = le32_rd(&ext2_g_aux[ind1_idx * 4]);
        if (ind1_block == 0) return EXT2_OK;
        ret = ext2_read_block(ctx, ind1_block, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;
        *out_phys = le32_rd(&ext2_g_aux[ind2_idx * 4]);
        return EXT2_OK;
    }
    /* 三重間接は未対応。範囲外は「未割当」= ファイルの終わり */
    return EXT2_OK;
}

/* 失敗時の約束 (票 B8 往復 3):
 *   EXT2_ERR_NOSPC … phys_block は**どの表にも書いていない**。呼び手は返してよい。
 *   EXT2_ERR_IO    … phys_block が**媒体上の表に載ったかもしれない**
 *                    (表の書き込みが途中で失敗すると区別できない)。呼び手は、
 *                    その表が媒体上の inode から辿れる (既存ファイル) なら
 *                    **返してはいけない** — 返すと、辿れる表が解放済みの
 *                    ブロックを指す。辿れない (作成中・切り詰め直後) なら返してよい。
 *                    表を割り当てるビットマップの I/O が落ちた場合もここに入る
 *                    (往復 6)。実際は何も載っていないが、呼び手は漏れ側に倒れる
 *                    だけなので区別しない (エラー状態に入っていて e2fsck が回収する)。
 *
 * **新しく割り当てた表は、中身を書き終えてから指させる。**以前は先に
 * inode (メモリ) や二重間接表 (媒体) へポインタを入れてから表を書いており、
 * その書き込みが失敗すると**ゴミの入った表**を指したまま戻っていた。
 * ゴミの表は任意のブロック番号を指すので、呼び手が inode を書き戻した時点で
 * 他のファイルのブロックや空きブロックへの参照が媒体に載る。
 * 指される前の表は誰からも辿れないので、失敗したらその場で返してよい。 */
int ext2_bmap_set(Ext2Ctx *ctx, Ext2Inode *inode, u32 file_block, u32 phys_block)
{
    int ret;

    if (file_block < EXT2_NDIR_BLOCKS) {
        inode->block[file_block] = phys_block;
        return EXT2_OK;
    }

    file_block -= EXT2_NDIR_BLOCKS;
    if (file_block < EXT2_ADDR_PER_BLOCK) {
        if (inode->block[EXT2_IND_BLOCK] == 0) {
            int ind_blk = ext2_alloc_block(ctx);
            if (ind_blk < 0) return ind_blk;   /* NOSPC / IO をそのまま (往復 6) */
            /* alloc_block は g_aux を使うので、表はその後で組む */
            ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
            le32_wr(&ext2_g_aux[file_block * 4], phys_block);
            ret = ext2_write_block(ctx, (u32)ind_blk, ext2_g_aux);
            if (ret != 0) {
                /* まだ誰も指していない。返せなければ漏れるだけ */
                if (ext2_free_block(ctx, (u32)ind_blk) != 0) { /* 漏れ */ }
                return EXT2_ERR_IO;
            }
            inode->block[EXT2_IND_BLOCK] = (u32)ind_blk;
            inode->blocks += 2;
            return EXT2_OK;
        }
        ret = ext2_read_block(ctx, inode->block[EXT2_IND_BLOCK], ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;
        le32_wr(&ext2_g_aux[file_block * 4], phys_block);
        return ext2_write_block(ctx, inode->block[EXT2_IND_BLOCK], ext2_g_aux) == 0
               ? EXT2_OK : EXT2_ERR_IO;
    }

    file_block -= EXT2_ADDR_PER_BLOCK;
    if (file_block < EXT2_ADDR_PER_BLOCK * EXT2_ADDR_PER_BLOCK) {
        u32 ind1_idx = file_block / EXT2_ADDR_PER_BLOCK;
        u32 ind2_idx = file_block % EXT2_ADDR_PER_BLOCK;
        u32 ind1_block;

        if (inode->block[EXT2_DIND_BLOCK] == 0) {
            int dind_blk = ext2_alloc_block(ctx);
            if (dind_blk < 0) return dind_blk; /* NOSPC / IO をそのまま (往復 6) */
            ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
            ret = ext2_write_block(ctx, (u32)dind_blk, ext2_g_aux);
            if (ret != 0) {
                if (ext2_free_block(ctx, (u32)dind_blk) != 0) { /* 漏れ */ }
                return EXT2_ERR_IO;
            }
            /* 空の表 (全 0) なので、この先で失敗しても指したままでよい */
            inode->block[EXT2_DIND_BLOCK] = (u32)dind_blk;
            inode->blocks += 2;
        }

        ret = ext2_read_block(ctx, inode->block[EXT2_DIND_BLOCK], ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;

        ind1_block = le32_rd(&ext2_g_aux[ind1_idx * 4]);
        if (ind1_block == 0) {
            int ind_blk = ext2_alloc_block(ctx);      /* g_aux を上書きする */
            if (ind_blk < 0) return ind_blk;   /* NOSPC / IO をそのまま (往復 6) */

            /* 1) 中身を書く (まだ誰も指していない) */
            ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
            le32_wr(&ext2_g_blk[ind2_idx * 4], phys_block);
            ret = ext2_write_block(ctx, (u32)ind_blk, ext2_g_blk);
            if (ret != 0) {
                if (ext2_free_block(ctx, (u32)ind_blk) != 0) { /* 漏れ */ }
                return EXT2_ERR_IO;
            }

            /* 2) 二重間接表から指させる。alloc_block が g_aux を潰したので読み直す */
            ret = ext2_read_block(ctx, inode->block[EXT2_DIND_BLOCK], ext2_g_aux);
            if (ret != 0) {
                if (ext2_free_block(ctx, (u32)ind_blk) != 0) { /* 漏れ */ }
                return EXT2_ERR_IO;
            }
            le32_wr(&ext2_g_aux[ind1_idx * 4], (u32)ind_blk);
            ret = ext2_write_block(ctx, inode->block[EXT2_DIND_BLOCK], ext2_g_aux);
            /* 失敗したら指されたかもしれない。**返さない** (漏れで止める) */
            if (ret != 0) return EXT2_ERR_IO;

            inode->blocks += 2;
            return EXT2_OK;
        }

        ret = ext2_read_block(ctx, ind1_block, ext2_g_blk);
        if (ret != 0) return EXT2_ERR_IO;
        le32_wr(&ext2_g_blk[ind2_idx * 4], phys_block);
        return ext2_write_block(ctx, ind1_block, ext2_g_blk) == 0 ? EXT2_OK : EXT2_ERR_IO;
    }
    return EXT2_ERR_NOSPC;
}

/* ======================================================================== */
/*  ブロック解放 (内部)                                                      */
/* ======================================================================== */

/* ---- 解放の順序 (票 B8 往復 3 / Codex P1-A) ----------------------------
 *
 * 守る不変条件は 1 つ:
 *
 *     **媒体上のどの参照も、解放済みのブロックを指してはならない。**
 *
 * 解放済みのブロックは次の ext2_alloc_block で別のファイルへ渡るので、
 * それを指す参照が媒体に残っていると 2 つのファイルが同じブロックを共有する
 * (2026-09-06 に踏んだ相互リンクと同じ壊れ方、gotcha §4-24)。
 *
 * 直す前 (往復 2 まで) は「下見で全部読めるのを確かめてから解放し、inode は
 * 呼び手が最後に書く」順序だった。**解放から inode の書き戻しまでの区間の
 * どこで失敗しても** (下見の後の再読、切り詰め後の新ブロックの書き込み、
 * inode の書き込みそのもの)、媒体上の inode が解放済みのブロックを指したまま
 * 残る。下見はその区間の一か所を塞いでいただけだった。
 *
 * **順序で閉じる** (ジャーナルは作らない):
 *   1. inode のブロックポインタ一式 (15 本) をメモリに写す
 *   2. inode のポインタと大きさを 0 にして **inode を先に書く**。
 *      失敗したら何も解放していないので整合している
 *   3. **その後で**、写しを辿って間接表を読み、ブロックを返す
 *
 * 3 のどこで失敗しても、媒体上の inode はもうそのブロックを指していない。
 * 起きるのは**漏れ** (ビットマップ上は使用中だが誰も指さない) だけで、
 * 別ファイルとの共有は原理的に起きない。漏れは安全側の失敗で、ホストの
 * e2fsck で回収できる (OS32 にはゲスト内の fsck が無いので、I/O 失敗を
 * 繰り返すと空き容量は減り得る)。
 *
 * **下見は不要になった**ので外した。安全性の根拠を下見に置かない。
 * ------------------------------------------------------------------------ */

/* 写し blocks[] (EXT2_N_BLOCKS 本) が指すブロックを返す。
 *
 * 呼び手の前提: **写しの参照は、もう媒体上のどこからも辿れない**。
 *
 * 表の中身を信用してよい理由: 媒体上で完成していたファイル (切り詰め・削除)
 * は当然として、作りかけのファイルの後始末でも、
 *   (1) ext2_bmap_set は**新しい表を書き終えてから指させる**ので、写しから
 *       辿れる表の中身は必ず一度は全部書かれている。
 *   (2) 書き込みが途中で落ちた表で信用できないのは、その回に足そうとした
 *       1 項目だけで、その項目のブロックは呼び手が既に返している。ここで
 *       もう一度返しても ext2_free_block は「既に空き」として何もしない
 *       (その間に割り当ては挟まらない)。
 * (往復 3 の途中まで「何本目まで信用するか」の limit を持っていたが、
 * (1)(2) のもとでは効いていないことを変異試験で確かめて外した。)
 *
 * 戻り値 EXT2_OK = 全部返した / EXT2_ERR_IO = **一部を返せなかった (漏れ)**。
 * 読めない表はその配下ごと漏らす (表ブロック自体も返さない)。1 つ返せなくても
 * 残りは返し続ける — 参照はもう無いので、途中で止める理由が無い。
 *
 * バッファ: 表は ext2_g_blk (単一間接・二重間接) と ext2_g_dat (二重間接の
 * 内側) に置く。ext2_free_block が ext2_g_aux へビットマップを読み直すので、
 * 表を ext2_g_aux に置くと 1 本目を返した瞬間に表がビットマップへ化ける
 * (2026-09-06 の相互リンクの直接原因。gotcha §4-24)。ext2_g_dat は
 * ext2_write_stream のデータ用だが、そこからこの関数は呼ばれない。 */
int ext2_release_blocks(Ext2Ctx *ctx, const u32 *blocks)
{
    int i;
    int leaked = 0;
    u32 j, k;

    for (i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (blocks[i] != 0 && ext2_free_block(ctx, blocks[i]) != 0) leaked = 1;
    }

    if (blocks[EXT2_IND_BLOCK] != 0) {
        if (ext2_read_block(ctx, blocks[EXT2_IND_BLOCK], ext2_g_blk) != 0) {
            leaked = 1;                         /* 表ごと漏らす */
        } else {
            for (j = 0; j < EXT2_ADDR_PER_BLOCK; j++) {
                u32 blk = le32_rd(&ext2_g_blk[j * 4]);
                if (blk != 0 && ext2_free_block(ctx, blk) != 0) leaked = 1;
            }
            if (ext2_free_block(ctx, blocks[EXT2_IND_BLOCK]) != 0) leaked = 1;
        }
    }

    if (blocks[EXT2_DIND_BLOCK] != 0) {
        if (ext2_read_block(ctx, blocks[EXT2_DIND_BLOCK], ext2_g_blk) != 0) {
            leaked = 1;
        } else {
            for (j = 0; j < EXT2_ADDR_PER_BLOCK; j++) {
                u32 ind1 = le32_rd(&ext2_g_blk[j * 4]);
                if (ind1 == 0) continue;
                if (ext2_read_block(ctx, ind1, ext2_g_dat) != 0) {
                    leaked = 1;                 /* 表ごと漏らす */
                    continue;
                }
                for (k = 0; k < EXT2_ADDR_PER_BLOCK; k++) {
                    u32 blk = le32_rd(&ext2_g_dat[k * 4]);
                    if (blk != 0 && ext2_free_block(ctx, blk) != 0) leaked = 1;
                }
                if (ext2_free_block(ctx, ind1) != 0) leaked = 1;
            }
            if (ext2_free_block(ctx, blocks[EXT2_DIND_BLOCK]) != 0) leaked = 1;
        }
    }

    return leaked ? EXT2_ERR_IO : EXT2_OK;
}

/* inode ino のブロックを全部外して返す (上の順序 1〜3)。
 *
 * inode は呼び手がメモリ上で用意したもの (dtime / links_count / mtime など
 * 他の欄は呼び手が決めて渡す)。ここではブロックポインタ・blocks・size を
 * 0 にして書き、その後で返す。
 *
 * 戻り値:
 *   EXT2_OK   … 媒体上の inode はもうブロックを指していない。
 *               *leaked = 1 なら**一部を返せなかった** (漏れ。整合はしている)
 *   負値      … **inode を書けなかった。何も返していない。**
 *               *inode はポインタを元に戻してある (媒体上の inode は、書けて
 *               いれば 0、書けていなければ元のまま — どちらも使用中のブロック
 *               しか指さない)
 *
 * ブロックを持たない inode でも必ず書く — 呼び手 (unlink / rmdir) は
 * links_count / dtime の書き戻しをこの 1 回に任せている。 */
int ext2_truncate_blocks(Ext2Ctx *ctx, u32 ino, Ext2Inode *inode, int *leaked)
{
    u32 saved[EXT2_N_BLOCKS];
    u32 saved_blocks, saved_size;
    int i, ret;

    if (leaked) *leaked = 0;

    for (i = 0; i < EXT2_N_BLOCKS; i++) saved[i] = inode->block[i];
    saved_blocks = inode->blocks;
    saved_size = inode->size;

    for (i = 0; i < EXT2_N_BLOCKS; i++) inode->block[i] = 0;
    inode->blocks = 0;
    inode->size = 0;

    /* 2. 参照を先に外す */
    ret = ext2_write_inode(ctx, ino, inode);
    if (ret != 0) {
        for (i = 0; i < EXT2_N_BLOCKS; i++) inode->block[i] = saved[i];
        inode->blocks = saved_blocks;
        inode->size = saved_size;
        return EXT2_ERR_IO;
    }

    /* 3. 参照が無くなってから返す */
    if (ext2_release_blocks(ctx, saved) != 0) {
        if (leaked) *leaked = 1;
    }
    return EXT2_OK;
}

/* ======================================================================== */
