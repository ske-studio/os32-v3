/* 名前の付け替え・親の更新・置き換えの公開と回収。ext2_dir.c から取り込む。 */

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
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR; /* ext2_parent_of */
    ret = ext2_bmap(ctx, &inode, 0, &phys);
    if (ret != 0) return EXT2_ERR_IO;
    if (phys == 0) return EXT2_ERR_IO;   /* ディレクトリに先頭ブロックが無い */
    if (ext2_read_block(ctx, phys, ext2_g_aux) != 0) return EXT2_ERR_IO;

    pos = 0;
    while (pos < EXT2_BLOCK_SIZE) {
        u32 de_inode   = le32_rd(&ext2_g_aux[pos]);
        u16 de_reclen  = le16_rd(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF]);
        u8  de_namelen = ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF];
        if (de_reclen == 0) break;
        if (de_inode != 0 && de_namelen == 2 &&
            ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF] == '.' && ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF + 1] == '.') {
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
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return EXT2_ERR_NOTDIR; /* ext2_set_dotdot */
    ret = ext2_bmap(ctx, &inode, 0, &phys);
    if (ret != 0) return EXT2_ERR_IO;
    if (phys == 0) return EXT2_ERR_IO;
    ret = ext2_read_block(ctx, phys, ext2_g_aux);
    if (ret != 0) return EXT2_ERR_IO;

    pos = 0;
    while (pos < EXT2_BLOCK_SIZE) {
        u32 de_inode   = le32_rd(&ext2_g_aux[pos]);
        u16 de_reclen  = le16_rd(&ext2_g_aux[pos + EXT2_DIRENT_RECLEN_OFF]);
        u8  de_namelen = ext2_g_aux[pos + EXT2_DIRENT_NAMELEN_OFF];
        if (de_reclen == 0) break;
        if (de_inode != 0 && de_namelen == 2 &&
            ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF] == '.' && ext2_g_aux[pos + EXT2_DIRENT_NAME_OFF + 1] == '.') {
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

    /* 置き換えは add_entry を通らない。別親なら宛先の mtime も更新する。 */
    if (new_dir != old_dir) {
        ret = ext2_read_inode(ctx, new_dir, &inode);
        if (ret == 0) {
            inode.mtime = ext2_current_time(); /* replacement parent */
            ret = ext2_write_inode(ctx, new_dir, &inode);
        }
        if (ret != 0) cleanup_failed = 1; /* replacement parent */
    }

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
    int ret, is_dir;

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

    /* dirent の種別は UNKNOWN のことがある。移動手順は実体で決める。 */
    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    is_dir = (inode.mode & EXT2_S_IFMT) == EXT2_S_IFDIR;

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
            if (is_dir) {
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
        if (is_dir) return EXT2_ERR_NOTDIR;
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

    if (is_dir) {
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
