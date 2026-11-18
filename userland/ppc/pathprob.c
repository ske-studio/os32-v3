#include "os32api.h"
#include "../../lib/path.h"
#include <string.h>

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    ParsedPath parsed = {0};
    const char *value;
    char magic[4];
    int fd, count;

    (void)argc;
    (void)argv;
    value = api->path_get_drive();
    if (!value || strcmp(value, "rd0"))
        return report(api, "PATH root drive failed\n", 1);
    value = api->path_get_cwd();
    if (!value || strcmp(value, "/"))
        return report(api, "PATH root cwd failed\n", 2);
    if (api->path_set_drive("rd0"))
        return report(api, "PATH set root drive failed\n", 1);
    if (api->sys_chdir("/bin"))
        return report(api, "PATH chdir failed\n", 2);

    /* VFS resolves relative opens against its own cwd. */
    api->path_set_cwd("/bin");
    api->path_parse("hello.bin", &parsed);
    if (strcmp(parsed.drive, "rd0") || strcmp(parsed.path, "hello.bin"))
        return report(api, "PATH parse failed\n", 3);
    fd = api->sys_open(parsed.path, O_RDONLY);
    if (fd < 0)
        return report(api, "PATH relative open failed\n", 4);
    count = api->sys_read(fd, magic, sizeof(magic));
    api->sys_close(fd);
    if (count != sizeof(magic) || memcmp(magic, "23SO", sizeof(magic)))
        return report(api, "PATH relative read failed\n", 5);
    if (strcmp(api->path_get_drive(), "rd0") ||
        strcmp(api->path_get_cwd(), "/bin"))
        return report(api, "PATH current state failed\n", 6);
    if (api->path_set_drive("missing") != -1)
        return report(api, "PATH invalid drive accepted\n", 7);
    if (strcmp(api->path_get_drive(), "rd0") ||
        strcmp(api->path_get_cwd(), "/bin"))
        return report(api, "PATH rejected drive changed state\n", 8);

    api->path_set_drive("rd0");
    api->sys_chdir("/");
    return report(api, "PATH OK\n", 0);
}
