#include "os32api.h"
#include "../../include/pci_bind_contract.h"
#include <stdio.h>
#include <string.h>

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    struct pci_dev dev, before;
    struct pci_bind_info bind, before_bind;
    char line[80];
    int count = api->pci_count();

    if (argc == 2) {
        void *out = api->sys_shm_alloc(1);
        if (!out) return 8;
        memset(out, 0xa5, sizeof(dev));
        if (api->sys_shm_lock(out)) return 9;
        if (!strcmp(argv[1], "get"))
            api->pci_get(0, out);
        else
            api->pci_bind_info(0, out);
        return 10;
    }
    if (count <= 0 || count > PCI_MAX_DEVS)
        return report(api, "PCI count failed\n", 1);
    for (int i = 0; i < count; i++) {
        memset(&dev, 0xa5, sizeof(dev));
        memset(&bind, 0xa5, sizeof(bind));
        if (api->pci_get(i, &dev))
            return report(api, "PCI get failed\n", 2);
        if (api->pci_cfg_read32(dev.bus, dev.dev, dev.fn, 0) !=
            (((u32)dev.device << 16) | dev.vendor))
            return report(api, "PCI config failed\n", 3);
        if (api->pci_bind_info(i, &bind) || bind.bus != dev.bus || bind.dev != dev.dev ||
            bind.fn != dev.fn || bind.result != PCI_BIND_NONE || bind.reason != PCI_BIND_NO_DRIVER)
            return report(api, "PCI bind failed\n", 4);
        snprintf(line, sizeof(line), "PCI %u %u %u %04x %04x\n",
                 (unsigned)dev.bus, (unsigned)dev.dev, (unsigned)dev.fn,
                 (unsigned)dev.vendor, (unsigned)dev.device);
        report(api, line, 0);
    }
    memset(&before, 0xa5, sizeof(before));
    memset(&before_bind, 0xa5, sizeof(before_bind));
    memcpy(&dev, &before, sizeof(dev));
    memcpy(&bind, &before_bind, sizeof(bind));
    if (!api->pci_get(count, &dev) || memcmp(&dev, &before, sizeof(dev)) ||
        !api->pci_bind_info(count, &bind) || memcmp(&bind, &before_bind, sizeof(bind)))
        return report(api, "PCI bounds failed\n", 5);
    const char *commands[] = {"/bin/pciprob.bin get", "/bin/pciprob.bin bind"};
    for (int i = 0; i < 2; i++) {
        int kind = -1, code = -1;
        api->exec_run(commands[i]);
        if (api->exec_last_result(&kind, &code) || kind != EXEC_KIND_FAULT || code != EXEC_ERR_FAULT)
            return report(api, "PCI output guard failed\n", 6);
    }
    return report(api, "PCI OK\n", 0);
}
