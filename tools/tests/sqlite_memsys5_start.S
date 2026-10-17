/* Linux i386 process entry for the newlib-linked host fixture. */
.text
.globl _start
.type _start, @function
_start:
    movl %esp, %eax
    andl $-16, %esp
    subl $8, %esp
    leal 4(%eax), %edx
    pushl %edx
    pushl (%eax)
    call main
    addl $16, %esp
    subl $12, %esp
    pushl %eax
    call exit
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
