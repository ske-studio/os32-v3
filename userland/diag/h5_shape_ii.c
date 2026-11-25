/* T2h: 64KiB code, explicit 512KiB stack (programs.mk). */
#include "os32api.h"
static int h5_run(int argc, char **argv, KernelAPI *api);
int main(int argc, char **argv, KernelAPI *api)
{
    return h5_run(argc, argv, api);
}
#define H5_TEXT_BYTES (64 * 1024)
#include "h5_shape.inc"
