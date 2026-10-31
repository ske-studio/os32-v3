/* ディレクトリの作成・空判定・削除。ext2_dir.c から元の順序で取り込む。 */

/* ======================================================================== */
/*  mkdir / rmdir                                                            */
/* ======================================================================== */

int ext2_mkdir(Ext2Ctx *ctx, u32 parent_ino, const char *name)
{
    int new_ino, new_blk;
    Ext2Inode inode, parent_inode;
    u32 now, pos;
    int ret;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    /* 空の名前はここまで素通りしていた。find_entry は長さ 0 の名前に一致する
     * 項目を持たないので NOTFOUND になり、名前の無いディレクトリを作った
     * (票 TASK_EXT2_EMPTY_NAME: cdinst の mkdir("/hd0") が作った inode 23) */
    ret = ext2_name_check(name);   /* mkdir */
    if (ret != 0) return ret;
    ret = ext2_check_writable(ctx);   /* エラー状態なら断る (票 B8 往復 5) */
    if (ret != 0) return ret;

    /* 存在確認は **3 値で受ける** (票 B8 / Codex 実装レビュー P1-1)。
     * 「EXT2_OK のときだけ拒否」だと I/O エラーでも下の割当・作成へ進み、
     * 既にある名前と同名のディレクトリを二重に作ってしまう (実測: 18 セクタ
     * 書き込み、同名エントリ 2 件)。**何も書かずに中断する**のが正解。 */
    {
        u32 tmp;
        ret = ext2_find_entry(ctx, parent_ino, name, &tmp, (u8 *)0);
        if (ret == EXT2_OK) return EXT2_ERR_EXIST;
        if (ret != EXT2_ERR_NOTFOUND) return ret;
    }

    /* 親の links_count は u16。上限を越えて回り込むと「名前の数 > links」に
     * なるので、何も書く前に断る (票 B8 往復 5、レビュー非 blocker) */
    ret = ext2_read_inode(ctx, parent_ino, &parent_inode);
    if (ret != 0) return ret;
    if (parent_inode.links_count >= EXT2_LINK_MAX) return EXT2_ERR_MLINK;

    new_ino = ext2_alloc_inode(ctx);
    if (new_ino < 0) return new_ino;       /* NOSPC / IO をそのまま (往復 6) */
    new_blk = ext2_alloc_block(ctx);
    if (new_blk < 0) {
        if (ext2_free_inode(ctx, (u32)new_ino) != 0) { /* 漏れ */ }
        return new_blk;
    }

    /* 親の links_count を**先に**上げる (票 B8 往復 4)。新しいディレクトリの
     * ".." が媒体に載った時点で親を指す名前が 1 つ増えるので、それより前に
     * 数を上げておけば、どこで落ちても「名前の数 <= links_count」が保てる
     * (多い側は孤児・漏れで、e2fsck が直す)。 */
    ret = ext2_read_inode(ctx, parent_ino, &parent_inode);
    if (ret == 0) {
        parent_inode.links_count++;
        ret = ext2_write_inode(ctx, parent_ino, &parent_inode);
    }
    if (ret != 0) {
        if (ext2_free_block(ctx, (u32)new_blk) != 0) { /* 漏れ */ }
        if (ext2_free_inode(ctx, (u32)new_ino) != 0) { /* 漏れ */ }
        return EXT2_ERR_IO;
    }

    now = ext2_current_time();
    ext2_mem_zero(&inode, sizeof(inode));
    inode.mode = (u16)(EXT2_S_IFDIR | OS32_DEFAULT_DIR_PERM);
    inode.size = EXT2_BLOCK_SIZE;
    inode.atime = now; inode.ctime = now; inode.mtime = now;
    inode.links_count = EXT2_EMPTY_DIR_LINKS;
    inode.blocks = EXT2_SECTORS_PER_BLOCK;
    inode.block[0] = (u32)new_blk;

    /* "." と ".." */
    ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
    pos = 0;
    le32_wr(&ext2_g_aux[pos], (u32)new_ino);
    le16_wr(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF], EXT2_DOT_RECLEN);
    ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF] = 1; ext2_g_aux[pos + EXT2_DIRENT_TYPE_OFF] = EXT2_FT_DIR;
    ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF] = '.';
    pos = EXT2_DOT_RECLEN;
    le32_wr(&ext2_g_aux[pos], parent_ino);
    le16_wr(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF], (u16)(EXT2_BLOCK_SIZE - EXT2_DOT_RECLEN));
    ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF] = 2; ext2_g_aux[pos + EXT2_DIRENT_TYPE_OFF] = EXT2_FT_DIR;
    ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF] = '.'; ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF + 1] = '.';

    ret = ext2_write_block(ctx, (u32)new_blk, ext2_g_aux);
    if (ret != 0) {
        /* inode はまだ書いていないので、new_blk を指すものは媒体に無い。
         * 親の links は上げたまま (多い側 = 安全側) */
        if (ext2_free_block(ctx, (u32)new_blk) != 0) { /* 漏れ */ }
        if (ext2_free_inode(ctx, (u32)new_ino) != 0) { /* 漏れ */ }
        return EXT2_ERR_IO;
    }

    /* 以前は失敗を捨てて名前を付けていた — 名前が**書けていない inode**
     * (前の持ち主の内容) を指すことになる。書けたか区別できないので
     * **何も返さない** (書けていれば new_blk を指している。票 B8 往復 3)。 */
    ret = ext2_write_inode(ctx, (u32)new_ino, &inode);
    if (ret != 0) return EXT2_ERR_IO;

    ret = ext2_add_entry(ctx, parent_ino, name, (u32)new_ino, EXT2_FT_DIR);
    if (ret != 0) {
        /* ext2_create と揃える (票 B8 往復 4、レビュー非 blocker):
         * NOSPC なら名前は載っていないので、参照を外してから inode を返す。
         * それ以外は名前が載ったか区別できないので何も触らない。 */
        if (ret == EXT2_ERR_NOSPC) {
            int leaked = 0;
            inode.links_count = 0;
            inode.dtime = ext2_current_time();
            if (ext2_truncate_blocks(ctx, (u32)new_ino, &inode, &leaked) == EXT2_OK) {
                if (ext2_free_inode(ctx, (u32)new_ino) != 0) { /* 漏れ */ }
                /* ".." はもう辿れないので、親の links を戻してよい */
                if (ext2_read_inode(ctx, parent_ino, &parent_inode) == 0 &&
                    parent_inode.links_count > 0) {
                    parent_inode.links_count--;
                    if (ext2_write_inode(ctx, parent_ino, &parent_inode) != 0) {
                        /* 多いまま (安全側) */
                    }
                }
            }
        }
        return ret;
    }

    {
        u32 dir_group = ((u32)new_ino - 1) / ctx->sb_info.inodes_per_group;
        if (dir_group < ctx->num_groups) {
            ctx->gd_table[dir_group].used_dirs++;
            ext2_meta_touch(ctx);
        }
    }
    /* write-through の約束 (戻った時点でディスクが正しい) を守れたかを返す */
    return ext2_sync(ctx);
}

