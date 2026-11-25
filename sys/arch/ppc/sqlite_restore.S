/* SysV PPC32 ABI, figures 3-32 and 3-33: r11 addresses the caller frame. */
.text
.macro restore_gpr n
.globl _restgpr_\n\()_x
_restgpr_\n\()_x:
    lmw \n, (\n - 32) * 4(11)
    b .Lexit
.endm
.irp n,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    restore_gpr \n
.endr

.globl _restfpr_30_x
_restfpr_30_x:
    lfd 30,-16(11)
.globl _restfpr_31_x
_restfpr_31_x:
    lfd 31,-8(11)
.Lexit:
    lwz 0,4(11)
    mr 1,11
    mtlr 0
    blr
