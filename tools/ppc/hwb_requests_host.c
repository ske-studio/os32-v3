/* PowerMac HW_OP §5 5b/7c: no port mechanism, check-only requests. */
#include "diag_slots.h"
#include "os32_kapi_shared.h"
#include "pci_hwb_contract.h"
#include "platform/powermac/hwb_resource.c"
static unsigned checks, accesses, windows;
static struct pm_kboot_info info;
struct pm_bridge_state *pm_bridge_lookup(int id)
{
    static struct pm_bridge_state bridge;
    return id == 42 ? &bridge : 0;
}

int pm_hwb_window_transfer(u32 pa, u32 width, int little, int store,
                           u32 count, u32 increment, u32 *value)
{
    ++windows;
    if (pa != 0x1000 || width != 4 || store || count != 1 || increment) return HW_IO_ERROR;
    *value = little ? 0x10 : 0x10000000;
    return HW_OK;
}

static u32 range_check(u32 pa, u32 width, u32 bytes)
{
    ++checks;
    return pm_hwb_phys_check(pa, width, bytes);
}
static u32 phys(struct hw_req *req)
{
    ++accesses;
    return pm_hwb_phys(req);
}
struct pm_diag_slots kdebug_import_slots = {.phys_check = range_check, .phys = phys};
#include "drivers/pci/pci_hwb.c"
#include "kdebug/hwb_access.c"

static int exercise(void)
{
    struct hw_range_req valid = {
        .req = {.version=HW_VERSION, .action=HW_PHYS_CHECK, .space=HW_PHYS,
                .width=4, .addr=0x1000}, .bytes=4
    };
    struct pm_pci_capture *pci = &info.pci;
    pci->bridges = 2;
    pci->bridge[0] = (struct pm_of_bridge){.id=42, .parent=-1, .supported=1};
    pci->bridge[1] = (struct pm_of_bridge){.parent=0, .host=0};
    pci->functions = pci->assigned = 1;
    pci->function[0] = (struct pm_of_function){.bridge=1, .vendor=0x106b,
        .device=0x003f, .class_code=0x0c0310};
    pci->assignment[0] = (struct pm_of_assignment){0, 0x02005a10, 0x3000, 0x100};
    pci->ranges = 2;
    pci->range[0] = (struct pm_of_range){1, 2, 2, 0x3000, 0x4000, 0x100};
    pci->range[1] = (struct pm_of_range){0, 2, 0, 0x4000, 0x1000, 0x100};
    pm_hwb_resource_init(&info);
    for (unsigned size = 0; size <= sizeof(valid)+1; ++size) {
        if (size == sizeof(valid)) continue;
        if (hwb_access(&valid, size) != OS32_ERR_INVAL || checks || accesses) return 10;
    }
    for (unsigned i = 0; i < 11; ++i) {
        struct hw_range_req bad = valid;
        switch (i) {
        case 0: bad.req.space = HW_IO; break;
        case 1: bad.req.width = 3; break;
        case 2: bad.req.bdf = 1; break;
        case 3: bad.bar = 1; break;
        case 4: bad.offset = 1; break;
        case 5: bad.req.value = 1; break;
        case 6: bad.req.status = 1; break;
        case 7: bad.req.addr++; break;
        case 8: bad.bytes = 0; break;
        case 9: bad.bytes++; break;
        case 10: bad.req.addr = 0xfffffffcU; bad.bytes = 8; break;
        }
        if (hwb_access(&bad, sizeof(bad)) || bad.req.status != HW_ARG ||
            bad.req.value || checks || accesses) return 11;
    }
    for (unsigned i = 0; i < 2; ++i) {
        struct hw_range_req r = valid;
        r.req.addr += i*4;
        i32 rc = hwb_access(&r, sizeof(r));
        if (windows) return 17;
        if (rc || accesses || checks != i+1 ||
            r.req.status != (i ? HW_WINDOW : HW_OK) || r.req.value) return 12;
    }
    for (unsigned width = 1; width <= 4; width *= 2) {
        for (unsigned action = HW_READ; action <= HW_WRITE; ++action) {
            struct hw_req r = {.version=HW_VERSION, .action=action, .width=width,
                               .space=HW_IO, .addr=0x80, .value=action == HW_WRITE};
            if (hwb_access(&r, sizeof(r)) || r.status != HW_NOSYS ||
                r.value != (action == HW_WRITE) || accesses) return 13;
        }
    }
    u32 pa = 0;
    if (windows || pm_hwb_bar(0x5a, 0, HW_PHYS, 0x3000, 0, 4, &pa) || pa != 0x1000)
        return 14;
    struct hw_req read = {.action=HW_READ, .width=4, .addr=pa};
    if (pm_hwb_phys(&read) || windows != 1 || read.value != 0x10) return 15;
    pci->function[1] = (struct pm_of_function){.bridge=0};
    pci->functions++;
    pci->assignment[1] = (struct pm_of_assignment){1, 0x02006010, 0x4002, 4};
    pci->assigned++;
    pm_hwb_resource_init(&info);
    if (pm_hwb_phys(&read) != HW_WINDOW || windows != 1) return 16;
    return 0;
}
void _start(void)
{
    int rc = exercise();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
