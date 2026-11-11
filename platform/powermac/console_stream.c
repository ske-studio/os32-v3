/* Terminal output is distinct from the diagnostic log sinks. */
#include "kernel/console/console_stream.h"
#include "lan_boot.h"
#include "ofw.h"
#include "arch/ppc/kernel_boot.h"

void console_stream_write(const void *bytes, u32 len)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    struct pm_escc serial = { dev->escc_csr_va, dev->escc_data_va, 0 };
    pm_term_write(bytes, len);
    (void)pm_escc_write(&serial, bytes, len);
}
