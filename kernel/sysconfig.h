/* ======================================================================== */
/*  SYSCONFIG.H — 最小の起動設定パーサ (K レーン K4)                         */
/*                                                                          */
/*  /etc/system.cfg のような行指向 KEY=VALUE 設定を読む。カーネルのシェル    */
/*  起動ループ (kernel.c) が GUI=0/1 を判定するために使う。                  */
/*  書式・値の意味は docs/archive/gui_v11/TASK_K4_gui_boot.md、契約 T9。     */
/* ======================================================================== */

#ifndef __SYSCONFIG_H
#define __SYSCONFIG_H

/* path の設定ファイルから key の整数値を返す。
 *   - 1 行 1 エントリ、"KEY=VALUE" 形式 (KEY の前後・= の前後の空白は許容)。
 *   - '#' または ';' で始まる行、空行はコメントとして無視。
 *   - ファイルが無い / key が見つからない / 読めない場合は def を返す。
 * カーネル内専用 (kstr* 使用、libc に依存しない)。 */
int sysconfig_get_int(const char *path, const char *key, int def);

/* 同じ書式から key の文字列値を out へ取り出す (票 H2b: GFX=pc98|pegc|auto)。
 *   - 値は行末 / 空白 / '#' / ';' までを切り出し、前後の空白は落とす。
 *   - out は必ず NUL 終端する。値が長ければ outsz-1 バイトで切り詰める。
 *   - 見つかったら切り出した長さ (0 以上)、無ければ -1 を返す
 *     (このとき out は空文字列)。
 * カーネル内専用 (kstr* 使用、libc に依存しない)。 */
int sysconfig_get_str(const char *path, const char *key, char *out, int outsz);

#endif /* __SYSCONFIG_H */
