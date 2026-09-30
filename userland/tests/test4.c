#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
/* test4.c — KernelAPI v4 テスト (ファイル操作API) */



/* VfsDirEntry互換 */
#include "os32api.h"

/* ls コールバック */
static KernelAPI *g_api;

static void __cdecl ls_callback(const DirEntry_Ext *e, void *ctx);

void main(int argc, char **argv, KernelAPI *api)
{
    int rc;
    char buf[64];

    g_api = api;
    api->kprintf(0xE1, "=== KernelAPI v4 Test ===\n");
    api->kprintf(0xE1, "  version: %d\n", api->version);

    /* mkdir テスト */
    api->kprintf(0xA1, "[mkdir /v4test]\n");
    rc = api->sys_mkdir("/v4test");
    api->kprintf(0xE1, "  result: %d\n", rc);

    /* file_write テスト */
    api->kprintf(0xA1, "[write /v4test/hello.txt]\n");
    int t_fd=api->sys_open("/v4test/hello.txt", KAPI_O_WRONLY|KAPI_O_CREAT|KAPI_O_TRUNC); if(t_fd>=0){ rc=api->sys_write(t_fd, "Hello v4!", 9); api->sys_close(t_fd); } else rc=-1;
    api->kprintf(0xE1, "  result: %d\n", rc);

    /* file_ls テスト */
    api->kprintf(0xA1, "[ls /v4test]\n");
    api->sys_ls("/v4test", (void *)ls_callback, (void *)0);

    /* file_read テスト */
    api->kprintf(0xA1, "[read /v4test/hello.txt]\n");
    int r_fd=api->sys_open("/v4test/hello.txt", KAPI_O_RDONLY); if(r_fd>=0){ rc=api->sys_read(r_fd, buf, 63); api->sys_close(r_fd); } else rc=-1;
    if (rc > 0) {
        buf[rc] = '\0';
        api->kprintf(0xE1, "  content: \"%s\" (%d bytes)\n", buf, rc);
    }

    /* file_rm テスト */
    api->kprintf(0xA1, "[rm /v4test/hello.txt]\n");
    rc = api->sys_unlink("/v4test/hello.txt");
    api->kprintf(0xE1, "  result: %d\n", rc);

    api->kprintf(0xE1, "=== v4 Test Done ===\n");
}

static void __cdecl ls_callback(const DirEntry_Ext *e, void *ctx)
{
    if (e->type == OS32_FILE_TYPE_DIR)
        g_api->kprintf(0xC1, "  <DIR> %s\n", e->name);
    else
        g_api->kprintf(0xE1, "        %s (%u bytes)\n", e->name, e->size);
}
