/* MACHCFG_DESIGN §4, §7 and P-R3 (post-configuration HID0).
 * User decision 2026-11-16 and mainline PPC NAP assignment.
 * ILP32, no libc: real product C with fake privileged operations. */
#include "ppc_trap.h"
#include "kernel/irq/irq.h"
#include "exec_arch.h"
volatile u32 kctx_irq_depth;
static u32 hid, reads, writes, order, naps, enables;
static int ee;
static void finish(int code)
{
    __asm__ volatile("int $0x80" :: "a"(1), "b"(code));
    __builtin_unreachable();
}
#define CHECK(c, n) do { if (!(c)) finish(n); } while (0)
u32 ppc_read_hid0(void) { ++reads; return hid; }
void ppc_sync(void) { order = order * 10 + 1; }
void ppc_write_hid0(u32 value) { ++writes; hid=value; order=order*10+2; }
void ppc_isync(void) { order=order*10+3; }
int ppc_irq_enabled(void) { return ee; }
void ppc_enable(void) { ++enables; ee=1; }
void ppc_cpu_nap(void) { ++naps; }
u32 ppc_trap_msr(void) { return PPC_MSR_IR | PPC_MSR_DR; }
u32 ppc_root_current(void) { return 0; }
void ppc_trap_stop(void) { finish(90); }
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 r) { (void)f; (void)r; finish(91); }
void ppc_user_fault_kill(void) { finish(92); }
u32 ppc_irq_save(void) { ee=0; return 0; }
void exec_syscall_dispatch(struct arch_syscall *c) { (void)c; }
static u32 expected, delivered;
static void handler(struct ppc_frame *f)
{
    CHECK(f->srr1 == expected && kctx_irq_depth == 1, 40);
    ++delivered;
}
#include "power.inc"
#include "idle.inc"
void _start(void)
{
    ee=1; arch_cpu_idle();
    CHECK(naps==1 && enables==0 && ee==1, 10);
    naps=enables=0; ee=0; arch_cpu_idle();
    CHECK(naps==0 && enables==1 && ee==1, 11);
    naps=enables=0; kctx_irq_depth=1; arch_cpu_idle();
    CHECK(naps==0 && ee==1, 12);
    kctx_irq_depth=0;
    for (u32 cpu=0; cpu<2; ++cpu) {
        /* Exercise every preserved bit individually, and SLEEP initially off/on. */
        for (u32 bit=0; bit<32; ++bit) {
            u32 before=(1u<<bit) | PPC_HID_SLEEP;
            reads=writes=order=0; hid=before;
            ppc_cpu_power_init(((cpu ? PPC_PVR_7455 : PPC_PVR_7450)<<16)|0x201);
            CHECK(hid==((before & ~0x00600000u)|(cpu ? 0x00500000u : 0x00100000u)), 20);
            CHECK(reads==1 && writes==1 && order==1213, 21);
        }
        reads=writes=order=0; hid=0;
        ppc_cpu_power_init((cpu ? PPC_PVR_7455 : PPC_PVR_7450)<<16);
        CHECK(hid==(cpu ? 0x00500000u : 0x00100000u) && order==1213, 22);
    }
    reads=writes=order=0; hid=0xa5a5a5a5;
    ppc_cpu_power_init(0x00080000);
    CHECK(reads==0 && writes==0 && order==0 && hid==0xa5a5a5a5, 23);
    const u32 pvrs[] = {0x80000201, 0x80010201, 0x00080000};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned ones = 0; ones <= 1; ++ones) {
            for (u8 nap = 0; nap <= 1; ++nap) {
                u32 before = ones ? ~0u : 0;
                reads = writes = order = 0;
                hid = before;
                ppc_cpu_power_apply(pvrs[i], nap);
                if (i == 0) {
                    CHECK(hid == ((before & ~0x00400000u) | ((u32)nap << 22)), 24);
                    CHECK(reads == 1 && writes == 1 && order == 1213, 25);
                } else {
                    CHECK(hid == before && reads == 0 && writes == 0 && order == 0, 26);
                }
            }
        }
    }
    ppc_trap_set_handler(0x500, handler);
    ppc_trap_set_handler(PPC_VECTOR_DEC, handler);
    for (u32 v=0; v<2; ++v) {
        for (u32 bit=0; bit<32; ++bit) {
            struct ppc_frame f={ .vector=v ? PPC_VECTOR_DEC : 0x500,
                                .srr1=(1u<<bit)|0x00040000u };
            expected=f.srr1 & ~0x00040000u;
            ppc_trap_dispatch(&f);
            CHECK(f.srr1==expected && kctx_irq_depth==0, 41);
        }
    }
    CHECK(delivered==64, 42);
    /* Non-IRQ syscall returns normally, retaining POW and all saved MSR bits. */
    struct ppc_frame f={ .vector=0xc00, .srr1=0xa5a54000u };
    ppc_trap_dispatch(&f);
    CHECK(f.srr1==0xa5a54000u, 43);
    finish(0);
}
