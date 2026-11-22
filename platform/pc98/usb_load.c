#include "usb_resident.h"
#include "usb_imports.h"
#include "platform_module_pair.h"
#include "config.h"
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
    return rc ? rc : module_start(owner);
}
