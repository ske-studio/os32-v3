#include "os32api.h"
#include "os32_ver_info.h"
#include <stdio.h>
#include <string.h>

static void emit(KernelAPI *api, const char *text)
{
    while (*text) api->shell_putchar(*text++, 7);
}

int main(int argc, char **argv, KernelAPI *api)
{
    OS32_VerInfo info;
    GFX2_HeadInfo head;
    char line[100];
    (void)argc;
    (void)argv;
    memset(&info, 0xa5, sizeof(info));
    memset(&head, 0xa5, sizeof(head));
    api->serial_init(9600);
    if (api->gfx2_head_info(0, 0, &head) || !head.attached) return 1;
    snprintf(line, sizeof(line), "HEAD %u %u\n", (unsigned)head.w, (unsigned)head.h);
    emit(api, line);
    int rc = api->sys_ver_info(&info, sizeof(info));
    if (rc == OS32_ERR_NOSYS) {
        emit(api, "VER NOSYS\n");
        return 2;
    }
    if (rc || info.count > OS32_VER_LINES) return 3;
    for (u32 i = 0; i < info.count; ++i) {
        OS32_VerLine *row = &info.lines[i];
        if (row->text[OS32_VER_TEXT - 1]) return 4;
        snprintf(line, sizeof(line), "%u %s\n", (unsigned)row->kind, row->text);
        emit(api, line);
    }
    return 0;
}
