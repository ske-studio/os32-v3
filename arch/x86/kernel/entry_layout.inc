; C headers are canonical: gdt.h, idt.h, tss.h, v86.h, appslot.h,
; cpu.h and pc98.h. check-double-fault-host checks every value,
; including derived frame offsets, against the C layout.
%ifndef OS32_ENTRY_LAYOUT_INC
%define OS32_ENTRY_LAYOUT_INC
%define ABI_WORD_BYTES 4
%define GDT_KERNEL_DS 0x10
%define USER_DS 0x2B
%define X86_SEL_RPL_MASK 3
%define APP_FRAME_WORDS 13
%define TVRAM_CHAR_BASE 0xA0000
%define TVRAM_ATTR_BASE 0xA2000
%define PIC1_CMD 0x00
%define PIC2_CMD 0x08
%define OCW2_EOI 0x20
%define OCW3_ISR 0x0B
%define OCW3_IRR 0x0A
%define PIC_LAST_ISR_BIT 0x80
%define TSS_OFF_ESP0 4
%define PUSHAD_BYTES (8 * ABI_WORD_BYTES)
%define IRET_CS_OFF ABI_WORD_BYTES
%define IRET_EFLAGS_OFF (2 * ABI_WORD_BYTES)
%define ISR_ERROR_BYTES ABI_WORD_BYTES
%define ISR_ERROR_EFLAGS_OFF (ISR_ERROR_BYTES + IRET_EFLAGS_OFF)
%define ISR_COMMON_VECTOR_OFF PUSHAD_BYTES
%define ISR_COMMON_ERROR_OFF (PUSHAD_BYTES + ABI_WORD_BYTES)
%define ISR_COMMON_EIP_OFF (PUSHAD_BYTES + 2 * ABI_WORD_BYTES)
%define ISR_COMMON_SKIP_BYTES (2 * ABI_WORD_BYTES)
%define ISR_PF_ERROR_OFF PUSHAD_BYTES
%define ISR_PF_EIP_OFF (PUSHAD_BYTES + ABI_WORD_BYTES)
%define ISR_IRQ_CS_OFF (PUSHAD_BYTES + IRET_CS_OFF)
%define ISR_IRQ_EFLAGS_OFF (PUSHAD_BYTES + IRET_EFLAGS_OFF)
%define V86_CTX_EIP 0
%define V86_CTX_CS 4
%define V86_CTX_EFLAGS 8
%define V86_CTX_ESP 12
%define V86_CTX_SS 16
%define V86_CTX_ES 20
%define V86_CTX_DS 24
%define V86_CTX_FS 28
%define V86_CTX_GS 32
%endif
