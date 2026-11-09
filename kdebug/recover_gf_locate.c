#include "recover.h"
#include "../drivers/pci/pci_locate.h"

static struct pci_locate gf_loc;
u32 recover_gf_dev(void)
{
    if (!gf_loc.found) pci_locate_fn0(RECOVER_GF_BUS, RECOVER_GF_DEVICE, &gf_loc);
    return gf_loc.found ? gf_loc.dev : RECOVER_GF_DEV_FALLBACK;
}
u32 recover_gf_second(void)
{
    (void)recover_gf_dev();
    return gf_loc.second_found ? gf_loc.second : U32_MAX;
}
