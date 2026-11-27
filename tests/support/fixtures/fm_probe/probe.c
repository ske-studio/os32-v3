#include "idt.h"
#include "sys/dev/sound/fm/fm_module.c"
#include "opn_chip.h"
#include "sys/platform/pc98/pic.c" /* Reset boot-only latches between machine scenarios. */

extern int fm_module_init(FmModuleApi *);
const SndChipBackend opn_chip_backend;
void (*fm_write_failed)(void);
int (*fm_read_mixer)(void);
volatile u32 kctx_irq_depth;

static enum opn_probe_result registered;
static unsigned status, id, index_reg, feature, alt_status, alt_data;
static unsigned writes, reads, alt_reads, imr_writes, mask = 0xff, elapsed;
static unsigned busy_after_ff, busy_reads, timer_reads;
static int frozen_clock, unstable_feature;
static unsigned feature_reads;
static unsigned trace[16], trace_count;
static int ticks, fail_stop, fail_restore, restores;

static void require(int ok, const char *why)
{
    if (ok) return;
    unsigned n = 0;
    while (why[n]) ++n;
    __asm__ volatile("int $0x80" :: "a"(4), "b"(2), "c"(why), "d"(n) : "memory");
    __asm__ volatile("int $0x80" :: "a"(1), "b"(1));
    __builtin_unreachable();
}

void *kmemcpy(void *dst, const void *src, u32 n)
{
    for (u32 i = 0; i < n; ++i) ((uchar *)dst)[i] = ((const uchar *)src)[i];
    return dst;
}
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int sys_time_now(u32 *lo, u32 *hi)
{
    if (lo) *lo = elapsed;
    if (hi) *hi = 0;
    return 0;
}
void snd2_chip_fault(u32 dev) { (void)dev; }
int snd2_chip_register(const SndChipBackend *be, void *chip, const SND2_DevInfo *info)
{
    (void)be; (void)info;
    registered = ((struct opn_chip *)chip)->model;
    return 1;
}
int snd2_chip_unregister(u32 device) { (void)device; registered = OPN_PROBE_UNKNOWN; return 0; }
const u32 *module_exports(struct module_handle handle) { (void)handle; return 0; }
int res_owner_get(void) { return 0; }
int snd2_chip_implicit(int owner) { (void)owner; return 0; }

static int sound_irq(unsigned irq, void *arg) { (void)irq; (void)arg; return IRQ_NONE; }

unsigned inp(unsigned port)
{
    ++reads;
    if (!frozen_clock) elapsed += 5;
    if (port == 0xa460) return feature ^ (feature_reads++ && unstable_feature);
    if (port == 0xa468) return 0;
    if (port == 0x188) {
        if (index_reg == 0xff && busy_reads < busy_after_ff) {
            ++busy_reads;
            return 0x80;
        }
        if (trace_count && trace[trace_count - 1] == 0x18a0a) ++timer_reads;
        return status;
    }
    if (port == 0x18a) return id;
    if (port == 0x288 || port == 0x28a) {
        ++alt_reads;
        return port == 0x288 ? alt_status : alt_data;
    }
    if (port == 0x0a) return mask;
    return 0xff;
}
void outp(unsigned port, unsigned value)
{
    ++writes;
    require(port != 0x288 && port != 0x28a, "alternate write");
    if (port == 0x188 || port == 0x18a) {
        require(trace_count < 16, "excess primary writes");
        trace[trace_count++] = (port << 8) | value;
    }
    if (port == 0x188) index_reg = value;
    if (port == 0x18a && index_reg == 0x27) {
        status = ticks && value == 0x0a ? 2 : 0;
        if (fail_stop && value == 0x30) status = 0x80;
    }
    if (port == 0x18a && index_reg == 0x29 && ++restores == 2 && fail_restore)
        status = 0x80;
    if (port == 0x0a || port == 2) {
        ++imr_writes;
        if (port == 0x0a) mask = value;
    }
}

