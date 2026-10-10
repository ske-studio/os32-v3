/* T2h: 200KiB code and one LARGE exec allocation. */
#include "os32api.h"
static int h5_run(int argc, char **argv, KernelAPI *api);
int main(int argc, char **argv, KernelAPI *api)
{
    return h5_run(argc, argv, api);
}
#define H5_TEXT_BYTES (200 * 1024)
#include "h5_shape.inc"
