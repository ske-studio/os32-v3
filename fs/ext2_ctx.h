/* ======================================================================== */
/*  EXT2_CTX.H — ext2ファイルシステム インスタンスコンテキスト               */
/*                                                                          */
/*  マルチインスタンス対応: 従来のグローバル変数を1構造体に集約。            */
/*  各マウントポイントごとに Ext2Ctx を kmalloc で確保する。                 */
/* ======================================================================== */

#ifndef EXT2_CTX_H
#define EXT2_CTX_H

#include "ext2.h"
#include "os32_kapi_shared.h"   /* OS32_MAX_PATH — 経路の長さの正典 */

/* 前方宣言 (dev.h の循環 include 回避) */
struct _Device;

/* マルチグループ対応: 200MBディスクで最大25グループ */
#define EXT2_MAX_GROUPS  32

/* 解決済み経路の記憶 (パス → inode 番号)。
 *
 * VFS の FD 層 (fs/vfs_fd.c) は read/write のたびに **パス文字列** を
 * FS ドライバへ渡すので、素直に書くと sys_write 1 回ごとに ext2_lookup が
 * ディレクトリを先頭から辿り直す。/tmp/e8.tar なら 1 回の書き込みにつき
 * ディレクトリ走査だけで 4 ブロック (= 8 セクタ) を読んでいた (票 S6-P)。
 *
 * 名前空間が動いた瞬間 (ext2_add_entry / ext2_delete_entry) に ns_gen が
 * 進み、記憶は全部まとめて無効になる。inode 番号が別のファイルを指すように
 * なる経路は必ずそのどちらかを通るので、世代が合う記憶は必ず現物と一致する。
 * gen == 0 は「空き」。ns_gen は 1 から始まる。 */
#define EXT2_PATH_MEMO_N  4

typedef struct {
    u32  gen;                     /* 記録時の ns_gen (0 = 空き) */
    u32  ino;
    char path[OS32_MAX_PATH];
} Ext2PathMemo;

/* ext2インスタンスコンテキスト
 * 1インスタンスあたり約1.6KB (経路の記憶が約1KB)。kmalloc で動的確保する。 */
typedef struct Ext2Ctx_tag {
    int mounted;
    int drive_num;           /* 後方互換 (format等で使用) */
    u32 base_lba;
    /* 区画の長さ (512B セクタ)。ブロック I/O はこの範囲の外を断る
     * (票 TASK_HDD_INSTALL 段 1-5 / F12: 以前は範囲検査が無く、壊れた
     * ブロック番号や大きすぎる format が区画の外を書けた)。 */
    u32 part_len;
    u32 num_groups;
    struct _Device *dev;     /* Device API ポインタ */
    Ext2Super sb_info;
    Ext2GroupDesc gd_table[EXT2_MAX_GROUPS];

    /* スーパーブロック / グループ記述子がディスクと食い違っているか。
     * 0 なら ext2_sync() は書くものが無いので I/O を出さない。 */
    int meta_dirty;

    /* **エラー状態** (票 B8 往復 5 / ユーザー決裁 2)。メタデータの読み書きで
     * I/O エラーが出たら 1 にし、以後の書き込み系操作を EXT2_ERR_ROFS で断る。
     * **このマウントの間だけ**。再マウント (新しい ctx、または ext2_mount) で 0 に
     * 戻る — 媒体の s_state には EXT2_ERROR_FS を残すが、Linux と同じく
     * 警告を出して読み書きでマウントする。 */
    int fs_error;
    /* マウント時に媒体の s_state が EXT2_ERROR_FS を持っていたか (情報用) */
    int mounted_with_errors;

    /* 名前空間の世代と解決済み経路の記憶 */
    u32 ns_gen;
    int memo_next;           /* 次に潰す記憶 (単純な巡回) */
    Ext2PathMemo memo[EXT2_PATH_MEMO_N];
} Ext2Ctx;

#endif /* EXT2_CTX_H */

