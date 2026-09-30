/* ======================================================================== */
/*  FD_REDIRECT.H -- FD 0/1/2 リダイレクト管理                               */
/*                                                                          */
/*  標準入出力 (stdin/stdout/stderr) をファイルやメモリバッファに             */
/*  リダイレクトするためのカーネルモジュール。                                */
/*  シェルからの > / >> / < / 2> 構文、およびパイプ (|) を支える基盤。        */
/* ======================================================================== */

#ifndef FD_REDIRECT_H
#define FD_REDIRECT_H

#include "types.h"
#include "os32_kapi_shared.h"   /* OS_S_IF* (fd_redirect_ifmt の戻り値) */

/* リダイレクトターゲットの種類 */
#define FD_TARGET_CONSOLE  0   /* デフォルト: コンソール (TTY) */
#define FD_TARGET_FILE     1   /* ファイルに接続 */
#define FD_TARGET_BUFFER   2   /* メモリバッファに接続 (パイプ用) */

/* リダイレクトモード */
#define FD_REDIR_READ      0   /* 読み込み (stdin用) */
#define FD_REDIR_WRITE     1   /* 書き込み・上書き */
#define FD_REDIR_APPEND    2   /* 書き込み・追記 */

/* リダイレクト状態管理構造体 */
typedef struct {
    int target_type;        /* FD_TARGET_* */
    int file_fd;            /* FD_TARGET_FILE の場合、実ファイルのFD番号 */
    u8 *buffer;             /* FD_TARGET_BUFFER の場合のバッファポインタ */
    u32 buf_capacity;       /* バッファ容量 */
    u32 buf_pos;            /* 現在の読み書き位置 */
    u32 buf_len;            /* バッファ内の有効データ長 */
    int owner;              /* 設定した実行レベル (res_owner_get() の値) */
    int user_origin;        /* 1 = バッファを CPL=3 のアプリが登録した
                             *     (登録時の ring3_call_from_user())。
                             *     書く前の検査を**文脈に関係なく**行う印
                             *     (fd_redirect_buf_write_ok)。 */
} FdRedirect;

/* ======== リソース所有者タグ ======== */
/*
 * exec のネスト深度 (0=カーネル, 1=シェル, 2+=外部プログラム) を「現在の
 * 所有者」として保持する。リダイレクト / パイプバッファ / open FD は確保時に
 * この値でタグ付けされ、exec_exit は **終了するレベルが確保したものだけ**
 * を回収する。
 *
 * 以前は exec_exit が全リダイレクト・全パイプ・全 FD を無条件に回収して
 * いたため、シェルが張ったパイプライン (cmd1 | cmd2) の 1 段目 (外部
 * プログラム) が終了した時点でシェルのパイプバッファが kfree され、2 段目
 * が stdin をキーボードから読んでハングしていた (2026-09-03 実測)。
 */
void res_owner_set(int owner);
int  res_owner_get(void);

/* ======== API ======== */

/* 初期化 (全FDをコンソールモードにリセット) */
void fd_redirect_init(void);

/* FD 0/1/2 をファイルにリダイレクト
 *   fd:   対象FD (0=stdin, 1=stdout, 2=stderr)
 *   path: リダイレクト先ファイルパス
 *   mode: FD_REDIR_READ / FD_REDIR_WRITE / FD_REDIR_APPEND
 * 戻り値: 0=成功, 負=エラー */
int fd_redirect_to_file(int fd, const char *path, int mode);

/* FD 0/1/2 をメモリバッファにリダイレクト (パイプ用)
 *   fd:   対象FD
 *   buf:  バッファポインタ
 *   size: バッファ容量
 *   len:  初期データ長 (読み込み用: バッファに既にあるデータ長)
 * 戻り値: 0=成功, 負=エラー
 * 呼び手が CPL=3 のアプリ (ring3_call_from_user() が 1) なら、[buf, buf+size)
 * が present + RW + USER であることを表を歩いて確かめ、だめなら -1 (表は
 * 変えない)。KAPI ラッパの出力検査と二重の守り。登録の由来は user_origin に
 * 残す。**カーネル内部からカーネル帯のバッファを張る用途には使えない**
 * (アプリの syscall の中で呼ぶと由来がアプリになる)。いまの呼び手は
 * KAPI の sys_redirect_fd_buf だけ。 */
int fd_redirect_to_buffer(int fd, u8 *buf, u32 size, u32 len);

/* バッファ型のリダイレクト r へ、いま to_write バイト書いてよいか。
 *   1 = 書いてよい / 0 = 書かずに畳む (fd_redirect_write が ring3_fault_kill)。
 * **門はポインタの由来で決める** (2026-09-26、代行レビュー P2):
 *   - r->user_origin (アプリが登録した) → ring3_user_ranges_writable_always で
 *     **必ず**表を歩く。WM (gshell) の文脈 (ring3_wm_depth >= 1) で書かれても
 *     素通しにしない — アプリが fd 1 を RO ページへ向けてから gui_call し、
 *     WM が fd 1 へ書くと、CR0.WP=0 のカーネルがアプリ指定の番地へ書くため。
 *   - それ以外 (常駐シェル / WM / --cpl0 が登録) → 従来どおり、ユーザ帯の
 *     番地だけを文脈つきの門 (ring3_user_ranges_writable) で見る。
 * 書き込みはしない (kselftest が kill せずに叩けるように分けてある)。 */
