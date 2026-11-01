/* PCI master policy: nouveau_drm_probe, nouveau_drm.c:858,881;
 * nouveau_drm_remove, nouveau_drm.c:926-940 (MIT reference). */
#include "foundation.h"
#include "../../drivers/pci/pci.h"
#include "../../kdebug/recover.h"

#define GF_LATENCY_MIN 16U
#define GF_LATENCY_TARGET 64U
#define GF_CACHE_LINE_MASK 0xffU
#define GF_LATENCY_SHIFT 8U

int gf_master_enable(struct gf_foundation *f)
{
    u16 cache;

    if (pci_cfg_write16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                        PCI_CFG_COMMAND, RECOVER_COMMAND_RUN) ||
        pci_cfg_read16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                       PCI_CFG_COMMAND) != RECOVER_COMMAND_RUN)
        return GF1_EPCI;
    f->expected_command = RECOVER_COMMAND_RUN;
    f->bm = true;

    if (pci_cfg_read8(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                      PCI_CFG_LATENCY_TIMER) < GF_LATENCY_MIN) {
        cache = pci_cfg_read16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                               PCI_CFG_CACHE_LINE);
        if (pci_cfg_write16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                            PCI_CFG_CACHE_LINE,
                            (cache & GF_CACHE_LINE_MASK) | (GF_LATENCY_TARGET << GF_LATENCY_SHIFT)) ||
            pci_cfg_read8(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                          PCI_CFG_LATENCY_TIMER) != GF_LATENCY_TARGET)
            return GF1_EPCI;
    }
    return GF1_OK;
}

int gf_master_disable(struct gf_foundation *f)
{
    if (pci_cfg_write16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                        PCI_CFG_COMMAND, RECOVER_COMMAND_STOP) ||
        pci_cfg_read16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                       PCI_CFG_COMMAND) != RECOVER_COMMAND_STOP)
        return GF1_EPCI;
    f->expected_command = RECOVER_COMMAND_STOP;
    f->bm = false;
    return GF1_OK;
}
