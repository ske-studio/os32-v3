#ifndef EXT2_PRIV_H
#define EXT2_PRIV_H

#include "ext2.h"
#include "ext2_ctx.h"
#include "dev.h"
#include "../vfs.h"
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

/* 共有バッファ (シングルタスクのため全インスタンスで共有)
 * 注意: 将来マルチタスク化する場合はインスタンスごとに分離が必要 */
extern u8 ext2_g_blk[EXT2_BLOCK_SIZE];
extern u8 ext2_g_aux[EXT2_BLOCK_SIZE];
extern u8 ext2_g_dat[EXT2_BLOCK_SIZE]; /* ext2_write_stream データ読み書き専用 */

/* D6: 同期 write 1 回だけが所有する作業領域。ここでは型・宣言だけを固定し、
 * 実体と既存 write への接続は後段で追加する。D1 の読取枠とは共有しない。 */
#define EXT2_WRITE_WINDOW_BLOCKS 64u
#define EXT2_WRITE_TABLE_SLOTS 3u /* 上位 1 枚 + 単間接/下位 2 枚 */
#define EXT2_WRITE_UPPER_SLOT 0u
#define EXT2_WRITE_LOWER_FIRST 1u
#define EXT2_WRITE_RECORDS (EXT2_WRITE_WINDOW_BLOCKS + EXT2_WRITE_TABLE_SLOTS)
#define EXT2_WRITE_WORK_MAX_BYTES (40u * EXT2_BLOCK_SIZE)
#define EXT2_WRITE_NEW_INO 0u

#define EXT2_WRITE_KIND_DATA 0u
#define EXT2_WRITE_KIND_LOWER 1u
#define EXT2_WRITE_KIND_UPPER 2u
#define EXT2_WRITE_REC_NEW 0x01u       /* この窓で予約した bit */
#define EXT2_WRITE_REC_DATA_DONE 0x02u /* data 要求全体が成功 */
#define EXT2_WRITE_REC_BITMAP_DONE 0x04u /* bit が媒体上で確保済み */
#define EXT2_WRITE_REC_LINKED 0x08u    /* RAM の親へ 0 から追加済み */

typedef struct {
    u8 data[EXT2_BLOCK_SIZE];
    u32 reserved; /* 未確定の予約数。ctx の空き数はまだ減らさない */
    u8 valid;
    u8 dirty;
} Ext2WriteBitmap;

typedef struct {
    u8 data[EXT2_BLOCK_SIZE];
    u32 physical;
    u8 valid;
    u8 dirty;
} Ext2WriteTable;

/* data は先頭 64 件、表は末尾 3 件。parent == 0 は inode.block[index]、
 * それ以外は物理ブロック parent の LE32 項目 index。既存参照は上書きしない。 */
typedef struct {
    u32 logical;
    u32 physical;
    u32 parent;
    u16 index;
    u8 kind;
    u8 flags;
} Ext2WriteRecord;

typedef struct {
    u8 raw[EXT2_BLOCK_SIZE];
    u32 physical; /* 生 inode 表の物理ブロック */
    u32 offset;   /* 対象 inode の表内 byte 位置 */
    Ext2Inode current;
    Ext2Inode window_start;
    u8 valid;
    u8 dirty;
} Ext2WriteInode;

typedef struct {
    u32 group;
    u32 bit;
    u8 reserved;
    u8 bitmap_done;
    u8 published; /* inode 書込み後は abort で再利用しない */
} Ext2WriteInodeReservation;

typedef struct {
    Ext2Ctx *ctx;
    u32 ino; /* inode 予約前だけ EXT2_WRITE_NEW_INO */
    Ext2WriteBitmap block_bitmap[EXT2_MAX_GROUPS]; /* 添字が group */
    Ext2WriteBitmap inode_bitmap;
    Ext2WriteInodeReservation inode_reservation;
    Ext2WriteTable table[EXT2_WRITE_TABLE_SLOTS];
    Ext2WriteInode inode;
    Ext2WriteRecord record[EXT2_WRITE_RECORDS];
    u32 data_count;
    u32 table_count;
    u32 data_done; /* 成功した先頭 data 件数。失敗 run は一件も含めない */
    u8 active;
} Ext2WriteWork;

/* 既存の native host 模型では u32 == unsigned long が 64bit になる。
 * 製品の ILP32 配置を検査し、模型へ 32bit の配置を強制しない。 */
STATIC_ASSERT(sizeof(u32) != 4 || sizeof(Ext2WriteRecord) == 16,
              ext2_write_record_size);
STATIC_ASSERT(sizeof(u32) != 4 || sizeof(Ext2WriteWork) <= EXT2_WRITE_WORK_MAX_BYTES,
              ext2_write_work_size);

/* 後段の batch 実装が 1 個だけ定義する。begin/finish/abort で全 valid を
 * 落とす。作業中に別の ext2 操作へ協調切替しない既存 scratch の契約を継承。 */
extern Ext2WriteWork ext2_write_work;

