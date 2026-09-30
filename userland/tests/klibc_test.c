/* ======================================================================== */
/*  KLIBC_TEST.C — カーネル libc互換レイヤー テストプログラム                 */
/*                                                                          */
/*  Step 0a-0d で追加した libc互換関数の動作検証を行う。                      */
/*  外部プログラムは newlib 経由で同名関数を呼ぶため、ここでは newlib の       */
/*  実装を通じて関数の正当性をテストする（カーネル側ASM関数の間接検証含む）。 */
/*                                                                          */
/*  テスト項目:                                                              */
/*    1. strcmp / strncmp (kstrcmp/kstrncmp のエイリアス)                     */
/*    2. memcmp (ASM新規実装)                                                */
/*    3. memmove (C新規実装, 重複領域対応)                                    */
/*    4. strchr (C新規実装)                                                  */
/*    5. strcspn / strspn (C新規実装)                                        */
/*    6. malloc / free / realloc (newlib sbrk 経由)                          */
/*    7. fabs (浮動小数点)                                                   */
/* ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os32api.h"
#include "rt/testresult.h"

/* 外部プログラムには libm がリンクされていないため、fabs を自前で定義 */
static double my_fabs(double x) { return x < 0.0 ? -x : x; }

static int pass_count = 0;
static int fail_count = 0;

static void check(const char *name, int cond)
{
    if (cond) {
        printf("  [PASS] %s\n", name);
        pass_count++;
    } else {
        printf("  [FAIL] %s\n", name);
        fail_count++;
    }
}

/* ======================================================================== */
/*  strcmp テスト                                                             */
/* ======================================================================== */
static void test_strcmp(void)
{
    printf("--- strcmp ---\n");
    check("equal strings",       strcmp("hello", "hello") == 0);
    check("first < second",      strcmp("abc", "abd") < 0);
    check("first > second",      strcmp("abd", "abc") > 0);
    check("empty vs non-empty",  strcmp("", "a") < 0);
    check("non-empty vs empty",  strcmp("a", "") > 0);
    check("both empty",          strcmp("", "") == 0);
    check("prefix shorter",      strcmp("ab", "abc") < 0);
}

/* ======================================================================== */
/*  strncmp テスト                                                           */
/* ======================================================================== */
static void test_strncmp(void)
{
    printf("--- strncmp ---\n");
    check("equal within n",      strncmp("hello", "hello", 5) == 0);
    check("equal prefix n=3",    strncmp("hello", "helXX", 3) == 0);
    check("diff at pos 4",       strncmp("hello", "helXX", 5) != 0);
    check("n=0 always equal",    strncmp("abc", "xyz", 0) == 0);
    check("short string n=10",   strncmp("ab", "ab", 10) == 0);
}

/* ======================================================================== */
/*  memcmp テスト                                                            */
/* ======================================================================== */
static void test_memcmp(void)
{
    char a[8], b[8];

    printf("--- memcmp ---\n");
    memset(a, 0x41, 8);
    memset(b, 0x41, 8);
    check("equal blocks",        memcmp(a, b, 8) == 0);

    b[7] = 0x42;
    check("diff at last byte",   memcmp(a, b, 8) < 0);

    a[0] = 0x43;
    check("diff at first byte",  memcmp(a, b, 8) > 0);

    check("n=0 always equal",    memcmp(a, b, 0) == 0);

    /* DWORD境界テスト (5バイト = 1 DWORD + 1 byte) */
    {
        char x[5], y[5];
        memset(x, 0x55, 5);
        memset(y, 0x55, 5);
        check("5 bytes equal",   memcmp(x, y, 5) == 0);
        y[4] = 0x56;
        check("5 bytes diff",    memcmp(x, y, 5) < 0);
    }
}

/* ======================================================================== */
/*  memmove テスト                                                           */
/* ======================================================================== */
static void test_memmove(void)
{
    char buf[16];

    printf("--- memmove ---\n");

    /* 重複なしコピー */
    memset(buf, 0, 16);
    memcpy(buf, "ABCDEFGH", 8);
    memmove(buf + 8, buf, 8);
    check("non-overlap copy",    memcmp(buf, "ABCDEFGHABCDEFGH", 16) == 0);

    /* 前方重複 (src > dst) */
    memcpy(buf, "0123456789ABCDEF", 16);
    memmove(buf, buf + 4, 8);
    check("forward overlap",     memcmp(buf, "456789AB", 8) == 0);

    /* 後方重複 (src < dst) */
    memcpy(buf, "0123456789ABCDEF", 16);
    memmove(buf + 4, buf, 8);
    check("backward overlap",    memcmp(buf + 4, "01234567", 8) == 0);

    /* ゼロ長コピー */
    memcpy(buf, "HELLO", 5);
    memmove(buf, buf + 2, 0);
    check("zero length",         buf[0] == 'H');
}

