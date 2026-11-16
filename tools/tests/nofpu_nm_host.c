#include "x87.h"
#include "x87_nm.h"
#include "cpu.h"
static void die(unsigned rc) { __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory"); for (;;) {} }
#define CHECK(x, n) do { if (!(x)) die(n); } while (0)
static u32 cr0, original, flags, saved_flags, writes;
static int ac;
/* Referenced from substituted FNSTCW, which still lives in the real asm path. */
u32 control_reads;
u32 x87_test_irq_save(void) { u32 f=flags; flags &= ~512u; return f; }
void x87_test_irq_restore(u32 f) { flags=f; }
u32 x87_test_cr0_read(void) { return cr0; }
void x87_test_cr0_write(u32 f) {
    CHECK(!(flags & 512), 1);
    if (!writes) CHECK(f == (original & ~12u), 2);
    cr0=f; writes++;
}
int x87_test_has_ac(void) { CHECK(!(flags & 512), 3); return ac; }
#include "x87_source.c"

static int emu_calls, faults, emulate_ok;
extern void asm_nm_test(void);
static u8 instruction[] = {0xdd, 0x1c, 0x24}; /* fstpl (%esp) */
static enum x87_emu_result fetch(void *ctx, u32 at, u8 *v) {
    (void)ctx;
    if (at < 0x1000 || at >= 0x1003) return X87_EMU_MEMORY_FAULT;
    *v=instruction[at-0x1000]; return X87_EMU_OK;
}
static int fixture_emulate(struct x87_emu_trap *t) {
    struct x87_emu_decoded d;
    emu_calls++;
    CHECK(t->gpr[X87_ESP]==0x2000 && t->segment[X87_SS]==0x2b, 20);
    CHECK(t->segment[X87_DS]==0 && t->segment[X87_FS]==0x23 && t->segment[X87_GS]==0, 21);
    CHECK(x87_emu_decode(t,fetch,0,&d)==X87_EMU_OK && d.offset==0x2000 && d.segment==X87_SS, 22);
    t->eip+=3; t->gpr[X87_EAX]=0x1234;
    return emulate_ok;
}
static void exception_handler(u32 error, u32 vector, u32 eip, u32 *r) {
    CHECK(error==0 && vector==7 && eip==r[10] && r[11]!=0, 23);
    faults++;
}
#define x87_emulate_nm fixture_emulate
#include "nm_handler.inc"
#undef x87_emulate_nm

static void probe_cases(void) {
    for (ac=0; ac<2; ++ac) for (unsigned ne=0;ne<2;++ne)
        for (unsigned irq=0;irq<2;++irq) for(unsigned bits=0;bits<4;++bits) {
            original=cr0=0x80010011 | (ne<<5) | (bits<<2);
            flags=saved_flags=2|(irq<<9); writes=control_reads=0;
            x87_probe();
            CHECK(writes==2 && flags==saved_flags && !probe_active, 4);
            CHECK(arch_fp_present()==EXPECT_PRESENT, 5);
            CHECK(control_reads==(PROBE_STATUS==0), 6);
            u32 expected=original|2;
            if (EXPECT_PRESENT) expected &= ~12u;
            else expected |= 12u;
            if (ac) { expected &= ~32u; if (EXPECT_PRESENT) expected |=32u; }
            CHECK(cr0==expected, 7);
        }
    u32 labels[]={(u32)x87_probe_init,(u32)x87_probe_status,(u32)x87_probe_control};
    for (unsigned i=0;i<3;++i) for(unsigned active=0;active<2;++active)
        for(unsigned user=0;user<2;++user) for(unsigned vm=0;vm<2;++vm) {
            probe_active=active; u32 pc=labels[i];
            int want=active && !user && !vm;
            CHECK(x87_probe_nm(user?0x23:8, vm<<17, &pc)==want,8);
            CHECK(pc==(want?(u32)x87_probe_absent:labels[i]),9);
        }
    probe_active=1;
    u32 pc=(u32)x87_probe_status+1;
    CHECK(!x87_probe_nm(8,0,&pc) && pc==(u32)x87_probe_status+1,10);
    probe_active=0;
}
static void frame_cases(void) {
    /* Construct raw ASM-order words, independent of the C field names. */
    u32 raw[]={0,0x23,0x2b,0, 7,6,5,0xdead0000,3,2,1,0,
               7,0,0x1000,0x23,0x602,0x2000,0x2b};
    struct x87_nm_frame *f=(void *)raw;
    emulate_ok=1; nm_handler(f);
    CHECK(emu_calls==1 && !faults && raw[14]==0x1003 && raw[11]==0x1234,24);
    CHECK(raw[7]==0xdead0000 && raw[17]==0x2000 && raw[18]==0x2b,25);
    CHECK(raw[0]==0 && raw[1]==0x23 && raw[2]==0x2b && raw[3]==0,26);
    raw[14]=0x1000; emulate_ok=0; nm_handler(f);
    CHECK(faults==1 && raw[14]==0x1000,27);
    raw[15]=8; nm_handler(f); CHECK(faults==2 && emu_calls==2,28);
    raw[15]=0x23; raw[16]|=1u<<17; nm_handler(f);
    CHECK(faults==3 && emu_calls==2,29);
    raw[15]=8; raw[16]=2; raw[14]=(u32)x87_probe_init; probe_active=1;
    nm_handler(f); CHECK(raw[14]==(u32)x87_probe_absent && faults==3 && emu_calls==2,30);
    probe_active=0;
    emulate_ok=1;
    asm_nm_test();
    CHECK(emu_calls==3 && faults==3,40);
}
#ifdef OS32_X87_EMU_LINKED
static AppSlot slot;
int appslot_cur(void) { return 1; }
AppSlot *appslot_get(int id) { CHECK(id==1,31); return &slot; }
enum x87_emu_result x87_emu_dispatch(struct x87_emu_trap *t,
    const struct addrspace *as, struct x87_emu_state *active) {
    CHECK(t && as==slot.as && active==&x87_active,32);
    return X87_EMU_OK;
}
static void software_cases(void) {
    struct arch_fp_context saved;
    struct x87_emu_trap t={0};
    x87_present=0; arch_fp_init();
    CHECK(x87_active.cw==0x37f && x87_active.tw==0xffff,33);
    x87_active.sw=3u<<11; x87_active.cw=0x77f;
    for(unsigned i=0;i<8;++i) for(unsigned j=0;j<10;++j)
        x87_active.reg[i].bytes[j]=i*10+j;
    arch_fp_save(&saved);
    const u8 *wire=(const u8 *)&saved;
    for(unsigned i=0;i<8;++i) for(unsigned j=0;j<10;++j)
        CHECK(wire[28+i*10+j]==((i+3)%8)*10+j,34);
    CHECK(x87_active.sw==0 && x87_active.cw==0x37f && x87_active.reg[3].bytes[0]==30,35);
    arch_fp_restore(&saved);
    CHECK(x87_active.sw==(3u<<11) && x87_active.cw==0x77f,36);
    for(unsigned i=0;i<8;++i) for(unsigned j=0;j<10;++j)
        CHECK(x87_active.reg[i].bytes[j]==i*10+j,37);
    CHECK(x87_emulate_nm(&t),38);
    arch_fp_init();
    for(unsigned i=0;i<8;++i) for(unsigned j=0;j<10;++j)
        CHECK(x87_active.reg[i].bytes[j]==0,39);
}
#endif
void _start(void) {
    probe_cases(); frame_cases();
#ifdef OS32_X87_EMU_LINKED
    software_cases();
#endif
    die(0);
}
