/* PPC_HWOP_BACKEND_DESIGN §5 5b/7b/7c: port status, profile bounds,
 * device byte order and check-only requests. */
#include "diag_slots.h"
#include "os32_kapi_shared.h"
#include "pci_hwb_contract.h"
#include "sys/platform/powermac/hwb_resource.c"
static unsigned checks, accesses, windows, stores;
static u8 backing[16] = {0x10};
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
    if (pa < 0x1000 || (u64)pa + count*width > 0x1000 + sizeof(backing)) return HW_IO_ERROR;
    for (unsigned i = 0; i < count; ++i) {
        u8 *cell = backing + (pa - 0x1000) + i*width;
        u32 v = *value + i*increment;
        if (!store) *value = 0;
        for (unsigned j = 0; j < width; ++j) {
            unsigned shift = 8*(little ? j : width-1-j);
            if (store) cell[j] = v >> shift;
            else *value |= (u32)cell[j] << shift;
        }
        if (store) ++stores;
    }
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
struct pm_diag_slots kdebug_import_slots = {.phys_check = range_check, .phys = phys, .block = pm_hwb_block};
#include "sys/dev/pci/pci_hwb.c"
#include "sys/kern/kdebug/hwb_access.c"

/* Install two orders in turn in this TU's private resource table. Production
 * OF capture only admits the two documented read-only device profiles. */
static int memory_cases(void)
{
    for (unsigned little = 0; little <= 1; ++little) {
        resource_count = 1;
        resources[0] = (struct hwb_assignment){.bdf=0x5a, .bar0=1, .pa=0x1000,
            .base=0x3000, .bytes=16, .reg_bytes=16, .little=little, .writable=1, .widths=7};
        for (u32 width = 1; width <= 4; width *= 2) {
            for (u32 count = 1; count <= 2; ++count) {
                u32 bytes = count*width, off = 16-bytes, addr;
                if (pm_hwb_bar(0x5a,0,HW_PHYS,0x3000,off,bytes,&addr) ||
                    addr != 0x1000+off) return 23;
                u32 mask = U32_MAX >> ((4-width)*8);
                struct hw_block_req b = {
                    .req={HW_VERSION,HW_WRITE,HW_PHYS,width,0,addr,0x123456fe & mask,0},
                    .count=count, .increment=1};
                if (count == 2) b.req.action = width == 4 ? HW_BLOCK32 :
                                              width == 2 ? HW_BLOCK16 : HW_BLOCK8;
                if (hwb_access(&b,count == 1 ? sizeof(b.req) : sizeof(b)) || b.req.status)
                    return 24;
                for (u32 i = 0; i < bytes; ++i) {
                    u32 shift = 8*(little ? i%width : width-1-i%width);
                    if (backing[off+i] != (u8)((b.req.value+i/width) >> shift)) return 25;
                }
                struct hw_req read = {HW_VERSION,HW_READ,HW_PHYS,width,0,0x1010-width,0,0};
                if (hwb_access(&read,sizeof(read)) || read.status ||
                    read.value != ((b.req.value + count-1) & mask)) return 26;
                if (count == 2) {
                    unsigned before = stores;
                    ++b.count;
                    if (hwb_access(&b,sizeof(b)) || b.req.status != HW_WINDOW ||
                        stores != before) return 27;
                }
            }
        }
    }
    return 0;
}

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
    pm_hwb_resource_init(&info, 0);
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
    struct hw_block_req block = {.req={HW_VERSION,HW_BLOCK32,HW_PHYS,4,0,pa,1,0}, .count=1};
    struct hw_req write = {HW_VERSION,HW_WRITE,HW_PHYS,4,0,pa,1,0};
    if (pm_hwb_block(&block) != HW_WINDOW || pm_hwb_phys(&write) != HW_WINDOW ||
        windows != 1 || stores) return 28;
    pci->assignment[0].bytes = 4;
    pm_hwb_resource_init(&info, 0);
    if (pm_hwb_bar(0x5a, 0, HW_PHYS, 0x3000, 0, 4, &pa) ||
        pm_hwb_bar(0x5a, 0, HW_PHYS, 0x3000, 0, 8, &pa) != HW_ARG ||
        pm_hwb_bar(0x5a, 0, HW_PHYS, 0x3004, 0, 4, &pa) != HW_WINDOW) return 18;
    pci->range[1].bytes = 3;
    pm_hwb_resource_init(&info, 0);
    if (pm_hwb_phys_check(pa, 4, 4) != HW_WINDOW) return 19;
    pci->range[1].bytes = 0x100;
    pci->function[0].device++;
    pm_hwb_resource_init(&info, 0);
    if (pm_hwb_phys_check(pa, 4, 4) != HW_WINDOW) return 20;
    pci->function[0].device--;
    pci->function[1] = (struct pm_of_function){.bridge=0};
    pci->functions++;
    pci->assignment[1] = (struct pm_of_assignment){1, 0x02006010, 0x4002, 4};
    pci->assigned++;
    pm_hwb_resource_init(&info, 0);
    if (pm_hwb_phys(&read) != HW_WINDOW || windows != 1) return 16;
    pci->assigned = 1;
    pci->assignment[0].bytes = pci->range[0].bytes = pci->range[1].bytes = 0x6038;
    pci->function[0].id = 7;
    pci->function[0].device = 0x21;
    info.gmac = (struct pm_gmac_boot){.profile=PM_GMAC_PROFILE_UNINORTH, .node=7,
        .vendor_device=0x0021106b, .root_node=42, .bdf=0x5a00, .bar_pa=0x1000, .bar_bus=0x3000};
    pm_hwb_resource_init(&info, 0);
    read.addr = 0x7030;
    if (pm_hwb_phys(&read) != HW_WINDOW || windows != 1 ||
        pm_hwb_bar(0x5a, 0, HW_PHYS, 0x3000, 0x6030, 4, &pa) != HW_WINDOW) return 29;
    pm_hwb_resource_init(&info, 1);
    for (u32 off = 0x6030; off <= 0x6034; off += 4)
        if (pm_hwb_bar(0x5a, 0, HW_PHYS, 0x3000, off, 4, &pa) || pa != 0x1000+off)
            return 21;
    if (pm_hwb_phys_check(pa+4, 4, 4) != HW_WINDOW ||
        pm_hwb_phys_check(pa-4, 4, 8) != HW_WINDOW) return 22;
    return memory_cases();
}
void test_fill(void);

int fill_dispatch(u32 op, void *buf, u32 size)
{
    return op == HW_OP ? hwb_access(buf, size) : OS32_ERR_INVAL;
}

void setup_fill_window(void)
{
    resource_count = 1;
    resources[0] = (struct hwb_assignment){.pa=0x1000, .bytes=4, .reg_bytes=4,
                                           .little=1, .writable=1, .widths=2};
    stores = 0;
}

unsigned fill_stores(void) { return stores; }
int fill_contents_ok(void)
{
    return backing[0] == 0x34 && backing[1] == 0x12 &&
           backing[2] == 0x34 && backing[3] == 0x12;
}

void _start(void)
{
    int rc = exercise();
    if (!rc) test_fill();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
