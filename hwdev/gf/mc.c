/* NV44 MC sequence: nouveau nvkm/subdev/mc/nv44.c:27-37,
 * nv44_mc_init. Register addresses are BAR0-relative. */
#include "foundation.h"
#include "production.h"

#define GF_MC_BAR 0U
#define GF_MC_WIDTH 4U
#define GF_MC_LIMIT0 0x1700U
#define GF_MC_LIMIT1 0x1704U
#define GF_MC_LIMIT2 0x1708U
#define GF_MC_LIMIT3 0x170cU
#define GF_MC_ENABLE_ALL 0xffffffffU

int gf_mc_init(struct gf_foundation *f)
{
    u32 amount;
    struct gf_io *io = &f->io;

    if (io->read(io->opaque, GF_MC_BAR, GF_PRODUCTION_PFB_AMOUNT,
                 GF_MC_WIDTH, &amount) ||
        io->write(io->opaque, GF_MC_BAR, RECOVER_PMC_ENABLE,
                  GF_MC_WIDTH, GF_MC_ENABLE_ALL))
        return GF1_EIO;
    if (io->write(io->opaque, GF_MC_BAR, GF_MC_LIMIT0, GF_MC_WIDTH, amount) ||
        io->write(io->opaque, GF_MC_BAR, GF_MC_LIMIT1, GF_MC_WIDTH, 0) ||
        io->write(io->opaque, GF_MC_BAR, GF_MC_LIMIT2, GF_MC_WIDTH, 0) ||
        io->write(io->opaque, GF_MC_BAR, GF_MC_LIMIT3, GF_MC_WIDTH, amount))
        return GF1_EIO;
    /* The enable register can read back only implemented engine bits.
     * As at foundation stop, publish a successful readback for status. */
    u32 enabled;
    if (io->read(io->opaque, GF_MC_BAR, RECOVER_PMC_ENABLE,
                 GF_MC_WIDTH, &enabled)) return GF1_EIO;
    f->expected_pmc = enabled;
    return GF1_OK;
}
