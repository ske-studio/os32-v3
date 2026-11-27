/* Run the shell loader in user mode. The test has a resident landing and a synchronous child landing. */
.global exec_setjmp
.global exec_longjmp
exec_setjmp:
#ifdef __PPC__
    lis 3,probe_jump@ha
    lwz 3,probe_jump@l(3)
    b setjmp
#else
    movl probe_jump,%eax
    movl %eax,4(%esp)
    jmp setjmp
#endif
exec_longjmp:
#ifdef __PPC__
    lis 3,probe_jump@ha
    lwz 3,probe_jump@l(3)
    li 4,1
    b longjmp
#else
    pushl $1
    pushl probe_jump
    call longjmp
#endif
