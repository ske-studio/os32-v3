/* Native VM/USER trap entry stays within x86. */
#include "kapi_call.h"
#include "exec_arch.h"
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
