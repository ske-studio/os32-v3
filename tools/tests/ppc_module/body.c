#include "kcg_imports.h"

int kcg_module_init(void)
{
    int value = PROBE_VALUE;
    /* The final integers travel on the stack under the PPC32 ABI. */
    return probe_sum(&value, 1, 2, 3, 4, 5, 6, 7, 8, 9) + value;
}

const struct {
    unsigned version;
    int (*initialize)(void);
} kcg_module_ops = {1, kcg_module_init};
