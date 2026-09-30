/* ======================================================================== */
/*  KSTRING_C.C — lib/kstring_asm.asm の 13 本を移植可能な C で書き直したもの */
/*                                                                          */
/*  x86 の既定ビルドは今も lib/kstring_asm.asm を使う (build/kernel.mk の    */
/*  ARCH 分岐)。このファイルは ARCH が x86 以外のときに代わりに積むための     */
/*  実装で、**アセンブリ版と 1 バイトも違わない結果を返すこと**が契約。      */
/*  照合は tools/tests/test_kstring_c.py (両版を同じ実行ファイルにリンクし、  */
/*  同じ入力で戻り値とバッファ全体を突き合わせる)。                          */
/*                                                                          */
/*  アセンブリ版から読み取った契約 (ここが正典ではなく、正典は .asm 本体):    */
/*                                                                          */
/*    kmemcpy/memcpy   戻り値 dst。前方コピー。n>=4 なら「dst を 4 境界まで   */
/*                     1 バイトずつ → 4 バイト単位 → 端数バイト」の順。      */
/*                     重なりは保証しないが、rep movsd と同じ「4 バイト読んで */
/*                     から 4 バイト書く」粒度なので壊れ方まで一致させる。    */
/*    kmemset/memset   戻り値 dst。val は下位 8 ビットだけ使う (and eax,0xFF)。*/
/*    kstrlen/strlen   NUL までのバイト数。                                  */
/*    kstrcmp/strcmp   **符号無し** バイト差 (movzx)。等しければ 0。          */
/*    kstrncmp/strncmp 同上。n==0 は常に 0。NUL に達したらそこで 0。          */
/*    kstrcpy          戻り値 dst。NUL 込みでコピー。                         */
/*    kstrncpy         戻り値 dst。n==0 は何もしない。n>0 なら                */
/*                     min(strlen(src), n-1) バイト写して NUL 終端。         */
/*                     **残りを 0 で埋めない** (libc の strncpy とは違う)。   */
/*    memcmp           最初に食い違ったバイトの **符号無し** 差。n==0 は 0。  */
/*                                                                          */
/*  符号の扱いが要になる: `*a - *b` を char のまま書くと 0x80 以上のバイト    */
/*  (日本語ファイル名の UTF-8) の並び順がアセンブリ版と食い違う。            */
/*  必ず u8 に載せてから int に上げる (movzx と同じ)。                       */
/*                                                                          */
/*  kstrcat / kstrncat / memmove / strchr / strcspn / strspn は元から C で    */
/*  lib/kstring.c にあるので、ここには無い。                                 */
/* ======================================================================== */

#include "kstring.h"

/* アセンブリ版のバルク転送の粒度 (rep movsd / rep stosd = 4 バイト)。
 * 数値そのものではなくこの名前で参照する ([C4])。 */
#define KSTR_WORD       4UL
#define KSTR_WORD_MASK  (KSTR_WORD - 1UL)

/* ポインタの 4 境界からのずれ。アセンブリ版の `test edi, 3` と同じ判定。 */
#define KSTR_MISALIGN(p) (((unsigned long)(p)) & KSTR_WORD_MASK)

/* ======================================================================== */
/*  kmemcpy — 前方コピー。戻り値 dst。                                       */
/*                                                                          */
/*  契約: 重なりは保証しない (lib/kstring.h の注記)。ただしアセンブリ版と    */
/*  **同じ壊れ方**をする必要があるので、rep movsd と同じく「4 バイト読み →   */
/*  4 バイト書き」を 1 単位とする。バイト列を一度ローカルに載せてから書くの   */
/*  は、非整列アクセスを避けつつ movsd の読み書き順を再現するため。          */
/* ======================================================================== */
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    u32 words;
    u32 tail;
    u8 b0, b1, b2, b3;

    /* n < 4 は素の rep movsb (n==0 は何もしない) */
    if (n < (u32)KSTR_WORD) {
        while (n-- != 0) *d++ = *s++;
        return dst;
    }

    /* dst を 4 境界に合わせる。n>=4 なので最大 3 バイトでも尽きない。 */
    while (KSTR_MISALIGN(d) != 0UL) {
        *d++ = *s++;
        n--;
    }

    words = n / (u32)KSTR_WORD;
    while (words-- != 0) {
        b0 = s[0]; b1 = s[1]; b2 = s[2]; b3 = s[3];
        d[0] = b0; d[1] = b1; d[2] = b2; d[3] = b3;
        s += KSTR_WORD;
        d += KSTR_WORD;
    }

    tail = n & (u32)KSTR_WORD_MASK;
    while (tail-- != 0) *d++ = *s++;

    return dst;
}

