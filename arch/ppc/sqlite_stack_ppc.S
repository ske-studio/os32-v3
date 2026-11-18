/* SysV integer ABI: call on a 16-byte aligned alternate stack. */
.text
.align 2
.globl sqlite_call_on_alt_stack
.type sqlite_call_on_alt_stack,@function
sqlite_call_on_alt_stack:
    stwu 1,-16(1)
    mflr 0
    stw 0,20(1)
    stw 30,8(1)
    stw 31,12(1)
    mr 30,1
    mr 31,3
    clrrwi 1,4,4
    addi 1,1,-16
    stw 30,0(1)
    mr 3,5
    mtctr 31
    bctrl
    mr 1,30
    lwz 30,8(1)
    lwz 31,12(1)
    lwz 0,20(1)
    mtlr 0
    addi 1,1,16
    blr
.size sqlite_call_on_alt_stack,.-sqlite_call_on_alt_stack
