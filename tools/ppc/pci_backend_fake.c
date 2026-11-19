#include "kstring.h"
#include "lan_boot.h"
#include "pci_config_contract.h"
#include "drivers/pci/pci_bind.h"

/* M1 adds these declarations to pci_config_contract.h at integration. */
const char *pci_config_name(void);
int pci_config_root_bus(u32 idx, u32 *bus);
int pci_config_bar_measure(void);

static u32 address, last_address, last_port, data, writes, reads, stage;
u32 ppc_irq_save(void) { return 0; }
void ppc_irq_restore(u32 flags) { (void)flags; }

void pm_pci_mmio_write32(u32 va, u32 raw)
{
    u32 value = __builtin_bswap32(raw);
    if (va == 0x1000) {
        address = value;
        if (value)
            last_address = value;
    } else {
        last_port = va;
        data = value;
        writes++;
    }
}

u32 pm_pci_mmio_read32(u32 va)
{
    if (va == 0x1000)
        return __builtin_bswap32(address);
    last_port = va;
    reads++;
    return __builtin_bswap32(data);
}

int pci_init(void) { stage = 1; return 0; }
int pci_bind_all(const struct pci_driver *const *table, int count)
{
    stage = stage == 1 && !table && !count ? 2 : 0;
    return 0;
}

static int test(void)
{
    struct pm_pci window = {0x1000, 0x2000, 2, 3, 0};
    u32 bus = 99;
    if (pci_config_probe() || reads || !pci_config_root_bus(0, &bus))
        return 1;
    pm_pci_boot(&window, (2u << 16) | (12u << 11));
    window.bus_first = 0;
    data = 0x1234abcd;
    if (!pci_config_probe() || last_address != (1u << 12) || writes || stage)
        return 2;
    if (pci_config_root_bus(0, &bus) || bus != 2 ||
        pci_config_root_bus(1, &bus) != -1 || pci_config_root_bus(U32_MAX, &bus) != -1)
        return 3;
    if (pci_config_bar_measure() || kstrcmp(pci_config_name(), "uni-north"))
        return 4;
    if (pci_config_read32(3, 1, 2, 0x0e) != data || last_address != 0x30a0d ||
        last_port != 0x2004)
        return 5;
    u32 previous = reads;
    if (pci_config_read32(2, 32, 0, 0) != U32_MAX ||
        pci_config_read32(3, 0, 8, 0) != U32_MAX ||
        pci_config_read32(258, 12, 0, 0) != U32_MAX ||
        pci_config_read32(2, 12, 0, 256) != U32_MAX ||
        pci_config_read32(2, 10, 0, 0) != U32_MAX || reads != previous)
        return 6;
    pci_config_write16(2, 12, 0, 4, 0xbeef);
    if (writes != 1 || data != 0xbeef || last_port != 0x2004 ||
        last_address != ((1u << 12) | 4))
        return 7;
    pci_config_write16(2, 12, 0, 6, 1);
    pci_config_write16(2, 12, 0, 8, 1);
    if (writes != 1)
        return 8;
    pci_config_write32(2, 12, 0, 4, 0xffff1234);
    if (writes != 2 || data != 0x1234)
        return 9;
    pci_config_write32(2, 12, 0, 0x10, 0xfeed1234);
    pci_config_write32(2, 12, 0, 0x11, 0);
    if (writes != 3 || data != 0xfeed1234 || last_port != 0x2000)
        return 10;
    data = U32_MAX;
    if (pci_config_probe())
        return 11;
    pm_pci_enumerate();
    return stage == 2 ? 0 : 12;
}

void _start(void)
{
    int result = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result));
    __builtin_unreachable();
}
