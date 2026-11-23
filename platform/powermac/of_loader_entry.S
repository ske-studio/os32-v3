    .text
    .globl _start
_start:
    lis 1,__stack_top@ha
    addi 1,1,__stack_top@l
    li 0,0
    stwu 0,-16(1)
    mr 3,5
    bl powermac_boot
1:  b 1b
    .section .note.GNU-stack,"",@progbits
