#include "os32api.h"

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)argv;
    (void)api;
    *(volatile unsigned int *)(void *)main = 0;
    return 0;
}
