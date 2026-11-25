/* Disposable CPL3 child for shm_reuse_test; no allocation between children. */
#include "os32api.h"
#include "ring3_marker.h"
#include <stdlib.h>
#include <string.h>
#define BLOCK_BYTES OS32_SHM_BLOCK_SIZE
#define PAGE_BYTES OS32_PAGE_SIZE
#define WRITE_PATTERN 0x39574853UL /* SHW9 */
int main(int argc, char **argv, KernelAPI *api)
{
    volatile u32 *report, *block;
    unsigned short cs;
    u32 i;
    int write_mode, lockwrite;
    u32 offset = 0;
    if (argc != 3) return 2;
    lockwrite = strcmp(argv[1], "lockwrite") == 0;
    if (lockwrite) {
        if (strcmp(argv[2], "last") == 0) offset = BLOCK_BYTES - PAGE_BYTES;
        else if (strcmp(argv[2], "first") != 0) return 2;
    }
    write_mode = strcmp(argv[1], "write") == 0;
    if (!lockwrite && !write_mode && strcmp(argv[1], "free") && strcmp(argv[1], "exit")) return 2;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    if ((cs & 3) != 3) return 1;
    if (lockwrite) {
        volatile u32 *target;
        report = (volatile u32 *)r3_marker(0x4C53UL); /* SL, separate writable block */
        block = (volatile u32 *)api->sys_shm_alloc(1);
        if (!block || api->sys_shm_lock((void *)block) != 0) return 1;
        target = block + offset / sizeof(u32);
        r3_arm((volatile unsigned long *)report, 0x3F4B4C53UL, (u32)target); /* SLK? */
        *target = WRITE_PATTERN; /* real CPL3 store: kernel WP=0 is no proof */
        report[1] = R3_SURV;
        return 1; /* surviving a locked write is a failure */
    }
    report = (volatile u32 *)strtoul(argv[2], 0, 16);
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
