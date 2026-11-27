#include "sys/dev/sound/cs4231/pcm_module.c"

static int available, reserved, route, id, family, fm_called, pcm_called, accepted;
static int route_reads;

unsigned inp(unsigned port)
{
    if (port == PCM_PORT_ROUTE) { ++route_reads; return route; }
    if (port == PCM_PORT_WSSID) return id;
    return family;
}

int platform_irq_reserved(unsigned irq) { return irq != 12 || reserved; }
static int memory_boot_dma_available(void) { return available; }
static int fm_module_load(void) { fm_called = 1; return -1; }
void kprintf(u8 attr, const char *fmt, ...) {}
int tick_register(unsigned slot, tick_fn callback) { return 0; }
int platform_module_pair(u32 *out) { *out = 1; return 1; }
int module_load(const struct module_request *request, struct module_handle *handle)
{
    return fm_called ? 0 : MODULE_INIT_FAILED;
}
int module_start(struct module_handle handle) { ++accepted; return 0; }
const u32 *module_exports(struct module_handle handle)
{
    static const u32 exports;
    return &exports;
}

static void observe_pcm_load(void)
{
    ++pcm_called;
    pcm_module_load();
}

#define pcm_module_load observe_pcm_load
static void boot_sound(void)
{
#include "boot_calls.inc"
    (void)snd_rc;
}

static int exercise(int dma, int fm, int value, int wss, int board, int expect)
{
    available = dma;
    reserved = fm;
    route = value;
    id = wss;
    family = board;
    bios_route = -1;
    route_sampled = fm_called = pcm_called = accepted = route_reads = 0;
    boot_sound();
    return fm_called && pcm_called == dma && accepted == expect &&
           (!expect || pcm_module_irq_line() == (fm ? 10u : 12u));
}

void _start(void)
{
    int ok = exercise(0, 0, 0x20, 4, 0xff, 0) &&
             exercise(1, 1, 0x20, 4, 0xff, 1) &&
             exercise(1, 0, 0x20, 0xff, 0xff, 0) &&
             exercise(1, 0, 0x20, 4, 0x60, 0);
    for (unsigned v = 0; v <= 255; ++v) {
        int valid = (v & 0x38) == 0x20 && ((v & 7) < 4 || (v & 7) == 7);
        ok &= exercise(1, 1, v, 4, 0xff, v != 0xff && ((v & 7) < 4 || (v & 7) == 7));
        ok &= exercise(1, 0, v, 4, 0xff, valid);
    }
    ok &= exercise(1, 0, 0x61, 4, 0xff, 1);
    route = 0x62;
    ok &= !pcm_boot_refusal() && pcm_module_bios_route() == 0x61 && route_reads == 1;
    pcm_quarantined = 1;
    ok &= pcm_boot_refusal() != 0;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(!ok));
    __builtin_unreachable();
}
