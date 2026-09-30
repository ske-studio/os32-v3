/* ======================================================================== */
/*  KSTRING.C — カーネル用 文字列/メモリ操作ユーティリティ                  */
/*                                                                          */
/*  高頻度で呼ばれるメモリ操作・文字列操作は kstring_asm.asm に移行済み:     */
/*    kmemcpy, memcpy, kmemset, memset (rep movsd/stosd)                    */
/*    kstrlen, strlen (repne scasb)                                        */
/*    kstrcmp (lodsb + 比較ループ)                                         */
/*    kstrncmp (同上 + カウンタ制限)                                        */
/*    kstrcpy (repne scasb + rep movsb)                                    */
/*    kstrncpy (repne scasb + rep movsb + NUL終端保証)                     */
/*                                                                          */
/*  以下はASM化の効果が限定的なため C 実装のまま残す:                       */
/*    kstrcat, kstrncat (NUL探索+連結の2段階処理, 呼出箇所少)               */
/*                                                                          */
/*  libc互換シンボル (memcpy, memset, strlen) も kstring_asm.asm で提供。   */
/* ======================================================================== */

#include "kstring.h"

/* ======================================================================== */
/*  kstrcat — 文字列連結                                                      */
/* ======================================================================== */
char *kstrcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++) != '\0');
    return dst;
}

/* ======================================================================== */
/*  kstrncat — バッファ全体サイズ n を考慮した安全な文字列連結               */
/*  dst のバッファサイズが n のとき、n-1 文字目まで書き込み NUL 終端する。    */
/* ======================================================================== */
char *kstrncat(char *dst, const char *src, u32 n)
{
    u32 dlen;
    u32 i;
    /* n==0 を先に弾く。弾かないと n-1 が 0xFFFFFFFF にラップして
     * ガードが一切効かなくなる */
    if (n == 0) return dst;

    /* dst の長さ測定も n で打ち切る。kstrlen(dst) をそのまま使うと、
     * dst が n バイト以内に NUL を持たない (呼び出し側のバグ or
     * 未初期化) 場合にバッファ外を読み続ける。 */
    for (dlen = 0; dlen < n && dst[dlen]; dlen++) {
    }
    if (dlen >= n - 1) return dst;
    for (i = 0; src[i] && (dlen + i) < n - 1; i++) {
        dst[dlen + i] = src[i];
    }
    dst[dlen + i] = '\0';
    return dst;
}

/* ======================================================================== */
/*  memmove — 重複領域対応メモリコピー                                       */
/*  src と dst が重なっていても正しく動作する。                               */
/* ======================================================================== */
void *memmove(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    if (d == s || n == 0) return dst;

    if (d < s) {
        /* 前方コピー (memcpy と同じ) */
        while (n--) *d++ = *s++;
    } else {
        /* 後方コピー (重複時に安全) */
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

/* ======================================================================== */
/*  strchr — 文字列中の文字検索                                              */
/*  NUL終端文字自体も検索対象 (c=='\0' のとき末尾のNULを返す)                */
/* ======================================================================== */
char *strchr(const char *s, int c)
{
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : (char *)0;
}

/* ======================================================================== */
/*  strcspn — reject に含まれない先頭からの連続長を返す                      */
/* ======================================================================== */
u32 strcspn(const char *s, const char *reject)
{
    const char *p;
    const char *r;
    for (p = s; *p; p++) {
        for (r = reject; *r; r++) {
            if (*p == *r) return (u32)(p - s);
        }
    }
    return (u32)(p - s);
}

/* ======================================================================== */
/*  strspn — accept に含まれる先頭からの連続長を返す                         */
/* ======================================================================== */
u32 strspn(const char *s, const char *accept)
{
    const char *p;
    const char *a;
    int found;
    for (p = s; *p; p++) {
        found = 0;
        for (a = accept; *a; a++) {
            if (*p == *a) { found = 1; break; }
        }
        if (!found) return (u32)(p - s);
    }
    return (u32)(p - s);
}
