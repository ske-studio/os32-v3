/* CPL3 map/write/unmap control and same-VA #PF acceptance (F-6). */
#include "os32api.h"
#include "memmap.h"

#define TEST_ATTR 0x07
#define TEST_WORD 0x5a32f5bUL

int main(int argc, char **argv, KernelAPI *api)
{
    volatile u32 *p;
    int fault = argc == 2 && argv[1][0] == 'p' && argv[1][1] == 'f' && !argv[1][2];
    if (api->mem_map(0, 0, 0) ||
        api->mem_map(OS32_PAGE_SIZE, 0, OS32_MEM_MAP_EXACT) ||
        api->mem_map(OS32_PAGE_SIZE, 0, ~(OS32_MEM_MAP_EXACT | OS32_MEM_MAP_TOPDOWN))) return 1;
    if (api->mem_map(OS32_PAGE_SIZE, (void *)MEM_LEGACY_APP_BASE, 0) ||
        api->mem_map(OS32_PAGE_SIZE,
                     (void *)(MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE),
                     OS32_MEM_MAP_EXACT) ||
        api->mem_unmap((void *)OS32_PAGE_SIZE, OS32_PAGE_SIZE) != OS32_ERR_INVAL) return 1;
    api->kprintf(TEST_ATTR, "PASS mem_map invalid addresses survived\n");
    p = (volatile u32 *)api->mem_map(OS32_PAGE_SIZE, 0, OS32_MEM_MAP_TOPDOWN);
    if (!p || *p) return 1;
    *p = TEST_WORD;
    if (*p != TEST_WORD || api->mem_unmap((void *)p, OS32_PAGE_SIZE)) return 1;
    if (fault) {
        api->kprintf(TEST_ATTR, "MEMMAP PF armed VA=%x; expect kill at this VA\n", (u32)p);
        *p = TEST_WORD;
        api->kprintf(TEST_ATTR, "FAIL mem_map: survived unmapped write\n");
        return 1;
    }
    if (api->mem_map(OS32_PAGE_SIZE, (void *)p, OS32_MEM_MAP_EXACT | OS32_MEM_MAP_TOPDOWN) != (void *)p) return 1;
    if (*p) return 1;
    *p = TEST_WORD;
    if (*p != TEST_WORD || api->mem_unmap((void *)p, OS32_PAGE_SIZE)) return 1;
    api->kprintf(TEST_ATTR, "PASS mem_map control\n");
    return 0;
}
