#include "os32api.h"

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)argv;
    const char *message = "MTIME OK\n";
    int status = 0;
    if (api->sys_set_mtime("/tmp", 0) != OS32_ERR_INVAL) {
        message = "MTIME invalid failed\n";
        status = 1;
    } else if (api->sys_set_mtime("/tmp", 1) != OS32_ERR_NOSYS) {
        message = "MTIME unsupported failed\n";
        status = 2;
    }
    while (*message)
        api->shell_putchar(*message++, 7);
    return status;
}
