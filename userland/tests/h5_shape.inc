/* CUI only: PM starts and supplies both lines through emu_key + RETURN.
 * free0 is the only argv value. Do not launch through rshell /api/cmd.
 * PD/private PT pages are supplied once, at the PREP stop; shared PTs
 * are excluded. The ALLOC stop holds the allocation for free2/P2 sampling.
 */
#include "os32api.h"
#include "memmap.h"

#define H5_TARGET_PAGES 512U
#define H5_ATTR 0x07
#define H5_LARGE_INDEX 4
#define H5_PATTERN 0xa5
#define H5_STRING_(x) #x
#define H5_STRING(x) H5_STRING_(x)

static int h5_number(const char *s, u32 *out);
static int h5_line(KernelAPI *api, u32 *out);
void h5_text_shape(void);

/* Shared by the three main-first wrappers and the ILP32 host harness. */
static int h5_run(int argc, char **argv, KernelAPI *api)
{
    MemStat before = {0}, after = {0};
    u32 free0, p1, launch_private, allocated = 0, shortage = 0;
    i32 add;
    void *allocation = 0;
    int continued;

    if (api->mem_stat(-1, &before, sizeof(before)) < (i32)sizeof(before) ||
        argc != 2 || !h5_number(argv[1], &free0)) {
        api->kprintf(H5_ATTR, "PREP FAIL free0\n");
        return 1;
    }
    h5_text_shape();
    api->kprintf(H5_ATTR, "PREP free1=%u\n", before.phys_free_pages);
    if (!h5_line(api, &p1) || free0 < before.phys_free_pages ||
        p1 > free0 - before.phys_free_pages) {
        api->kprintf(H5_ATTR, "PREP FAIL P1\n");
        return 1;
    }
    launch_private = free0 - before.phys_free_pages - p1;
    if (launch_private > H5_TARGET_PAGES) {
        /* Avoid unsigned wrap and i32 overflow even for malformed counts. */
        shortage = launch_private - H5_TARGET_PAGES;
        api->kprintf(H5_ATTR, "ALLOC launch_private=%u add=-%u allocated=0 short=%u\n",
                     launch_private, shortage, shortage);
    } else {
        add = H5_TARGET_PAGES - launch_private;
        if (add) {
            allocation = api->mem_alloc((u32)add * MEM_PAGE_SIZE);
            /* LARGE is page-exact. A small request may reuse an existing
             * arena: those pages were already counted in launch_private.
             * Never label that reuse as newly allocated physical pages. */
            if (allocation &&
                api->mem_stat(-1, &after, sizeof(after)) >= (i32)sizeof(after) &&
                after.extents[H5_LARGE_INDEX] == before.extents[H5_LARGE_INDEX] + 1 &&
                after.exec_heap_used - before.exec_heap_used == (u32)add * MEM_PAGE_SIZE) {
                allocated = (u32)add;
                for (u32 i = 0; i < allocated * MEM_PAGE_SIZE; i += MEM_PAGE_SIZE)
                    ((volatile u8 *)allocation)[i] = H5_PATTERN;
            }
        }
        shortage = (u32)add - allocated;
        api->kprintf(H5_ATTR, "ALLOC launch_private=%u add=%d allocated=%u short=%u\n",
                     launch_private, add, allocated, shortage);
    }
    continued = h5_line(api, 0);
    if (allocation) api->mem_free(allocation);
    if (!continued || shortage) {
        api->kprintf(H5_ATTR, "FAIL\n");
        return 1;
    }
    api->kprintf(H5_ATTR, "PASS\n");
    return 0;
}

static int h5_number(const char *s, u32 *out)
{
    u32 n = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > (~(u32)0 - (u32)(*s - '0')) / 10U)
            return 0;
        n = n * 10U + (u32)(*s - '0');
    }
    *out = n;
    return 1;
}

/* Byte reads use the stdin canonical keyboard path without libc allocation.
 * Drain the whole first line, including invalid/overflowing input. A second
 * line may contain anything, but EOF/error is not a continuation. */
static int h5_line(KernelAPI *api, u32 *out)
{
    u32 n = 0;
    int valid = 1, digits = 0;
    char c;
    for (;;) {
        if (api->sys_read(0, &c, 1) != 1) return 0;
        if (c == '\n') break;
        if (!out) continue;
        if (c < '0' || c > '9' || n > (~(u32)0 - (u32)(c - '0')) / 10U)
            valid = 0;
        else if (valid) {
            n = n * 10U + (u32)(c - '0');
            digits = 1;
        }
    }
    if (!out) return 1;
    if (!valid || !digits) return 0;
    *out = n;
    return 1;
}

/* Reachable executable padding: jump over the NOPs, keeping the section
 * under --gc-sections without spending guest ticks executing the padding. */
__asm__(".pushsection .text.h5_shape,\"ax\",@progbits\n"
        ".global h5_text_shape\n.type h5_text_shape,@function\n"
        "h5_text_shape: jmp 1f\n.space " H5_STRING(H5_TEXT_BYTES) ",0x90\n"
        "1: ret\n.size h5_text_shape,.-h5_text_shape\n.popsection\n");
