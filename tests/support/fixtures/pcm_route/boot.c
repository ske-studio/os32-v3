static void model_halt(void);
#define _halt model_halt
#include "sys/dev/sound/cs4231/pcm_module.c"

static int available, reserved, route, id, family, fm_called, pcm_called, accepted;
static int route_reads, mask, opens, state, fail_stop, handled, r2, early_pi;
static unsigned written;

struct irq_line irq_lines[IRQ_DYN_COUNT];
int irq_dyn_index(unsigned irq) { return irq == PCM_IRQ && !reserved ? 0 : -1; }
void platform_irq_mask(unsigned irq) { mask |= 16; }
void platform_irq_unmask(unsigned irq) { mask &= ~16; }
int irq_quarantine_line(unsigned irq)
{
    irq_lines[0].quarantined = 1;
    platform_irq_mask(irq);
    return 0;
}
static irq_handler_fn installed;
static int model_handler(unsigned irq, void *arg) { return handled; }
int res_owner_get(void) { return 1; }
struct module_record *module_record_get(struct module_handle handle)
{
    static struct module_record record;
    return &record;
}
int module_ref_acquire(struct module_handle h, u32 owner, enum module_ref_kind kind, u32 fn)
{
    return 1;
}
int module_ref_release(struct module_handle h, enum module_ref_kind kind) { return 1; }
int irq_register(unsigned irq, irq_handler_fn fn, void *arg, unsigned flags)
{
    if (irq != PCM_IRQ || installed) return IRQ_ERR_BUSY;
    installed = fn;
    ++irq_lines[0].count;
    platform_irq_unmask(irq);
    return 0;
}
int irq_unregister(unsigned irq, irq_handler_fn fn, void *arg)
{
    if (irq != PCM_IRQ || installed != fn) return IRQ_ERR_INVAL;
    installed = 0;
    --irq_lines[0].count;
    platform_irq_mask(irq);
    return 0;
}
static int model_state(void) { return state; }
static int model_open(u32 rate)
{
    ++opens;
    state = PCM_ST_OPEN;
    written = 0;
    int rc = pcm_module_irq_register(PCM_IRQ, model_handler, 0, IRQ_F_SHARED);
    if (!rc && early_pi) {
        handled = IRQ_HANDLED;
        installed(PCM_IRQ, 0);
        handled = IRQ_NONE;
    }
    return rc;
}
static int model_write(const void *buf, u32 bytes)
{
    written += bytes;
    if (written >= PCM_RING_FRAMES * PCM_FRAME_BYTES) state = PCM_ST_RUNNING;
    return bytes;
}
volatile u32 tick_count;
static void model_halt(void)
{
    ++tick_count;
    installed(PCM_IRQ, 0);
}
static int model_close(void)
{
    if (pcm_module_irq_unregister(PCM_IRQ, model_handler, 0)) return OS32_ERR_IO;
    state = fail_stop ? PCM_ST_FAULTED : PCM_ST_CLOSED;
    return fail_stop ? OS32_ERR_IO : 0;
}
static int window(void)
{
    reserved = pcm_quarantined = 0;
    pcm_irq_line = PCM_IRQ;
    route = bios_route = 0x20;
    candidate.state = model_state;
    candidate.open = model_open;
    candidate.write = model_write;
    candidate.close = model_close;
    irq_lines[0].count = 1;
    if (pcm_diag_wss_irq() != OS32_ERR_BUSY || opens) return 0;
    irq_lines[0].count = 0;
    handled = IRQ_HANDLED;
    if (pcm_diag_wss_irq() || mask || irq_lines[0].count) return 0;
    mask = 0xff;
    if (pcm_diag_wss_irq() || mask != 0xff) return 0;
    r2 = 1;
    if (pcm_diag_wss_irq() != OS32_ERR_IO) return 0;
    r2 = 0;
    handled = IRQ_NONE;
    early_pi = 1;
    if (pcm_diag_wss_irq() != OS32_ERR_IO) return 0;
    mask = 0;
    fail_stop = 1;
    if (pcm_diag_wss_irq() != OS32_ERR_IO || mask != 16 || !irq_lines[0].quarantined)
        return 0;
    return pcm_diag_wss_irq() == OS32_ERR_BUSY && opens == 5;
}

unsigned inp(unsigned port)
{
    if (port == PIC2_DATA) return mask;
    if (port == PCM_PORT_R2) return r2;
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
             exercise(1, 0, 0x20, 4, 0x73, 1) &&
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
    if (ok && !window()) ok = -1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(ok < 0 ? 2 : !ok));
    __builtin_unreachable();
}
