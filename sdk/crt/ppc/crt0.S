/* The loader clears BSS and provides r3-r6 and a 16-byte aligned stack. */
.section .text.startup,"ax",@progbits
.globl _start
.type _start,@function
_start:
    stwu 1,-16(1)
    bl os32_start_c
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
