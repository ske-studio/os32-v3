#include "machcfg_contract.h"
#include "cpu.h"
#include "os32_snd2.h"

struct machcfg_seed platform_machcfg_seed(void)
{
    return (struct machcfg_seed){
        .defaults = {.perf_request = 0, .nap7450 = 0, .snd_flags = arch_cpu_pentium_or_later() ? SND2_DEV_SOFT_CPU : 0},
        .accepts_nap7450 = 0
    };
}
