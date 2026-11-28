bits 32
extern irq_dyn_index, irq_dispatch, v86_reflect_irq, vm, mouse_irq_handler, outp, kctx_irq_depth
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

extern v86_check_exit_request, irq_exited
; Model the nonlocal exit separately from the normal IRETD return.
v86_exit_to_kernel:
        mov dword [irq_exited], 1
        add esp, 4
        IRQ_LEAVE
        popad
        ret 8

; Execute the production frame builder. Capture the stack at the IRETD boundary.
section .bss
align 4
kernel_tss: resd 2
host_sp: resd 1
global entered
entered: resd 17
section .text
global check_enter
check_enter:
        mov eax, [esp + 4]
        pushad
        mov [host_sp], esp
        push eax
        call test_enter
captured:
        pushad
        mov esi, esp
        mov edi, entered
        mov ecx, 17
        cld
        rep movsd
        mov esp, [host_sp]
        popad
        ret
