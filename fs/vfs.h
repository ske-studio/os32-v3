/* ======================================================================== */
/*  VFS.H — 仮想ファイルシステム抽象化レイヤー                                */
/*                                                                          */
/*  ext2/fat等を透過的に扱うための共通インターフェース。                      */
/*  Linuxライクなコマンド体系 (ls, cat, rm, mkdir等) を実現する。            */
/*                                                                          */
/*  マルチインスタンス対応: 各FSドライバは mount() でコンテキストを返し、    */
/*  以降の全操作は void *ctx 経由でインスタンスを識別する。                  */
/* ======================================================================== */

#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "os32_kapi_shared.h"

/* ファイルタイプ */
#define VFS_TYPE_FILE  OS32_FILE_TYPE_FILE
#define VFS_TYPE_DIR   OS32_FILE_TYPE_DIR

/* VFS制限値 */
#define VFS_MAX_PATH       OS32_MAX_PATH
#define VFS_MAX_PATH_DEPTH 32
#define VFS_MAX_DEVNAME    16
#define VFS_MAX_FS         8
#define VFS_MAX_OPEN_FILES 16
#define VFS_MNTPATH_MAX    16

/* デバイス種別と、ops->mount() へ渡すデバイス指定のエンコード。
 * FS ドライバは必ず VFS_MOUNT_DEV_TYPE() で種別を確かめてから
 * VFS_MOUNT_DEV_ID() を使うこと。下位バイトだけを見ると、別種別の
 * 同一 unit 番号 (fd0 と hd0 等) を取り違えて同じ実デバイスを
 * 二重マウントする (2026-09-10 の ext2 スーパーブロック巻き戻し)。 */
#define VFS_DEV_HD      0
#define VFS_DEV_FD      1
#define VFS_DEV_SERIAL  2
#define VFS_DEV_CD      3
#define VFS_DEV_HOSTDRV 4

#define VFS_MOUNT_DEV_ENCODE(t, i) ((((t) & 0xFF) << 8) | ((i) & 0xFF))
#define VFS_MOUNT_DEV_TYPE(x)      (((x) >> 8) & 0xFF)
#define VFS_MOUNT_DEV_ID(x)        ((x) & 0xFF)

/* エラーコード */
/* 値の定義元は sdk/include/os32/os32_kapi_shared.h の OS32_ERR_* (SSoT)。
 * 外部プログラムも同じ値を見るので、ここで独自の番号を振らないこと */
#define VFS_OK          0
#define VFS_ERR_IO       OS32_ERR_IO
#define VFS_ERR_NOTFOUND OS32_ERR_NOTFOUND
#define VFS_ERR_NOMOUNT  OS32_ERR_NOMOUNT
#define VFS_ERR_NOSPC    OS32_ERR_NOSPC
#define VFS_ERR_EXIST    OS32_ERR_EXIST
#define VFS_ERR_NOTDIR   OS32_ERR_NOTDIR
#define VFS_ERR_NOTEMPTY OS32_ERR_NOTEMPTY
#define VFS_ERR_ISDIR    OS32_ERR_ISDIR
/* 書き込みを受け付けない状態 (ext2 のエラー状態、票 B8 往復 5) */
#define VFS_ERR_ROFS     OS32_ERR_ROFS
/* 資源が満杯 (ext2 の links_count 上限など) */
#define VFS_ERR_FULL     OS32_ERR_FULL
#define VFS_ERR_INVAL    OS32_ERR_INVAL
/* このバックエンドが実装していない操作 (票 H3 の set_mtime 等)。
 * 「できなかった」ではなく「持っていない」— 呼び手はエラーにせず
 * 省略したことを表示して続ける。 */
