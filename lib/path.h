/* ======================================================================== */
/*  PATH.H — パス解決 (DOS風ドライブ名方式)                                */
/*                                                                          */
/*  "fdd0:file.txt"  → drive="fdd0", path="file.txt"                      */
/*  "file.txt"       → drive=カレントドライブ, path="file.txt"             */
/*  "fdd0:/sub/file" → drive="fdd0", path="/sub/file"                     */
/* ======================================================================== */

#ifndef PATH_H
#define PATH_H

/* ======== 定数 ======== */
#define PATH_DRIVE_LEN   8      /* ドライブ名最大長 */
#define PATH_MAX_LEN     64     /* パス最大長 */

/* ======== パース結果 ======== */
typedef struct {
    char drive[PATH_DRIVE_LEN]; /* "fdd0" (0終端) */
    char path[PATH_MAX_LEN];   /* "file.txt" or "/sub/file" (0終端) */
} ParsedPath;

/* ======== カレントドライブ ======== */

/* カレントドライブ名取得 (戻り値: "fdd0" 等) */
const char *path_get_drive(void);

/* カレントドライブ設定 (戻り値: 0=OK, -1=デバイス不明) */
int path_set_drive(const char *name);

/* カレントパス取得 ("/" 等) */
const char *path_get_cwd(void);

/* カレントパス設定 */
void path_set_cwd(const char *p);

/* ======== パス解析 ======== */

/* デバイス名検証コールバック型
 * name: デバイス名 (e.g. "fdd0", "hd0")
 * 戻り値: 0=存在しない, 非0=存在する
 */
typedef int (*PathDeviceValidator)(const char *name);

/* デバイス名検証コールバックを登録 */
void path_set_device_validator(PathDeviceValidator fn);

/* 入力文字列をドライブ名+パスに分解
 * 戻り値: 0=OK, -1=エラー */
int path_parse(const char *input, ParsedPath *out);

/* ======== ユーティリティ ======== */

/* ファイル名部分を取得 ("fdd0:/sub/file.txt" → "file.txt") */
const char *path_basename(const char *path);

/* ======== 初期化 ======== */
void path_init(void);

#endif /* PATH_H */
