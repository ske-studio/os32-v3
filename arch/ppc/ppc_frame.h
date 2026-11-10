#ifndef OS32_PPC_FRAME_H
#define OS32_PPC_FRAME_H
#define PPC_FRAME_LR 128
#define PPC_FRAME_CR 132
#define PPC_FRAME_CTR 136
#define PPC_FRAME_XER 140
#define PPC_FRAME_SRR0 144
#define PPC_FRAME_SRR1 148
#define PPC_FRAME_DAR 152
#define PPC_FRAME_DSISR 156
#define PPC_FRAME_VECTOR 160
#define PPC_FRAME_SIZE 176
#define PPC_CALL_SIZE 32
#define PPC_KERNEL_STACK_SIZE 16384
#define PPC_KERNEL_FATAL_STACK_SIZE 4096
#define PPC_SAVE_CR 16
#define PPC_SAVE_SRR0 20
#define PPC_SAVE_SRR1 24
#define PPC_SAVE_DAR 28
#define PPC_SAVE_DSISR 32
#define PPC_SAVE_SIZE 48
#define PPC_VECTOR_DSI 0x300
#define PPC_VECTOR_ISI 0x400
#define PPC_VECTOR_DEC 0x900
#define PPC_PROBE_MAP 1
#define PPC_PROBE_ONFAULT 2
#ifndef __ASSEMBLER__
#include "ppc_oea.h"
struct ppc_vector_image { const void *src; u32 size; };
struct ppc_frame {
    u32 gpr[32], lr, cr, ctr, xer, srr0, srr1, dar, dsisr, vector;
};
struct ppc_cpu_save { u32 r28, r29, r30, r31, cr, srr0, srr1, dar, dsisr; };
extern struct ppc_cpu_save ppc_cpu_save;
extern const struct ppc_vector_image ppc_vectors;
void ppc_exception(struct ppc_frame *frame);
/* Kernel-only image; resident physical symbols must also be identity mapped. */
extern const struct ppc_vector_image ppc_kernel_vectors;
extern unsigned char ppc_kernel_trap_stack[PPC_KERNEL_STACK_SIZE];
extern unsigned char ppc_kernel_spill_stack[PPC_KERNEL_STACK_SIZE];
extern unsigned char ppc_kernel_fatal_stack[PPC_KERNEL_FATAL_STACK_SIZE];
void ppc_trap_dispatch(struct ppc_frame *frame);
void pm_mmu_console_bind(u32 escc_pa);
void pm_mmu_dump(const char *tag, const struct ppc_frame *frame);
void ppc_dec_init(u32 tb_hz);
void ppc_dec_interrupt(struct ppc_frame *frame);
u32 ppc_dec_count(void);
extern unsigned char ppc_probe_pages[2][PPC_PAGE_SIZE];
struct ppc_onfault { u32 pc, sp, cr, regs13_31[19]; };
struct ppc_probe_record {
    u32 active, vector, fault_pc, va, pa, attr, action;
    struct ppc_onfault onfault;
};
extern struct ppc_probe_record ppc_probe;
_Static_assert(__builtin_offsetof(struct ppc_frame,vector)==PPC_FRAME_VECTOR,"frame ABI");
_Static_assert(__builtin_offsetof(struct ppc_cpu_save,dsisr)==PPC_SAVE_DSISR,"save ABI");
#endif
#endif
