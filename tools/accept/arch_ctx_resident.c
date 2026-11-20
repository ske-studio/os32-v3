/* B acceptance image. The fixture binds its observer to a reserved slot of the real table. */
#include "os32api.h"
#include "arch_ctx_observer.h"
#include "memmap.h"
#if GENERATION != 4
OS32_KAPI_LAYOUT_STAMP();
#endif
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
    extern unsigned ctx_linkage[4];
    if (argc != 2 || (sp & 15) || ctx_linkage[0] || ctx_linkage[1] ||
        ctx_linkage[2] || ctx_linkage[3]) __builtin_trap();
    ctx_observer *check = (ctx_observer *)api->kapi_reserved[0];
    api->kprintf(7, "CTX resident %s %u\n", "generation", (unsigned)GENERATION);
    if (!aux) {
        api->kprintf(7, "CTX mismatch resident-aux gen=%u aux=0\n", (unsigned)GENERATION);
        for (;;) __asm__ volatile("" ::: "memory");
    }
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
/* Record before either C or the SDK CRT can reuse the caller linkage. */
unsigned ctx_initial_sp, ctx_linkage[4];
__asm__(".section .text.startup,\"ax\"\n.globl _start\n_start:\n"
        "lis 7,ctx_linkage@ha\naddi 7,7,ctx_linkage@l\n"
        ".irp off,0,4,8,12\nlwz 8,\\off(1)\nstw 8,\\off(7)\n.endr\n"
        "lis 7,ctx_initial_sp@ha\nstw 1,ctx_initial_sp@l(7)\nmr 7,1\n"
#if GENERATION == 4
        "b ctx_crt_entry\n"
#else
        "b entry\n"
#endif
        ".previous");
#if GENERATION == 4
extern void *__os32_aux;
int main(int argc, char **argv, KernelAPI *api)
{
    entry(argc, argv, api, __os32_aux, ctx_initial_sp);
    return 17;
}
#endif
