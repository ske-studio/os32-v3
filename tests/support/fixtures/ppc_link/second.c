#include "os32api.h"

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    if (msr & (1u << 14)) return 1;
    api->kprintf(7, "%s", "S12 second resident\n");
    return 12;
}