/* 1 = 空、0 = 空ではない、負値 = **判定できなかった** (票 B8)。
 * 以前は読めなかったときも 0 を返していたので、rmdir が I/O エラーを
 * NOTEMPTY と名乗っていた (拒否自体は安全側だが、理由が偽になる)。 */
static int ext2_is_dir_empty(Ext2Ctx *ctx, u32 dir_ino)
{
    Ext2Inode inode;
    int ret;
    u32 bi, pos, phys;

    ret = ext2_read_inode(ctx, dir_ino, &inode);
    if (ret != 0) return ret;
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR; /* ext2_is_dir_empty */

    for (bi = 0; ; bi++) {
        ret = ext2_bmap(ctx, &inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;
        ret = ext2_read_block(ctx, phys, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;

        pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_inode  = le32_rd(&ext2_g_aux[pos]);
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF]);
            u8  de_namelen = ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF];
            if (de_reclen == 0) break;
            if (de_inode != 0) {
                if (!(de_namelen == 1 && ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF] == '.') &&
                    !(de_namelen == 2 && ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF] == '.' && ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF + 1] == '.')) {
                    return 0;
                }
            }
            pos += de_reclen;
        }
    }
    return 1;
}

int ext2_rmdir(Ext2Ctx *ctx, u32 parent_ino, const char *name)
{
    u32 ino;
    u8 ftype;
    Ext2Inode inode, parent_inode;
    int ret;
    int free_ret = EXT2_OK;
    int dropped = 0;                  /* inode を手放せた (links 0 を書けた) */

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    /* "." / ".." を名指しで消すとディレクトリの形が壊れる */
    ret = ext2_name_check(name);   /* rmdir */
    if (ret != 0) return ret;
    ret = ext2_check_writable(ctx);   /* エラー状態なら断る (票 B8 往復 5) */
    if (ret != 0) return ret;

    ret = ext2_find_entry(ctx, parent_ino, name, &ino, &ftype);
    if (ret != 0) return ret;
    if (ftype != EXT2_FT_DIR) return EXT2_ERR_NOTDIR;
    ret = ext2_is_dir_empty(ctx, ino);
    if (ret < 0) return ret;          /* 判定できなかった。NOTEMPTY と偽らない */
    if (!ret) return EXT2_ERR_NOTEMPTY;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;

    /* **空なのに links_count > 2 なら返さない** (票 B8 往復 6、ユーザー決裁)。
     *
     * 空のディレクトリの links は "." と親の中の名前の 2 本。それより多いのは、
     * 名前を持たない子 (孤児) の ".." がまだこの inode を指して数えられている
     * ときだけ — 孤児は決裁 1 のディレクトリ rename の途中や、往復 3/4 の
     * mkdir / rmdir の途中で落ちるとできる。ここで inode を返すと、孤児の ".."
     * が解放済み inode を指し、次の mkdir がその番号を受け取った時点で孤児の
     * ".." が無関係な生きたディレクトリを指す (links 不足 / dangling)。
     *
     * 整合した媒体では「空なのに links > 2」は起きないので、健全なディレクトリを
     * 断ることは無い。**エラー状態には入れない** — I/O エラーではなく、漏れ側の
     * 無害な不整合 (孤児が 1 本残っている) で、全書き込みを止めるほどではない。
     * 返すのは NOTEMPTY: 名前では見えないが、まだ子 (孤児) がこのディレクトリを
     * 親として参照している、という意味で最も近い。 */
    if (inode.links_count > EXT2_EMPTY_DIR_LINKS) {
        kprintf(KLOG_LEGACY_YELLOW, "[EXT2] rmdir refused: empty directory inode %d still has "
                      "links_count %d (orphaned subdirectory), run e2fsck\n",
                (int)ino, (int)inode.links_count);
        return EXT2_ERR_NOTEMPTY;
    }

    ret = ext2_delete_entry(ctx, parent_ino, name);
    if (ret != 0) return ret;

    /* ext2_unlink と同じ扱い (票 B8 往復 3、理由は ext2_unlink の同じ箇所):
     * 名前は既に消えているので「消えていない」とは言えない。
     *   1. links 0・dtime 付き・ポインタ 0 の inode を**先に書く**
     *      -> 書けなければ**何も返さない** (孤児として残す)
     *   2. ブロックを返す -> 返しきれなくても漏れで済む
     *   3. inode を返す (もう何も指していない)
     *   4. 親の links_count を下げる — **1 が成功したときだけ** (票 B8 往復 4)。
     *      1 が落ちると孤児のディレクトリブロックの ".." が親を指したまま
     *      なので、先に下げると「親を指す名前の数 > links_count」になる。 */
    {
        int leaked = 0;
        inode.links_count = 0;
        inode.dtime = ext2_current_time();
        ret = ext2_truncate_blocks(ctx, ino, &inode, &leaked);
        if (ret != 0) {
            free_ret = ret;
        } else {
            dropped = 1;
            free_ret = ext2_free_inode(ctx, ino);
            if (leaked) free_ret = EXT2_ERR_IO;

            ret = ext2_read_inode(ctx, parent_ino, &parent_inode);
            if (ret == 0) {
                if (parent_inode.links_count > 0) parent_inode.links_count--;
                parent_inode.mtime = ext2_current_time();
                if (ext2_write_inode(ctx, parent_ino, &parent_inode) != 0) {
                    /* 多いまま (安全側) */
                }
            }
        }
    }

    /* ディレクトリ数を減らすのは **inode を手放したとき (links 0 を書けた) だけ**
     * (票 B8 往復 6、レビュー非 blocker 4)。書けずに孤児として残したディレクトリは
     * まだディレクトリとして在るので数えたままにする。 */
    if (dropped) {
        u32 dir_group = (ino - 1) / ctx->sb_info.inodes_per_group;
        if (dir_group < ctx->num_groups) {
            ctx->gd_table[dir_group].used_dirs--;
            ext2_meta_touch(ctx);
        }
    }
    ret = ext2_sync(ctx);
    if (ret != 0) return ret;
    return free_ret;
}
