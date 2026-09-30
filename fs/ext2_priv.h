#ifndef EXT2_PRIV_H
#define EXT2_PRIV_H

#include "ext2.h"
#include "ext2_ctx.h"
#include "dev.h"
#include "vfs.h"
#include "kstring.h"
/* 媒体の上の欄 (superblock / group descriptor / inode / dirent / 間接表) は
 * すべて LE のバイト列。`*(u32 *)&buf[off]` は x86 の非アラインアクセスと
 * バイト順に寄りかかるので使わず、le16_rd / le16_wr / le32_rd / le32_wr を通す。
 * ビットマップは 1 バイトずつ触るので対象外。番人は tools/check_le_access.py。 */
#include "endian_le.h"

/* ext2独自ヘルパーを廃止し、ASM最適化済みのkstring関数に転送 */
#define ext2_mem_copy(d,s,n)  kmemcpy((d),(s),(n))
#define ext2_mem_zero(d,n)    kmemset((d),0,(n))
#define ext2_str_len(s)       ((int)kstrlen(s))
#define ext2_str_ncmp(a,b,n)  kstrncmp((a),(b),(u32)(n))

/* 1KBブロック時、1グループあたりの最大ブロック数 (ビットマップ1ブロック = 8192ビット) */
#define EXT2_BLOCKS_PER_GROUP_MAX  8192

/* 共有バッファ (シングルタスクのため全インスタンスで共有)
 * 注意: 将来マルチタスク化する場合はインスタンスごとに分離が必要 */
extern u8 ext2_g_blk[EXT2_BLOCK_SIZE];
extern u8 ext2_g_aux[EXT2_BLOCK_SIZE];
extern u8 ext2_g_dat[EXT2_BLOCK_SIZE]; /* ext2_write_stream データ読み書き専用 */

/* ======================================================================== */
/* 内部関数プロトタイプ (全関数が Ext2Ctx* を第1引数に取る)                  */
/* ======================================================================== */

/* -- ext2_super.c -- */
/* **メタデータ**のブロック I/O (superblock / group descriptor / ビットマップ /
 * inode 表 / ディレクトリブロック / 間接表)。失敗すると**マウントをエラー状態に
 * する** (ext2_fs_error)。票 B8 往復 5 / ユーザー決裁 2。
 * ファイルの中身 (データブロック) は下の _data 版を使うこと。 */
int ext2_read_block(Ext2Ctx *ctx, u32 block_num, void *buf);
int ext2_write_block(Ext2Ctx *ctx, u32 block_num, const void *buf);
/* **データブロック**の I/O。失敗しても I/O エラーを返すだけで、エラー状態には
 * しない (Linux ext2 と同じ。FS の構造は壊れていないので、以後の操作を止める
 * 理由が無い)。 */
int ext2_read_data_block(Ext2Ctx *ctx, u32 block_num, void *buf);
int ext2_write_data_block(Ext2Ctx *ctx, u32 block_num, const void *buf);
/* **セクタ 1 本だけ**の I/O (票 H2 §2-2-1)。sect は 0 か 1。エラー状態は
 * 立てない — 呼び手が読み直して 3 値を決める。 */
int ext2_read_sector(Ext2Ctx *ctx, u32 block_num, u32 sect, void *buf);
int ext2_write_sector(Ext2Ctx *ctx, u32 block_num, u32 sect, const void *buf);
/* マウントをエラー状態にする。媒体の s_state に EXT2_ERROR_FS を立てる書き込みを
 * **1 度だけ**試みる (失敗してもメモリ上の状態は立てる)。何度呼んでもよい。 */
void ext2_fs_error(Ext2Ctx *ctx);
/* 書き込み系操作の入口で呼ぶ。エラー状態なら EXT2_ERR_ROFS、そうでなければ EXT2_OK */
int ext2_check_writable(Ext2Ctx *ctx);
u32 ext2_current_time(void);
/* ext2_mem_copy/ext2_mem_zero/ext2_str_len/ext2_str_ncmp は
 * ext2_priv.h 先頭のマクロで kstring 関数に転送済み */
