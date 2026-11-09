#include "../../drivers/pci/pci_locate.h"
#include "../../kdebug/recover.h"
#include "../../drivers/pci/pci.h"

static u32 cfg[2][32][8];
static u32 reads;
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    (void)reg; reads++;
    return cfg[bus][dev][fn];
}
int main(int argc, char **argv)
{
    struct pci_locate loc;
    char c = argc > 1 ? argv[1][0] : 'a';
    for (u32 b=0;b<2;b++) for (u32 d=0;d<32;d++)
        for (u32 f=0;f<8;f++) cfg[b][d][f]=U32_MAX;
    if (c=='a' || c=='d' || c=='e') cfg[0][12][0]=RECOVER_GF_DEVICE;
    if (c=='b' || c=='d') cfg[0][13][0]=RECOVER_GF_DEVICE;
    if (c=='f') { cfg[0][12][1]=RECOVER_GF_DEVICE; cfg[1][0][0]=RECOVER_GF_DEVICE; }
    if (c=='g') {
        cfg[1][7][0]=RECOVER_GF_DEVICE;
        pci_locate_fn0(1, RECOVER_GF_DEVICE, &loc);
        return !(loc.found && loc.dev==7 && reads==32);
    }
    u32 dev=recover_gf_dev();
    if (dev != ((c=='a'||c=='d'||c=='e') ? 12U : 13U) || reads!=32) return 1;
    if (c=='c') { (void)recover_gf_dev(); return reads!=64; }
    if (c=='e') { (void)recover_gf_dev(); return reads!=32; }
    if (c=='d') return recover_gf_second()!=13;
    if (c=='a' || c=='b') return recover_gf_second()!=U32_MAX;
    return 0;
}
