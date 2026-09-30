#include "ext2_priv.h"
#include "kprintf.h"

/*  ファイル読み込み — g_aux使用                                            */
/* ======================================================================== */

int ext2_read_file(Ext2Ctx *ctx, u32 ino, void *buf, u32 max_size)
{
    Ext2Inode inode;
    int ret;
    u32 bi, remaining, to_copy, total_read, phys;
    u8 *dst;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;

    remaining = inode.size;
    if (remaining > max_size) remaining = max_size;
    total_read = 0;
    dst = (u8 *)buf;

    for (bi = 0; remaining > 0; bi++) {
        /* 読めなかったのを「穴 = ファイルの終わり」にしない (票 B8)。
         * 途中までのバイト数を返すと、呼び手には短いファイルに見える。 */
        ret = ext2_bmap(ctx, &inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        to_copy = remaining;
        if (to_copy > EXT2_BLOCK_SIZE) to_copy = EXT2_BLOCK_SIZE;

        if (to_copy == EXT2_BLOCK_SIZE) {
            /* 宛先バッファに直接読み込み — ext2_g_auxを経由しない。
             * ext2_bmapが間接ブロック参照でext2_g_auxを使うため、
             * ここでext2_g_auxに読むとバッファ競合が発生する。 */
            ret = ext2_read_data_block(ctx, phys, &dst[total_read]);
            if (ret != 0) return EXT2_ERR_IO;
        } else {
            /* 端数ブロック。ext2_read_block は to_copy に関係なく **必ず
             * 1KB 書く** ので、宛先へ直接読むと max_size を超えて溢れる。
             * 呼び出し側が「ヘッダだけ」のような小さいバッファを渡すと、
             * その先のカーネル .bss を静かに壊す (2026-09-11: exec の
             * ヘッダ先読み 108 バイトが隣の resolved[] / トランポリンごと
             * 潰し、shell.bin の読み込みが NOT_FOUND になった)。
             * 中間バッファは ext2_g_blk — ext2_g_aux は上の ext2_bmap が
             * 使うので不可 (ext2_read_stream と同じ約束、gotcha §4-24)。 */
            ret = ext2_read_data_block(ctx, phys, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
            kmemcpy(&dst[total_read], ext2_g_blk, to_copy);
        }

        total_read += to_copy;
        remaining -= to_copy;
    }
    return (int)total_read;
}

int ext2_read_stream(Ext2Ctx *ctx, u32 ino, void *buf, u32 size, u32 offset)
{
    Ext2Inode inode;
    int ret;
    u32 bi, remaining, to_copy, total_read, phys;
    u32 byte_in_blk;
    u8 *dst;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    /* 通常ファイル以外は読まない (票 TASK_VFS_FD_PATH の最後の砦)。
     * ディレクトリのブロックをファイルの中身として返さない。 */
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;

    if (offset >= inode.size) return 0;
    remaining = inode.size - offset;
    if (remaining > size) remaining = size;
    
    total_read = 0;
    dst = (u8 *)buf;

    bi = offset / EXT2_BLOCK_SIZE;
    byte_in_blk = offset % EXT2_BLOCK_SIZE;

    for (; remaining > 0; bi++) {
        ret = ext2_bmap(ctx, &inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        to_copy = EXT2_BLOCK_SIZE - byte_in_blk;
        if (to_copy > remaining) to_copy = remaining;

        if (byte_in_blk == 0 && to_copy == EXT2_BLOCK_SIZE) {
            /* ブロック全体: 直接宛先に読み込み */
            ret = ext2_read_data_block(ctx, phys, &dst[total_read]);
        } else {
            /* 部分ブロック: ext2_g_blkを中間バッファとして使用
             * (ext2_g_auxはext2_bmapと競合するため使えない) */
            ret = ext2_read_data_block(ctx, phys, ext2_g_blk);
            if (ret == 0) {
                ext2_mem_copy(&dst[total_read], &ext2_g_blk[byte_in_blk], to_copy);
            }
        }
        if (ret != 0) return EXT2_ERR_IO;

        total_read += to_copy;
        remaining -= to_copy;
        byte_in_blk = 0;
    }
    return (int)total_read;
}

/* ======================================================================== */
/*  ファイル作成 / 書き込み / 削除                                          */
/* ======================================================================== */

/* ext2_create の途中失敗の後始末 (票 B8 往復 3 / Codex P1-C)。
 *
 * **inode をまだ媒体に書いていない**段階専用。新しいブロックと表は
 * メモリ上の inode からしか辿れないので、返す順序に縛りは無い。
 * 媒体上の inode (ビットを立てただけの領域) は新しいブロックを指していない。
 *
 * 戻り値 EXT2_OK = 全部返した / 負値 = 何かを返せなかった (漏れ)。
 * 呼び手はもう失敗を返す途中なので、元の失敗を優先して返す。 */
static int ext2_create_abort_unwritten(Ext2Ctx *ctx, u32 ino, const Ext2Inode *inode)
{
    int r1 = ext2_release_blocks(ctx, inode->block);
    int r2 = ext2_free_inode(ctx, ino);
    return (r1 != 0) ? r1 : r2;
}

int ext2_create(Ext2Ctx *ctx, u32 dir_ino, const char *name, const void *data, u32 size)
{
    int new_ino;
    Ext2Inode inode;
    u32 blocks_needed, bi, remaining, to_write, now;
    const u8 *src;
    int ret;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    /* 空・"."・".."・長すぎる名前は inode を割り当てる前に断る
     * (票 TASK_EXT2_EMPTY_NAME)。add_entry で断ると割り当て済みの inode が漏れる */
    ret = ext2_name_check(name);   /* create */
    if (ret != 0) return ret;
    ret = ext2_check_writable(ctx);   /* エラー状態なら断る (票 B8 往復 5) */
    if (ret != 0) return ret;

    /* 存在確認は **3 値で受ける** (票 B8 / Codex 実装レビュー P1-2)。
     * 「EXT2_OK のときだけ拒否」だと I/O エラーでも下の割当・作成へ進み、
     * 同名ファイルを二重に作る (実測: 16 セクタ書き込み)。
     * 判定できないときは**何も書かずに中断する**。 */
    {
        u32 tmp;
        ret = ext2_find_entry(ctx, dir_ino, name, &tmp, (u8 *)0);
        if (ret == EXT2_OK) return EXT2_ERR_EXIST;
        if (ret != EXT2_ERR_NOTFOUND) return ret;
    }

    new_ino = ext2_alloc_inode(ctx);
    if (new_ino < 0) return new_ino;       /* NOSPC / IO をそのまま (往復 6) */

    now = ext2_current_time();
    ext2_mem_zero(&inode, sizeof(inode));
    inode.mode = (u16)(EXT2_S_IFREG | 0644);
    inode.size = size;
    inode.atime = now; inode.ctime = now; inode.mtime = now;
    inode.links_count = 1;

    blocks_needed = (size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    src = (const u8 *)data;
    remaining = size;

    for (bi = 0; bi < blocks_needed; bi++) {
        int blk = ext2_alloc_block(ctx);
        if (blk < 0) {
            if (ext2_create_abort_unwritten(ctx, (u32)new_ino, &inode) != 0) { /* 漏れ */ }
            return blk;                    /* NOSPC / IO をそのまま (往復 6) */
        }

        ret = ext2_bmap_set(ctx, &inode, bi, (u32)blk);
        if (ret != 0) {
            /* 表はまだ媒体上の inode から辿れないので、blk は返してよい */
            if (ext2_free_block(ctx, (u32)blk) != 0) { /* 漏れ */ }
            if (ext2_create_abort_unwritten(ctx, (u32)new_ino, &inode) != 0) { /* 漏れ */ }
            return ret;
        }

        ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
        to_write = remaining;
        if (to_write > EXT2_BLOCK_SIZE) to_write = EXT2_BLOCK_SIZE;
        ext2_mem_copy(ext2_g_aux, &src[bi * EXT2_BLOCK_SIZE], to_write);

        ret = ext2_write_data_block(ctx, (u32)blk, ext2_g_aux);
        if (ret != 0) {
            if (ext2_create_abort_unwritten(ctx, (u32)new_ino, &inode) != 0) { /* 漏れ */ }
            return EXT2_ERR_IO;
        }

        inode.blocks += 2;
        remaining -= to_write;
    }

    /* ここで失敗すると、媒体上の inode に新しい内容が載ったかどうか区別
     * できない (載っていればブロックを指している)。**何も返さない** —
     * inode もブロックも使用中のまま残す (漏れ。相互リンクにはならない)。 */
    ret = ext2_write_inode(ctx, (u32)new_ino, &inode);
    if (ret != 0) return ret;

    ret = ext2_add_entry(ctx, dir_ino, name, (u32)new_ino, EXT2_FT_REG_FILE);
    if (ret != 0) {
        /* add_entry の失敗が NOSPC 以外なら、名前が媒体に載ったかどうか区別
         * できない (ディレクトリブロックの書き込みが途中で失敗し得る)。
         * 載っていれば、ここで inode を返すと**名前が解放済みの inode を指す**。
         * そのときは何も触らない — ファイルとして完成しているか、孤児として
         * 漏れるかのどちらかで、どちらも整合している。 */
        if (ret == EXT2_ERR_NOSPC) {
            /* 名前は載っていない。inode は媒体上でブロックを指しているので
             * **参照を先に外してから**返す (ext2_truncate_blocks)。外せなければ
             * 何も返さない (inode を返すと、ブロックを指す inode が再利用される)。 */
            int leaked = 0;
            inode.links_count = 0;
            inode.dtime = ext2_current_time();
            if (ext2_truncate_blocks(ctx, (u32)new_ino, &inode, &leaked) == EXT2_OK) {
                if (ext2_free_inode(ctx, (u32)new_ino) != 0) {
                    /* inode が漏れる。もう何も指していないので整合はしている */
                }
            }
        }
        return ret;
    }

    /* write-through の約束 (戻った時点でディスクが正しい) を守れたかを返す */
    return ext2_sync(ctx);
}

int ext2_write(Ext2Ctx *ctx, u32 ino, const void *data, u32 size)
{
    Ext2Inode inode;
    u32 blocks_needed, bi, remaining, to_write, now;
    const u8 *src;
    int ret;
    int leaked = 0;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_check_writable(ctx);   /* エラー状態なら断る (票 B8 往復 5) */
    if (ret != 0) return ret;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;

    now = ext2_current_time();
    inode.mtime = now; inode.ctime = now;

    /* 切り詰め (票 B8 往復 3 / Codex P1-A)。**媒体上の inode から参照を外して
     * 書いてから**旧ブロックを返す。以前は返してから最後に inode を書いて
     * いたので、その間 (旧ブロックの再読・新ブロックの割り当てと書き込み・
     * inode の書き込み) のどこで失敗しても、媒体上の inode が解放済みの
     * 旧ブロックを指したまま残り、次の割り当てで別ファイルと共有された。
     *
     * 代わりに、ここから先で失敗すると**ファイルは 0 バイトになる**
     * (旧内容は戻らない)。このファイルの書き込みは「全体の置き換え」なので、
     * 失敗を返して呼び手にやり直させる。 */
    ret = ext2_truncate_blocks(ctx, ino, &inode, &leaked);
    if (ret != 0) return ret;                  /* 何も返していない。旧内容のまま */

    inode.size = size;
    blocks_needed = (size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    src = (const u8 *)data;
    remaining = size;

    /* ここから先の失敗では、媒体上の inode は 0 本を指している (上で書いた)。
     * 新しいブロックと表はメモリ上の inode からしか辿れないので返してよい。 */
    for (bi = 0; bi < blocks_needed; bi++) {
        int blk = ext2_alloc_block(ctx);
        if (blk < 0) { ret = blk; goto fail; }   /* NOSPC / IO をそのまま (往復 6) */

        ret = ext2_bmap_set(ctx, &inode, bi, (u32)blk);
        if (ret != 0) {
            if (ext2_free_block(ctx, (u32)blk) != 0) { /* 漏れ */ }
            goto fail;
        }

        ext2_mem_zero(ext2_g_aux, EXT2_BLOCK_SIZE);
        to_write = remaining;
        if (to_write > EXT2_BLOCK_SIZE) to_write = EXT2_BLOCK_SIZE;
        ext2_mem_copy(ext2_g_aux, &src[bi * EXT2_BLOCK_SIZE], to_write);

        ret = ext2_write_data_block(ctx, (u32)blk, ext2_g_aux);
        if (ret != 0) { ret = EXT2_ERR_IO; goto fail; }

        inode.blocks += 2;
        remaining -= to_write;
    }

    /* 失敗すると新しい内容が媒体に載ったか区別できない (載っていれば新しい
     * ブロックを指している)。**何も返さない** (漏れで止める)。 */
    ret = ext2_write_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    ret = ext2_sync(ctx);
    if (ret != 0) return ret;
    /* 旧ブロックを返しきれなかった。中身は正しく書けているが、漏れを
     * 「成功」とは言わない */
    return leaked ? EXT2_ERR_IO : EXT2_OK;

fail:
    if (ext2_release_blocks(ctx, inode.block) != 0) {
        /* 漏れ。元の失敗を優先して返す */
    }
    return ret;
}

int ext2_write_stream(Ext2Ctx *ctx, u32 ino, const void *buf, u32 size, u32 offset)
{
    Ext2Inode inode;
    int ret;
    int io_err = 0;
    u32 bi, remaining, to_write, phys;
    u32 byte_in_blk, now;
    const u8 *src;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_check_writable(ctx);   /* エラー状態なら断る (票 B8 往復 5) */
    if (ret != 0) return ret;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    /* 通常ファイル以外へは書かない (票 TASK_VFS_FD_PATH 欠陥 1: 開いた FD の
     * 書き込みが、同じ名前に作り直したディレクトリのブロックを上書きした)。
     * FD の失効 (VFS) が無い経路・将来の FS の呼び手に対する最後の砦。 */
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;

    now = ext2_current_time();
    inode.mtime = now;

    src = (const u8 *)buf;
    remaining = size;

    bi = offset / EXT2_BLOCK_SIZE;
    byte_in_blk = offset % EXT2_BLOCK_SIZE;

    /* データ読み書きには ext2_g_dat を使用する。
     * ext2_g_aux は ext2_bmap / ext2_alloc_block / ext2_bmap_set が
     * 間接ブロックテーブルやビットマップの読み書きに使うため、
     * 同じバッファをデータに使うとバッファ競合が発生する。 */
    for (; remaining > 0; bi++) {
        /* **読めなかったまま「未割当」と見なして新ブロックを割り当てると、
         * 既にあった割り当てを捨てて中身を失う** (票 B8)。書き込みを止める。 */
        ret = ext2_bmap(ctx, &inode, bi, &phys);
        if (ret != 0) {
            kprintf(0x0C, "[E2W] bmap FAIL bi=%d\n", (int)bi);
            io_err = 1;
            break;
        }
        if (phys == 0) {
            int new_blk = ext2_alloc_block(ctx);
            if (new_blk < 0) {
                kprintf(0x0C, "[E2W] alloc FAIL bi=%d rem=%d\n",
                        (int)bi, (int)remaining);
                /* 満杯なら従来どおり短い書き込み。ビットマップの I/O エラーは
                 * 1 バイトも書けていなければ 0 ではなく IO にする (往復 6) */
                if (new_blk != EXT2_ERR_NOSPC) io_err = 1;
                break;
            }
            ret = ext2_bmap_set(ctx, &inode, bi, (u32)new_blk);
            if (ret != 0) {
                kprintf(0x0C, "[E2W] bmap_set FAIL bi=%d ret=%d\n",
                        (int)bi, ret);
                /* 既存ファイルの表は媒体上の inode から辿れる。IO なら
                 * new_blk が表に載ったかもしれないので**返さない** (漏れで
                 * 止める)。NOSPC ならどこにも載っていない (票 B8 往復 3)。 */
                if (ret == EXT2_ERR_NOSPC &&
                    ext2_free_block(ctx, (u32)new_blk) != 0) { /* 漏れ */ }
                io_err = 1;
                break;
            }
            inode.blocks += 2;
            phys = (u32)new_blk;
            /* 部分ブロック書き込み: 新規ブロックをゼロ初期化 */
            if (byte_in_blk > 0 || remaining < EXT2_BLOCK_SIZE) {
                ext2_mem_zero(ext2_g_dat, EXT2_BLOCK_SIZE);
            }
        } else {
            /* 既存ブロックの部分書き込み: 既存データを読み込み保持 */
            if (byte_in_blk > 0 || remaining < EXT2_BLOCK_SIZE) {
                ret = ext2_read_data_block(ctx, phys, ext2_g_dat);
                if (ret != 0) {
                    kprintf(0x0C, "[E2W] rblk FAIL bi=%d phys=%d\n",
                            (int)bi, (int)phys);
                    io_err = 1;          /* 1 バイトも書けていなければ IO (往復 5) */
                    break;
                }
            }
        }

        to_write = EXT2_BLOCK_SIZE - byte_in_blk;
        if (to_write > remaining) to_write = remaining;

        ext2_mem_copy(&ext2_g_dat[byte_in_blk], &src[size - remaining], to_write);

        ret = ext2_write_data_block(ctx, phys, ext2_g_dat);
        if (ret != 0) {
            kprintf(0x0C, "[E2W] wblk FAIL bi=%d phys=%d\n",
                    (int)bi, (int)phys);
            io_err = 1;                  /* 1 バイトも書けていなければ IO (往復 5) */
            break;
        }

        remaining -= to_write;
        byte_in_blk = 0;

        if (offset + size - remaining > inode.size) {
            inode.size = offset + size - remaining;
        }
    }

    /* 防御ログ: 書き込み不完全の場合は警告 */
    if (remaining > 0) {
        kprintf(0x0C, "[E2W] INCOMPLETE ino=%d rem=%d/%d off=%d\n",
                (int)ino, (int)remaining, (int)size, (int)offset);
    }

    /* **inode の更新と sync の失敗を捨てない** (票 B8 / Codex P1-5)。
     *
     * この関数が正の値を返すことの意味は「そのバイト数がファイルの中身として
     * 読み戻せる」である。**長さは inode にしかない**ので、inode を書けな
     * かったらその約束は成り立たない (実測: 5 バイトのファイルに 4 バイト
     * 追記 -> 戻り値 4、媒体上のサイズは 5 のまま = 追記が見えない)。
     * ext2_sync も同じ — この FS の write-through の約束は「戻った時点で
     * ディスクが正しい」ことなので、書き戻せていないなら成功と言わない。
     *
     * **部分成功を表す値は用意しない。**データブロックは既に媒体に載って
     * いるかもしれないが、それは (a) 旧サイズの内側なら中身の更新として
     * 正しく、(b) 外側なら inode から参照されない = 見えない、のどちらか。
     * 呼び手には「確認できなかった」とだけ伝え、同じ書き込みをやり直せる
     * ようにする (同じ offset へ同じ内容なので再実行は安全)。 */
    ret = ext2_write_inode(ctx, ino, &inode);
    if (ret != 0) {
        kprintf(0x0C, "[E2W] inode FAIL ino=%d\n", (int)ino);
        return EXT2_ERR_IO;
    }
    ret = ext2_sync(ctx);
    if (ret != 0) {
        kprintf(0x0C, "[E2W] sync FAIL ino=%d\n", (int)ino);
        return EXT2_ERR_IO;
    }
    /* 1 バイトも書けていないなら、0 (「書けた」) ではなくエラーを返す。
     * 途中まで書けた場合はこれまでどおり実バイト数を返す (票 B8)。 */
    if (io_err && remaining == size) return EXT2_ERR_IO;
    return (int)(size - remaining);
}

/* **ディレクトリには答えない** (票 B8 の ③)。ディレクトリの inode にも
 * size はあるので、以前はここが成功し、open の受け手がそれを「通常ファイル」
 * の根拠にできてしまった (`cat /etc` がディレクトリの生データを吐く)。
 * サイズ取得は「ファイルであること」も含めて答える。 */
int ext2_get_size_ino(Ext2Ctx *ctx, u32 ino, u32 *size)
{
    Ext2Inode inode;
    int ret;
    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    if ((inode.mode & EXT2_S_IFMT) == EXT2_S_IFDIR) return EXT2_ERR_ISDIR;
    if (size) *size = inode.size;
    return EXT2_OK;
}

int ext2_unlink(Ext2Ctx *ctx, u32 dir_ino, const char *name)
{
    u32 ino;
    u8 ftype;
    Ext2Inode inode;
    int ret;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_name_check(name);   /* unlink */
    if (ret != 0) return ret;
    ret = ext2_check_writable(ctx);   /* エラー状態なら断る (票 B8 往復 5) */
    if (ret != 0) return ret;

    ret = ext2_find_entry(ctx, dir_ino, name, &ino, &ftype);
    if (ret != 0) return ret;
    if (ftype == EXT2_FT_DIR) return EXT2_ERR_ISDIR;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;

    ret = ext2_delete_entry(ctx, dir_ino, name);
    if (ret != 0) return ret;

    inode.links_count--;
    if (inode.links_count == 0) {
        /* ここまで来ると名前は既に消えている (delete_entry 済み) ので、
         * 「消えていない」とは言えない。**返しきれなかったことだけを
         * 最後に報告する** (票 B8)。
         *
         * 順序 (往復 3): links 0・dtime 付き・ポインタ 0 の inode を**先に
         * 書き**、その後でブロックを返し、最後に inode を返す。
         *
         * inode を残すかどうか:
         *   - inode を書けなかった -> **何も返さない** (ブロックも inode も)。
         *     媒体上の inode はまだブロックを指しているかもしれないので、
         *     ブロックを返すと相互リンクに、inode を返すと指したままの inode
         *     の再利用になる。孤児として残り、e2fsck が回収する。
         *   - inode を書けた -> ブロックを返しきれなくても **inode は返す**。
         *     inode はもう何も指していないので、残しても漏れたブロックへは
         *     辿れない (往復 2 で「残す」にしたのは、返しきれなかったブロック
         *     を inode が指したままだったから。順序を変えてその理由は消えた)。 */
        int leaked = 0;
        int r;
        inode.dtime = ext2_current_time();
        ret = ext2_truncate_blocks(ctx, ino, &inode, &leaked);
        if (ret != 0) { ext2_sync(ctx); return ret; }
        r = ext2_free_inode(ctx, ino);
        ext2_sync(ctx);
        if (leaked) return EXT2_ERR_IO;
        return r;
    } else {
        /* **戻り値を捨てない** (票 H2 §2-2-3 / Codex 往復 2 所見 5)。
         * 名前はもう消えているので「消えていない」とは言えないが、
         * links_count を減らせなかったのを成功と言うと、復旧の掃除が
         * 「名前は消えたが links は 2 のまま」を errors=0 で報告する。 */
        inode.ctime = ext2_current_time();
        ret = ext2_write_inode(ctx, ino, &inode);
        if (ret != 0) {
            (void)ext2_sync(ctx);
            return EXT2_ERR_IO;
        }
    }

    /* write-through の約束 (戻った時点でディスクが正しい) を守れたかを返す。
     * ここも以前は捨てていた (票 H2 §2-2-3)。 */
    return ext2_sync(ctx);
}

/* ======================================================================== */
