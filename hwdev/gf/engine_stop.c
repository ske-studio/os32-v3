/* nvkm_subdev_fini: nvkm/core/subdev.c:54-85; engine bits:
 * nvkm/subdev/mc/nv17.c:27-30. The FIFO/GR/MPEG order is OS32's. */
#include "foundation.h"

#define GF_ENGINE_FIFO  0x00000100U
#define GF_ENGINE_GR    0x00001000U
#define GF_ENGINE_MPEG  0x00000002U

int gf_engine_stop(struct gf_foundation *f)
{
    static const u32 engines[] = { GF_ENGINE_FIFO, GF_ENGINE_GR, GF_ENGINE_MPEG };
    u32 value, observed;
    unsigned int i;

    for (i = 0; i < sizeof(engines) / sizeof(engines[0]); ++i) {
        if (f->io.read(f->io.opaque, 0, RECOVER_PMC_ENABLE, 4, &value))
            return GF1_ESTOP;
        value &= ~engines[i];
        if (f->io.write(f->io.opaque, 0, RECOVER_PMC_ENABLE, 4, value) ||
            f->io.read(f->io.opaque, 0, RECOVER_PMC_ENABLE, 4, &observed) ||
            observed != value)
            return GF1_ESTOP;
        value |= engines[i];
        if (f->io.write(f->io.opaque, 0, RECOVER_PMC_ENABLE, 4, value) ||
            f->io.read(f->io.opaque, 0, RECOVER_PMC_ENABLE, 4, &observed) ||
            observed != value)
            return GF1_ESTOP;
    }
    return GF1_OK;
}
