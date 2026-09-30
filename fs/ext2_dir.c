#include "ext2_priv.h"
#include "kprintf.h"

/*  ディレクトリ操作 — g_aux使用                                            */
/* ======================================================================== */

int ext2_list_dir(Ext2Ctx *ctx, u32 dir_ino, ext2_dir_callback cb, void *user_ctx)
{
    Ext2Inode inode;
    int ret;
    u32 bi, pos, phys;
    /* 走査中のディレクトリブロックはローカルに写す。コールバックの中で
     * 呼び出し側がファイルへ書く (例: `ls > file` — printf がリダイレクト先の
     * ext2_write_stream を呼ぶ) と、その経路の ext2_bmap / alloc_block が
     * 共有バッファ ext2_g_aux を上書きし、2 件目以降のエントリがビットマップ
     * の中身 (0xFF...) に化けていた (2026-09-03 実測)。 */
    u8 blk[EXT2_BLOCK_SIZE];

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    ret = ext2_read_inode(ctx, dir_ino, &inode);
    if (ret != 0) return ret;
    if (!(inode.mode & EXT2_S_IFDIR)) return EXT2_ERR_NOTDIR;

    for (bi = 0; ; bi++) {
        /* 「読めなかった」を「ここで終わり」と読み替えない (票 B8) */
        ret = ext2_bmap(ctx, &inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        ret = ext2_read_block(ctx, phys, blk);
        if (ret != 0) return EXT2_ERR_IO;

        pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_inode  = le32_rd(&blk[pos]);
            u16 de_reclen = le16_rd(&blk[pos + 4]);
            u8  de_namelen = blk[pos + 6];
            u8  de_type    = blk[pos + 7];

            if (de_reclen == 0) break;

            if (de_inode != 0 && de_namelen > 0) {
                Ext2DirEntry entry;
                int j;
                entry.inode = de_inode;
                entry.rec_len = de_reclen;
                entry.name_len = de_namelen;
                entry.file_type = de_type;
                for (j = 0; j < (int)de_namelen && j < EXT2_NAME_LEN; j++) {
                    entry.name[j] = (char)blk[pos + 8 + j];
                }
                entry.name[j] = '\0';
                cb(&entry, user_ctx);
            }
            pos += de_reclen;
        }
    }
    return EXT2_OK;
}

/* ---- 名前の検査 (票 TASK_EXT2_EMPTY_NAME) --------------------------------
 *
 * 新しく載せる名前と、名指しで消す / 付け替える名前はここを通す。断るのは
 *   - 長さ 0            … 媒体に name_len = 0 の項目ができる。OS32 の読み手は
 *                          読み飛ばすが、Linux の e2fsck は「directory
 *                          corrupted」と判定し、Linux の ext2 はルートの stat が
 *                          I/O エラーになる (cdinst の NHD、2026-09-23)
 *   - "." / ".."         … 自分 / 親を指す予約名。2 つ目の "." を作る・消すと
 *                          ディレクトリの形が壊れる
 *   - EXT2_NAME_LEN 超え … name_len は u8。256 文字は 0 に回り込む
 *   - "/" を含む         … パスの区切り。ext2 の項目としては不正
 * どれも**何も書く前に** EXT2_ERR_INVAL を返す。 */
int ext2_name_check(const char *name)
{
    int i, n;

    if (!name) return EXT2_ERR_INVAL;
    n = ext2_str_len(name);
    if (n == 0) return EXT2_ERR_INVAL;
    if (n > EXT2_NAME_LEN) return EXT2_ERR_INVAL;
    if (n == 1 && name[0] == '.') return EXT2_ERR_INVAL;
    if (n == 2 && name[0] == '.' && name[1] == '.') return EXT2_ERR_INVAL;
    for (i = 0; i < n; i++) {
        if (name[i] == '/') return EXT2_ERR_INVAL;
    }
    return EXT2_OK;
}

int ext2_find_entry_loc(Ext2Ctx *ctx, u32 dir_ino, const char *name,
                        u32 *out_ino, u8 *out_type, u32 *out_phys, u32 *out_pos)
{
    Ext2Inode inode;
    int ret, name_len;
    u32 bi, pos, phys;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    name_len = ext2_str_len(name);
    /* 長さ 0 と 255 超えの名前は媒体上に在り得ない (在れば壊れている)。
     * 比べると (u8) の回り込みや、以前の版が作った名前の無い項目に一致して
     * しまうので、探さずに「無い」と答える (票 TASK_EXT2_EMPTY_NAME)。 */
    if (name_len == 0 || name_len > EXT2_NAME_LEN) return EXT2_ERR_NOTFOUND;

    ret = ext2_read_inode(ctx, dir_ino, &inode);
    if (ret != 0) return ret;

    for (bi = 0; ; bi++) {
        /* **ここが B8 の入口**。間接ブロックが読めなかったのを「未割当 =
         * 検索終了」と混同すると、実在する名前に NOTFOUND を返す。 */
        ret = ext2_bmap(ctx, &inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        ret = ext2_read_block(ctx, phys, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;

        pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_inode  = le32_rd(&ext2_g_aux[pos]);
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + 4]);
            u8  de_namelen = ext2_g_aux[pos + 6];
            u8  de_type    = ext2_g_aux[pos + 7];

            if (de_reclen == 0) break;

            if (de_inode != 0 && de_namelen == (u8)name_len) {
                if (ext2_str_ncmp(name, (const char *)&ext2_g_aux[pos + 8], name_len) == 0) {
                    if (out_ino) *out_ino = de_inode;
                    if (out_type) *out_type = de_type;
                    if (out_phys) *out_phys = phys;
                    if (out_pos) *out_pos = pos;
                    return EXT2_OK;
                }
            }
            pos += de_reclen;
        }
    }
    return EXT2_ERR_NOTFOUND;
}