#define VFS_ERR_NOSYS    OS32_ERR_NOSYS
/* 失効した FD (unlink / 置き換え rename / umount の後、票 TASK_VFS_FD_PATH) */
#define VFS_ERR_STALE    OS32_ERR_STALE
/* パスが長すぎる / 深すぎる。切り詰めずに断る (票 TASK_VFS_FD_PATH) */
#define VFS_ERR_NAMETOOLONG OS32_ERR_NAMETOOLONG
/* 使用中 (開いている SQLite DB の rename、使用中の loop イメージ) */
#define VFS_ERR_BUSY     OS32_ERR_BUSY

/* ディレクトリエントリ (FS共通) */
typedef struct {
    char name[VFS_MAX_PATH];
    u32  size;
    u8   type;    /* VFS_TYPE_FILE or VFS_TYPE_DIR */
} VfsDirEntry;

/* ディレクトリ列挙コールバック */
typedef void (*vfs_dir_cb)(const VfsDirEntry *entry, void *ctx);

/* inode で動く口 (票 TASK_VFS_FD_PATH 方針 v3)。**任意実装** — 持つ FS
 * (ext2 のみ) の FD は open 時の inode を記録し、以後の read / write / fstat /
 * 切り詰めはパスを引き直さずこの口で行う (rename・親の rename の後も同じ実体を
 * 指す)。持たない FS (FAT / HostDrv / ISO) は従来どおりパスで動く。
 *   lookup   … パスの inode 番号 (種別を問わない)。無ければ VFS_ERR_NOTFOUND
 *   read / write / stat / truncate … inode で。通常ファイル以外は VFS_ERR_ISDIR
 * 戻り値は VFS 番号体系 (read / write は正ならバイト数)。 */
typedef struct {
    int (*lookup)(void *ctx, const char *path, u32 *ino);
    int (*read)(void *ctx, u32 ino, void *buf, u32 size, u32 offset);
    int (*write)(void *ctx, u32 ino, const void *buf, u32 size, u32 offset);
    int (*stat)(void *ctx, u32 ino, OS32_Stat *buf);
    int (*truncate)(void *ctx, u32 ino);
} VfsInoOps;

/* FS操作テーブル (各FSドライバが実装)
 *
 * マルチインスタンス対応:
 *   mount()  → void* を返す (FSドライバ固有のコンテキスト)
 *   以降の全関数 → void *ctx を第1引数で受け取る
 *   umount() → コンテキストを解放する
 */
