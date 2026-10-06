/* Position-independent cdecl shim, copied into the RO+USER trampoline page.
 * All indirect callback calls execute after int80 returns to CPL3. */
#include "ring3_ls.h"
#include "os32_generations.h"
    .section .os32_generations,"",@progbits
    .long OS32X_HDR_VERSION, OS32_KAPI_ABI_GENERATION
    .long OS32_MEMORY_LAYOUT_GENERATION, OS32_SHLIB_PROTOCOL
    .text
    .code32
    .global ring3_ls_start, ring3_ls_end
ring3_ls_start:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    andl $-16, %esp
    subl $RING3_LS_STACK_SIZE, %esp
    /* Snapshot the path once, including NULL=cwd and overlong paths. */
    movl 8(%ebp), %esi
    leal RING3_LS_PATH_OFF(%esp), %edi
    movl $RING3_LS_PATH_SIZE, %ecx
    testl %esi, %esi
    jz .Lnull_path
.Lpath:
    lodsb
    stosb
    testb %al, %al
    jz .Lpath_ready
    loop .Lpath
    jmp .Lpath_ready
.Lnull_path:
    movb $0, (%edi)
.Lpath_ready:
    xorl %ebx, %ebx             /* number already delivered */
.Lbatch:
    movl %esp, %eax
    pushl %eax                 /* output */
    pushl %ebx                 /* skip */
    leal RING3_LS_PATH_OFF+8(%esp), %eax
    pushl %eax                 /* snapshotted path */
    pushl $0                   /* dispatcher skips cdecl return address */
    movl $RING3_LS_CALL, %eax
    int $0x80
    addl $16, %esp
    testl %eax, %eax
    js .Lreturn
    movl (%esp), %edi
    addl %edi, %ebx
    leal RING3_LS_ENTRIES_OFF(%esp), %esi
.Lentry:
    testl %edi, %edi
    jz .Ldone
    subl $8, %esp              /* i386 ABI: align before call */
    pushl 16(%ebp)             /* original ctx, never read by the kernel */
    pushl %esi
    call *12(%ebp)             /* original callback, CPL3 */
    addl $16, %esp
    addl $RING3_LS_ENTRY_SIZE, %esi
    decl %edi
    jmp .Lentry
.Ldone:
    cmpl $0, 8(%esp)
    je .Lbatch
    movl 4(%esp), %eax         /* preserve backend's return value */
.Lreturn:
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    leave
    ret
    /* Backward .org is an assembler error if the shim outgrows its budget. */
    .org ring3_ls_start + RING3_LS_SHIM_CAP, 0x90
ring3_ls_end:
    .section .note.GNU-stack,"",@progbits
