#include "os32api.h"

enum {
    SHM_APP_BLOCKS = 9, /* docs/09_exec.md: 14 total, five reserved */
    CHILD_BLOCKS = SHM_APP_BLOCKS - 1,
    MARKER = 0x5a,
};

static void print(KernelAPI *api, const char *text)
{
    while (*text)
        api->shell_putchar(*text++, 7);
}

static int report(KernelAPI *api, const char *text, int status)
{
    print(api, text);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    volatile unsigned char *parent;
    void *region;
    int kind, code;

    if (argc == 2 && argv[1][0] == 'c') {
        volatile unsigned char *child = api->sys_shm_alloc(CHILD_BLOCKS);
        if (!child)
            return 10;
        child[0] = MARKER;
        if (api->sys_shm_lock((void *)child) != 0 || child[0] != MARKER)
            return 11;
        print(api, "SHM locked store\n");
        child[0] = MARKER + 1;
        return 12; /* A successful store means the lock failed to protect SHM. */
    }

    region = api->sys_shm_alloc(SHM_APP_BLOCKS);
    if (!region)
        return report(api, "SHM initial capacity\n", 1);
    if (api->sys_shm_free(region) != 0)
        return report(api, "SHM free failed\n", 2);
    region = api->sys_shm_alloc(SHM_APP_BLOCKS);
    if (!region)
        return report(api, "SHM capacity after free\n", 3);
    if (api->sys_shm_free(region) != 0)
        return report(api, "SHM second free failed\n", 4);

    parent = api->sys_shm_alloc(1);
    if (!parent)
        return report(api, "SHM parent alloc failed\n", 5);
    parent[0] = MARKER;
    if (api->sys_shm_lock((void *)parent) != 0)
        return report(api, "SHM parent lock failed\n", 6);

    api->exec_run("/bin/shmprobe.bin child");
    if (api->exec_last_result(&kind, &code) != 0 ||
        kind != EXEC_KIND_FAULT || code != EXEC_ERR_FAULT)
        return report(api, "SHM child lock did not fault\n", 7);

    if (api->sys_shm_alloc(SHM_APP_BLOCKS) || parent[0] != MARKER)
        return report(api, "SHM parent ownership lost\n", 8);
    volatile unsigned char *reused = api->sys_shm_alloc(CHILD_BLOCKS);
    if (!reused)
        return report(api, "SHM capacity after child fault\n", 9);
    reused[0] = MARKER;
    if (reused[0] != MARKER)
        return report(api, "SHM reused block readback failed\n", 12);
    if (api->sys_shm_free((void *)reused) != 0 ||
        api->sys_shm_free((void *)parent) != 0)
        return report(api, "SHM cleanup failed\n", 13);
    return report(api, "SHM OK\n", 0);
}
