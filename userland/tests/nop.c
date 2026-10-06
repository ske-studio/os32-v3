/* PM observation / per-case expectations: ring3_marker.h acceptance table. */
#include "os32api.h"
#include "ring3_marker.h"
int main(int argc, char **argv, KernelAPI *api)
{
    volatile unsigned long *mark = r3_marker(0x4B4FUL); /* OK */
    (void)argc; (void)argv; (void)api;
    mark[0] = 0x21504F4EUL; /* NOP! */
    mark[1] = R3_DONE;
    return 0;
}
