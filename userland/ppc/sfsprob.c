#include "os32api.h"
#include <string.h>

int main(int argc, char **argv, KernelAPI *api)
{
    if (argc == 2 && !strcmp(argv[1], "leave"))
        return api->sfs_begin() == 0 ? 0 : 1;
    char data[8];
    int fd = api->sys_open("/host/probe", 0);
    if (fd < 0) return 1;
    int n = api->sys_read(fd, data, sizeof(data));
    api->sys_close(fd);
    return n == 6 && !memcmp(data, "scope\n", 6) ? 7 : 2;
}
