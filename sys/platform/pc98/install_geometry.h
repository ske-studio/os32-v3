#ifndef PC98_INSTALL_GEOMETRY_H
#define PC98_INSTALL_GEOMETRY_H

#include "sys/include/platform_install_boot.h"

static inline void platform_install_geometry(int drive, const HddGeom *hg, HdprepGeom *g)
{
    g->drive = drive;
    g->ata_present   = hg->ata_present;
    g->addr_mode     = hg->addr_mode;
    g->ata_total     = hg->ata_total;
    g->ata_cur_cyl   = hg->ata_cur_cyl;
    g->ata_cur_heads = hg->ata_cur_heads;
    g->ata_cur_spt   = hg->ata_cur_spt;
    g->bios_queried  = hg->bios_queried;
    g->bios_valid    = hg->bios_valid;
    g->bios_cyl      = hg->bios_cyl;
    g->bios_heads    = hg->bios_heads;
    g->bios_spt      = hg->bios_spt;
    g->bios_seclen   = hg->bios_seclen;
}

#endif
