#include "ppc_trap.h"
static void *stop[5];
static u32 last, marks;
u32 ppc_trap_msr(void) { return 0x30; }
u32 ppc_root_current(void) { return 0x1000; }
void ppc_trap_stop(void) { __builtin_longjmp(stop,1); }
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 reason) { (void)f; last=reason; }
static u32 reader(u32 pa, void *ctx) { (void)ctx; return pa<0x2000 ? 0x2003 : 0; }
static void mark(u32 n) { marks=n; __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(&marks),"d"(4) : "memory"); }
#define CHECK(x) do { if (!(x)) { __asm__ volatile("int $0x80" : : "a"(1),"b"(marks)); __builtin_unreachable(); } } while (0)
static void fatal(struct ppc_frame *f, u32 reason) {
    last=0; if (!__builtin_setjmp(stop)) { ppc_trap_dispatch(f); CHECK(0); }
    CHECK(last==reason);
}
int main(void) {
    struct ppc_frame f={.srr0=0x100,.dar=0x100,.vector=PPC_VECTOR_DSI};
    const u32 causes[]={PPC_DSISR_MISS,PPC_DSISR_PROTECT,0x04000000};
    ppc_trap_set_reader(reader,0); ppc_trap_set_fatal_ready(1);
    for (u32 i=0;i<3;i++) { mark(10+i); f.dsisr=causes[i]; f.srr0=0x100;
        CHECK(ppc_trap_onfault_data(0x100,0x100,0x200)); ppc_trap_dispatch(&f); CHECK(f.srr0==0x200); }
    mark(27); f.srr0=0x100; f.dsisr=PPC_DSISR_PROTECT;
    CHECK(ppc_trap_onfault(0x100,0x100,PPC_TRAP_DSI_MISS,0x200));
    fatal(&f,PPC_TRAP_DSI_PROTECT);
    mark(28); CHECK(ppc_trap_onfault_data(0x100,0x100,0x200));
    ppc_trap_onfault_clear(); f.dsisr=0x04000000;
    CHECK(ppc_trap_onfault(0x100,0x100,PPC_TRAP_DSI_MISS,0x200));
    fatal(&f,PPC_TRAP_DSI_IO);
    mark(20); f.srr0=0x100; f.vector=PPC_VECTOR_ISI; f.srr1=PPC_ISI_MISS;
    CHECK(ppc_trap_onfault_data(0x100,0x100,0x200)); fatal(&f,PPC_TRAP_ISI);
    mark(21); f.vector=PPC_VECTOR_DSI; f.srr1=0; f.dsisr=0;
    CHECK(ppc_trap_onfault_data(0x100,0x100,0x200)); fatal(&f,PPC_TRAP_UNKNOWN);
    mark(22); f.dsisr=PPC_DSISR_MISS;
    CHECK(ppc_trap_onfault_data(0x104,0x100,0x200)); fatal(&f,PPC_TRAP_DSI_MISS);
    mark(23); CHECK(ppc_trap_onfault_data(0x100,0x104,0x200)); fatal(&f,PPC_TRAP_DSI_MISS);
    mark(24); CHECK(ppc_trap_onfault_data(0x100,0x100,0x200));
    CHECK(!ppc_trap_onfault_data(0x104,0x104,0x204)); ppc_trap_dispatch(&f); CHECK(f.srr0==0x200);
    mark(25); f.srr0=0x100; fatal(&f,PPC_TRAP_DSI_MISS);
    mark(26); CHECK(!ppc_trap_onfault_data(0x101,0x100,0x200));
    return 0;
}
void _start(void) { int r=main(); __asm__ volatile("int $0x80" : : "a"(1),"b"(r)); __builtin_unreachable(); }

/* The fixture never enters an exec user context. */
struct arch_syscall;
void exec_syscall_dispatch(struct arch_syscall *call) { (void)call; }
void ring3_fault_kill(void) { }
