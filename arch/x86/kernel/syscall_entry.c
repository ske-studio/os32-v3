/* VM frames and transitional raw cdecl consumers stay within x86. */
#include "kapi_call.h"
#include "legacy_guest.h"
#include "kapi_print.h"
#include "kapi_strings.h"
#include "ring3_ls.h"
#include "os32_kapi_slots.h"
#include "os32_kapi_shared.h"
extern void ring3_fault_kill(void);

void __cdecl ring3_syscall_dispatch(u32 *native)
{
    if (legacy_trap(native)) return;
    struct arch_syscall call = { native };
    exec_syscall_dispatch(&call);
}

u32 arch_kapi_legacy_ls(const struct arch_syscall *call)
{
    return (u32)ring3_ls_dispatch(((const u32 *)call->native)[11]);
}

int arch_kapi_legacy_call(const struct arch_syscall *call, u32 slot,
                          void *wrapper, u32 *result)
{
    u32 sp = ((const u32 *)call->native)[11];
    const void *args = (const void *)(uptr)(sp + sizeof(u32));
    u32 bytes = kapi_argsize[slot];
    int handled = legacy_call_dispatch(slot, wrapper, args, bytes, result);
    if (handled) return handled;
    if (slot == KAPI_SLOT_KPRINTF) {
        if (!kapi_print_call(args)) return -1;
        *result = 0;
        return 1;
    }
    return kapi_strings_call(slot, wrapper, args, bytes, kapi_argptr[slot], result);
}