typedef struct {
    const char *name;                /* "ext2", "fat" */

    /* マウント/アンマウント */
    void *(*mount)(int dev_id);      /* 成功: コンテキストptr, 失敗: NULL */
    void (*umount)(void *ctx);
    int  (*is_mounted)(void *ctx);

    /* ディレクトリ操作 (パス文字列ベース) */
    int  (*list_dir)(void *ctx, const char *path, vfs_dir_cb cb, void *user_ctx);
    int  (*mkdir)(void *ctx, const char *path);
    int  (*rmdir)(void *ctx, const char *path);

    /* ファイル操作 (パス文字列ベース) */
    int  (*read_file)(void *ctx, const char *path, void *buf, u32 max_size);
    int  (*write_file)(void *ctx, const char *path, const void *data, u32 size);
    int  (*unlink)(void *ctx, const char *path);
    int  (*rename)(void *ctx, const char *oldpath, const char *newpath);

    /* ストリーム操作 (シーク・部分読み書き対応用) */
    int  (*get_file_size)(void *ctx, const char *path, u32 *size);
    int  (*read_stream)(void *ctx, const char *path, void *buf, u32 size, u32 offset);
    int  (*write_stream)(void *ctx, const char *path, const void *buf, u32 size, u32 offset);

    /* メタデータ */
    int  (*sync)(void *ctx);

    /* ファイルシステム情報 */
    u32  (*total_blocks)(void *ctx);
    u32  (*free_blocks)(void *ctx);
    u32  (*block_size)(void *ctx);

    /* 追加: ファイル属性・状態 */
    int  (*stat)(void *ctx, const char *path, OS32_Stat *buf);

    /* 更新日時の設定 (票 H3)。**任意実装** — 埋めない FS ドライバは
     * ここが NULL のままになり (C89 の集成体初期化で残りはゼロ)、
     * vfs_set_mtime が VFS_ERR_NOSYS を返す。実装済みは ext2 だけ。
     * mtime は UNIX Epoch 秒 (UTC)。0 は「不明」の印なので受け付けない。 */
    int  (*set_mtime)(void *ctx, const char *path, os_time_t mtime);

    /* 排他的作成 (票 H2 §2-1、KAPI v53 の O_EXCL)。**任意実装** —
     * 埋めない FS ドライバはここが NULL のままになり (C89 の集成体初期化で
     * 残りはゼロ)、vfs_open が O_EXCL に VFS_ERR_NOSYS を返す。実装済みは
     * ext2 だけ。
     *
     * 契約: 「無いことの確認 → 作成」を **1 回の呼び出しの中で** 行う。
     *   VFS_OK          … 作った (長さ 0 の通常ファイル)
     *   VFS_ERR_EXIST   … その名前が既に在る (**種別を問わない** —
     *                     ディレクトリでも ISDIR ではなく EXIST)
     *   その他の負値     … 在るかどうかを判定できなかった。**「無い」と
     *                     読み替えず**その値をそのまま返す (票 B8)
     * 排他性の根拠は VFS が非再入でゲストが協調型であること。**ホスト側が
     * 同時に書ける FS では成り立たない**ので HostDrv / FAT / ISO9660 は
     * 実装しない。 */
    int  (*create_excl)(void *ctx, const char *path);

    /* inode で動く口 (上の VfsInoOps)。**任意実装** — NULL の FS は FD が
     * パスで動く。実装済みは ext2 だけ。 */
    const VfsInoOps *ino;

    /* 名前の比較規則 (Codex 実装レビュー ラリー 1 の B4)。**任意実装** —
     * NULL は「バイトごとに区別する」(ext2)。大文字小文字を区別しない FS
     * (FAT / HostDrv / ISO9660) は、名前の 1 バイトをその FS が比較に使う形へ
     * 畳む関数を置く。VFS は開いている SQLite DB の rename (BUSY) と使用中の
     * loop イメージ (pinned) の名前比較をこれに従わせる — バイト比較のままだと
     * FAT で `DB` と `db` が別名に見え、同じ実体の付け替え・削除をすり抜ける。 */
    u8 (*name_fold)(u8 c);
} VfsOps;

/* ---- VFS API ---- */

/* ファイルシステム実装の登録 */
void vfs_register_fs(VfsOps *ops);

/* デバイス名 → デバイスID変換 ("hd0"→0, "fd0"→0等) */
int vfs_dev_parse(const char *name, int *dev_type, int *dev_id);

/* マウント/アンマウント */
int  vfs_mount(const char *prefix, const char *dev_name, const char *fstype);
void vfs_umount(const char *prefix);
/* 戻り値つきのアンマウント (KAPI sys_umount_checked、v64、票 TASK_HDD_INSTALL
 * §1-v3 N6)。**先に FS の sync を呼び、失敗したらアンマウントしない**。
 *   VFS_OK           … 書き戻してアンマウントした
 *   VFS_ERR_BUSY     … prefix が "/" (ルートは外さない)
 *   VFS_ERR_NOTFOUND … その prefix はマウントされていない
 *   VFS_ERR_INVAL    … prefix が NULL
 *   sync の負値      … 書き戻せなかった (マウントは残る)
 * 既存の vfs_umount (void、sync の失敗を捨てる) は変えない ([ABI2])。 */
int  vfs_umount_checked(const char *prefix);
/* IDE ドライブ drive (hd<drive>) を指すマウントの数 (どの prefix でも、ルートも
 * 数える)。drive が 0〜3 の外なら VFS_ERR_INVAL。KAPI dev_mount_count (v64)。 */
