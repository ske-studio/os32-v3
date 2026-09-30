; ========================================================================
;  CRT0.ASM — 外部プログラム用スタートアップルーチン
; ========================================================================

[BITS 32]

global _start
extern _start_c

extern __bss_start
extern __bss_end

section .text.startup

_start:
    ; --- BSSゼロクリア ---
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2          ; dword単位
    xor eax, eax
    rep stosd
    
    ; 端数のクリア (必要であれば)
    mov ecx, __bss_end
    sub ecx, __bss_start
    and ecx, 3          ; 余りバイト数
    jz .bss_done
    rep stosb
.bss_done:

    ; スタックには exec.c から [esp+12]=kapi, [esp+8]=argv, [esp+4]=argc 
    ; 呼び出し元のリターンアドレスが[esp]に積まれている状態
    ; そのまま _start_c へジャンプすれば引数が引き継がれる
    jmp _start_c

; 非実行可能スタック宣言 (リンカ警告抑制)
section .note.GNU-stack noalloc noexec nowrite progbits
