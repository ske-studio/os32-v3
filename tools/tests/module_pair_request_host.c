/* Compile one product adapter per executable. Observe its request at module_load. */
#include "module_runtime.h"
#include "platform_module_pair.h"

static u32 expected_valid;
static int getter_calls;

static __attribute__((noreturn)) void finish(int status)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}

int platform_module_pair(u32 *out)
{
    getter_calls++;
    if (expected_valid) *out = 0x89abcdefU;
    return expected_valid;
}

inline __attribute__((always_inline)) int module_load(const struct module_request *q,
                                                       struct module_handle *out)
{
    (void)out;
    if (getter_calls != 1 || q->policy.img_valid != expected_valid ||
        q->policy.pair_image_crc != (expected_valid ? 0x89abcdefU : 0)) finish(1);
#ifdef PAIR_USB
    *out = (struct module_handle){0};
    return expected_valid ? 0 : MODULE_NOT_FOUND;
#else
    finish(0);
#endif
}

#ifdef PAIR_FEP
#include "host_arch/arch_io.h"
#endif
#include PAIR_ADAPTER

#ifdef PAIR_USB
static int boot_lines, skip_lines, starts, dma_available;
int memory_boot_dma_available(void) { return dma_available; }
int module_start(struct module_handle owner)
{
    (void)owner;
    starts++;
    return MODULE_INIT_FAILED;
}
void kprintf(u8 attr, const char *fmt, ...)
{
    const char *expected = "[BOOT] usb module rc=%d pair=%08x\n";
    if (!dma_available) expected = "[BOOT] usb module skipped: DMA unavailable\n";
    va_list args;
    va_start(args, fmt);
    while (*expected && *expected == *fmt) { expected++; fmt++; }
    if (!dma_available && !*fmt && !*expected && attr == KLOG_ATTR) skip_lines++;
    if (dma_available && !*fmt && !*expected && attr == KLOG_ATTR &&
        va_arg(args, int) == (expected_valid ? MODULE_INIT_FAILED : MODULE_NOT_FOUND) &&
        va_arg(args, u32) == (expected_valid ? 0x89abcdefU : 0)) boot_lines++;
    va_end(args);
}
#endif

#if defined(PAIR_PCM) || defined(PAIR_LAN)
int tick_register(unsigned int slot, tick_fn fn) { return 0; }
#elif defined(PAIR_V86)
int v86_call_init(void) { return 1; }
#elif defined(PAIR_GEFORCE)
i32 gf4_data_bind(const struct gf4_data_ops **ops, struct gf4_state **state) { return 0; }
struct geforce_platform *geforce_platform_get(void) { return 0; }
#elif defined(PAIR_TRIDENT)
const struct trident_resources *trident_resources_get(void)
{
    static const struct trident_resources resources;
    return &resources;
}
u32 cpu_loops_per_tick(void) { return 1; }
#elif defined(PAIR_FEP)
int services_public;
enum services_stage services_stage;
void input_ime_action_flush(void) {}
void kbd_policy_ime_state(u32 active, u32 owner, u32 command_gen) {}
int input_ime_cancel_take(const KbdPolicySnapshot *current) { return 0; }
#elif defined(PAIR_SQLITE)
const SqliteModuleApi sqlite_stub_api;
#elif defined(PAIR_KDEBUG)
int sysconfig_get_int(const char *path, const char *key, int def) { return 1; }
#endif

void _start(void)
{
    int count;
    __asm__ volatile("int $0x80" : "=a"(count) : "0"(3), "b"(0),
                     "c"(&expected_valid), "d"(sizeof(expected_valid)) : "memory");
    if (count != sizeof(expected_valid)) finish(2);
#ifdef PAIR_SQLITE
    sqlite_module_api = &sqlite_stub_api;
#endif
#ifdef PAIR_USB
    pc98_usb_boot();
    if (getter_calls || starts || boot_lines || skip_lines != 1) finish(3);
    dma_available = 1;
    pc98_usb_boot();
    finish(boot_lines != 1 || skip_lines != 1 || starts != (int)expected_valid);
#else
    PAIR_ENTRY;
    finish(2);
#endif
}