static struct module_record module = {.text = 1, .text_size = 0xfffffffe};
static int start(void)
{
    return fm_start(&module, (u32)(uptr)fm_module_init, 0);
}
static void reset(void)
{
    require(fm_detach(&module), "detach");
    boot_probe = (struct fm_probe_record){0};
    trace_count = writes = reads = elapsed = alt_reads = imr_writes = restores = 0;
    index_reg = busy_after_ff = busy_reads = timer_reads = frozen_clock = 0;
    fm_irq12_released = fm_irq12_blocked = unstable_feature = feature_reads = 0;
    status = id = alt_status = alt_data = feature = 0xff;
}
static void check_opna_wait(unsigned delay, int frozen)
{
    reset();
    frozen_clock = frozen;
    busy_after_ff = delay;
    status = 0;
    id = 1;
    struct fm_probe_record record = {0};
    opn_probe_once(&record, 0, 0, sys_time_now);
    /* Native write sequence and sample boundary from main d15296e91. */
    if (delay == 10000) {
        require(record.native_model == OPN_PROBE_UNKNOWN &&
                busy_reads == delay && trace_count == 1 && trace[0] == 0x188ff,
                "poll boundary");
        require(!record.contained, "unconfirmed stop");
    } else {
        require(record.native_model == OPN_PROBE_OPNA && record.contained &&
                busy_reads == delay && trace_count == 3 && trace[0] == 0x188ff &&
                trace[1] == 0x18827 && trace[2] == 0x18a30, "poll boundary");
    }
}
static void check_board(unsigned value, enum opn_probe_result expected)
{
    reset();
    feature = value;
    require(!start() && boot_probe.bank[0].model == expected,
            expected == OPN_PROBE_ABSENT ? "CS4231-only open bus" : "FM board open bus");
}

static void check_registered_bus(unsigned line, unsigned board, int unstable,
                                 unsigned primary_data, unsigned alternate, int absent)
{
    reset();
    require(platform_irq_reserved(12), "initial IRQ12 topology");
    require(!irq_register(line, sound_irq, 0, IRQ_F_SHARED), "candidate registration");
    writes = imr_writes = 0;
    feature = board;
    unstable_feature = unstable;
    id = primary_data;
    alt_data = alternate;
    require(!start() && boot_probe.native_model == OPN_PROBE_ABSENT, "registered native absence");
    require(!writes && !imr_writes, "registered IRQ write");
    if (absent && !boot_probe.bank[0].model && !boot_probe.bank[1].model)
        require(0, "registered open bus: 0188=0 0288=0");
    enum opn_probe_result expected = absent ? OPN_PROBE_ABSENT : OPN_PROBE_UNKNOWN;
    require(boot_probe.bank[0].model == expected && boot_probe.bank[1].model == expected &&
            platform_irq_reserved(12) == !absent, "registered bus evidence");
    require(!irq_unregister(line, sound_irq, 0), "candidate unregister");
}

