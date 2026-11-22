#include "usb_resident.h"
#include "usb_imports.h"
#include "platform_module_pair.h"
#include "config.h"
#include "memory_boot.h"
#include "kprintf.h"
#include "bootlog.h"
#include "con_sink.h"
#include "console.h"
#include "sysconfig.h"
#include "io.h"
#include "kstring.h"
#include <stdarg.h>
#include "os32_generations.h"
#include "os32_kapi_shared.h"

extern const u32 usb_kexports[];
extern const struct module_symbol_contract usb_import_contracts[], usb_export_contracts[];

int pc98_usb_load(void)
{
    struct module_handle owner;
    u32 pair = 0;
    int pair_valid = platform_module_pair(&pair);
    const struct module_request request = {
        .path = SYS_USB_MOD, .name = "usb",
        .policy = {KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, USB_MODULE_ABI_GEN,
                   pair, 1, pair_valid, USB_IMPORT_COUNT, usb_import_contracts,
                   usb_export_contracts, MODULE_ID_USB},
        .imports = usb_kexports,
        .lifecycle = {.start = pc98_usb_module_start}
    };
    int rc = module_load(&request, &owner);
    if (!rc) rc = module_start(owner);
    kprintf(KLOG_ATTR, "[BOOT] usb module rc=%d pair=%08x\n", rc, pair);
    return rc;
}

static int pc98_usb_live_log;

void pc98_usb_boot(void)
{
    pc98_usb_live_log = sysconfig_get_int(SYS_SYSTEM_CFG, "USBDIAG", 0) == 1;
    if (memory_boot_dma_available()) pc98_usb_load();
    else kprintf(KLOG_ATTR, "[BOOT] usb module skipped: DMA unavailable\n");
}

/* Live output uses the normal console and its configured serial mirror.
 * Keep masked and early output on memory-backed sinks. */
static void usb_tick_write(const char *buf, u32 len, u8 attr)
{
    if (pc98_usb_live_log && !bootlog_is_active() && _irq_enabled()) {
        console_write(buf, len, attr);
        return;
    }
    bootlog_push(buf, len);
    con_sink_push_print(buf, len, attr);
}

#define KPRINTF_WRITE usb_tick_write
#define KPRINTF_PRINT(str, attr) usb_tick_write(str, kstrlen(str), attr)
#define KPRINTF_PRINT_UTF8 KPRINTF_PRINT
#define KPRINTF_VNAME pc98_usb_tick_vprintf
#define KPRINTF_NAME pc98_usb_tick_printf
#include "../../lib/kprintf_format.inc"