int ext2_find_entry(Ext2Ctx *ctx, u32 dir_ino, const char *name, u32 *out_ino, u8 *out_type)
{
    return ext2_find_entry_loc(ctx, dir_ino, name, out_ino, out_type,
                               (u32 *)0, (u32 *)0);
}

/* [a, b) のバイト列が 1 セクタに収まるか (= 1 回の書き込みで原子的に届くか) */
static int ext2_same_sector(u32 a, u32 b)
{
    return (a / EXT2_SECTOR_SIZE) == ((b - 1) / EXT2_SECTOR_SIZE);
}

/* ---- ディレクトリエントリの追加 (票 B8 往復 4) ----------------------------
 *
 * 守る不変条件は往復 3 と同じものの inode 側:
 *
 *     **媒体上のどの名前も、解放済みの inode や、中身の無いブロックを指さない。**
 *
 * 見える状態を変える書き込み (名前が現れる瞬間) を**最後の 1 か所**に寄せ、
 * その 1 か所が 1 セクタに収まるようにする。それより前の書き込みは、途中で
 * 落ちても名前として見えない場所 (前のエントリの rec_len の内側 = スラック、
 * まだ繋いでいないブロック) にだけ書く。
 *
 * 直す前:
 *   X1 新ブロックを既存の単一間接表へ**繋いでから**中身を書いていた。中身の
 *      書き込みが落ちると、前の持ち主のバイト列がディレクトリとして見える。
 *   X2 既存ブロックへ足すとき、前のエントリの rec_len を縮める書き込みと
 *      新しいエントリの書き込みが**別セクタ**に載り得た。後半だけ落ちると
 *      rec_len は縮んだまま、新エントリの位置にはスラックの古いバイト列
 *      (以前消したエントリ) が残り、**消した名前が復活**する。
 * ------------------------------------------------------------------------ */
int ext2_add_entry(Ext2Ctx *ctx, u32 dir_ino, const char *name, u32 ino, u8 file_type)
{
    Ext2Inode dir_inode;
    int ret, name_len;
    u32 bi, pos, phys;
    u16 new_rec_len;
    u32 now;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    /* 最後の砦。呼び手 (create / mkdir / rename) は入口で検査済みなので、
     * ここで断るのは新しい呼び手の取りこぼしだけ (何も書く前に返す) */
    ret = ext2_name_check(name);   /* add_entry */
    if (ret != 0) return ret;

    name_len = ext2_str_len(name);
    new_rec_len = (u16)((8 + name_len + 3) & ~3);

    ret = ext2_read_inode(ctx, dir_ino, &dir_inode);
    if (ret != 0) return ret;

    for (bi = 0; ; bi++) {
        /* 読めなかったまま抜けると下の「新ブロック割り当て」へ落ちて、
         * まだ空きのあるブロックを見落としたままディレクトリを伸ばす。 */
        ret = ext2_bmap(ctx, &dir_inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        ret = ext2_read_block(ctx, phys, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;

        pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_inode  = le32_rd(&ext2_g_aux[pos]);
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + 4]);
            u8  de_namelen = ext2_g_aux[pos + 6];
            u16 de_actual;

            if (de_reclen == 0) break;

            de_actual = (de_inode != 0) ? (u16)((8 + de_namelen + 3) & ~3) : 0;

            if (de_reclen - de_actual >= new_rec_len) {
                /* 見える状態を変えるフィールド (commit_off, 幅 commit_len) と、
                 * その前に書いておく新エントリの中身 [body_start, body_end) */
                u32 npos = pos + de_actual;
                u32 body_start = npos;
                u32 body_end = npos + 8 + (u32)name_len;
                u32 commit_off, commit_len;

                if (de_inode != 0) {
                    /* 分割: 前のエントリの rec_len を縮めた瞬間に新エントリが見える */
                    commit_off = pos + 4;
                    commit_len = 2;
                } else {
                    /* 空きエントリの再利用: inode 番号を入れた瞬間に見える */
                    commit_off = npos;
                    commit_len = 4;
                    body_start = npos + 4;
                }

                /* 1. 中身 (まだ見えない場所) */
                if (de_inode != 0) {
                    le32_wr(&ext2_g_aux[npos], ino);
                    le16_wr(&ext2_g_aux[npos + 4], (u16)(de_reclen - de_actual));
                } else {
                    le32_wr(&ext2_g_aux[npos], 0);   /* まだ見せない */
                }
                ext2_g_aux[npos + 6] = (u8)name_len;
                ext2_g_aux[npos + 7] = file_type;
                ext2_mem_copy(&ext2_g_aux[npos + 8], name, (u32)name_len);

                if (!ext2_same_sector(commit_off < body_start ? commit_off : body_start,
                                      body_end > commit_off + commit_len
                                          ? body_end : commit_off + commit_len)) {
                    /* 中身と見せる瞬間が別セクタに載る: 中身だけ先に書く。
                     * 落ちても見えない (スラック / inode 0 のまま) */
                    ret = ext2_write_block(ctx, phys, ext2_g_aux);
                    if (ret != 0) return EXT2_ERR_IO;
                }

                /* 2. 見せる (1 セクタに収まるフィールド 1 つ) */
                if (de_inode != 0) {
                    le16_wr(&ext2_g_aux[pos + 4], de_actual);
                } else {
                    le32_wr(&ext2_g_aux[npos], ino);
                }
                ret = ext2_write_block(ctx, phys, ext2_g_aux);
                if (ret != 0) return EXT2_ERR_IO;

                now = ext2_current_time();
                dir_inode.mtime = now;
                ext2_write_inode(ctx, dir_ino, &dir_inode);
                ext2_ns_touch(ctx);
                return EXT2_OK;
            }
            pos += de_reclen;
        }
    }

    /* 新ブロック割り当て (票 B8 往復 4 で順序を変えた)
     *   1. 新ブロックの中身を書く     … まだどこからも辿れない。落ちたら返す
     *   2. ディレクトリの size を伸ばして inode を書く
     *                                  … まだ繋いでいないので、落ちたら返す。
     *                                    届いていれば size だけ先に伸びる (末尾の
     *                                    穴)。**穴は次の追加で埋まるとは限らない** —
     *                                    次の名前が既存ブロックのスラックに収まれば
     *                                    穴は残る (往復 5 レビュー)。穴は i_size が
     *                                    大きいだけで参照を持たないので、e2fsck の
     *                                    i_size 修正で済む (漏れ側)
     *   3. 繋ぐ (ext2_bmap_set)        … 既存の間接表ならここで媒体に載る
     *   4. inode を書く                … 直接ポインタ / 新しい単一間接表への
     *                                    ポインタはここで媒体に載る
     * どの段で落ちても、繋がったブロックは中身が書けていて、size の内側にある。 */
    {
        int new_blk = ext2_alloc_block(ctx);
        u32 new_size;
        if (new_blk < 0) return new_blk;   /* NOSPC / IO をそのまま (往復 6) */

        /* alloc_block が g_aux を使った後で組む。bmap_set も g_aux を潰すので
         * 中身はその前に書き終える (gotcha §4-24) */
        ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
        le32_wr(&ext2_g_aux[0], ino);
        le16_wr(&ext2_g_aux[4], (u16)EXT2_BLOCK_SIZE);
        ext2_g_aux[6] = (u8)name_len;
        ext2_g_aux[7] = file_type;
        ext2_mem_copy(&ext2_g_aux[8], name, (u32)name_len);

        ret = ext2_write_block(ctx, (u32)new_blk, ext2_g_aux);
        if (ret != 0) {
            if (ext2_free_block(ctx, (u32)new_blk) != 0) { /* 漏れ */ }
            return EXT2_ERR_IO;
        }

        now = ext2_current_time();
        new_size = (bi + 1) * EXT2_BLOCK_SIZE;
        if (dir_inode.size < new_size) dir_inode.size = new_size;
        dir_inode.mtime = now;
        ret = ext2_write_inode(ctx, dir_ino, &dir_inode);
        if (ret != 0) {
            if (ext2_free_block(ctx, (u32)new_blk) != 0) { /* 漏れ */ }
            return EXT2_ERR_IO;
        }

        ret = ext2_bmap_set(ctx, &dir_inode, bi, (u32)new_blk);
        if (ret != 0) {
            /* ディレクトリの表は媒体上の inode から辿れる。IO なら new_blk が
             * 表に載ったかもしれないので**返さない** (漏れで止める)。
             * NOSPC ならどこにも載っていない (票 B8 往復 3)。 */
            if (ret == EXT2_ERR_NOSPC &&
                ext2_free_block(ctx, (u32)new_blk) != 0) { /* 漏れ */ }
            return ret;
        }

        dir_inode.blocks += 2;
        /* ここは**新しいブロックへの参照そのもの**を運ぶことがある (往復 3、
         * 段 C の掃引で発見)。書けなければ名前は辿れないかもしれないので
         * 成功と言わない。書けたか区別できないので、ブロックは返さない。 */
        ret = ext2_write_inode(ctx, dir_ino, &dir_inode);
        if (ret != 0) return EXT2_ERR_IO;
    }
    ext2_ns_touch(ctx);
    return EXT2_OK;
}

