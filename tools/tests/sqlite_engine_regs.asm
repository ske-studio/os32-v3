cpu 386
bits 32
section .text
extern sqlite_call_on_alt_stack
global check_registers
check_registers:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    push ebp
    mov ebx, 0x12345678
    mov esi, 0x23456789
    mov edi, 0x34567890
    mov ebp, 0x45678901
    ;; Argument window starts after the saved EBP.
    push dword [esp+12]
    push dword [esp+12]
    push dword [esp+12]
    call sqlite_call_on_alt_stack
    add esp, 12
    xor eax, eax
    cmp ebx, 0x12345678
    jne .done
    cmp esi, 0x23456789
    jne .done
    cmp edi, 0x34567890
    jne .done
    cmp ebp, 0x45678901
    jne .done
    inc eax
.done:
    pop ebp
    add esp, 12
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
