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
static int busy, busy_on_address;
static u32 polls, faults;
static void fault(void) { faults++; }
static u32 busy_after;
static void port_write(u32 port, u32 value)
{
    if (port == OPN_ADDR) {
        reg = value;
        if (busy_on_address) busy = 1;
    }
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
#define inp(p) ((void)(p), polls++, (busy || (busy_after && writes >= busy_after)) ? OPN_BUSY : 0)
#define outp(p,v) port_write(p,v)
#define io_wait() ((void)0)
#define io_wait_n(n) ((void)(n))
#define _halt() ((void)0)
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
#define OS32_FM_MODULE 1
#include FM_SOURCE
#include BODY_SOURCE
#include "kstring.h"

static enum opn_probe_result detected;
enum opn_probe_result opn_probe(unsigned char shadow, int *safe)
{
    (void)shadow;
    *safe = 1;
    return detected;
}

int res_owner_get(void) { return 5; }
void *kmemcpy(void *dst, const void *src, u32 n)
{
    while (n--) ((u8 *)dst)[n] = ((const u8 *)src)[n];
    return dst;
}

static int caps(void *ctx, SND2_ChipCaps *out)
{
    CHECK(((struct opn_chip *)ctx)->model == detected);
    out->banks = 1;
    return 0;
}
static int quiet_error;
static int quiet_chip(void *ctx) { return quiet_error; }
static int ok(void *ctx) { (void)ctx; return 0; }
static int write_regs(void *ctx, const SND2_RegW *w, u32 n) { return n; }
static int read_reg(void *ctx, u32 bank, u32 what, u32 reg) { return values[reg]; }
const SndChipBackend opn_chip_backend = {
    .abi = SND_HAL2_ABI, .caps = caps, .open = ok, .close = ok, .quiet = quiet_chip,
    .write = write_regs, .read = read_reg
};

static void registration(void)
{
    FmModuleApi api;
    CHECK(!fm_module_init(&api));
    detected = OPN_PROBE_ABSENT;
    CHECK(!api.attach(snd2_chip_fault));
    CHECK(snd2_chip_implicit(5) == OS32_ERR_NOTFOUND && polls == 0);
    CHECK(api.quiet() && api.detach());
    detected = OPN_PROBE_OPN;
    CHECK(!api.attach(snd2_chip_fault));
    CHECK(!snd2_chip_implicit(5));
    api.ssg_volume(0, 1);
    values[SSG_REG_MIXER] = SSG_MIXER_IO_FLAG | SSG_MIXER_ALL_OFF;
    api.ssg_volume(0, 1);
    CHECK(!(values[SSG_REG_MIXER] & 1));
    CHECK(api.detach() && snd2_chip_implicit(5) == OS32_ERR_NOTFOUND);
    CHECK(!api.attach(snd2_chip_fault) && !snd2_chip_implicit(5));
    polls = 0;
    busy = 1;
    api.fm_note_on(0, 60);
    CHECK(polls == OPN_BUSY_TIMEOUT && snd2_chip_implicit(5) == OS32_ERR_IO);
    CHECK(!api.detach());
    busy = 0;
}
void _start(void)
{
    char scenario = 0;
    int count;
    __asm__ volatile("int $0x80" : "=a"(count) : "0"(3), "b"(0),
                     "c"(&scenario), "d"(1) : "memory");
    if (count == 1 && scenario == 'q') {
        FmModuleApi api;
        detected = OPN_PROBE_OPNA;
        CHECK(!fm_module_init(&api) && !api.attach(snd2_chip_fault));
        CHECK(!snd2_chip_implicit(5));
        quiet_error = OS32_ERR_IO;
        CHECK(api.quiet() && !api.detach());
        CHECK(snd2_chip_implicit(5) == OS32_ERR_IO);
        finish(0);
    }
    registration();
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
    fm_write_failed = fault;
    polls = 0;
    fm_note_on(0, 60);
    CHECK(faults == 1 && polls == OPN_BUSY_TIMEOUT);
    busy = 0;
    busy_on_address = 1;
    before = writes;
    fm_note_on(0, 60);
    CHECK(faults == 2 && writes == before);
    finish(0);
}
