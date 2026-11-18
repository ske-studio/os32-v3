#include "machcfg_contract.h"

static struct machcfg_seed machine_seed;
static struct machcfg current;
static int finished;

void machcfg_begin(struct machcfg_seed seed)
{
    machine_seed = seed;
    current = seed.defaults;
    finished = 0;
}

void machcfg_finish(const struct machcfg *values)
{
    if (finished)
        return;
    struct machcfg next = values ? *values : machine_seed.defaults;
    if (!machine_seed.accepts_nap7450)
        next.nap7450 = machine_seed.defaults.nap7450;
    current = next;
    finished = 1;
}

const struct machcfg *machcfg_get(void)
{
    return &current;
}