/* ======================================================================== */
/*  kmemset — 戻り値 dst。val は下位 8 ビットだけ (アセンブリ版の and 0xFF)。 */
/* ======================================================================== */
void *kmemset(void *dst, int val, u32 n)
{
    u8 *d = (u8 *)dst;
    u8 v = (u8)(val & 0xFF);
    u32 words;
    u32 tail;

    if (n < (u32)KSTR_WORD) {
        while (n-- != 0) *d++ = v;
        return dst;
    }

    while (KSTR_MISALIGN(d) != 0UL) {
        *d++ = v;
        n--;
    }

    words = n / (u32)KSTR_WORD;
    while (words-- != 0) {
        d[0] = v; d[1] = v; d[2] = v; d[3] = v;
        d += KSTR_WORD;
    }

    tail = n & (u32)KSTR_WORD_MASK;
    while (tail-- != 0) *d++ = v;

    return dst;
}

/* ======================================================================== */
/*  kstrlen — NUL までのバイト数。                                           */
/* ======================================================================== */
u32 kstrlen(const char *s)
{
    const char *p = s;
    while (*p != '\0') p++;
    return (u32)(p - s);
}

/* ======================================================================== */
/*  kstrcmp — **符号無し** バイト差。等しければ 0。                          */
/*                                                                          */
/*  アセンブリ版は movzx で 0 拡張してから引く。char のまま引くと 0x80 以上の */
/*  バイト (日本語ファイル名) の並び順が逆転するので、必ず u8 を経由する。   */
/* ======================================================================== */
int kstrcmp(const char *a, const char *b)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;

    while (*pa == *pb) {
        if (*pa == 0) return 0;
        pa++;
        pb++;
    }
    return (int)*pa - (int)*pb;
}

/* ======================================================================== */
/*  kstrncmp — 最大 n バイト比較。n==0 は常に 0。差は符号無し。              */
/* ======================================================================== */
int kstrncmp(const char *a, const char *b, u32 n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;

    if (n == 0) return 0;

    for (;;) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        if (*pa == 0) return 0;      /* 両方 NUL */
        pa++;
        pb++;
        if (--n == 0) return 0;      /* n バイト見きった */
    }
}

/* ======================================================================== */
/*  kstrcpy — NUL 込みでコピー。戻り値 dst。長さの上限は見ない。             */
/* ======================================================================== */
char *kstrcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d = *src) != '\0') {
        d++;
        src++;
    }
    return dst;
}

/* ======================================================================== */
/*  kstrncpy — strlcpy セマンティクス (n は **バッファ全体サイズ**)。        */
/*                                                                          */
/*  n==0 : dst に一切触らない。                                              */
/*  n>0  : min(strlen(src), n-1) バイト写して直後に NUL。**残りは埋めない**。 */
/*                                                                          */
/*  アセンブリ版は NUL 探索に最大 n バイト読むが、n-1 バイト目より先の内容は  */
/*  結果に影響しないので、ここは n-1 バイトまでしか読まない (より安全側)。    */
/* ======================================================================== */
char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i;

    if (n == 0) return dst;

    for (i = 0; (i + 1) < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    return dst;
}

/* ======================================================================== */
/*  memcmp — 最初に食い違ったバイトの **符号無し** 差。n==0 は 0。           */
/*                                                                          */
/*  アセンブリ版は repe cmpsd で 4 バイトずつ走り、食い違った DWORD の中を    */
/*  バイトで見直す。結果は「最初に違うバイトの符号無し差」でここと同じ。     */
/* ======================================================================== */
int memcmp(const void *a, const void *b, u32 n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;

    while (n-- != 0) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++;
        pb++;
    }
    return 0;
}

/* ======================================================================== */
/*  libc 互換シンボル                                                        */
/*                                                                          */
/*  アセンブリ版は同じラベルを重ねているので memcpy と kmemcpy は **同一の    */
/*  実体** (同じ番地) になる。C 版でもエイリアスにして、関数ポインタの比較や  */
/*  シンボル表の見え方まで揃える (別々の関数にすると番地が 2 つになる)。      */
/* ======================================================================== */
void *memcpy(void *dst, const void *src, u32 n) __attribute__((alias("kmemcpy")));
void *memset(void *dst, int val, u32 n) __attribute__((alias("kmemset")));
u32   strlen(const char *s) __attribute__((alias("kstrlen")));
int   strcmp(const char *a, const char *b) __attribute__((alias("kstrcmp")));
int   strncmp(const char *a, const char *b, u32 n) __attribute__((alias("kstrncmp")));
