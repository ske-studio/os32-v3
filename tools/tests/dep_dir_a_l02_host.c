/* Fixed PCI IDs/layout from the pre-move table, independent of machine macros. */
#include <stddef.h>
#include "pci_bind_contract.h"
#include "pci_bind.h"
#include "lan_module.h"
_Static_assert(sizeof(struct pci_dev) == 40, "pci_dev size");
_Static_assert(offsetof(struct pci_dev, bar) == 12, "pci_dev BARs");
_Static_assert(offsetof(struct pci_dev, command) == 38, "pci_dev command");
_Static_assert(sizeof(struct pci_driver) == 20, "pci_driver size");
_Static_assert(offsetof(struct pci_driver, probe) == 16, "pci_driver callback");
_Static_assert(sizeof(struct pci_bind_info) == 8, "bind info size");
static int calls;
int lan_module_probe(const struct pci_dev *dev)
{
    (void)dev;
    calls++;
    return PCI_PROBE_DECLINE;
}
#define CHECK(x) do { if (!(x)) return 1; } while (0)
int main(void)
{
    const struct pci_driver *driver = platform_lan_driver();
    const struct pci_driver *const table[] = {driver};
    struct pci_dev dev = {.vendor = 0x8086, .device = 0x1229,
                         .class = 2, .subclass = 0};
    struct pci_bind_info info = {0};
    CHECK(driver == platform_lan_driver());
    CHECK(driver->size == 20 && driver->probe == lan_module_probe);
    CHECK(pci_bind_match(driver, &dev));
    dev.vendor ^= 1; CHECK(!pci_bind_match(driver, &dev)); dev.vendor ^= 1;
    dev.device ^= 1; CHECK(!pci_bind_match(driver, &dev)); dev.device ^= 1;
    dev.class ^= 1; CHECK(!pci_bind_match(driver, &dev)); dev.class ^= 1;
    dev.subclass ^= 1; CHECK(!pci_bind_match(driver, &dev)); dev.subclass ^= 1;
    CHECK(pci_bind_one(table, 1, &dev, &info) == PCI_PROBE_DECLINE);
    CHECK(calls == 1 && info.result == PCI_BIND_DECLINED);
    return 0;
}
__attribute__((noreturn)) void _start(void)
{
    int rc = main();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
