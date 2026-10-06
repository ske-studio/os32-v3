/* Disposable CPL3 child for shm_reuse_test; no allocation between children. */
#include "os32api.h"
#include <stdlib.h>
#include <string.h>
/* Existing DB SHM size also describes the allocator block; no new SDK ABI. */
#define BLOCK_BYTES DB_SHM_BLOCK_SIZE
/* Private page size until public SHM/page constants are settled in e11. */
#define PAGE_BYTES 4096UL
#define WRITE_PATTERN 0x39574853UL /* SHW9 */
int main(int argc, char **argv, KernelAPI *api)
{
    volatile u32 *report, *block;
    unsigned short cs;
    u32 i;
    int write_mode;
    if (argc != 3) return 2;
    write_mode = strcmp(argv[1], "write") == 0;
    if (!write_mode && strcmp(argv[1], "free") && strcmp(argv[1], "exit")) return 2;
    report = (volatile u32 *)strtoul(argv[2], 0, 16);
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    if ((cs & 3) != 3) return 1;
    block = (volatile u32 *)api->sys_shm_alloc(1);
    if (!block) return 1;
    if (write_mode) {
        report[2] = (u32)block;
        if ((u32)block != report[0]) return 1;
        /* Every page must retain USER and regain RW, not merely page zero. */
        for (i = 0; i < BLOCK_BYTES / sizeof(u32); i += PAGE_BYTES / sizeof(u32)) {
            block[i] = WRITE_PATTERN + i;
            if (block[i] != WRITE_PATTERN + i) return 1;
        }
        report[3] = WRITE_PATTERN;
        return 0;
    }
    report[0] = (u32)block;
    block[0] = 0x4B434F4CUL; /* LOCK */
    if (api->sys_shm_lock((void *)block) != 0) return 1;
    report[1] = 1; /* lock succeeded; do not write the locked block again */
    if (strcmp(argv[1], "free") == 0) {
        if (api->sys_shm_free((void *)block) != 0) return 1;
        report[1] = 2;
    }
    return 0; /* exit mode deliberately leaves the block locked and owned */
}
