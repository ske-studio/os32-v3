/* Real FM/snd sources. Only port I/O, ticks and STOP landing are replaced. */
#include "types.h"
#include "fm.h"
#include "io.h"
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); \
    finish(1); \
} } while (0)
static u32 reg, values[256], writes, keyoffs;
static int busy;
static u32 busy_after;
static void port_write(u32 port, u32 value)
{
    if (port == OPN_ADDR) reg = value;
    if (port == OPN_DATA) {
        values[reg] = value; writes++;
        if (reg == OPN_REG_KEY_ONOFF && !(value & OPN_KEY_ALL_SLOTS)) keyoffs++;
    }
}
#undef inp
#undef outp
#undef io_wait
#undef io_wait_n
#undef _halt
#undef irq_save
#undef irq_restore
#define inp(p) ((void)(p), (busy || (busy_after && writes >= busy_after)) ? OPN_BUSY : 0)
#define outp(p,v) port_write(p,v)
#define io_wait() ((void)0)
#define io_wait_n(n) ((void)(n))
#define _halt() ((void)0)
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
#define OS32_FM_MODULE 1
#include FM_SOURCE
void _start(void)
{
    fm_note_on(0, 60);
    ssg_volume(1, 12);
    u32 before = keyoffs;
    CHECK(fm_quiet() && keyoffs == before + OPN_CHANNEL_COUNT);
    CHECK(!values[SSG_REG_VOL_A] && !values[SSG_REG_VOL_B] && !values[SSG_REG_VOL_C]);
    CHECK(values[SSG_REG_MIXER] == (SSG_MIXER_ALL_OFF | SSG_MIXER_IO_FLAG));
    ssg_all_off();
    ssg_tone(1, 0x123);
    ssg_volume(1, 12);
    CHECK(values[SSG_REG_MIXER] == ((SSG_MIXER_ALL_OFF & ~2u) | SSG_MIXER_IO_FLAG));
    CHECK(values[SSG_REG_VOL_B] == 12);
    before = writes;
    busy_after = writes + 7; /* BUSY after the final mixer write. */
    CHECK(!fm_quiet() && writes == before + 7);
    busy_after = 0;
    busy = 1; before = writes;
    CHECK(!fm_quiet() && writes == before);
    finish(0);
}