int ext2_write_super_raw(Ext2Ctx *ctx);
int ext2_write_gd_raw(Ext2Ctx *ctx);
/* SB / GD を動かしたら必ず呼ぶ。次の ext2_sync() が書き戻す。 */
void ext2_meta_touch(Ext2Ctx *ctx);
/* 名前空間 (ディレクトリエントリ) を動かしたら必ず呼ぶ。
 * 解決済み経路の記憶が全部無効になる。 */
void ext2_ns_touch(Ext2Ctx *ctx);
void ext2_path_memo_reset(Ext2Ctx *ctx);
int  ext2_path_memo_get(Ext2Ctx *ctx, const char *path, u32 *out_ino);
void ext2_path_memo_put(Ext2Ctx *ctx, const char *path, u32 ino);
/* 区画表 (LBA 1) の OS32 区画を探す (drivers/pc98pt.c)。CHS → LBA の幾何は
 * bootinfo_part_geom (BIOS 幾何、無ければ IDENTIFY)。戻り値 EXT2_OK /
 * EXT2_ERR_IO (ドライブが無い・読めない) / EXT2_ERR_NOPART (項目が無い・
 * 範囲が壊れている・ディスクの外)。**失敗時にどこかの LBA を仮定しない**。 */
int ext2_find_partition(int ide_drive, u32 *out_start, u32 *out_len);
/* 同じ。geom_src に使った幾何 (BOOTINFO_GEOM_BIOS / _IDENTIFY) を返す。
 * 書き込み (ext2_format) は BIOS 幾何でなければ断る (m3)。 */
int ext2_find_partition_src(int ide_drive, u32 *out_start, u32 *out_len,
                            int *geom_src);
/* IDEドライブ番号から Device* を解決 ("hd0".."hd3")。
 * ide_drive は VFS から (dev_type<<8)|dev_id 形式で渡ることがあるため
 * 下位バイトのみを使用する。未登録なら NULL。 */
Device *ext2_dev_for(int ide_drive);

/* -- ext2_inode.c -- */
int ext2_read_inode(Ext2Ctx *ctx, u32 ino, Ext2Inode *inode);
int ext2_write_inode(Ext2Ctx *ctx, u32 ino, const Ext2Inode *inode);
int ext2_alloc_block(Ext2Ctx *ctx);
/* 戻り値 EXT2_OK = 返した / 負値 = 返せなかった (票 B8 往復 3)。空き数は
 * ビットマップの書き込みが成功したときだけ動く。**このブロック (inode) を
 * 指す参照が媒体上にもう無いこと**を呼び手が先に確かめてから呼ぶ — そう
 * してあれば失敗は漏れで済む (相互リンクにはならない)。 */
int ext2_free_block(Ext2Ctx *ctx, u32 block_num);
int ext2_alloc_inode(Ext2Ctx *ctx);
int ext2_free_inode(Ext2Ctx *ctx, u32 ino);
/* 論理ブロック -> 物理ブロック。**「未割当」と「読めなかった」を戻り値で
 * 分ける** (票 B8)。以前は u32 を返し、間接ブロックの読み取り失敗を 0 =
 * 「未割当」と同じ値に潰していたので、呼び手 (ext2_find_entry 等) がそれを
 * 「ここで終わり」と読んで NOTFOUND を返していた。
 *   戻り値 EXT2_OK      … *out_phys に物理ブロック (0 = **未割当**)
 *   戻り値 EXT2_ERR_IO  … 間接ブロックが読めなかった (*out_phys = 0)
 * 番兵値ではなく引数の形を変えてある — そうすればコンパイラが全呼び出し元に
 * 判断を強制でき、「見落とした呼び手が黙って誤動作する」余地が無い。 */
int ext2_bmap(Ext2Ctx *ctx, const Ext2Inode *inode, u32 file_block,
              u32 *out_phys);
/* 失敗時: EXT2_ERR_NOSPC なら phys_block はどの表にも載っていない (返してよい)。
 * EXT2_ERR_IO なら載ったかもしれない — 媒体上の inode から辿れる表なら
 * 呼び手は phys_block を返してはいけない。詳細は定義の上のコメント。 */
int ext2_bmap_set(Ext2Ctx *ctx, Ext2Inode *inode, u32 file_block, u32 phys_block);

