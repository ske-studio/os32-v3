/* Private #NM stack contract: four segment pushes, PUSHAD, common exception
 * words, then the CPU privilege-transition frame. Segment push upper halves
 * are unspecified on 386; consumers always narrow them to u16. */
#ifndef OS32_X87_NM_H
#define OS32_X87_NM_H
#include "x87_emu.h"
struct x87_nm_frame {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, pushad_esp, ebx, edx, ecx, eax;
    u32 vector, error, eip, cs, eflags;
    u32 user_esp, user_ss; /* Only present for CPL3. Never read for CPL0. */
};
_Static_assert(__builtin_offsetof(struct x87_nm_frame, eip) == 56,
               "#NM PUSHAD and segment layout");
static inline void x87_nm_read(const struct x87_nm_frame *f, struct x87_emu_trap *t)
{
    t->gpr[X87_EAX] = f->eax; t->gpr[X87_ECX] = f->ecx;
    t->gpr[X87_EDX] = f->edx; t->gpr[X87_EBX] = f->ebx;
    t->gpr[X87_ESP] = f->user_esp; t->gpr[X87_EBP] = f->ebp;
    t->gpr[X87_ESI] = f->esi; t->gpr[X87_EDI] = f->edi;
    t->eip = f->eip; t->eflags = f->eflags;
    t->segment[X87_CS] = f->cs; t->segment[X87_SS] = f->user_ss;
    t->segment[X87_DS] = f->ds; t->segment[X87_ES] = f->es;
    t->segment[X87_FS] = f->fs; t->segment[X87_GS] = f->gs;
}
static inline void x87_nm_write(struct x87_nm_frame *f, const struct x87_emu_trap *t)
{
    f->eax = t->gpr[X87_EAX]; f->ecx = t->gpr[X87_ECX];
    f->edx = t->gpr[X87_EDX]; f->ebx = t->gpr[X87_EBX];
    f->user_esp = t->gpr[X87_ESP]; f->ebp = t->gpr[X87_EBP];
    f->esi = t->gpr[X87_ESI]; f->edi = t->gpr[X87_EDI];
    f->eip = t->eip; f->eflags = t->eflags;
}
#endif
