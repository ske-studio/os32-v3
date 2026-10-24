; cpu.h と同じ EFLAGS/ABI の値。host comparison checks the C definitions.
%ifndef OS32_CPU_FLAGS_INC
%define OS32_CPU_FLAGS_INC
%define EFLAGS_IF 0x000200
%define EFLAGS_VM 0x020000
%define EFLAGS_RESERVED1 0x000002
%define ABI_STACK_ALIGN 16
%define TSS_OFF_CR3 28
%define JB_ESP 0
%define JB_EBP 4
%define JB_EBX 8
%define JB_ESI 12
%define JB_EDI 16
%define JB_EIP 20
%define JB_IRQ_DEPTH 24
%define JB_EXC_DEPTH 28
%endif
