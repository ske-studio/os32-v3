/* Run the shell loader in user mode. The test has one resident landing. */
.global exec_setjmp
.global exec_longjmp
exec_setjmp:
#ifdef __PPC__
    lis 3,probe_jump@ha
    addi 3,3,probe_jump@l
    b setjmp
#else
    movl $probe_jump,4(%esp)
    jmp setjmp
#endif
exec_longjmp:
#ifdef __PPC__
    lis 3,probe_jump@ha
    addi 3,3,probe_jump@l
    li 4,1
    b longjmp
#else
    pushl $1
    pushl $probe_jump
    call longjmp
#endif
