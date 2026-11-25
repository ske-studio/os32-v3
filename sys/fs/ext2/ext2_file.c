#include "ext2_priv.h"
#include "kprintf.h"

/* Shared range reader for file and stream entry points. */
#include "ext2_file_read_cache.inc"
#include "ext2_file_read_run.inc"

int ext2_read_file(Ext2Ctx *ctx, u32 ino, void *buf, u32 max_size)
{
    Ext2Inode inode;
    int ret;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;

    return ext2_read_inode_range(ctx, &inode, buf, max_size, 0);
}

int ext2_read_stream(Ext2Ctx *ctx, u32 ino, void *buf, u32 size, u32 offset)
{
    Ext2Inode inode;
    int ret;

    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;

    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    /* 通常ファイル以外は読まない (票 TASK_VFS_FD_PATH の最後の砦)。
     * ディレクトリのブロックをファイルの中身として返さない。 */
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;

    if (offset >= inode.size) return 0;
    return ext2_read_inode_range(ctx, &inode, buf, size, offset);
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
    int r1 = ext2_write_release_blocks(ctx, inode->block);
    int r2 = ctx->fs_error ? EXT2_ERR_IO : ext2_free_inode(ctx, ino);
    return (r1 != 0) ? r1 : r2;
}

#include "ext2_file_write_run.inc"
#include "ext2_file_write_batch.inc"

int ext2_create(Ext2Ctx *ctx, u32 dir_ino, const char *name, const void *data, u32 size)
{
    u32 new_ino, now;
    Ext2Inode inode;
    int ret;
    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_name_check(name);   /* create */
    if (ret != 0) return ret;
    ret = ext2_check_writable(ctx);
    if (ret != 0) return ret;
    {
        u32 tmp;
        ret = ext2_find_entry(ctx, dir_ino, name, &tmp, (u8 *)0);
        if (ret == EXT2_OK) return EXT2_ERR_EXIST;
        if (ret != EXT2_ERR_NOTFOUND) return ret;
    }
    now = ext2_current_time();
    ext2_mem_zero(&inode, sizeof(inode));
    inode.mode = (u16)(EXT2_S_IFREG | OS32_DEFAULT_FILE_PERM);
    inode.atime = now; inode.ctime = now; inode.mtime = now;
    inode.links_count = 1;
    ret = ext2_write_begin(ctx, EXT2_WRITE_NEW_INO, &inode);
    if (ret != 0) return ret;
    new_ino = ext2_write_work.ino;
    ret = ext2_write_batch_transfer(ctx, data, size, 0, 1);
    if (ret < 0) {
        if (ext2_write_work.active) ext2_write_build_abort(ctx, 1);
        return ret;
    }
    /* finish closed the child workspace before parent directory RMW. */
    inode.size = size;
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
            if (ext2_read_inode(ctx, new_ino, &inode) != 0) return EXT2_ERR_IO;
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
    int ret, leaked = 0;
    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_check_writable(ctx);
    if (ret != 0) return ret;
    ret = ext2_read_inode(ctx, ino, &inode);
    if (ret != 0) return ret;
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;
    inode.mtime = ext2_current_time(); inode.ctime = inode.mtime;
    /* The old references are removed before freeing or building new data. */
    ret = ext2_truncate_blocks(ctx, ino, &inode, &leaked);
    if (ret != 0) return ret;
    /* A metadata failure while returning old blocks forbids further writes. */
    if (ctx->fs_error) return EXT2_ERR_IO;
    ret = ext2_write_begin(ctx, ino, &inode);
    if (ret != 0) return ret;
    ret = ext2_write_batch_transfer(ctx, data, size, 0, 1);
    if (ret < 0) {
        if (ext2_write_work.active) ext2_write_build_abort(ctx, 0);
        return ret;
    }
    if (ext2_sync(ctx) != 0) return EXT2_ERR_IO;
    return leaked ? EXT2_ERR_IO : EXT2_OK;
}

int ext2_write_stream(Ext2Ctx *ctx, u32 ino, const void *buf, u32 size, u32 offset)
{
    int ret;
    if (!ctx->mounted) return EXT2_ERR_NOMOUNT;
    ret = ext2_check_writable(ctx);
    if (ret != 0) return ret;
    ret = ext2_write_begin(ctx, ino, (const Ext2Inode *)0);
    if (ret != 0) return ret;
    if ((ext2_write_work.inode.current.mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
        ext2_write_abort(ctx);
        return EXT2_ERR_ISDIR;
    }
    ext2_write_work.inode.current.mtime = ext2_current_time();
    ext2_write_work.inode.dirty = 1;
    ret = ext2_write_batch_transfer(ctx, buf, size, offset, 0);
    if (ret < 0) return ret;
    if (ext2_sync(ctx) != 0) return EXT2_ERR_IO;
    return ret;
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
