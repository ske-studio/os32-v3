;; Intel SDM 253666-088US (June 2025), FINIT/FNINIT, FLDCW,
;; FRSTOR, FSAVE/FNSAVE; Intel 80386 PRM (1986), 11.1 / 11.3.
;; CPU 386 also rejects accidentally introduced later integer instructions.
cpu 386
bits 32
section .text

global font_x87_boot_init
font_x87_boot_init:
        ;; AC can change on 486+, where CR0.NE exists. Preserve 386 reserved
        ;; bits and its RESET-selected ET (387 protocol); no CPUID required.
        pushfd
        pop     edx
        mov     eax, edx
        xor     eax, 1 << 18
        push    eax
        popfd
        pushfd
        pop     ecx
        push    edx
        popfd
        xor     ecx, edx
        mov     edx, cr0
        and     edx, ~((1 << 2) | (1 << 3)) ; EM=TS=0 for non-wait probe
        or      edx, 1 << 1                ; MP
        test    ecx, 1 << 18
        jz      .probe
        or      edx, 1 << 5                ; NE=1, native #MF on 486/Pentium
.probe:
        mov     cr0, edx
        sub     esp, 4
        mov     dword [esp], 0x5a5a5a5a
        fninit
        fnstsw  [esp]
        fnstcw  [esp + 2]
        xor     eax, eax
        cmp     word [esp], 0
        jne     .absent
        mov     cx, [esp + 2]
        and     cx, 0x103f
        cmp     cx, 0x003f
        jne     .absent
        inc     eax
        jmp     .done
.absent:
        or      edx, 1 << 2                ; EM=1: later accidental FP -> #NM
        mov     cr0, edx
.done:
        add     esp, 4
        ret

global font_x87_enter
font_x87_enter:
        mov     eax, [esp + 4]
        fnsave  [eax]                      ; do not deliver caller exceptions
        fwait                             ; 387/486 save completion (SDM)
        fninit
        fldcw   [font_x87_cw]
        ret

global font_x87_leave
font_x87_leave:
        mov     eax, [esp + 4]
        fninit                            ; discard font-side pending exceptions
        frstor  [eax]
        ;; No FWAIT: a restored pending exception belongs to the caller.
        ret

section .rodata
align 2
font_x87_cw: dw 0x037f                     ; all masked, nearest, extended precision

section .note.GNU-stack noalloc noexec nowrite progbits
