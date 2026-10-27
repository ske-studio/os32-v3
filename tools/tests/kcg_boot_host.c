/* T2e e8b: entire real kcg.c; VFS is a byte-stream stub.
 * Physical RAM is redirected only in this fixture. No guest mailbox access.
 */
#include "memmap.h"
#include "kcg_contract.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
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
int vfs_open(const char *, int);
#include "kcg_source.inc"

/* No real low memory/ports: writes and accidental VFS reads are observed. */
static unsigned opens;
int vfs_open(const char *path, int flags)
{ (void)path; (void)flags; ++opens; return 3; }
void kcg_rom_read_ank(u8 ch, u8 *buf)
{ for (u32 i=0; i<16; ++i) buf[i] = ch+i; }
void kcg_rom_read_kanji(u16 jis, u8 *buf)
{ for (u32 i=0; i<32; ++i) buf[i] = jis+i; }
static void output(const char *s, unsigned n)
{ __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
static void stop(int rc) __attribute__((noreturn));
static void stop(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x, why) do { if (!(x)) { \
    output("FAIL " why "\n", sizeof("FAIL " why "\n")-1); stop(1); } } while (0)
void _start(void)
{
    for (u32 i=0; i<sizeof(ram); ++i) before[i] = ram[i] = (u8)(i*37+11);
    for (u32 repeat=0; repeat<2; ++repeat) {
        int rc = kcg_load_font(repeat ? (const char *)0 : "legacy-font");
        CHECK(!opens, "legacy VFS untouched");
        CHECK(rc == OS32_ERR_NOSYS, "legacy NOSYS");
        kcg_set_scale(3); kcg_init();
        CHECK(kcg_scale == 1, "init resets scale");
        u8 glyph[32] = {0};
        kcg_read_ank('A', glyph);
        for (u32 i=0; i<16; ++i) CHECK(glyph[i] == (u8)('A'+i), "ROM ANK bytes");
        kcg_read_kanji(0x3021, glyph);
        for (u32 i=0; i<32; ++i) CHECK(glyph[i] == (u8)(0x3021+i), "ROM kanji bytes");
        for (u32 i=0; i<sizeof(ram); ++i) CHECK(ram[i] == before[i], "low memory unchanged");
    }
    output("PASS kcg boot: NOSYS, ROM only, low writes=0 file I/O=0\n",
           sizeof("PASS kcg boot: NOSYS, ROM only, low writes=0 file I/O=0\n")-1);
    stop(0);
}