/* -- ext2_inode_write_bitmap.inc (ext2_inode.c 内) -- */
/* 現行の群/bit 探索順。予約は保持 bitmap と reserved だけを変更する。
 * commit は各 bitmap 書込み成功後だけ ctx 空き数と meta_dirty を反映。
 * cancel はこの窓の未確定の新規 bit だけを RAM 上で取り消し、I/O はしない。 */
int ext2_write_bitmap_reserve(Ext2Ctx *ctx, u32 *out_phys);
int ext2_write_bitmap_reserve_inode(Ext2Ctx *ctx, u32 *out_ino);
void ext2_write_bitmap_cancel(Ext2Ctx *ctx, u32 physical);
int ext2_write_bitmap_commit(Ext2Ctx *ctx);

/* -- ext2_inode_write_bmap.inc (ext2_inode.c 内) -- */
/* get は未割当を *out_phys == 0 で返す。set は必要な表を予約・全 0 で準備し、
 * 親の 0 項目へ追加する。新規表も record に記録する。
 * trim は成功 data_done より後の予約/追加参照と不要な新規表だけを RAM で取消。
 * commit は bitmap 確定後に dirty な下位表→上位表だけを書く。 */
int ext2_write_bmap_get(Ext2Ctx *ctx, u32 logical, u32 *out_phys);
int ext2_write_bmap_set(Ext2Ctx *ctx, u32 logical, u32 physical);
void ext2_write_bmap_trim(Ext2Ctx *ctx);
int ext2_write_bmap_commit(Ext2Ctx *ctx);

/* -- ext2_inode_write_cache.inc (ext2_inode.c 内) -- */
/* load は生の表を一度読む。initial != NULL は create 用のデコード初期値。
 * commit は対象欄だけ encode し、隣接 inode と未知欄を保存する。 */
int ext2_write_inode_load(Ext2Ctx *ctx, u32 ino, const Ext2Inode *initial);
int ext2_write_inode_commit(Ext2Ctx *ctx);

/* -- ext2_file_write_batch.inc (ext2_file.c 内) -- */
/* initial == NULL は既存 inode、ino == EXT2_WRITE_NEW_INO は create の予約。
 * begin は D1 を全失効し、所有者を固定する。全置換は既存 truncate 成功後。
 * 以下の int 口は EXT2_OK / 負の EXT2_ERR_* (data_count は返値にしない)。 */
int ext2_write_begin(Ext2Ctx *ctx, u32 ino, const Ext2Inode *initial);
int ext2_write_map(Ext2Ctx *ctx, u32 logical, u32 *out_phys);
/* 昇順の logical を窓に追加。既存/新規とも record 先頭の data_count 件へ。
 * out_record は次の commit/finish/abort まで有効。buf のコピーは保持しない。 */
int ext2_write_reserve(Ext2Ctx *ctx, u32 logical, Ext2WriteRecord **out_record);
/* 要求全体の成功後だけ n 件前進 (累積値ではない)。失敗 run は渡さない。 */
void ext2_write_data_done(Ext2Ctx *ctx, u32 n);
/* 未完了 suffix を trim 後、data→bitmap→下位表→上位表→inode の順で公開。
 * 次窓へ進む際も clean な保持枠は残し、window_start を更新する。 */
int ext2_write_commit_window(Ext2Ctx *ctx);
/* 残りの窓/inode を排出して全保持を捨てる。従来の sync は呼び手が完了する。
 * create は finish 後に既存 add_entry→sync。正の stream 長は sync 成功後だけ。 */
int ext2_write_finish(Ext2Ctx *ctx);
/* I/O なしで全保持を捨てる。媒体上の bit/参照を逆書込み・再利用しない。
 * metadata IO 失敗後は必ずこれで閉じる。create の data 失敗では既存の
 * ext2_create_abort_unwritten→ext2_release_blocks より先に呼ぶ。 */
void ext2_write_abort(Ext2Ctx *ctx);

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
/* Physical contiguous data only: bypass D1's retained blocks, never register.
 * Validate the whole range before I/O. One Device request on valid input.
 * Success 0 / negative Device error; failed read destination is undefined,
 * failed writes may be partial. Neither entry calls ext2_fs_error. */
#define EXT2_DATA_RUN_MAX_BLOCKS 32u
int ext2_read_data_blocks(Ext2Ctx *ctx, u32 first, u32 blocks, void *dst);
int ext2_write_data_blocks(Ext2Ctx *ctx, u32 first, u32 blocks, const void *src);
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
/* 区画表 (LBA 1) の OS32 区画を探す (drivers/block/pc98pt.c)。CHS → LBA の幾何は
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

/* Fixed read slots, shared across mounts. Reset performs no I/O or clearing.
 * All writes reset before issue; raw read failure and mount lifetime reset too. */
void ext2_read_cache_reset(void);
void ext2_data_cache_reset(void);

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
 * 書く (fs/ext2/ext2_super.c)。1 回の書き込みが途中で落ちると「前半だけ届いた」
 * 状態が媒体に残る。ディレクトリの更新はこの境界を意識して順序を決める
 * (票 B8 往復 4、fs/ext2/ext2_dir.c の ext2_add_entry)。 */

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
