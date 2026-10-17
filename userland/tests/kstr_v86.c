/* T4b-8c CUI-only fixture. The bad second input must fault before V86. */
#include "os32api.h"
#include <string.h>

int main(int argc, char **argv, KernelAPI *api)
{
    if (argc != 2 || strcmp(argv[1], "boot2")) return 1;
    char *pages = api->mem_map(OS32_PAGE_SIZE * 2, 0, 0);
    if (!pages) return 1;
    char *end = pages + OS32_PAGE_SIZE;
    if (api->mem_unmap(end, OS32_PAGE_SIZE)) return 1;

    api->kprintf(7, "KSTR armed %s; expect FAULT before target\n", argv[1]);
    char *bad = end - 16;
    memset(bad, '!', 16);
    memcpy(bad, "KSTR-PARTIAL", 12);
    api->v86_boot2("/__t4b8b_missing__", bad);
    api->kprintf(7, "kstr_v86: FAIL survived\n");
    return 1;
}
