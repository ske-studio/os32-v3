/* B acceptance image. The fixture binds its observer to a reserved slot of the real table. */
#include "os32api.h"
#include "exec/shell_service.h"
#include "memmap.h"
#if GENERATION != 4
OS32_KAPI_LAYOUT_STAMP();
#endif
typedef u32 (*observer)(unsigned, unsigned, char **, KernelAPI *, void *, unsigned, int (*)(int), u32);
static int fp(int seed)
{
    static const unsigned long long two = 0x4000000000000000ULL;
    unsigned long long value, status, sum;
    if (seed) {
        __asm__ volatile("lfd 31,0(%0); mtfsfi 7,1" :: "r"(&two) : "memory");
        return 1;
    }
    __asm__ volatile("stfd 31,0(%0); mffs 0; stfd 0,0(%1); fadd 0,31,31; stfd 0,0(%2)"
                     :: "r"(&value), "r"(&status), "r"(&sum) : "fr0", "memory");
    return value == two && (status & 3) == 1 && sum == 0x4010000000000000ULL;
}
void entry(unsigned argc, char **argv, KernelAPI *api, void *aux, unsigned sp)
{
    if (argc != 2) __builtin_trap();
    observer check = (observer)api->kapi_reserved[0];
    api->kprintf(7, "CTX resident %s %u\n", "generation", (unsigned)GENERATION);
    ShellService *service = aux;
    u32 id, size;
    unsigned char byte;
    if (!service || service->magic != SHELL_SERVICE_MAGIC ||
        service->font_source_open("/bin/ctx.bin", &id, &size) || !size ||
        service->font_source_read_at(id, 0, &byte, 1) != 1)
        __builtin_trap();
    u32 old = check(GENERATION, argc, argv, api, aux, sp, fp, id);
    if (old && service->font_source_read_at(old, 0, &byte, 1) != OS32_ERR_STALE) {
        api->kprintf(7, "CTX FAIL stale lease\n");
        for (;;) __asm__ volatile("" ::: "memory");
    }
#if GENERATION == 2
    *(volatile unsigned *)MEM_SHELL_GUARD = 1;
#elif GENERATION == 3
    __asm__ volatile("trap");
#endif
}
#if GENERATION == 4
extern void *__os32_aux;
int main(int argc, char **argv, KernelAPI *api)
{
    unsigned sp;
    __asm__ volatile("mr %0,1" : "=r"(sp));
    entry(argc, argv, api, __os32_aux, sp);
    return 17;
}
#else
__asm__(".section .text.startup,\"ax\"\n"
        ".globl _start\n_start:\n mr 7,1\n b entry\n.previous");
#endif