int  vfs_dev_mount_count(int drive);
int  vfs_is_mounted(const char *prefix);
const char *vfs_fstype(const char *prefix);
const char *vfs_devname(const char *prefix);

/* ディレクトリ操作 */
int  vfs_ls(const char *path, vfs_dir_cb cb, void *ctx);
int  vfs_mkdir(const char *path);
int  vfs_rmdir(const char *path);

/* ファイル操作 (パスベース・一括処理) */
int  vfs_read(const char *path, void *buf, u32 max_size);
int  vfs_write(const char *path, const void *data, u32 size);
int  vfs_rm(const char *path);
int  vfs_rename(const char *oldpath, const char *newpath);

/* ストリーム操作 (ファイルディスクリプタ・シーク対応) */
int  vfs_open(const char *path, int mode);
void vfs_close(int fd);
/* 指定所有者 (res_owner_get() の値で open 時にタグ付け) の FD を一括クローズ。
 * vfs_fd_set_protect で保護された FD は対象外。exec_exit の安全網用 */
void vfs_close_owned(int owner);
int  vfs_read_fd(int fd, void *buf, u32 size);
int  vfs_write_fd(int fd, const void *buf, u32 size);
int  vfs_seek(int fd, int offset, int whence);
int  vfs_tell(int fd);
u32  vfs_get_size(int fd);
int  vfs_isatty(int fd);
int  vfs_fd_set_protect(int fd, int on);
int  vfs_fd_is_protected(int fd);

/* Kernel-internal FD leases; not a KAPI/SDK interface. */
#define VFS_FD_GENERATION_MAX 0xffffffffUL
#define VFS_FD_GENERIC 0
#define VFS_FD_SQLITE  1
typedef struct {
    int group_index;
    u32 generation;
} VfsSqliteCookie;
typedef struct {
    int fd;
    u32 generation;
    VfsSqliteCookie cookie;
} VfsSqliteLease;
int vfs_open_sqlite(const char *path, int mode, int owner,
                    const VfsSqliteCookie *cookie, int sqlite_flags,
                    VfsSqliteLease *out);
/* open returns VFS_OK and fills out only on success; owner >= 0,
 * cookie index >= 0 and generation != 0. The caller must validate group
 * state/identity (including rejecting future opens after quarantine).
 * FD generations never wrap, even across GENERIC reuse. No owner mutation.
 * All functions require the existing non-reentrant VFS calling discipline. */

/* Validation is read-only and performs no I/O. Close verifies the lease,
 * rejects quarantine and releases only the table entry (no backend close).
 * Neither function modifies the caller's lease. Failures return INVAL. */
int vfs_close_sqlite(const VfsSqliteLease *lease);
int vfs_validate_sqlite(const VfsSqliteLease *lease);
/* Count includes quarantined live members. Quarantine marks only matching
 * live members, is sticky/idempotent, and returns OK; NULL returns INVAL.
 * No group registry, group generation allocator or quarantine reset here. */
int vfs_count_sqlite(const VfsSqliteCookie *cookie);
int vfs_quarantine_sqlite(const VfsSqliteCookie *cookie);

/* ファイル情報 */
/* stat が返す st_dev。FS ドライバは埋めないので VFS が正典
 * (VFS_MOUNT_DEV_ENCODE() + 1、未マウントは 0)。同一ファイル判定で
 * (st_dev, st_ino) の組を使う側が、別デバイスの同じ inode 番号を
 * 取り違えないようにするための値 */
u32  vfs_path_dev(const char *path);
int  vfs_stat(const char *path, OS32_Stat *buf);
int  vfs_fstat(int fd, OS32_Stat *buf);
/* 更新日時の設定 (票 H3)。KAPI スロット sys_set_mtime の実体。
 *   VFS_OK          … 設定した
 *   VFS_ERR_NOSYS   … その FS は set_mtime を持たない (エラーにしない)
 *   VFS_ERR_INVAL   … path が NULL / mtime == 0 (不明の印)
 *   VFS_ERR_NOMOUNT … 該当マウントなし
 *   VFS_ERR_NOTFOUND / VFS_ERR_IO … FS 側の失敗
 * **データを書き終えてから**呼ぶこと (書き込みは mtime を現在時刻で
 * 上書きするので、先に設定すると消える、設計書 §5.2)。 */
