/* T4b-8: PM guest fixture. Negative modes intentionally fault-kill the child. */
#include "os32api.h"
#include "os32_kapi_slots.h"
#include <string.h>

#define STR_INNER(x) #x
#define STR(x) STR_INNER(x)

/* The argument frame may end at a mapped page boundary. int80 preserves EBX
 * and restores the original USER ESP, so EBX can hold our ordinary stack. */
extern void print_at(const u32 *frame);
__asm__(".text\n"
        ".globl print_at\n"
        "print_at:\n"
        "push %ebx\n"
        "mov 8(%esp), %eax\n"
        "mov %esp, %ebx\n"
        "mov %eax, %esp\n"
        "mov $" STR(KAPI_SLOT_KPRINTF) ", %eax\n"
        "int $0x80\n"
        "mov %ebx, %esp\n"
        "pop %ebx\n"
        "ret\n");

int main(int argc, char **argv, KernelAPI *api)
{
    const char *mode = argc > 1 ? argv[1] : "normal";
    char *pages = api->mem_map(OS32_PAGE_SIZE * 2, 0, 0);
    if (!pages) return 1;
    char *end = pages + OS32_PAGE_SIZE;
    if (api->mem_unmap(end, OS32_PAGE_SIZE)) return 1;

    if (!strcmp(mode, "normal")) {
        memcpy(end - 4, "%s!", 4);
        api->kprintf(7, end - 4, "KPRINT format edge");
        memcpy(end - 4, "abc", 4);
        api->kprintf(7, "KPRINT string edge [%s]\n", end - 4);
        u32 *frame = (u32 *)(end - 4 * sizeof(u32));
        frame[0] = 0; frame[1] = 7; frame[2] = (u32)"KPRINT word edge %d%%\n"; frame[3] = 42;
        print_at(frame);
        frame = (u32 *)(end - 3 * sizeof(u32));
        frame[0] = 0; frame[1] = 7; frame[2] = (u32)"KPRINT fixed edge %%\n";
        print_at(frame);
        api->kprintf(7, "KPRINT grammar %02u|%4d|%-4s|%lX|%c|%%|%s\n",
                     5u, -42, "hi", 0xabUL, 'C', (const char *)0);
        if (api->mem_unmap(pages, OS32_PAGE_SIZE)) return 1;
        api->kprintf(7, "kprint_boundary: PASS 1/1\n");
        return 0;
    }

    api->kprintf(7, "KPRINT armed %s; expect FAULT and no KPRINT-PARTIAL\n", mode);
    if (!strcmp(mode, "format")) {
        memcpy(end - 4, "FAIL", 4);
        api->kprintf(7, end - 4);
    } else if (!strcmp(mode, "string")) {
        memcpy(end - 4, "FAIL", 4);
        api->kprintf(7, "KPRINT-PARTIAL %s", end - 4);
    } else if (!strcmp(mode, "late")) {
        api->kprintf(7, "KPRINT-PARTIAL %s %s", "valid", end);
    } else if (!strcmp(mode, "word")) {
        u32 *frame = (u32 *)(end - 3 * sizeof(u32));
        frame[0] = 0; frame[1] = 7; frame[2] = (u32)"KPRINT-PARTIAL %d";
        print_at(frame);
    } else if (!strcmp(mode, "window")) {
        /* Raw int80 avoids making an over-window SDK call in the source. */
        u32 frame[18] = {0, 7, (u32)"%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d"};
        for (u32 i = 3; i < 18; i++) frame[i] = 1;
        print_at(frame);
    } else if (!strcmp(mode, "unterminated")) {
        memset(pages, 'x', OS32_PAGE_SIZE);
        api->kprintf(7, pages);
    } else {
        api->kprintf(7, "Unknown kprint_boundary mode\n");
        return 1;
    }
    api->kprintf(7, "kprint_boundary: FAIL survived\n");
    return 1;
}
