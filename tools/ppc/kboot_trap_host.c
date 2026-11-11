static unsigned verdict=1;
static void checkpoint(unsigned n)
{
    verdict=n;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(&verdict), "d"(4) : "memory");
}
#define assert(x) do { if (!(x)) { __asm__ volatile("movl $1,%%eax; int $0x80" : : "b"(verdict) : "eax"); __builtin_unreachable(); } } while (0)
#define setjmp(x) __builtin_setjmp(x)
#define longjmp(x,n) __builtin_longjmp(x,n)
#include "ppc_trap.h"
static void *stopped[5];
static u32 machine=0x30, reason, dumps, reads, handled, root=0x1000, leaf;
u32 ppc_trap_msr(void) { return machine; }
u32 ppc_root_current(void) { return root; }
void ppc_trap_stop(void) { longjmp(stopped,1); }
void ppc_trap_fatal_sink(const struct ppc_frame *f,u32 r)
{ assert(f); assert(machine==0x30); reason=r; ++dumps; }
static u32 reader(u32 pa,void *ctx)
{ (void)ctx; ++reads; return pa<0x2000 ? 0x2003 : leaf; }
static void handler(struct ppc_frame *f) { ++handled; ++f->gpr[3]; }
static void fatal(struct ppc_frame *f,u32 expected)
{
    u32 before=dumps;
    if (!setjmp(stopped)) { ppc_trap_dispatch(f); assert(0); }
    assert(dumps==before+1 && reason==expected);
}
int main(void)
{
    checkpoint(10);
    ppc_trap_set_fatal_ready(1); ppc_trap_set_reader(reader,0);
    struct ppc_frame f={.srr0=0x100,.dar=0x50000000,.vector=0x300};
    const u32 causes[]={0x40000000,0x08000000,0x04000000,0x00100000,0x00400000,0};
    const u32 reasons[]={PPC_TRAP_DSI_MISS,PPC_TRAP_DSI_PROTECT,PPC_TRAP_DSI_IO,
                         PPC_TRAP_DSI_IO,PPC_TRAP_UNKNOWN,PPC_TRAP_UNKNOWN};
    for (u32 i=0;i<6;++i) { checkpoint(20+i); f.dsisr=causes[i]; fatal(&f,reasons[i]);
        f.dsisr|=0x02000000; fatal(&f,reasons[i]); }
    assert(reads==24);
    checkpoint(40);
    f.dsisr=0x40000000;
    ppc_trap_set_reader(0,0);
    assert(ppc_trap_onfault(f.srr0,f.dar,PPC_TRAP_DSI_MISS,0x200));
    fatal(&f,PPC_TRAP_UNKNOWN);
    ppc_trap_set_reader(reader,0);
    checkpoint(41);
    root=0;
    assert(ppc_trap_onfault(f.srr0,f.dar,PPC_TRAP_DSI_MISS,0x200));
    u32 before=dumps;
    if (!setjmp(stopped)) { ppc_trap_dispatch(&f); assert(0); }
    assert(dumps==before && f.srr0==0x100);
    root=0x1000;
    checkpoint(42);
    leaf=0x3013; /* present guarded data page, miss is not an I/O cause */
    assert(ppc_trap_onfault(f.srr0,f.dar,PPC_TRAP_DSI_IO,0x200));
    fatal(&f,PPC_TRAP_UNKNOWN);
    leaf=0;
    checkpoint(43);
    f.dsisr=0x40000000;
    assert(ppc_trap_onfault(0x100,f.dar,PPC_TRAP_DSI_MISS,0x200));
    assert(!ppc_trap_onfault(0x100,f.dar,PPC_TRAP_DSI_MISS,0x200));
    ppc_trap_dispatch(&f); assert(f.srr0==0x200);
    f.srr0=0x100; fatal(&f,PPC_TRAP_DSI_MISS);
    for (u32 i=0;i<3;++i) {
        checkpoint(50+i);
        assert(ppc_trap_onfault(0x100+(i==0?4:0),f.dar+(i==1?4:0),
                    i==2?PPC_TRAP_DSI_IO:PPC_TRAP_DSI_MISS,0x200));
        fatal(&f,PPC_TRAP_DSI_MISS);
    }
    const u32 vectors[]={0x400,0x600,0x700,0xc00,0x500,0x900,0x800};
    const u32 other[]={PPC_TRAP_ISI,PPC_TRAP_ALIGNMENT,0x100|PPC_TRAP_UNKNOWN,
        0x100|PPC_TRAP_SC,PPC_TRAP_UNREGISTERED,PPC_TRAP_UNREGISTERED,PPC_TRAP_UNKNOWN};
    for(u32 i=0;i<7;++i) {
        f.vector=vectors[i]; f.srr1=i==0?PPC_ISI_MISS:0; fatal(&f,other[i]);
    }
    checkpoint(60);
    f.vector=PPC_VECTOR_ISI; fatal(&f,PPC_TRAP_UNKNOWN);
    f.srr1=PPC_ISI_MISS;
    assert(ppc_trap_onfault(f.srr0,f.srr0,PPC_TRAP_ISI,0x200));
    ppc_trap_dispatch(&f); assert(f.srr0==0x200);
    f.srr0=0x100;
    const u32 isi_causes[]={0x08000000,0x10000000};
    const u32 isi_reasons[]={PPC_TRAP_ISI_PROTECT,PPC_TRAP_ISI_NOEXEC};
    for (u32 i=0;i<2;++i) {
        checkpoint(61+i); f.srr1=isi_causes[i];
        assert(ppc_trap_onfault(f.srr0,f.srr0,PPC_TRAP_ISI,0x200));
        fatal(&f,isi_reasons[i]);
        assert(ppc_trap_onfault(f.srr0,f.srr0,isi_reasons[i],0x200));
        ppc_trap_dispatch(&f); assert(f.srr0==0x200); f.srr0=0x100;
    }
    const u32 program_causes[]={0x100000,0x80000,0x40000,0x20000};
    const u32 program_reasons[]={PPC_TRAP_PROGRAM_FP,PPC_TRAP_PROGRAM_ILLEGAL,
        PPC_TRAP_PROGRAM_PRIVILEGED,PPC_TRAP_PROGRAM_TRAP};
    for (u32 i=0;i<4;++i) {
        checkpoint(65+i); f.vector=0x700; f.srr1=program_causes[i];
        fatal(&f,program_reasons[i]|PPC_TRAP_UNSUPPORTED);
    }
    checkpoint(70); f.srr1=0;
    for(u32 i=0;i<2;++i) {
        f.vector=i?0x900:0x500; ppc_trap_set_handler(f.vector,handler);
        ppc_trap_dispatch(&f); ppc_trap_set_handler(f.vector,0);
        fatal(&f,PPC_TRAP_UNREGISTERED);
    }
    assert(handled==2 && f.gpr[3]==2);
    f.vector=PPC_VECTOR_DSI; f.srr1=0x4000; fatal(&f,0x100|PPC_TRAP_USER); f.srr1=0;
    for(u32 i=0;i<3;++i) {
        checkpoint(80+i);
        u32 before=dumps;
        machine=i==0?0: i==1?0x8030:0x30;
        ppc_trap_set_fatal_ready(i!=2);
        if(!setjmp(stopped)) { ppc_trap_dispatch(&f); assert(0); }
        assert(dumps==before);
    }
    return 0;
}

void _start(void) { int rc=main(); __asm__ volatile("movl $1,%%eax; int $0x80" : : "b"(rc) : "eax"); __builtin_unreachable(); }

/* The fixture never enters an exec user context. */
struct arch_syscall;
void exec_syscall_dispatch(struct arch_syscall *call) { (void)call; }
void ring3_fault_kill(void) { }