/* ext2_write_block は 1KB ブロックを**セクタ 0 -> セクタ 1 の順**に 2 回に分けて
 * 書く (fs/ext2_super.c)。1 回の書き込みが途中で落ちると「前半だけ届いた」
 * 状態が媒体に残る。ディレクトリの更新はこの境界を意識して順序を決める
 * (票 B8 往復 4、fs/ext2_dir.c の ext2_add_entry)。 */
#define EXT2_SECTOR_SIZE  (EXT2_BLOCK_SIZE / 2)

/* ---- ブロックの解放 (票 B8 往復 3) ----
 * 不変条件: **媒体上のどの参照も解放済みのブロックを指さない。**
 * そのため「参照を先に外して書き、その後で返す」順序だけを用意する
 * (旧 ext2_free_all_blocks は「返してから呼び手が inode を書く」順序で、
 * その間の失敗で相互リンクが起きた。取り違えを防ぐため名前ごと廃止)。 */

/* inode ino のポインタ・blocks・size を 0 にして**先に書き**、その後で
 * ブロックを返す。戻り値 EXT2_OK = 媒体上の inode はもう何も指さない
 * (*leaked = 1 なら一部を返せず漏れた) / 負値 = inode を書けず**何も返して
 * いない** (*inode のポインタは元に戻してある)。 */
int ext2_truncate_blocks(Ext2Ctx *ctx, u32 ino, Ext2Inode *inode, int *leaked);

/* 写し blocks[EXT2_N_BLOCKS] が指すブロックを返す。**写しの参照が媒体上の
 * どこからも辿れないこと**が前提。戻り値 EXT2_ERR_IO = 一部を返せず漏れた。 */
int ext2_release_blocks(Ext2Ctx *ctx, const u32 *blocks);

/* -- ext2_dir.c -- */
/* 項目の名前として載せてよいか。空・"."・".."・EXT2_NAME_LEN 超え・"/" 入りは
 * EXT2_ERR_INVAL (票 TASK_EXT2_EMPTY_NAME)。 */
int ext2_name_check(const char *name);
int ext2_list_dir(Ext2Ctx *ctx, u32 dir_ino, ext2_dir_callback cb, void *user_ctx);
int ext2_find_entry(Ext2Ctx *ctx, u32 dir_ino, const char *name, u32 *out_ino, u8 *out_type);
int ext2_add_entry(Ext2Ctx *ctx, u32 dir_ino, const char *name, u32 ino, u8 file_type);
int ext2_delete_entry(Ext2Ctx *ctx, u32 dir_ino, const char *name);
/* name が載っているディレクトリブロック (*out_phys) とブロック内の位置
 * (*out_pos) も返す ext2_find_entry (票 H2 §2-2)。呼び手がエントリの
 * inode フィールドだけを書き換えるために使う。 */
int ext2_find_entry_loc(Ext2Ctx *ctx, u32 dir_ino, const char *name,
                        u32 *out_ino, u8 *out_type, u32 *out_phys, u32 *out_pos);
/* old_dir/old_name を new_dir/new_name へ付け替える (同一 FS 内、ファイル/ディレクトリ両対応) */
int ext2_rename(Ext2Ctx *ctx, u32 old_dir, const char *old_name,
                u32 new_dir, const char *new_name);
int ext2_lookup(Ext2Ctx *ctx, const char *path, u32 *out_ino);
int ext2_mkdir(Ext2Ctx *ctx, u32 parent_ino, const char *name);
int ext2_rmdir(Ext2Ctx *ctx, u32 parent_ino, const char *name);

/* -- ext2_file.c -- */
int ext2_create(Ext2Ctx *ctx, u32 dir_ino, const char *name, const void *data, u32 size);
int ext2_write(Ext2Ctx *ctx, u32 ino, const void *data, u32 size);
int ext2_write_stream(Ext2Ctx *ctx, u32 ino, const void *buf, u32 size, u32 offset);
int ext2_get_size_ino(Ext2Ctx *ctx, u32 ino, u32 *size);
int ext2_unlink(Ext2Ctx *ctx, u32 dir_ino, const char *name);
int ext2_read_file(Ext2Ctx *ctx, u32 ino, void *buf, u32 max_size);
int ext2_read_stream(Ext2Ctx *ctx, u32 ino, void *buf, u32 size, u32 offset);

#endif
