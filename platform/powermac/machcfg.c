#include "machcfg_contract.h"
#include "ppc_oea.h"
#include "platform_machcfg.h"

int pm_machcfg_seed(u32 pvr, struct machcfg_seed *out)
{
    static const struct {
        u32 version;
        struct machcfg_seed seed;
    } rows[] = {
        {PPC_PVR_7450, {{0, 0}, 1}},
        {PPC_PVR_7455, {{0, 0}, 0}}
    };
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        if (rows[i].version == (pvr >> 16)) {
            *out = rows[i].seed;
            return 1;
        }
    }
    *out = (struct machcfg_seed){{0, 0}, 0};
    return 0;
}

struct machcfg_seed platform_machcfg_seed(void)
{
    struct machcfg_seed seed;
    pm_machcfg_seed(ppc_read_pvr(), &seed);
    return seed;
}
