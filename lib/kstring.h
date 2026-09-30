/* ======================================================================== */
/*  KSTRING.H — カーネル用 文字列/メモリ操作ユーティリティ                  */
/*                                                                          */
/*  ベアメタル環境でlibc無しに使える基本関数群。                             */
/*  GCCが構造体代入等で暗黙に生成する memcpy/memset シンボルも提供する。     */
/* ======================================================================== */

#ifndef __KSTRING_H
#define __KSTRING_H

#include "types.h"

/* ======== メモリ操作 ======== */
/* 注意: kmemcpy はオーバーラップ時の動作を保証しない (rep movsd 実装)。
 * 重なる可能性がある範囲のコピーは memmove を使うこと。 */
void *kmemcpy(void *dst, const void *src, u32 n);
void *kmemset(void *dst, int val, u32 n);

/* ======== 文字列操作 ======== */
u32   kstrlen(const char *s);
char *kstrncpy(char *dst, const char *src, u32 n);
char *kstrcpy(char *dst, const char *src);
int   kstrcmp(const char *a, const char *b);
int   kstrncmp(const char *a, const char *b, u32 n);
char *kstrcat(char *dst, const char *src);
char *kstrncat(char *dst, const char *src, u32 n);

/* ======== libc互換シンボル ======== */
/* GCC -ffreestanding でも構造体コピー等で memcpy/memset を暗黙に呼ぶ場合がある。 */
/* リンクエラー防止のため、標準名のシンボルも提供する。                           */
/* SQLite等の外部コードが暗黙に使用する libc 標準関数もここで提供。               */
void *memcpy(void *dst, const void *src, u32 n);
void *memset(void *dst, int val, u32 n);
u32   strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, u32 n);
int   memcmp(const void *a, const void *b, u32 n);
void *memmove(void *dst, const void *src, u32 n);

/* kstrncpy/kstrncat の n は「コピー文字数」ではなく**バッファ全体サイズ**
 * (BSD strlcpy/strlcat と同じセマンティクス)。libc の strncpy/strncat とは
 * 違うことを名前で明示するため、こちらのエイリアスを推奨する。 */
#define kstrlcpy kstrncpy
#define kstrlcat kstrncat
char *strchr(const char *s, int c);
u32   strcspn(const char *s, const char *reject);
u32   strspn(const char *s, const char *accept);

#endif /* __KSTRING_H */