int  vfs_set_mtime(const char *path, os_time_t mtime);
/* メタデータ */
int  vfs_sync(void);

/* ファイルシステム情報 */
u32 vfs_total_blocks(void);
u32 vfs_free_blocks(void);
u32 vfs_block_size(void);

/* カレントディレクトリ */
const char *vfs_cwd(void);
int vfs_chdir(const char *path);   /* 存在するディレクトリ以外は VFS_ERR_NOTFOUND / VFS_ERR_NOTDIR */

/* パスの種別判定。戻り値: VFS_KIND_DIR / VFS_KIND_FILE、負値は VFS_ERR_* */
#define VFS_KIND_FILE 0
#define VFS_KIND_DIR  1
int vfs_path_kind(const char *path);

/* パスの正規化 (相対→絶対)。**切り詰めずに断る** (票 TASK_VFS_FD_PATH):
 *   VFS_OK              … output に絶対名 (NUL 込み out_size 以内)
 *   VFS_ERR_NAMETOOLONG … 入力が NUL 抜き VFS_MAX_PATH - 1 を超える /
 *                         正規化の途中で要素が VFS_MAX_PATH_DEPTH を超える /
 *                         要素 1 つが 255 バイトを超える / 結果が out_size に
 *                         収まらない
 *   VFS_ERR_INVAL       … output が NULL / out_size <= 0
 * 相対パスは cwd + "/" + 入力を大きい一時領域で正規化してから判定する。
 * 失敗したとき output は空文字列。 */
int vfs_resolve_path(const char *input, char *output, int out_size);

/* ---- vfs.c と vfs_fd.c の間だけで使う (KAPI ではない) ----
 * FD 表は vfs_fd.c が持つので、名前空間を変える vfs.c がここを呼ぶ。 */
/* (fs_ctx, ino) の開いた FD に失効の印を付ける */
void vfs_fd_invalidate_ino(void *fs_ctx, u32 ino);
/* そのマウントの全 FD に失効の印を付け、fs_ctx を外す (umount の前に呼ぶ) */
void vfs_fd_invalidate_mount(void *fs_ctx);
/* rel_old / rel_new (同じマウントの相対名、rel_new は NULL 可) の rename が
 * 開いている SQLite DB・そのジャーナル・それらの祖先に当たるなら 1 */
int  vfs_fd_rename_busy(void *fs_ctx, const char *rel_old, const char *rel_new);
/* (fs_ctx, ino) か (inode を持たない FS なら) rel が使用中の固定 FD
 * (loop イメージ) に当たるなら 1。has_ino = 0 なら rel で比べる */
int  vfs_fd_pinned_busy(void *fs_ctx, int has_ino, u32 ino, const char *rel);
/* 失効の問い合わせ (常駐 SQLite 接続の開き直し判断用)。1 = 失効 */
int  vfs_fd_is_stale(int fd);
/* 使用中の印 (loop_dev が付ける)。印の付いた実体の unlink と置き換えは BUSY */
int  vfs_fd_set_pinned(int fd, int on);
/* SQLite の DB / ジャーナルとして開いた印 (旧来の vfs_open 経路用)。
 * vfs_open_sqlite の FD には最初から付いている */
int  vfs_fd_set_sqlite_db(int fd);

/* 内部ルーターの公開 (vfs_fd.c向け) */
VfsOps *vfs_route(const char *path, char *rel_out, int max_rel, void **ctx_out);

/* レガシー互換ラッパー */
void vfs_sys_compat_shell_print(const char *s, u8 attr);

#endif /* VFS_H */
