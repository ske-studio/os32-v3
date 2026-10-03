/* T2e e8b: entire real kcg.c + utf8.c; VFS is a byte-stream stub.
 * Physical RAM is redirected only in this fixture. No guest mailbox access.
 */
#include "memmap.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "utf8.h"
static u8 ram[MEM_CONV_END];
static u8 before[MEM_CONV_END];
#undef P2V_CONST
#define P2V_CONST(pa) ((void *)(ram + (pa)))
#define P2V(pa) ((void *)(ram + (pa)))
/* Hardware access is not part of this host boundary test. */
#define IO_H
static inline void outp(unsigned port, unsigned value) { (void)port; (void)value; }
static inline unsigned inp(unsigned port) { (void)port; return 0; }
static inline void io_wait(void) { }
static int host_validate_jis_table(void);
#include "kcg_source.inc"
#include "utf8_source.inc"

static unsigned checks;
static void output(const char *s, unsigned n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void stop(int rc) __attribute__((noreturn));
static void stop(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x, why) do { ++checks; if (!(x)) { \
    output("FAIL " why "\n", sizeof("FAIL " why "\n")-1); stop(1); } } while (0)
static int opens, reads, closes, sizes, logs, stage, watch;
static u8 image[16 + KCG_PAYLOAD_SIZE + KCG_PAYLOAD_SIZE / 255 + 2];
static unsigned image_size, cursor;

void *kmemset(void *dst, int value, u32 n)
{
    if (watch) {
        if (dst == P2V(MEM_GFX_BB_BASE)) {
            CHECK(stage == 1, "BB follows validation");
            CHECK(n == MEM_GFX_BB_SIZE && value == 0, "BB entire range");
            stage = 2;
        } else if (dst == P2V(MEM_AUTOPLAY_MAILBOX_BASE)) {
            CHECK(stage == 2, "mailbox follows BB");
            CHECK(n == MEM_AUTOPLAY_MAILBOX_SIZE && value == 0, "mailbox exact range");
            stage = 3;
        } else CHECK(0, "unexpected close write");
    }
    u8 *p = dst;
    while (n--) *p++ = (u8)value;
    return dst;
}
static int host_validate_jis_table(void)
{
    CHECK(!kcg_boot_phase_open, "scratch invalidated before validation");
    CHECK(stage == 0, "validation before clearing");
    stage = 1;
    return utf8_validate_jis_table();
}
void kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr; (void)fmt; ++logs;
}
int vfs_open(const char *path, int flags)
{
    (void)path; (void)flags; ++opens; cursor = 0; return 3;
}
int vfs_read_fd(int fd, void *dst, u32 len)
{
    (void)fd; ++reads;
    if ((unsigned)len > image_size - cursor) len = image_size - cursor;
    u8 *p = dst;
    for (u32 i = 0; i < len; ++i) p[i] = image[cursor++];
    return len;
}
void vfs_close(int fd) { (void)fd; ++closes; }
u32 vfs_get_size(int fd) { (void)fd; ++sizes; return image_size; }
static void word(u8 *p, u32 x)
{
    for (int i = 0; i < 4; ++i) p[i] = (u8)(x >> (8 * i));
}
static void font_image(int compressed)
{
    word(image, KCG_FONT_MAGIC); word(image+4, KCG_PAYLOAD_SIZE);
    word(image+8, compressed ? KCG_FLAG_LZ4 : 0); word(image+12, 0);
    image_size = 16;
    if (compressed) {
        unsigned left = KCG_PAYLOAD_SIZE - 15;
        image[image_size++] = 0xf0;
        while (left >= 255) { image[image_size++] = 255; left -= 255; }
        image[image_size++] = left;
    }
    for (unsigned i = 0; i < KCG_PAYLOAD_SIZE; ++i)
        image[image_size++] = (u8)(i * 37 + 11);
}
static const u32 cp[4] = {0x4E9C, 0x4E00, 0x5BFE, 0x9078};
static const u16 jis[4] = {0x3021, 0x306C, 0x4250, 0x412A};
static void table(void)
{
    kmemset(P2V(MEM_UNICODE_TABLE_BASE), 0, MEM_UNICODE_TABLE_SIZE);
    for (int i = 0; i < 4; ++i) {
        u8 *p = ram + MEM_UNICODE_TABLE_BASE + cp[i]*2;
        p[0] = (u8)jis[i]; p[1] = jis[i] >> 8;
    }
    utf8_set_jis_table_ready(1);
}
static void snapshot(void)
{
    for (unsigned i = 0; i < sizeof(ram); ++i) before[i] = ram[i];
}
static void closed_call(const char *path)
{
    int o=opens, r=reads, c=closes, s=sizes, l=logs;
    snapshot();
    int rc = kcg_load_font(path);
    CHECK(opens==o && reads==r && closes==c && sizes==s, "closed VFS untouched");
    CHECK(rc == OS32_ERR_NOSYS, "closed NOSYS");
    CHECK(logs == l, "closed logging untouched");
    for (unsigned i = 0; i < sizeof(ram); ++i)
        CHECK(ram[i] == before[i], "closed memory unchanged");
}
static void tests(void)
{
    kmemset(ram, 0xa5, sizeof(ram));
    for (int compressed = 0; compressed <= 1; ++compressed) {
        font_image(compressed);
        CHECK(kcg_load_font("font") == 0, "boot load succeeds");
        for (unsigned i = 0; i < KCG_PAYLOAD_SIZE; ++i)
            CHECK(kanji_cache[i] == (u8)(i*37+11), "boot cache payload");
    }
    CHECK(opens == 2 && closes == 2 && reads > 2 && sizes == 1, "boot VFS exercised");
    table(); snapshot(); stage = 0; watch = 1;
    kcg_boot_phase_close();
    watch = 0;
    CHECK(stage == 3, "close completes order");
    for (unsigned i = 0; i < sizeof(ram); ++i) {
        int cleared = (i >= MEM_GFX_BB_BASE && i < MEM_GFX_BB_BASE+MEM_GFX_BB_SIZE) ||
                      (i >= MEM_AUTOPLAY_MAILBOX_BASE && i < MEM_AUTOPLAY_MAILBOX_BASE+MEM_AUTOPLAY_MAILBOX_SIZE);
        CHECK(ram[i] == (cleared ? 0 : before[i]), "close exact memory ranges");
    }
    CHECK(unicode_to_jis(cp[0]) == jis[0], "valid table ready");
    closed_call("font"); closed_call((const char *)0);
    /* A repeated close must not clear live BB/mailbox. init cannot reopen it. */
    ram[MEM_GFX_BB_BASE] = 0x7a; ram[MEM_AUTOPLAY_MAILBOX_BASE] = 0x7b;
    snapshot(); kcg_boot_phase_close();
    for (unsigned i=0; i<sizeof(ram); ++i)
        CHECK(ram[i] == before[i], "repeated close unchanged");
    kcg_init(); closed_call("font");
    /* Private state reset is fixture-only: four independent boot failures. */
    for (int bad=0; bad<4; ++bad) {
        table(); ram[MEM_UNICODE_TABLE_BASE + cp[bad]*2] ^= 1;
        kcg_boot_phase_open = 1; stage = 0; watch = 1;
        kcg_boot_phase_close(); watch = 0;
        CHECK(stage == 3, "failed probe still initializes");
        CHECK(unicode_to_jis(cp[(bad+1)%4]) == 0, "invalid table ready zero");
    }
    /* A complete-looking table cannot revive a failed/short boot read. */
    table(); utf8_set_jis_table_ready(0);
    kcg_boot_phase_open = 1; stage = 0; watch = 1;
    kcg_boot_phase_close(); watch = 0;
    CHECK(unicode_to_jis(cp[0]) == 0, "failed read remains not ready");
}
void _start(void)
{
    tests();
    output("PASS kcg boot (12 scenarios)\n", sizeof("PASS kcg boot (12 scenarios)\n")-1);
    stop(0);
}
