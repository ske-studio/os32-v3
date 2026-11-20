#include "machcfg_contract.h"

struct machcfg_seed platform_machcfg_seed(void)
{
    return (struct machcfg_seed){
        .defaults = {.perf_request = 0, .nap7450 = 0},
        .accepts_nap7450 = 0
    };
}