/* ======================================================================== */
/*  strchr テスト                                                            */
/* ======================================================================== */
static void test_strchr(void)
{
    const char *s = "hello world";
    char *p;

    printf("--- strchr ---\n");
    p = strchr(s, 'w');
    check("find 'w'",           p != NULL && *p == 'w');
    check("offset correct",     (int)(p - s) == 6);

    p = strchr(s, 'z');
    check("not found",          p == NULL);

    p = strchr(s, '\0');
    check("find NUL",           p != NULL && *p == '\0');

    p = strchr(s, 'h');
    check("find first char",    p == s);
}

/* ======================================================================== */
/*  strcspn / strspn テスト                                                  */
/* ======================================================================== */
static void test_strspn(void)
{
    printf("--- strcspn / strspn ---\n");

    check("strcspn basic",      strcspn("hello", "lo") == 2);
    check("strcspn no match",   strcspn("hello", "xyz") == 5);
    check("strcspn first",      strcspn("hello", "h") == 0);
    check("strcspn empty rej",  strcspn("hello", "") == 5);

    check("strspn basic",       strspn("aabbc", "ab") == 4);
    check("strspn no match",    strspn("hello", "xyz") == 0);
    check("strspn all match",   strspn("aaa", "a") == 3);
    check("strspn empty acc",   strspn("hello", "") == 0);
}

/* ======================================================================== */
/*  malloc / realloc テスト                                                  */
/* ======================================================================== */
static void test_malloc(void)
{
    char *p;
    char *p2;

    printf("--- malloc/free/realloc ---\n");

    p = (char *)malloc(64);
    check("malloc(64) != NULL",  p != NULL);
    if (p) {
        memset(p, 'A', 64);
        check("write ok",       p[0] == 'A' && p[63] == 'A');

        p2 = (char *)realloc(p, 128);
        check("realloc(128) != NULL", p2 != NULL);
        if (p2) {
            check("data preserved", p2[0] == 'A' && p2[63] == 'A');
            free(p2);
        } else {
            free(p);
        }
    }

    /* NULL realloc = malloc */
    p = (char *)realloc(NULL, 32);
    check("realloc(NULL,32)",    p != NULL);
    if (p) free(p);

    /* free(NULL) は安全 */
    free(NULL);
    check("free(NULL) safe",     1);
}

/* ======================================================================== */
/*  fabs テスト                                                              */
/* ======================================================================== */
static void test_fabs(void)
{
    printf("--- fabs ---\n");
    check("fabs(3.14)",         my_fabs(3.14) == 3.14);
    check("fabs(-3.14)",        my_fabs(-3.14) == 3.14);
    check("fabs(0.0)",          my_fabs(0.0) == 0.0);
    check("fabs(-0.0)",         my_fabs(-0.0) == 0.0);
}

/* ======================================================================== */
/*  64bit除算テスト (libgcc __divdi3 等)                                     */
/* ======================================================================== */
static void test_div64(void)
{
    long long a, b, q, r;

    printf("--- 64bit division ---\n");

    a = 1000000000LL * 10LL;   /* 10,000,000,000 */
    b = 3LL;
    q = a / b;
    r = a % b;
    check("10B / 3 quotient",   q == 3333333333LL);
    check("10B %% 3 remainder", r == 1LL);

    /* 符号付き除算 */
    a = -100LL;
    b = 7LL;
    q = a / b;
    r = a % b;
    check("-100 / 7",           q == -14LL);
    check("-100 %% 7",          r == -2LL);
}

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)argv;

    /* 何度呼ばれても同じ答えを出すように、集計は毎回ここで 0 に戻す。 */
    pass_count = 0;
    fail_count = 0;

    printf("=== klibc compatibility test ===\n\n");

    test_strcmp();
    test_strncmp();
    test_memcmp();
    test_memmove();
    test_strchr();
    test_strspn();
    test_malloc();
    test_fabs();
    test_div64();

    return os32_test_summary(api, "klibc_test",
                             pass_count, pass_count + fail_count);
}
