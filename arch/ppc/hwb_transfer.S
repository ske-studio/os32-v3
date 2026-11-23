/* Caller owns the IRQ-disabled trap guard. r3 is VA, r4 points to the
 * read result or holds the first store value, r5=count, r6=increment.
 * The DSI continuation uses the same live LR as the normal leaf exit. */
    .text
    .macro transfer name, insn, width, store, reg, indexed=0
    .globl ppc_hwb_\name, ppc_hwb_\name\()_insn, ppc_hwb_\name\()_resume
ppc_hwb_\name:
    .if \store
    mtctr 5
    .endif
    eieio
ppc_hwb_\name\()_insn:
    .if \indexed
    \insn \reg,0,3
    .else
    \insn \reg,0(3)
    .endif
    eieio
    .if \store
    addi 3,3,\width
    add 4,4,6
    bdnz ppc_hwb_\name\()_insn
    .endif
    sync
    .if !\store
    stw 7,0(4)
    .endif
    li 3,0
    blr
ppc_hwb_\name\()_resume:
    eieio
    sync
    li 3,4
    blr
    .endm
    transfer read8,lbz,1,0,7
    transfer read16,lhz,2,0,7
    transfer read32,lwz,4,0,7
    transfer read16le,lhbrx,2,0,7,1
    transfer read32le,lwbrx,4,0,7,1
    transfer write8,stb,1,1,4
    transfer write16,sth,2,1,4
    transfer write32,stw,4,1,4
    transfer write16le,sthbrx,2,1,4,1
    transfer write32le,stwbrx,4,1,4,1
