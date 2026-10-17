/* ILP32 fixture: real current utf8.c + disposable kernel + mock SDK path. */
#include "unicode_lookup_mock.h"
#include "unicode_lookup_oracle.h"

u16 unicode_to_jis(u32 cp);
void utf8_set_jis_table(const u8 *table);
u16 t3d0_unicode_to_jis(u32 cp);
int t3d0_table_order(void);
u32 t3d0_table_bytes(void);

static u32 kapi_calls;
static u16 __attribute__((cdecl)) wrap_unicode_to_jis(u32 cp)
{
    kapi_calls++;
    return t3d0_unicode_to_jis(cp);
}
static const struct unicode_mock_api api = { wrap_unicode_to_jis };
const struct unicode_mock_api *unicode_mock_api = &api;

static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    u32 ret;
    /* The syscall returns in eax; declare it so the next call reloads 4. */
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(4), "b"(1), "c"(s), "d"(n) : "memory");
    (void)ret;
}
static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(c, label) do { if (!(c)) { say("FAIL " label "\n"); die(1); } } while (0)

static void compare(u32 cp, const char *label)
{
    u16 old = unicode_to_jis(cp);
    u32 calls = kapi_calls;
    if (t3d0_unicode_to_jis(cp) != old) {
        say("FAIL "); say(label); say(" direct\n"); die(1);
    }
    CHECK(kapi_calls == calls, "kernel-direct");
    if (sdk_unicode_to_jis(cp) != old) {
        say("FAIL "); say(label); say(" sdk\n"); die(1);
    }
    CHECK(kapi_calls == calls + 1, "sdk-kapi");
}

void harness_main(void) __attribute__((used));
void harness_main(void)
{
    CHECK(t3d0_table_order(), "table-order");
    CHECK(t3d0_table_bytes() == 28252, "table-bytes");
    utf8_set_jis_table(oracle_flat);
    CHECK(unicode_to_jis(0x4E9C) == 0x3021, "oracle-ready");
    compare(0x00A7, "first");
    compare(0xFFE5, "last");
    compare(0, "missing");
    compare(0xFFFF, "missing-last");
    const u32 outside[] = {0x10000, 0x1F600, 0x10FFFF, 0x110000, 0xFFFFFFFF};
    for (u32 i = 0; i < sizeof(outside) / sizeof(outside[0]); i++)
        compare(outside[i], "outside");
    const u32 kana[] = {0x3040, 0x3041, 0x3093, 0x3094,
                        0x30A0, 0x30A1, 0x30F6, 0x30F7};
    for (u32 i = 0; i < sizeof(kana) / sizeof(kana[0]); i++)
        compare(kana[i], "kana");
    CHECK(unicode_to_jis(0x3041) == 0x2421 && unicode_to_jis(0x3093) == 0x2473 &&
          unicode_to_jis(0x30A1) == 0x2521 && unicode_to_jis(0x30F6) == 0x2576,
          "kana-arithmetic");
    for (u32 i = 0; i < 7063; i++) compare(oracle_cp[i], "pair");
    for (u32 cp = 0; cp <= 0xFFFF; cp++) compare(cp, "bmp");
    say("PASS pairs=7063 BMP=65536 first/last/missing/outside/kana direct+SDK/KAPI table=28252B\n");
    die(0);
}
__asm__(".globl _start\n_start:\n"
        "andl $-16, %esp\ncall harness_main\nud2\n");
