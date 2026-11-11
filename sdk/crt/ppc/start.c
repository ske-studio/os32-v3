#include "os32api.h"

KernelAPI *kapi;
void *__os32_aux;
OS32_KAPI_LAYOUT_STAMP();

extern int main(int argc, char **argv, KernelAPI *api);

void os32_start_c(int argc, char **argv, KernelAPI *api, void *aux)
{
    int rc;

    kapi = api;
    __os32_aux = aux;
    rc = main(argc, argv, api);
    api->sys_exit(rc);
    for (;;) {}
}
