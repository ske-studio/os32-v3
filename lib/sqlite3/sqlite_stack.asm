;; CPL0 cdecl trampoline. TSS.ESP0 and the saved USER caller are untouched.
;; Intel SDM vol.1, procedure calls: preserve EBX/ESI/EDI/EBP and original ESP.
cpu 386
bits 32
section .text
global sqlite_call_on_alt_stack
;; void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame)
sqlite_call_on_alt_stack:
        push    ebp
        mov     ebp, esp
        push    ebx
        push    esi
        push    edi
        mov     eax, [ebp+8]
        mov     edx, [ebp+16]
        mov     esi, esp
        mov     esp, [ebp+12]
        and     esp, -16
        sub     esp, 12
        push    edx
        call    eax
        mov     esp, esi
        pop     edi
        pop     esi
        pop     ebx
        pop     ebp
        ret
section .note.GNU-stack noalloc noexec nowrite progbits