void _start(void)
{
    check_opna_wait(410, 0);
    check_opna_wait(9999, 1);
    check_opna_wait(10000, 1);
    reset();
    require(!start() && boot_probe.bank[0].model == OPN_PROBE_ABSENT &&
            boot_probe.bank[1].model == OPN_PROBE_ABSENT, "open bus");
    require(!boot_probe.feature_port && !boot_probe.bank[0].chip_id_port, "open ID");
    reset();
    check_board(0x73, OPN_PROBE_ABSENT);
    check_board(0x70, OPN_PROBE_ABSENT);
    check_board(0x41, OPN_PROBE_UNKNOWN);
    check_board(0x51, OPN_PROBE_UNKNOWN);
    check_board(0x81, OPN_PROBE_UNKNOWN);
    check_board(0x60, OPN_PROBE_UNKNOWN);
    reset();
    status = 4;
    require(!start() && boot_probe.bank[0].model == OPN_PROBE_UNKNOWN, "unknown first");
    reset();
    status = 0;
    require(!start() && !registered, "missing timer transition");
    require(timer_reads == 10001 && boot_probe.contained, "timer poll limit");
    reset();
    status = alt_status = 0; /* Multimedia Orchestra OPL3 returns 00h/FFh. */
    ticks = 1;
    feature = 0x51;
    require(!start() && registered == OPN_PROBE_OPN, "native timer attach");
    static const unsigned native_trace[] = {
        0x188ff, 0x18827, 0x18a30, 0x18826, 0x18aff, 0x18829, 0x18a00,
        0x18827, 0x18a0a, 0x18827, 0x18a30, 0x18829, 0x18a00
    };
    require(trace_count == sizeof(native_trace) / sizeof(native_trace[0]), "native write count");
    for (unsigned i = 0; i < trace_count; ++i)
        require(trace[i] == native_trace[i], "native write sequence");
    require(boot_probe.bank[0].model == OPN_PROBE_PRESENT &&
            boot_probe.bank[1].model == OPN_PROBE_UNKNOWN, "alternate response");
    require(!boot_probe.bank[0].chip_id_port && !boot_probe.feature_port, "unverified ID");
    reset();
    alt_data = 0;
    require(!start() && boot_probe.bank[1].model == OPN_PROBE_UNKNOWN, "alternate data");
    reset();
    status = 0;
    id = 1;
    feature = 0x41;
    require(!start() && registered == OPN_PROBE_OPNA && boot_probe.bank[0].chip_id == 1 &&
            boot_probe.bank[0].chip_id_port == 0x18a && boot_probe.feature_id == feature &&
            boot_probe.feature_port == 0xa460 && boot_probe.fifo_port == 0xa468, "verified IDs");
    require(fm_quiesce(&module) && fm_detach(&module), "lifecycle");
    reads = writes = 0;
    require(!start() && registered == OPN_PROBE_OPNA && !reads && !writes, "reattach");
    reset();
    require(!irq_register(10, sound_irq, 0, IRQ_F_SHARED), "register IRQ10");
    imr_writes = 0;
    status = 0;
    require(!start() && registered == OPN_PROBE_OPN, "registered native attach");
    require(!imr_writes && !alt_reads && !boot_probe.bank[0].model &&
            !boot_probe.bank[1].model, "registered IRQ");
    require(platform_irq_reserved(12), "native IRQ12 reservation");
    require(!irq_unregister(10, sound_irq, 0), "unregister IRQ10");
    check_registered_bus(10, 0x73, 0, 0xff, 0xff, 1);
    check_registered_bus(3, 0x73, 0, 0xff, 0xff, 1);
    check_registered_bus(10, 0xff, 0, 0xff, 0xff, 1);
    check_registered_bus(10, 0x73, 0, 0xff, 0, 0);
    check_registered_bus(10, 0x73, 1, 0xff, 0xff, 0);
    check_registered_bus(10, 0x41, 0, 0xff, 0xff, 0);
    check_registered_bus(10, 0x73, 0, 0, 0xff, 0);
    reset();
    status = 0;
    mask = 0;
    fail_restore = 1;
    require(start() && !boot_probe.contained && !fm_quiesce(&module), "restore timeout");
    require(mask == 0x10, "IRQ12 isolation");
    /* Exercise a second failing start while the physical latch persists. */
    boot_probe = (struct fm_probe_record){0};
    trace_count = 0;
    status = 0;
    fail_restore = 0;
    fail_stop = 1;
    require(start() && !boot_probe.contained && !fm_detach(&module), "first stop timeout");
    irq_enable(12); /* same physical PIC entry used by V86 enter */
    require(mask & 0x10, "IRQ12 reopened");
    __asm__ volatile("int $0x80" :: "a"(1), "b"(0));
    __builtin_unreachable();
}
