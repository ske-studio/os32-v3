/* Real KMOD relocation for the existing trace oracles. Hardware stays fake. */
#include "kernel/module_image.h"
#include "hwdev/generated/hwdev_imports.h"
#include "hwdev_pair.h"
#include "hwdev_trace_symbols.h"
static u8 image[MODULE_IMAGE_MAX] __attribute__((aligned(4096)));
static u32 body;
static void fail(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(2));
    __builtin_unreachable();
}
static void read_all(void *p, u32 bytes)
{
    while (bytes) {
        int n;
        __asm__ volatile("int $0x80" : "=a"(n) : "0"(3), "b"(0), "c"(p), "d"(bytes) : "memory");
        if (n <= 0) fail();
        p = (u8 *)p + n; bytes -= n;
    }
}
u32 hwdev_image_symbol(u32 offset) { return body + offset; }
void hwdev_trace_load(const u32 *imports)
{
    (void)hwdev_fill_imports;
    u32 size;
    read_all(&size, 4);
    if (size > sizeof(image)) fail();
    read_all(image, size);
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(image), "c"(sizeof(image)), "d"(7) : "memory");
    if (rc) fail();
    const struct module_image_policy policy = {
        KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, HWDEV_CONTRACT_CRC,
        HWDEV_KERNEL_PAIR_CRC, HWDEV_ABI, 1, HWDEV_IMPORT_COUNT,
        hwdev_import_contracts, hwdev_export_contracts, MODULE_ID_HWDEV
    };
    struct module_image_plan plan;
    u32 base = (u32)(uptr)image + ((u32 *)image)[MI_PAYLOAD_OFF];
    if (module_image_relocate(image, size, &policy, base, &plan)) fail();
    body = (u32)(uptr)image + plan.word[MI_PAYLOAD_OFF];
    u32 *slots = (u32 *)(uptr)(body + TRACE_IMPORT_SLOTS);
    for (u32 i = 0; i < HWDEV_IMPORT_COUNT; i++) slots[i] = imports[i];
}
