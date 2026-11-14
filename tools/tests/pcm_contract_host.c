/* Freestanding reuse of the existing device model; no libc or actual I/O. */
#include "types.h"
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
static void report(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
void *memcpy(void *d, const void *s, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = ((const u8 *)s)[i];
    return d;
}
void *memset(void *d, int c, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = (u8)c;
    return d;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
#define fprintf(stream, fmt, ...) report("FAIL " #__VA_ARGS__ "\n")
#define main fixture_main
#include "fixture.c"
#undef main
#include "pcm_module.h"
static int loaded;
static struct module_handle pcm_handle;
static PcmModuleApi candidate;
const u32 *module_exports(struct module_handle h)
{
    (void)h;
    return loaded ? (const u32 *)&candidate : 0;
}
#include "proxy.c"
#define KAPI_HIT(slot) ((void)(slot))
#define KAPI_OUT_LEN(p, n) ((p) ? (n) : 0)
static u32 range_calls;
static int ring3_user_range_ok(u32 a, u32 n)
{
    (void)n; range_calls++;
    return a != 0;
}
static int ring3_user_ranges_writable(u32 a, u32 n, u32 b, u32 m)
{
    (void)a; (void)n; (void)b; (void)m;
    return 1;
}
static void ring3_fault_kill(void) { finish(2); }
#include "wrappers.c"
#include "ppc.c"
static void device_irq(void) { (void)pcm_irq(PCM_IRQ, 0); }
static void playback_wait(void)
{
    u32 half = pcm_pos_frames(sim_left) / PCM_HALF_FRAMES;
    dev_tick();
    if (pcm_pos_frames(sim_left) / PCM_HALF_FRAMES != half) run_isr(device_irq);
    run_isr(pcm_tick);
}
void _start(void)
{
    static u8 data[16388];
    u32 freeb = 123, counters = 456;
    CHECK(PCM_RING_FRAMES == 4096 && PCM_RING_BYTES == 16384);
    CHECK(PCM_FRAME_BYTES == 4 && PCM_STG_FRAMES == 4096 && PCM_REFILL_MARGIN == 512);
    CHECK(wrap_pcm_open(48000) == OS32_ERR_INVAL);
    CHECK(wrap_pcm_open(44100) == OS32_ERR_NOSYS);
    CHECK(wrap_pcm_open(22050) == OS32_ERR_NOSYS);
    CHECK(wrap_pcm_write(0, 0) == OS32_ERR_INVAL && range_calls == 0);
    CHECK(wrap_pcm_status(&freeb, &counters) == OS32_ERR_INVAL);
    CHECK(freeb == 123 && counters == 456);
    CHECK(wrap_pcm_close() == OS32_ERR_INVAL);
    CHECK(wrap_pcm_set_volume(50) == OS32_ERR_INVAL);
    CHECK(proxy_pcm_state() == PCM_ST_CLOSED && !proxy_pcm_present());
    candidate = (PcmModuleApi){.open=pcm_open, .write_check=pcm_write_check,
        .write=pcm_write, .status=pcm_status, .close=pcm_close,
        .set_volume=pcm_set_volume, .state=pcm_state, .present=pcm_present,
        .reclaim=pcm_reclaim};
    loaded = 1;
    reset_all(); dev_present = 0;
    CHECK(wrap_pcm_open(44100) == OS32_ERR_NOSYS);
    CHECK(proxy_pcm_state() == PCM_ST_CLOSED);
    for (u32 rate = 22050; rate <= 44100; rate *= 2) {
        reset_all();
        CHECK(wrap_pcm_open(rate) == 0);
        CHECK(wrap_pcm_open(rate) == PCM_ERR_BUSY);
        CHECK(wrap_pcm_status(&freeb, &counters) == 0 && freeb == 16384 && counters == 0);
        CHECK(wrap_pcm_write(0, 0) == OS32_ERR_INVAL);
        CHECK(wrap_pcm_write(data, 3) == 0);
        CHECK(wrap_pcm_write(data, 16380) == 16380);
        CHECK(proxy_pcm_state() == PCM_ST_OPEN && sim_masked);
        CHECK(wrap_pcm_write(data, 7) == 4); /* truncate to one whole frame */
        CHECK(proxy_pcm_state() == PCM_ST_RUNNING && !sim_masked);
        CHECK(wrap_pcm_write(data, 16388) == 16384); /* partial acceptance */
        CHECK(wrap_pcm_write(data, 4) == 0); /* full, caller must retry */
        dev_play = 1;
        wait_probe = playback_wait; /* half-boundary IRQ plus periodic tick */
        CHECK(wrap_pcm_close() == 0);
        CHECK(proxy_pcm_state() == PCM_ST_CLOSED && !sim_irq_registered);
        CHECK(!pool_used[0] && !heap_used);
        CHECK(wrap_pcm_close() == OS32_ERR_INVAL);
        CHECK(wrap_pcm_write(data, 0) == OS32_ERR_INVAL);
        CHECK(wrap_pcm_status(&freeb, &counters) == OS32_ERR_INVAL);
        CHECK(wrap_pcm_open(rate) == 0);
        sim_set_pos(0); /* model the reprogrammed DMA counter on reopen */
        CHECK(wrap_pcm_write(data, 4) == 4); /* short stream starts on close */
        CHECK(proxy_pcm_state() == PCM_ST_OPEN);
        CHECK(wrap_pcm_close() == 0 && proxy_pcm_state() == PCM_ST_CLOSED);
        CHECK(wrap_pcm_open(rate) == 0 && wrap_pcm_close() == 0); /* empty */
    }
    /* PPC absent providers intentionally differ from the x86 unloaded proxy. */
    freeb = 123; counters = 456;
    CHECK(ppc_pcm_open(44100) == OS32_ERR_NOSYS);
    CHECK(ppc_pcm_open(48000) == OS32_ERR_NOSYS);
    CHECK(ppc_pcm_write(0, 0) == OS32_ERR_NOSYS);
    CHECK(ppc_pcm_status(&freeb, &counters) == OS32_ERR_NOSYS);
    CHECK(freeb == 123 && counters == 456);
    CHECK(ppc_pcm_close() == OS32_ERR_NOSYS);
    CHECK(ppc_pcm_set_volume(101) == OS32_ERR_NOSYS);
    if (!failed) report("GREEN PCM contract\n");
    finish(failed != 0);
}
