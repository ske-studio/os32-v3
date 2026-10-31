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
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR;

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
            u16 de_reclen = le16_rd(&blk[pos + EXT2_DIRENT_RECLEN_OFF]);
            u8  de_namelen = blk[pos + EXT2_DIRENT_NAMELEN_OFF];
            u8  de_type    = blk[pos + EXT2_DIRENT_TYPE_OFF];

            if (de_reclen == 0) break;

            if (de_inode != 0 && de_namelen > 0) {
                Ext2DirEntry entry;
                int j;
                entry.inode = de_inode;
                entry.rec_len = de_reclen;
                entry.name_len = de_namelen;
                entry.file_type = de_type;
                for (j = 0; j < (int)de_namelen && j < EXT2_NAME_LEN; j++) {
                    entry.name[j] = (char)blk[pos + EXT2_DIRENT_NAME_OFF + j];
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
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR; /* ext2_find_entry_loc */

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
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF]);
            u8  de_namelen = ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF];
            u8  de_type    = ext2_g_aux[pos + EXT2_DIRENT_TYPE_OFF];

            if (de_reclen == 0) break;

            if (de_inode != 0 && de_namelen == (u8)name_len) {
                if (ext2_str_ncmp(name, (const char *)&ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF], name_len) == 0) {
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
    new_rec_len = (u16)EXT2_DIRENT_SIZE(name_len);

    ret = ext2_read_inode(ctx, dir_ino, &dir_inode);
    if (ret != 0) return ret;
    if ((dir_inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR; /* ext2_add_entry */

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
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF]);
            u8  de_namelen = ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF];
            u16 de_actual;

            if (de_reclen == 0) break;

            de_actual = (de_inode != 0) ? (u16)EXT2_DIRENT_SIZE(de_namelen) : 0;

            if (de_reclen - de_actual >= new_rec_len) {
                /* 見える状態を変えるフィールド (commit_off, 幅 commit_len) と、
                 * その前に書いておく新エントリの中身 [body_start, body_end) */
                u32 npos = pos + de_actual;
                u32 body_start = npos;
                u32 body_end = npos + EXT2_DIRENT_NAME_OFF + (u32)name_len;
                u32 commit_off, commit_len;

                if (de_inode != 0) {
                    /* 分割: 前のエントリの rec_len を縮めた瞬間に新エントリが見える */
                    commit_off = pos + EXT2_DIRENT_RECLEN_OFF;
                    commit_len = sizeof(u16);
                } else {
                    /* 空きエントリの再利用: inode 番号を入れた瞬間に見える */
                    commit_off = npos;
                    commit_len = sizeof(u32);
                    body_start = npos + EXT2_DIRENT_RECLEN_OFF;
                }

                /* 1. 中身 (まだ見えない場所) */
                if (de_inode != 0) {
                    le32_wr(&ext2_g_aux[npos], ino);
                    le16_wr(&ext2_g_aux[npos + EXT2_DIRENT_RECLEN_OFF], (u16)(de_reclen - de_actual));
                } else {
                    le32_wr(&ext2_g_aux[npos], 0);   /* まだ見せない */
                }
                ext2_g_aux[npos + EXT2_DIRENT_NAMELEN_OFF] = (u8)name_len;
                ext2_g_aux[npos + EXT2_DIRENT_TYPE_OFF] = file_type;
                ext2_mem_copy(&ext2_g_aux[npos + EXT2_DIRENT_NAME_OFF], name, (u32)name_len);

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
                    le16_wr(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF], de_actual);
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
        le16_wr(&ext2_g_aux[EXT2_DIRENT_RECLEN_OFF], (u16)EXT2_BLOCK_SIZE);
        ext2_g_aux[EXT2_DIRENT_NAMELEN_OFF] = (u8)name_len;
        ext2_g_aux[EXT2_DIRENT_TYPE_OFF] = file_type;
        ext2_mem_copy(&ext2_g_aux[EXT2_DIRENT_NAME_OFF], name, (u32)name_len);

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

        dir_inode.blocks += EXT2_SECTORS_PER_BLOCK;
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
    if ((dir_inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR; /* ext2_delete_entry */

    for (bi = 0; ; bi++) {
        ret = ext2_bmap(ctx, &dir_inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        ret = ext2_read_block(ctx, phys, ext2_g_aux);
        if (ret != 0) return EXT2_ERR_IO;

        pos = 0; prev_pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_inode  = le32_rd(&ext2_g_aux[pos]);
            u16 de_reclen = le16_rd(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF]);
            u8  de_namelen = ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF];

            if (de_reclen == 0) break;

            if (de_inode != 0 && de_namelen == (u8)name_len) {
                if (ext2_str_ncmp(name, (const char *)&ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF], name_len) == 0) {
                    if (pos != prev_pos) {
                        u16 prev_reclen = le16_rd(&ext2_g_aux[prev_pos + EXT2_DIRENT_RECLEN_OFF]);
                        le16_wr(&ext2_g_aux[prev_pos + EXT2_DIRENT_RECLEN_OFF],
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

#include "ext2_dir_directory.inc"

#include "ext2_dir_rename.inc"

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
        if (path[i] == '/') {
            Ext2Inode inode;
            /* A separator requires a directory, including a trailing slash.
             * Use the inode mode; the directory entry type may be UNKNOWN. */
            ret = ext2_read_inode(ctx, current_ino, &inode);
            if (ret != 0) return ret;
            if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR; /* lookup */
            i++;
        }
    }

    *out_ino = current_ino;
    return EXT2_OK;
}

/* ======================================================================== */
