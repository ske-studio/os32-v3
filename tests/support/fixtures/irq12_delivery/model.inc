bits 32
extern platform_irq_reserved, irq_dispatch, v86_reflect_irq, vm, mouse_irq_handler, outp, kctx_irq_depth
%define IRQ_ENTER inc dword [kctx_irq_depth]
%define IRQ_LEAVE dec dword [kctx_irq_depth]
%define RESTORE_KSEG
%include "sys/arch/x86/kernel/entry_layout.inc"
%include "sys/arch/x86/kernel/cpu_flags.inc"
%define IRETD_USER ret 8
%macro out 2
        push eax
        movzx eax, %2
        push eax
        push dword %1
        call outp
        add esp, 8
        pop eax
%endmacro
%macro FIRE 1
global fire_%1
fire_%1:
        mov eax, [vm]
        imul eax, EFLAGS_VM
        push eax
        push dword 0
        call irq_stub_%1
        ret
%endmacro
FIRE 2
FIRE 12
FIRE 13