int fd_redirect_buf_write_ok(const FdRedirect *r, u32 to_write);

/* FD 0/1/2 のリダイレクトを解除 (コンソールモードに戻す)
 * ファイルリダイレクト中の場合、ファイルを自動でクローズする */
void fd_redirect_reset(int fd);

/* 指定所有者が設定したリダイレクトだけを解除する (exec_exit の安全網用) */
void fd_redirect_reset_owned(int owner);

/* ======== fd 0/1/2 の種別 ([C4] ここが唯一の管理元) ======== */
/*
 * 「その fd はいま**何に**繋がっているか」を答えるのはこの関数だけ。
 * `vfs_isatty` / `vfs_fstat` / `fd_is_redirected` は 3 つともここから導く。
 *
 * 2026-09-17 まで `vfs_isatty` は fd_is_redirected() を見て、`vfs_fstat` は
 * fd 0/1/2 を**無条件で S_IFCHR** と答えていた。対話で叩く限りどちらも
 * 正しく見えるが、`stat_t > file` のように出力を向け直すと**同じ fd に
 * ついて 2 つの API が食い違う** (票 docs/archive/test/TASK_FSTAT_REDIR.md)。
 * 判定が 2 か所にあると必ずまた割れるので、引ける場所を 1 つにする。
 *
 *   戻り値      OS_S_IFCHR  コンソール (端末)
 *               OS_S_IFREG  ファイルへリダイレクト中
 *               OS_S_IFIFO  メモリバッファ = パイプ
 *   out_file_fd OS_S_IFREG のときだけ実ファイルの FD、他は -1。NULL 可。
 *   fd が 0/1/2 でなければ OS_S_IFCHR / -1 (呼び手が別経路で扱う)。
 */
u16 fd_redirect_ifmt(int fd, int *out_file_fd);

/* リダイレクト状態の問い合わせ (fd_redirect_ifmt から導く)
 * 戻り値: 1=リダイレクト中, 0=コンソールモード */
int fd_is_redirected(int fd);

/* リダイレクト先への読み書き (vfs_fd.c から呼び出される)
 * 戻り値: 読み書きバイト数, 負=エラー */
int fd_redirect_read(int fd, void *buf, u32 size);
int fd_redirect_write(int fd, const void *buf, u32 size);

/* パイプバッファの書き込み済みデータ長を取得 */
u32 fd_redirect_get_buf_len(int fd);

/* ======================================================================== */
/*  アプリ ID ごとの退避枠 (票 T9 §12 T1、Codex 網羅レビュー 往復 8)         */
/*                                                                          */
/*  この表は FD 0/1/2 の 3 本しかなく、**全アプリで 1 つ**だった。GUI で     */
/*  アプリが同時に生きるようになると、park してある sh のリダイレクトが      */
/*  そのまま生きているので:                                                  */
/*                                                                          */
/*    - WM の Start → Run で別アプリを起動すると、その printf (sys_write(1)) */
/*      が **sh の `> /tmp/out`** に入る (反例 1)                            */
/*    - パイプ中なら stdout は sh の .bss (sh の**仮想**番地) なので、別     */
/*      アプリの sys_write(1) がその番地を**別アプリの CR3** で解決して書く   */
/*      → 別アプリの領域破壊か fault (反例 2)                                */
/*                                                                          */
/*  そこで表そのものを「走っている ID の文脈」にする。exec/appslot.c の      */
/*  park / resume が **持ち替える** (コピーではなく移す — 同じ file_fd を    */
/*  2 か所が持つと二重 close になる)。                                       */
/* ======================================================================== */

#define FD_REDIRECT_SLOTS  3    /* FD 0/1/2 */

typedef struct {
    FdRedirect fd[FD_REDIRECT_SLOTS];
} FdRedirectState;

/* いまの表を out へ **移す** (out へ写し、いまの表はコンソールへ戻す)。
 * ファイルは閉じない — 所有ごと out へ移るだけ。 */
void fd_redirect_save(FdRedirectState *out);

/* in をいまの表へ **移す** (上書き)。呼ぶ前に必ず fd_redirect_save で
 * いまの表を退避しておくこと (しないと生きている file_fd を取りこぼす)。
 * 呼んだ後の in は「空になったもの」として扱う (fd_redirect_clear_state)。 */
void fd_redirect_restore(const FdRedirectState *in);

/* 枠を空 (全部コンソール) にする。**閉じない**。回収のときもこれを使う —
 * 枠の中の file_fd は fd_redirect_to_file の vfs_open がその ID の owner
 * タグを付けて取ったものなので、park したまま畳まれても
 * exec_reclaim_owned の (2) vfs_close_owned(id) が閉じる。ここで閉じると
 * 同じ FD に vfs_close が 2 回掛かる (Codex 網羅レビュー 往復 9 の指摘)。 */
void fd_redirect_clear_state(FdRedirectState *st);

/* 枠の fd がリダイレクト中か (1/0)。自己診断とホスト試験のための問い合わせ。 */
int fd_redirect_state_active(const FdRedirectState *st, int fd);

#endif /* FD_REDIRECT_H */