int ext2_delete_entry(Ext2Ctx *ctx, u32 dir_ino, const char *name)
{
    Ext2Inode dir_inode;
    int ret, name_len;
    u32 bi, pos, prev_pos, phys;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    name_len = ext2_str_len(name);
    if (name_len == 0 || name_len > EXT2_NAME_LEN) return EXT2_ERR_NOTFOUND;

    ret = ext2_read_inode(ctx, dir_ino, &dir_inode);
    if (ret != 0) return ret;

    for (bi = 0; ; bi++) {
        ret = ext2_bmap(ctx, &dir_inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        ret = ext2_read_block(ctx, phys, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;

        pos = 0; prev_pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_inode  = le32_rd(&ext2_g_aux[pos]);
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + 4]);
            u8  de_namelen = ext2_g_aux[pos + 6];

            if (de_reclen == 0) break;

            if (de_inode != 0 && de_namelen == (u8)name_len) {
                if (ext2_str_ncmp(name, (const char *)&ext2_g_aux[pos + 8], name_len) == 0) {
                    if (pos != prev_pos) {
                        u16 prev_reclen = le16_rd(&ext2_g_aux[prev_pos + 4]);
                        le16_wr(&ext2_g_aux[prev_pos + 4],
                                (u16)(prev_reclen + de_reclen));
                    }
                    /* 前のエントリへ併合する場合も**消したエントリの inode 番号を
                     * 0 にする** (票 B8 往復 4 / X2)。以前は rec_len を伸ばすだけで
                     * バイト列をスラックに残したので、後の追加が部分書き込みで
                     * 落ちると、そのバイト列が「生きた名前」として復活した
                     * (その inode 番号が別ファイルに再利用されていれば、そのファイル
                     * を別名で指し、unlink でファイルを壊す)。
                     * rec_len と inode 番号が別セクタに載っても、どちらか一方が
                     * 届けばエントリは見えなくなる。 */
                    le32_wr(&ext2_g_aux[pos], 0);
                    ret = ext2_write_block(ctx, phys, ext2_g_aux);
                    if (ret != 0) return EXT2_ERR_IO;
                    dir_inode.mtime = ext2_current_time();
                    ext2_write_inode(ctx, dir_ino, &dir_inode);
                    ext2_ns_touch(ctx);
                    return EXT2_OK;
                }
            }
            prev_pos = pos;
            pos += de_reclen;
        }
    }
    return EXT2_ERR_NOTFOUND;
}

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
    inode.mode = (u16)(EXT2_S_IFDIR | 0755);
    inode.size = EXT2_BLOCK_SIZE;
    inode.atime = now; inode.ctime = now; inode.mtime = now;
    inode.links_count = 2;
    inode.blocks = 2;
    inode.block[0] = (u32)new_blk;

    /* "." と ".." */
    ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
    pos = 0;
    le32_wr(&ext2_g_aux[pos], (u32)new_ino);
    le16_wr(&ext2_g_aux[pos + 4], 12);
    ext2_g_aux[pos + 6] = 1; ext2_g_aux[pos + 7] = EXT2_FT_DIR;
    ext2_g_aux[pos + 8] = '.';
    pos = 12;
    le32_wr(&ext2_g_aux[pos], parent_ino);
    le16_wr(&ext2_g_aux[pos + 4], (u16)(EXT2_BLOCK_SIZE - 12));
    ext2_g_aux[pos + 6] = 2; ext2_g_aux[pos + 7] = EXT2_FT_DIR;
    ext2_g_aux[pos + 8] = '.'; ext2_g_aux[pos + 9] = '.';

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

    for (bi = 0; ; bi++) {
        ret = ext2_bmap(ctx, &inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;
        ret = ext2_read_block(ctx, phys, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;

        pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_inode  = le32_rd(&ext2_g_aux[pos]);
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + 4]);
            u8  de_namelen = ext2_g_aux[pos + 6];
            if (de_reclen == 0) break;
            if (de_inode != 0) {
                if (!(de_namelen == 1 && ext2_g_aux[pos + 8] == '.') &&
                    !(de_namelen == 2 && ext2_g_aux[pos + 8] == '.' && ext2_g_aux[pos + 9] == '.')) {
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
        kprintf(0x0E, "[EXT2] rmdir refused: empty directory inode %d still has "
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

/* ======================================================================== */
/*  rename                                                                  */
/* ======================================================================== */

/* ディレクトリ dir_ino の ".." が指す inode を *out へ。
 * 戻り値 EXT2_OK = 引けた / 負値 = 引けなかった (票 B8)。
 * 以前は u32 の 0 で「無い」と「読めなかった」を兼ねていたので、
 * 呼び手の循環検査が I/O エラーを「祖先ではない」と読んでいた。 */
static int ext2_parent_of(Ext2Ctx *ctx, u32 dir_ino, u32 *out)
{
    Ext2Inode inode;
    u32 phys, pos;
    int ret;

    *out = 0;
    ret = ext2_read_inode(ctx, dir_ino, &inode);
    if (ret != 0) return ret;
    ret = ext2_bmap(ctx, &inode, 0, &phys);
    if (ret != 0) return EXT2_ERR_IO;
    if (phys == 0) return EXT2_ERR_IO;   /* ディレクトリに先頭ブロックが無い */
    if (ext2_read_block(ctx, phys, ext2_g_aux) != 0) return EXT2_ERR_IO;

    pos = 0;
    while (pos < EXT2_BLOCK_SIZE) {
        u32 de_inode   = le32_rd(&ext2_g_aux[pos]);
        u16 de_reclen  = le16_rd(&ext2_g_aux[pos + 4]);
        u8  de_namelen = ext2_g_aux[pos + 6];
        if (de_reclen == 0) break;
        if (de_inode != 0 && de_namelen == 2 &&
            ext2_g_aux[pos + 8] == '.' && ext2_g_aux[pos + 9] == '.') {
            *out = de_inode;
            return EXT2_OK;
        }
        pos += de_reclen;
    }
    /* ".." が無いディレクトリは壊れている。NOTFOUND は呼び手 (rename) から
     * 見ると「元の名前が無い」と読めてしまうので使わない。 */
    return EXT2_ERR_IO;
}

/* ディレクトリ dir_ino の ".." を new_parent に書き換える */
static int ext2_set_dotdot(Ext2Ctx *ctx, u32 dir_ino, u32 new_parent)
{
    Ext2Inode inode;
    u32 phys, pos;
    int ret;

    ret = ext2_read_inode(ctx, dir_ino, &inode);
    if (ret != 0) return ret;
    ret = ext2_bmap(ctx, &inode, 0, &phys);
    if (ret != 0) return EXT2_ERR_IO;
    if (phys == 0) return EXT2_ERR_IO;
    ret = ext2_read_block(ctx, phys, ext2_g_aux);
    if (ret != 0) return EXT2_ERR_IO;

    pos = 0;
    while (pos < EXT2_BLOCK_SIZE) {
        u32 de_inode   = le32_rd(&ext2_g_aux[pos]);
        u16 de_reclen  = le16_rd(&ext2_g_aux[pos + 4]);
        u8  de_namelen = ext2_g_aux[pos + 6];
        if (de_reclen == 0) break;
        if (de_inode != 0 && de_namelen == 2 &&
            ext2_g_aux[pos + 8] == '.' && ext2_g_aux[pos + 9] == '.') {
            le32_wr(&ext2_g_aux[pos], new_parent);
            ret = ext2_write_block(ctx, phys, ext2_g_aux);
            return (ret != 0) ? EXT2_ERR_IO : EXT2_OK;
        }
        pos += de_reclen;
    }
    return EXT2_ERR_NOTFOUND;
}

/* new_dir が ino 自身、または ino の子孫か (ディレクトリを自分の中へ移す
 * 循環を防ぐ)。".." を root まで辿る。
 *
 * 1 = 自身か子孫、0 = 違う、負値 = **判定できなかった** (票 B8)。
 * 読めなかったのを 0 (「違う」) と言うと、ディレクトリを自分の配下へ
 * 移す rename が通って木が輪になる。 */
static int ext2_is_self_or_descendant(Ext2Ctx *ctx, u32 ino, u32 new_dir)
{
    u32 cur = new_dir;
    int hops = 0;
    while (cur != 0 && hops < EXT2_RENAME_MAX_DEPTH) {
        if (cur == ino) return 1;
        if (cur == EXT2_ROOT_INO) return 0;
        {
            u32 parent = 0;
            int ret = ext2_parent_of(ctx, cur, &parent);
            if (ret != 0) return ret;
            if (parent == 0 || parent == cur) return 0;
            cur = parent;
        }
        hops++;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 *  ディレクトリの rename — **旧名を先に消す** (票 B8 往復 5 / ユーザー決裁 1)
 *
 *  往復 4 まではファイルと同じ「新名を載せてから旧名を消す」だったので、
 *  旧名の削除 (6 セクタの I/O) のどれか 1 回の失敗で**1 つのディレクトリに
 *  名前が 2 つ**残った。links は合っていても OS32 の rmdir は links を見ずに
 *  inode を返すので、残った名前が解放済み inode を指し (dangling)、2 名からは
 *  ".." しか辿らない循環検査をすり抜けてディレクトリの輪も作れた。
 *
 *  ディレクトリは「先に旧名を消し、最後に新名を載せる」。途中で落ちると
 *  **名前 0 個の孤児**になる。孤児は OS32 のどの操作からも辿れないので、後続の
 *  rmdir / mkdir / rename に化けない (ホストの e2fsck が lost+found へ回収する)。
 *  往復 3 の「破壊より漏れ」と同じ方針。
 *
 *  守る不変条件:
 *    (a) どの inode も、それを指す名前 ("." ".." を含む) の数 <= links_count
 *    (b) ディレクトリ inode を指す "." ".." 以外の名前は 1 つ以下
 *    (c) ".." を新しい親へ向ける前に新しい親の links を上げ、
 *        旧親の links は ".." が離れた後で下げる
 *    (a)-(c) は **rename の各段が書き終えた媒体について**成り立つ。
 *    孤児の ".." は旧親 (または新親) の links に数えられたまま残り、それ自体は
 *    漏れ側 (e2fsck が直す) だが、**その親が rmdir で解放され番号が再利用される
 *    と** ".." が dangling / 無関係な生きたディレクトリを指す。これは ext2_rmdir の
 *    ガード (空なのに links_count > 2 なら inode を返さない、票 B8 往復 6) が防ぐ。
 *
 *  段と、その段で落ちたときの媒体 (別の親へ移すとき。同じ親なら 2〜4 が無い):
 *    0. 新しい親の links が上限でないか読む        … 何も書いていない
 *    1. 旧名を消す (ext2_delete_entry)              … 名前 1 (届かなかった) か
 *                                                     0 (孤児)。links・".." は元
 *    2. 新しい親の links +1                          … 孤児。新親の links は元か +1
 *    3. D の ".." を新しい親へ (1 セクタの 4B = 二値) … 孤児。".." は旧親か新親、
 *                                                     どちらも数えられている
 *    4. 旧親の links -1                              … 孤児。旧親の links は元か -1
 *    5. 新名を載せる (ext2_add_entry)                … 名前 0 (孤児) か 1 (完了)。
 *                                                     NOSPC なら巻き戻す (下)
 *    6. D の ctime                                  … 名前は完成。失敗はエラー状態
 *                                                     を立てて **IO を返す** (下)
 *  D 自身の links_count は動かさない (名前は 1 -> 0 -> 1 で、多い側にしか振れない)。
 *
 *  NOSPC の巻き戻し (新名はどこにも載っていない): 旧親の links +1 -> ".." を
 *  旧親へ -> 新親の links -1 -> 旧名を載せ直す。どこで落ちても孤児で止まり、
 *  (a)(b)(c) は保たれる。旧名を消した隙間は同じ長さの名前がちょうど入るので、
 *  載せ直しは NOSPC にならない。
 * ------------------------------------------------------------------------ */
static int ext2_rename_dir(Ext2Ctx *ctx, u32 ino, u32 old_dir, const char *old_name,
                           u32 new_dir, const char *new_name)
{
    Ext2Inode pinode, dinode;
    int ret;
    int cross = (old_dir != new_dir);

    /* 0 */
    if (cross) {
        ret = ext2_read_inode(ctx, new_dir, &pinode);
        if (ret != 0) return ret;
        if (pinode.links_count >= EXT2_LINK_MAX) return EXT2_ERR_MLINK;
    }

    /* 1 */
    ret = ext2_delete_entry(ctx, old_dir, old_name);
    if (ret != 0) return ret;

    if (cross) {
        /* 2 */
        ret = ext2_read_inode(ctx, new_dir, &pinode);
        if (ret == 0) {
            pinode.links_count++;
            ret = ext2_write_inode(ctx, new_dir, &pinode);
        }
        if (ret != 0) return EXT2_ERR_IO;

        /* 3 */
        ret = ext2_set_dotdot(ctx, ino, new_dir);
        if (ret != 0) return ret;

        /* 4 */
        ret = ext2_read_inode(ctx, old_dir, &pinode);
        if (ret == 0) {
            if (pinode.links_count > 0) pinode.links_count--;
            ret = ext2_write_inode(ctx, old_dir, &pinode);
        }
        if (ret != 0) return EXT2_ERR_IO;
    }

    /* 5 */
    ret = ext2_add_entry(ctx, new_dir, new_name, ino, EXT2_FT_DIR);
    if (ret == EXT2_ERR_NOSPC) {
        /* 新名は載っていない。旧名へ戻す (落ちたら孤児で止まる) */
        if (cross) {
            if (ext2_read_inode(ctx, old_dir, &pinode) != 0) return ret;
            pinode.links_count++;
            if (ext2_write_inode(ctx, old_dir, &pinode) != 0) return ret;
            if (ext2_set_dotdot(ctx, ino, old_dir) != 0) return ret;
            if (ext2_read_inode(ctx, new_dir, &pinode) != 0) return ret;
            if (pinode.links_count > 0) pinode.links_count--;
            if (ext2_write_inode(ctx, new_dir, &pinode) != 0) return ret;
        }
        if (ext2_add_entry(ctx, old_dir, old_name, ino, EXT2_FT_DIR) != 0) {
            /* 孤児で止まる */
        }
        return ret;
    }
    if (ret != 0) return ret;

    /* 6 — 名前は完成している。**ctime を書けなければ IO を返す**
     * (票 B8 往復 6、レビュー非 blocker 3)。
     *
     * 往復 5 は「完了しているので OK」としていたが、段 5 で新しいディレクトリ
     * ブロックを割り当てた回は、エラー状態の ext2_sync が `meta_dirty` を見て
     * IO を返すので、実際には OK / IO が段 5 の中身次第で分かれていた。
     * 往復 3 で決めた「漏れや未永続を成功と言わない」契約に揃えて**常に IO**。
     * 呼び手がやり直しても二重には作れない: このセッションはエラー状態が
     * 入口で ROFS を返し、再マウント後は旧名がもう無いので NOTFOUND になる。 */
    ret = ext2_read_inode(ctx, ino, &dinode);
    if (ret == 0) {
        dinode.ctime = ext2_current_time();
        ret = ext2_write_inode(ctx, ino, &dinode);
    }
    if (ret != 0) return EXT2_ERR_IO;

    return ext2_sync(ctx);
}

/* ======================================================================== */
/*  通常ファイル同士の置き換え (票 H2 §2-2)                                  */
/* ======================================================================== */

/* 段 2 の公開は「フィールドを含むセクタ 1 本」だけを書く。**共有バッファ
 * (g_blk / g_aux / g_dat) は使わない** — 失敗したときに同じセクタを読み直して
 * 3 値を決めるので、途中で誰かに潰されると判定が壊れる。 */
static u8 ext2_g_pub[EXT2_SECTOR_SIZE];

/* 宛先エントリの inode フィールド (4 バイト、エントリ先頭) を old_ino から
 * new_ino へ書き換える。**そのフィールドを含むセクタ 1 本だけ**を書く
 * (票 H2 §2-2-1 / Codex 往復 2 所見 2)。
 *
 * エントリの位置は 4 バイト境界で、1 セクタは 512 バイトなので、この 4 バイトは
 * 必ず 1 セクタに収まる (ext2_same_sector と同じ考え方)。
 *
 * 戻り値  1 … **公開済み** (媒体上の名前が new_ino を指している)
 *         0 … **未公開** (old_ino のまま。何も壊れていない)
 *        -1 … **不明**。呼び手は D を解放してはいけない
 *
 * 「書き込みが失敗した = 未変更」とは**言えない**。1KB を 512B ずつ書く経路
 * なので、フィールドがブロックの後半にある配置では前半だけ届くこともある。
 * だから失敗したら必ず読み直す。 */
static int ext2_publish_entry(Ext2Ctx *ctx, u32 phys, u32 pos,
                              u32 old_ino, u32 new_ino)
{
    u32 sect = pos / EXT2_SECTOR_SIZE;
    u32 off  = pos % EXT2_SECTOR_SIZE;
    u32 seen;

    /* 読めなければ 1 バイトも書いていない = 未公開 */
    if (ext2_read_sector(ctx, phys, sect, ext2_g_pub) != 0) return 0;
    /* 走査した時と違うものが載っている。何も書かずに「不明」で止める */
    if (le32_rd(&ext2_g_pub[off]) != old_ino) return -1;

    le32_wr(&ext2_g_pub[off], new_ino);
    if (ext2_write_sector(ctx, phys, sect, ext2_g_pub) == 0) return 1;

    if (ext2_read_sector(ctx, phys, sect, ext2_g_pub) != 0) return -1;
    seen = le32_rd(&ext2_g_pub[off]);
    if (seen == new_ino) return 1;
    if (seen == old_ino) return 0;
    return -1;                       /* どちらでもない = 不明 */
}

/* 段 5: 置き換えられた旧 inode D の名前が 1 つ減ったことを媒体へ書く。
 * links が 0 になるなら **参照を消してから解放する** (票 B8 往復 3、
 * ext2_unlink の後半と同じ順序)。戻り値 0 = 書き切れた / -1 = 落ちた。 */
static int ext2_drop_replaced(Ext2Ctx *ctx, u32 d_ino)
{
    Ext2Inode inode;
    int ret, leaked = 0;

    ret = ext2_read_inode(ctx, d_ino, &inode);
    if (ret != 0) return -1;

    if (inode.links_count > 1) {
        inode.links_count--;
        inode.ctime = ext2_current_time();
        return ext2_write_inode(ctx, d_ino, &inode) == 0 ? 0 : -1;
    }

    inode.links_count = 0;
    inode.dtime = ext2_current_time();
    ret = ext2_truncate_blocks(ctx, d_ino, &inode, &leaked);
    if (ret != 0) return -1;         /* 媒体上の inode はまだ指している。何も返さない */
    if (ext2_free_inode(ctx, d_ino) != 0) return -1;
    return leaked ? -1 : 0;
}

/* ---- 通常ファイル同士の置き換え (票 H2 §2-2、決裁 D2 (a)) ---------------
 *
 * 旧実装は `ext2_unlink(new)` で**宛先の名前を先に消して**いたので、その後で
 * 落ちると「宛先の名前が無い」状態が残った。新しい順序は宛先エントリの
 * **inode 番号をその場で書き換える**ので、宛先の名前はどの段でも消えない。
 *
 * 不変条件は 2 つ:
 *   (i)  どの inode も「それを指す名前の数 <= links_count」
 *   (ii) 宛先の名前は常に旧 inode D か新 inode S のどちらかを指す
 *
 *   段 0  名前解決メモの世代を進める (ext2_ns_touch)
 *         — 段 2 は add/delete_entry を通らないので既存の無効化に相乗り
 *           できない (往復 2 所見 4)。**試みる前に**捨てる。
 *   段 1  S の links_count++            … 落ちても多い側
 *   段 2  **公開**: 宛先エントリの inode を含むセクタ 1 本を書く
 *   段 3  移動元の名前を消す            … 段 2 が**公開済みと確定**したときだけ
 *   段 4  S の links_count--            … **段 3 の削除が確定したときだけ**
 *         (往復 2 所見 1: 段 3 が失敗したのに減らすと「名前 2・links 1」に
 *          なり、次の掃除が一時名を消した瞬間に**生きている新内容が解放される**)
 *   段 5  D の links_count--、0 なら解放 … 段 2 が公開済みなら実行。
 *         段 3 / 4 の成否には**依存しない**
 *   段 6  ext2_sync                      … **この失敗も戻り値に含める** (往復 3 所見 5)
 *
 * 戻り値: 公開済みで段 3〜6 まで通れば EXT2_OK。公開済みだが後始末が落ちたら
 * EXT2_ERR_IO (呼び手は宛先を読み直して公開の有無を確かめる)。未公開の失敗も
 * EXT2_ERR_IO。不明は書き込み禁止にして EXT2_ERR_IO。 */
static int ext2_rename_replace(Ext2Ctx *ctx, u32 old_dir, const char *old_name,
                               u32 new_dir, const char *new_name,
                               u32 s_ino, u32 d_ino)
{
    Ext2Inode inode;
    u32 phys = 0, pos = 0, cur_ino = 0;
    u8 cur_type = 0;
    int ret, pub;
    int cleanup_failed = 0;

    /* 段 0 */
    ext2_ns_touch(ctx);

    /* 宛先エントリの位置を取り直す (走査は g_aux を使うので段 1 より前に) */
    ret = ext2_find_entry_loc(ctx, new_dir, new_name, &cur_ino, &cur_type,
                              &phys, &pos);
    if (ret != EXT2_OK) return ret;          /* 判定できないものは畳まない */
    if (cur_ino != d_ino || cur_type != EXT2_FT_REG_FILE) return EXT2_ERR_IO;

    /* 段 1 */
    ret = ext2_read_inode(ctx, s_ino, &inode);
    if (ret != 0) return ret;
    if (inode.links_count >= EXT2_LINK_MAX) return EXT2_ERR_MLINK;
    inode.links_count++;
    inode.ctime = ext2_current_time();
    ret = ext2_write_inode(ctx, s_ino, &inode);
    if (ret != 0) return EXT2_ERR_IO;

    /* 段 2 */
    pub = ext2_publish_entry(ctx, phys, pos, d_ino, s_ino);
    if (pub <= 0) {
        /* 未公開 (0) も不明 (-1) も、**D を解放しない**で止める。
         * メタデータの書き込みが落ちているので、これ以上媒体を動かさない
         * (票 B8 往復 5 の決裁と同じ扱い)。S の links が 1 多いのは
         * 多い側 = 整合する側で、e2fsck が回収する。 */
        ext2_fs_error(ctx);
        return EXT2_ERR_IO;
    }

    /* ---- ここから先は「公開の後」。宛先には検証済みの新しい内容が見えている ---- */

    /* 段 3 */
    if (ext2_delete_entry(ctx, old_dir, old_name) == EXT2_OK) {
        /* 段 4 (段 3 の確定が条件) */
        ret = ext2_read_inode(ctx, s_ino, &inode);
        if (ret == 0) {
            if (inode.links_count > 0) inode.links_count--;
            inode.ctime = ext2_current_time();
            ret = ext2_write_inode(ctx, s_ino, &inode);
        }
        if (ret != 0) cleanup_failed = 1;    /* 減らせない = 多い側に残す */
    } else {
        /* 段 4 へ進まない。名前 2・links 2 で整合している */
        cleanup_failed = 1;
    }

    /* 段 5 (段 3 / 4 の成否に依存しない) */
    if (ext2_drop_replaced(ctx, d_ino) != 0) cleanup_failed = 1;

    /* 段 6 */
    if (ext2_sync(ctx) != 0) cleanup_failed = 1;

    return cleanup_failed ? EXT2_ERR_IO : EXT2_OK;
}

int ext2_rename(Ext2Ctx *ctx, u32 old_dir, const char *old_name,
                u32 new_dir, const char *new_name)
{
    u32 ino, dst_ino;
    u8 ftype, dst_type;
    Ext2Inode inode;
    int ret;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_check_writable(ctx);   /* エラー状態なら断る (票 B8 往復 5) */
    if (ret != 0) return ret;
    ret = ext2_name_check(old_name);
    if (ret != 0) return ret;
    ret = ext2_name_check(new_name);   /* rename */
    if (ret != 0) return ret;

    ret = ext2_find_entry(ctx, old_dir, old_name, &ino, &ftype);
    if (ret != 0) return ret;

    /* 同一エントリへの rename は no-op */
    if (old_dir == new_dir && ext2_str_ncmp(old_name, new_name,
            ext2_str_len(old_name) + 1) == 0) {
        return EXT2_OK;
    }

    /* 移動先に同名がある場合: ディレクトリは上書きしない。
     * ファイル同士なら POSIX と同じく置き換える。
     *
     * **3 値で受ける** (票 B8 / Codex 実装レビュー P1-3)。読めなかったのを
     * 「無い」と読み替えると置き換えの分岐を丸ごと飛ばし、下の add_entry が
     * 宛先に同名エントリを二重に作ったうえで delete_entry が移動元の名前を
     * 消す — **名前が片方だけ消えて二重になる**(実測: 10 セクタ書き込み)。
     * 判定できないなら何も書かずに中断する。 */
    ret = ext2_find_entry(ctx, new_dir, new_name, &dst_ino, &dst_type);
    if (ret == EXT2_OK) {
        if (dst_ino == ino) {
            if (ftype == EXT2_FT_DIR) {
                /* **1 つのディレクトリに名前が 2 つ** = 媒体が壊れている
                 * (票 B8 往復 5)。往復 5 以降の rename はこの状態を作らない
                 * (旧名を先に消す) ので、ここに来るのは往復 4 以前のコードが
                 * 書いた媒体か、別の破損だけ。以前は「ハードリンク同士」として
                 * OK を返し 2 名を残していた。完了させようとはせず (".." や
                 * 親の links がどこまで進んでいたか分からない)、メタデータの
                 * 不整合としてエラー状態にして断る。 */
                ext2_fs_error(ctx);
                return EXT2_ERR_IO;
            }
            return EXT2_OK;   /* ファイルのハードリンク同士 (POSIX どおり何もしない) */
        }
        if (dst_type == EXT2_FT_DIR) return EXT2_ERR_EXIST;
        if (ftype == EXT2_FT_DIR) return EXT2_ERR_NOTDIR;
        /* **通常ファイル同士だけ**が新しい置き換え経路 (票 H2 §2-2)。
         * 種別が揃わない組み合わせ (特殊ファイル等) は従来の unlink + add の
         * ままにする — 宛先エントリの file_type を書き換えずに inode だけ
         * 差し替えると、名前の型と実体の型が食い違う。 */
        if (ftype == EXT2_FT_REG_FILE && dst_type == EXT2_FT_REG_FILE) {
            return ext2_rename_replace(ctx, old_dir, old_name,
                                       new_dir, new_name, ino, dst_ino);
        }
        ret = ext2_unlink(ctx, new_dir, new_name);
        if (ret != 0) return ret;
    } else if (ret != EXT2_ERR_NOTFOUND) {
        return ret;
    }

    if (ftype == EXT2_FT_DIR) {
        if (old_dir != new_dir) {
            int desc = ext2_is_self_or_descendant(ctx, ino, new_dir);
            if (desc < 0) return desc;      /* 判定できなかった。INVAL と偽らない */
            if (desc) return EXT2_ERR_INVAL;
        }
        return ext2_rename_dir(ctx, ino, old_dir, old_name, new_dir, new_name);
    }

    /* ---- ファイルの名前の付け替え (票 B8 往復 4 / X3、往復 5 もこのまま) ----
     *
     * 不変条件: **どの inode も、それを指す名前の数 <= links_count**。
     * ファイルのハードリンクは正当なので、失敗して名前が 2 つ残っても整合する。
     *
     * 順序と、各段で落ちたときの媒体:
     *   1. 移す inode の links_count を +1 して書く
     *        落ちた: 名前 1、links は元か +1 (多い側 = 安全)。何も付け替えていない
     *   3. 新しい名前を載せる (ext2_add_entry)
     *        落ちた: 名前 1 か 2、links は +1 済み。NOSPC なら名前は載って
     *        いないので 1 を戻す (戻せなくても多い側)
     *   4. 古い名前を消す (ext2_delete_entry)
     *        落ちた: 名前 2 か 1、links は +1 済み = 整合。**巻き戻さない**
     *   6. 移す inode の links_count を -1 して書く (ctime も)
     *        落ちた: links が 1 多い (孤児側。e2fsck が直す)
     * (段 2 / 5 は別の親へ移すディレクトリの段だったので、往復 5 で
     *  ext2_rename_dir へ移した。番号は往復 4 の記録と揃えて残している。)
     */
    /* 1 */
    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    if (inode.links_count >= EXT2_LINK_MAX) return EXT2_ERR_MLINK;
    inode.links_count++;
    inode.ctime = ext2_current_time();
    ret = ext2_write_inode(ctx, ino, &inode);
    if (ret != 0) return EXT2_ERR_IO;

    /* 3 */
    ret = ext2_add_entry(ctx, new_dir, new_name, ino, ftype);
    if (ret != 0) {
        if (ret == EXT2_ERR_NOSPC) {
            /* 名前は載っていない。上げた数を戻す (落ちても多い側) */
            if (ext2_read_inode(ctx, ino, &inode) == 0 && inode.links_count > 0) {
                inode.links_count--;
                if (ext2_write_inode(ctx, ino, &inode) != 0) { /* 多いまま */ }
            }
        }
        return ret;
    }

    /* 4 */
    ret = ext2_delete_entry(ctx, old_dir, old_name);
    if (ret != 0) return ret;

    /* 6 */
    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret == 0) {
        if (inode.links_count > 0) inode.links_count--;
        inode.ctime = ext2_current_time();
        ret = ext2_write_inode(ctx, ino, &inode);
    }
    if (ret != 0) return EXT2_ERR_IO;

    /* write-through の約束 (戻った時点でディスクが正しい) を守れたかを返す */
    return ext2_sync(ctx);
}

/* ======================================================================== */
/*  パス検索                                                                */
/* ======================================================================== */

int ext2_lookup(Ext2Ctx *ctx, const char *path, u32 *out_ino)
{
    u32 current_ino = EXT2_ROOT_INO;
    char component[EXT2_NAME_LEN + 1];
    int i, ci;
    u32 found_ino;
    u8 found_type;
    int ret;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    if (!path || path[0] == '\0') { *out_ino = EXT2_ROOT_INO; return EXT2_OK; }

    i = 0;
    if (path[0] == '/') i++;

    while (path[i] != '\0') {
        ci = 0;
        while (path[i] != '\0' && path[i] != '/' && ci < EXT2_NAME_LEN) {
            component[ci++] = path[i++];
        }
        component[ci] = '\0';
        if (ci == 0) { if (path[i] == '/') { i++; continue; } break; }

        /* **畳まない** (票 B8)。以前は I/O エラーもここで NOTFOUND に
         * なっていたので、「読めなかった」が「無い」として上へ伝わり、
         * open の O_CREAT 経路が既存ファイルを空で作り直していた。 */
        ret = ext2_find_entry(ctx, current_ino, component, &found_ino, &found_type);
        if (ret != 0) return ret;
        current_ino = found_ino;
        if (path[i] == '/') i++;
    }

    *out_ino = current_ino;
    return EXT2_OK;
}

/* ======================================================================== */
