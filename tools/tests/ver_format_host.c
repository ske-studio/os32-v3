/* Independent full-line oracle for the real V8 formatter and V4 name lookup. */
#include "support.h"
#include "ver_info.h"
#include "../lib/kstring.h"
#include "ver_format_source.c"
#include "ver_state_source.c"

#define DEV (OS32_VER_DEVICE | OS32_VER_INDENT)
#define ID (OS32_VER_IDENTITY | OS32_VER_INDENT)
#define DBG OS32_VER_DEBUG
/* Compile-time maxima, independent of the production append helpers.
 * Decimal u32=10, hex u32=8, u16=5, u8=3; stored text excludes NUL.
 * Every row has a bound, including unknowns and both continuation forms. */
#define FIT(text) STATIC_ASSERT(sizeof(text)-1 <= OS32_VER_COLS, line_fits_##__LINE__)
FIT("PC-9821 OS32 v" "VVVVVVVVVVVVVVVVVVVV" " (Ring3 Native)");
FIT("CPU: x86 32-bit PM+Paging; calib=measured");
FIT("PIC: 8259A x2; vectors=ffh/ffh (configured)");
FIT("PIT: 8254 tick=4294967295Hz input=4294967295Hz");
FIT("KBD: 8251-compatible; IRQ4294967295 (configured)");
FIT("SER: 8251 baud=4294967295 mode=compat FIFO=yes");
FIT("SND: OPN configured; PCM=CS4231-family");
FIT("GFX: Cirrus 65535x65535x255 bpp; GUI target");
FIT("API: v4294967295");
FIT("Build: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
FIT("Commit: CCCCCCCCCCCCCCCCCCCCCCC");
FIT("Image CRC: ffffffff (4294967295 bytes, HDD loader)");
FIT("Image CRC: none (loader did not record)");
FIT("kdebug: on (pair ffffffff)");
FIT("kdebug: rejected 4294967295 BAD_DEPENDENCY");
FIT("kdebug pages: body=4294967295 owner=4294967295");
FIT("kdebug file pair: ffffffff");
FIT("sqlite stack high-water=4294967295 of 4294967295");
FIT("services: SSSSSSSSSSSSSSSSSSSSSSS public=4294967295");
FIT("SER: not initialized");
FIT("sqlite stack high-water: unknown");
STATIC_ASSERT(16 <= OS32_VER_LINES, maximum_row_count);
STATIC_ASSERT(sizeof(((VerSnapshot *)0)->build)-1 == 31, build_width);
STATIC_ASSERT(BOOT_IMAGE_COMMIT_MAX-1 == 23, commit_width);
STATIC_ASSERT(SERVICES_VER_STATE_TEXT-1 == 23, services_width);
static const OS32_VerLine expected[] = {
    {OS32_VER_HEAD, "PC-9821 OS32 v3.0-dev (Ring3 Native)"},
    {DEV, "CPU: x86 32-bit PM+Paging; calib=measured"},
    {DEV, "PIC: 8259A x2; vectors=20h/28h (configured)"},
    {DEV, "PIT: 8254 tick=100Hz input=1996800Hz"},
    {DEV, "KBD: 8251-compatible; IRQ1 (configured)"},
    {DEV, "SER: 8251 baud=9600 mode=compat FIFO=yes"},
    {DEV, "SND: OPN configured; PCM=CS4231-family"},
    {DEV, "GFX: PEGC 640x480x8 bpp; active"},
    {ID, "API: v78"},
    {ID, "Build: Oct 26 2026 12:34:56"},
    {ID, "Commit: 0acd73e4-dirty"},
    {ID, "Image CRC: 0123abcd (123456 bytes, HDD loader)"},
    {DBG, "kdebug: on (pair 000012ab)"},
    {DBG, "kdebug pages: body=123 owner=456"},
    {DBG, "sqlite stack high-water=1234 of 32768"},
    {DBG | OS32_VER_INDENT, "services: SERVICES_READY public=1"}
};
static VerSnapshot s;
static struct {
    u32 before;
    OS32_VerInfo info;
    u32 after;
} guarded;
#define report guarded.info

static void formatted(u32 count)
{
    guarded.before = 0xabcdef01;
    guarded.after = 0x23456789;
    kmemset(&report, 0xa5, sizeof(report));
    ver_format(&s, &report);
    CHECK(guarded.before == 0xabcdef01 && guarded.after == 0x23456789);
    CHECK(report.count == count && count <= OS32_VER_LINES);
    for (u32 i = 0; i < count; i++) {
        u32 n = 0;
        while (n < OS32_VER_TEXT && report.lines[i].text[n]) {
            CHECK(report.lines[i].text[n] >= ' ' && report.lines[i].text[n] <= '~');
            n++;
        }
        CHECK(n <= OS32_VER_COLS);
        CHECK(report.lines[i].text[0] != ' ');
        for (; n < OS32_VER_TEXT; n++) CHECK(report.lines[i].text[n] == 0);
    }
    for (u32 i = count; i < OS32_VER_LINES; i++) {
        CHECK(report.lines[i].kind == 0);
        for (u32 j = 0; j < OS32_VER_TEXT; j++) CHECK(report.lines[i].text[j] == 0);
    }
}

static void line(u32 index, const char *text)
{
    CHECK(equal(report.lines[index].text, text));
}

static void baseline(void)
{
    kmemset(&s, 0, sizeof(s));
    s.cpu_measured = s.pit_valid = s.ser_ready = s.ser_fifo = s.pcm_present = 1;
    s.pit_hz = 100; s.pit_input = 1996800; s.ser_baud = 9600;
    s.gfx.width = 640; s.gfx.height = 480; s.gfx.bpp = 8;
    s.gfx_kind = 2; s.gfx_active = 1;
    s.api_version = 78;
    kstrncpy(s.build, "Oct 26 2026 12:34:56", sizeof(s.build));
    s.boot_ok = s.debug_ok = 1;
    s.boot.crc_valid = 1; s.boot.source = 2;
    s.boot.image_crc = 0x0123abcd; s.boot.image_size = 123456;
    kstrncpy(s.boot.commit, "0acd73e4-dirty", sizeof(s.boot.commit));
    s.debug.kdebug_state = 1; s.debug.kdebug_pair_crc = 0x12ab;
    s.debug.kdebug_pages = 123; s.debug.kdebug_owner_pages = 456;
    s.services.stack_used = 1234; s.services.stack_size = 32768; s.services.public = 1;
    kstrncpy(s.services.state, "SERVICES_READY", sizeof(s.services.state));
}

static char *oracle_append(char *out, const char *text)
{
    while (*text) *out++ = *text++;
    *out = 0;
    return out;
}

static void check_states(void)
{
    static const char *const names[] = {
        "OFF", "RUNNING", "NOT_FOUND", "BAD_HEADER", "PAIR_MISMATCH",
        "NO_IMAGE_CRC", "ABI_MISMATCH", "NO_PAGES", "SHORT_READ", "BAD_CRC",
        "INIT_FAILED", "RELOC_INVALID", "IMPORT_INVALID", "RECORD_FULL",
        "BAD_DEPENDENCY", "BUSY", "QUARANTINED"
    };
    static const u32 states[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,0x7fffffff,0x80000000,0xffffffff};
    for (u32 i = 0; i < sizeof(states)/sizeof(states[0]); i++) {
        char want[OS32_VER_TEXT];
        s.debug.kdebug_state = states[i];
        formatted(states[i] ? 16 : 15);
        if (states[i] == 0) line(12, "kdebug: off");
        else if (states[i] == 1) {
            line(12, "kdebug: on (pair ffffffff)");
            line(13, "kdebug pages: body=4294967295 owner=4294967295");
        } else {
            /* Independent decimal conversion for the expected rejected line. */
            char number[11]; u32 value = states[i], pos = 10;
            number[pos] = 0;
            do { number[--pos] = '0' + value % 10; value /= 10; } while (value);
            char *end = oracle_append(want, "kdebug: rejected ");
            end = oracle_append(end, number + pos);
            end = oracle_append(end, " ");
            oracle_append(end, states[i] < 17 ? names[states[i]] : "UNKNOWN");
            line(12, want);
            line(13, "kdebug file pair: ffffffff");
        }
    }
}

void _start(void)
{
    baseline(); formatted(16);
    for (u32 i = 0; i < sizeof(expected)/sizeof(expected[0]); i++) {
        CHECK(report.lines[i].kind == expected[i].kind);
        line(i, expected[i].text);
    }
    s.boot.crc_valid = 0; formatted(16);
    line(11, "Image CRC: none (loader did not record)");
    s.boot.crc_valid = 1;
    s.boot.source = 1; formatted(16);
    line(11, "Image CRC: 0123abcd (123456 bytes, FD loader)");
    s.boot.source = 255; formatted(16);
    line(11, "Image CRC: 0123abcd (123456 bytes, ? loader)");
    s.boot_ok = s.debug_ok = 0; formatted(13);
    line(10, "kdebug: unknown");
    CHECK(report.lines[11].kind == DBG);
    CHECK(report.lines[12].kind == (DBG | OS32_VER_INDENT));
    s.cpu_measured = s.pit_valid = s.ser_ready = s.pcm_present = 0;
    s.gfx_kind = 0; s.gfx_active = 0; formatted(13);
    line(1, "CPU: x86 32-bit PM+Paging; calib=default");
    line(3, "PIT: unknown"); line(5, "SER: not initialized");
    line(6, "SND: OPN configured; PCM=none"); line(7, "GFX: unknown; GUI target");
    baseline(); s.gfx_active = 0; s.gfx_kind = 1;
    s.gfx.height = 400; s.gfx.bpp = 4; formatted(16);
    line(7, "GFX: 98 640x400x4 bpp; GUI target");
    s.gfx_kind = 3; formatted(16);
    line(7, "GFX: Cirrus 640x400x4 bpp; GUI target");
    for (u32 part = 0; part < 3; part++) {
        baseline();
        if (part == 0) s.gfx.width = 0;
        if (part == 1) s.gfx.height = 0;
        if (part == 2) s.gfx.bpp = 0;
        formatted(16); line(7, "GFX: unknown; active");
    }
    baseline(); s.ser_mode = 1; s.ser_fifo = 0; formatted(16);
    line(5, "SER: 8251 baud=9600 mode=VFAST FIFO=no");
    s.ser_mode = 2; formatted(16); line(5, "SER: unknown");
    /* Every numeric storage width at maximum; all longest variable strings. */
    baseline();
    s.pit_hz = s.pit_input = s.ser_baud = s.api_version = U32_MAX;
    s.debug.kdebug_pair_crc = s.debug.kdebug_pages = s.debug.kdebug_owner_pages = U32_MAX;
    s.boot.image_crc = s.boot.image_size = U32_MAX;
    s.services.stack_used = s.services.stack_size = s.services.public = U32_MAX;
    kmemset(s.build, 'B', sizeof(s.build)-1);
    kmemset(s.boot.commit, 'C', sizeof(s.boot.commit)-1);
    kmemset(s.services.state, 'S', sizeof(s.services.state)-1);
    s.gfx_kind = 3; s.gfx.width = s.gfx.height = U16_MAX; s.gfx.bpp = U8_MAX;
    s.gfx_active = 0;
    formatted(16);
    line(3, "PIT: 8254 tick=4294967295Hz input=4294967295Hz");
    line(5, "SER: 8251 baud=4294967295 mode=compat FIFO=yes");
    line(7, "GFX: Cirrus 65535x65535x255 bpp; GUI target");
    line(8, "API: v4294967295");
    line(9, "Build: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
    line(10, "Commit: CCCCCCCCCCCCCCCCCCCCCCC");
    line(11, "Image CRC: ffffffff (4294967295 bytes, HDD loader)");
    line(14, "sqlite stack high-water=4294967295 of 4294967295");
    line(15, "services: SSSSSSSSSSSSSSSSSSSSSSS public=4294967295");
    check_states();
    /* Missing string terminators fall back locally, retaining identity rows. */
    baseline(); kmemset(s.build, 'B', sizeof(s.build));
    formatted(16); line(9, "Build: unknown"); line(10, expected[10].text); line(11, expected[11].text);
    baseline(); kmemset(s.services.state, 'S', sizeof(s.services.state));
    formatted(16); line(15, "services: unknown");
    /* Exercise the bounded helper's overflow without changing production data. */
    VerLineBuilder b = ver_line(&report, DBG, "test");
    ver_text(&b, "012345678901234567890123456789012345678901234567890123456789");
    CHECK(b.overflow); ver_end(&b); CHECK(equal(b.line->text, "test: unknown"));
    say("PASS formatter: exact rows/kinds, all states, maxima, CRC, local fallback\n");
    die(0);
}
