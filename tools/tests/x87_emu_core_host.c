/* Shared corpus; reference code executes native x87 without libc (ELF64).
 * Product code runs ILP32 through host32; it consumes captured hardware bits. */
#define ARITH_PAIRS {{0x3f800000,0x40400000},{0xc0200000,0x3eaaaaab},\
 {0x3f800000,0},{0,0},{0x7f800000,0x3f800000},{0x3f800000,0x33800000}}
#define CONVERT_VALUES {0x3fc00000,0xbfc00000,0x4f000000,0x7f800000,0x80000000}
#define EXT_CASES { \
 {0,0x37f,{0xffffffffffffffffULL,0x7ffe},{0xffffffffffffffffULL,0x7ffe}}, \
 {0,0x377,{0xffffffffffffffffULL,0x7ffe},{0xffffffffffffffffULL,0x7ffe}}, \
 {1,0x377,{0xffffffffffffffffULL,0x7ffe},{0x8000000000000000ULL,0x4000}}, \
 {6,0x377,{0xffffffffffffffffULL,0x7ffe},{0x8000000000000000ULL,0x3ffe}}, \
 {1,0x36f,{0x8000000000000000ULL,1},{0x8000000000000000ULL,0x3ffe}}, \
 {6,0x36f,{0x8000000000000000ULL,1},{0x8000000000000000ULL,0x4000}}, \
 {6,0x35f,{0x8000000000000000ULL,0x3fff},{0xc000000000000000ULL,0x4000}}, \
 {0,0x37f,{1,0},{0x8000000000000000ULL,0x3fff}}, \
 {0,0x37d,{1,0},{0x8000000000000000ULL,0x3fff}}, \
 {0,0x37f,{0xc000000000000001ULL,0x7fff},{1,0}}, \
 {0,0x37f,{0x8000000000000001ULL,0x7fff},{1,0}}, \
 {2,0x37e,{0xc000000000000001ULL,0x7fff},{1,0}}, \
 {1,0x37f,{1,0x4000},{0x8000000000000000ULL,0x3fff}}, \
 {1,0x37e,{1,0x4000},{0x8000000000000000ULL,0x3fff}} \
}
#define STORE_CASES { \
 {0,0x37f,{0x8000000000000000ULL,0x3f80},{0,0}}, \
 {0,0x36f,{0x8000000000000000ULL,0x3f80},{0,0}}, \
 {0,0x377,{0x8000000000000000ULL,0x407f},{0,0}}, \
 {0,0x35f,{0x8000008000000000ULL,0x3fff},{0,0}}, \
 {0,0x37e,{0x8000000000000001ULL,0x7fff},{0,0}}, \
 {0,0x37f,{0x8000000000000000ULL,0x407f},{0,0}} \
}
#define STORE_COUNT 6
#define EXT_COUNT 14
#define EXT_TYPES struct raw80 { unsigned long long sig; u16 exp; }; \
 struct ext_case { unsigned op; u16 cw; struct raw80 a,b; }
#ifdef X87_ORACLE_BUILD
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
static u8 hw[108];
static void emit(const void *p, unsigned long n)
{
    while(n) {
        long rc;
        __asm__ volatile("syscall":"=a"(rc):"a"(1),"D"(1),"S"(p),"d"(n):"rcx","r11","memory");
        if(rc<=0) { __asm__ volatile("syscall"::"a"(60),"D"(1):"rcx","r11","memory"); __builtin_unreachable(); }
        p=(const u8 *)p+rc; n-=rc;
    }
}
#define ORACLE(code) __asm__ volatile("fninit; fldcw %3; flds %2; flds %1; " code "; fnsave %0" : "=m"(hw) : "m"(a), "m"(b), "m"(cw) : "memory")
static void oracle(unsigned op, u32 a, u32 b, u16 cw)
{
    switch (op) {
    case 0: ORACLE("fadd %%st(1), %%st"); break;
    case 1: ORACLE("fmul %%st(1), %%st"); break;
    case 2: ORACLE("fcom %%st(1)"); break;
    case 3: ORACLE("fcomp %%st(1)"); break;
    case 4: ORACLE("fsub %%st(1), %%st"); break;
    case 5: ORACLE("fsubr %%st(1), %%st"); break;
    case 6: ORACLE("fdiv %%st(1), %%st"); break;
    case 7: ORACLE("fdivr %%st(1), %%st"); break;
    }
}

EXT_TYPES;
static void oracle_ext(unsigned op, struct raw80 a, struct raw80 b, u16 cw)
{
#define EXT_ORACLE(code) __asm__ volatile("fninit; fldt %2; fldt %1; fldcw %3; " code "; fnsave %0" : "=m"(hw) : "m"(a), "m"(b), "m"(cw) : "memory")
    switch(op) {
    case 0: EXT_ORACLE("fadd %%st(1), %%st"); break;
    case 1: EXT_ORACLE("fmul %%st(1), %%st"); break;
    case 2: EXT_ORACLE("fcom %%st(1)"); break;
    case 6: EXT_ORACLE("fdiv %%st(1), %%st"); break;
    }
}
void _start(void)
{
    const u32 values[][2]=ARITH_PAIRS;
    for(unsigned rc=0;rc<4;++rc) for(unsigned pc=0;pc<4;++pc)
        for(unsigned k=0;k<6;++k) for(unsigned op=0;op<8;++op) {
            oracle(op,values[k][0],values[k][1],0x7f|(rc<<10)|(pc<<8));
            emit(hw,108);
        }
    const u32 converted[]=CONVERT_VALUES;
    for(unsigned rc=0;rc<4;++rc) for(unsigned k=0;k<5;++k) {
        u16 cw=0x37f|(rc<<10),sw; u32 v=converted[k],out;
        __asm__ volatile("fninit; fldcw %3; flds %2; fistpl %0; fnstsw %1; fninit"
            :"=m"(out),"=m"(sw):"m"(v),"m"(cw):"memory");
        emit(&out,4);emit(&sw,2);
    }
    for(unsigned rc=0;rc<4;++rc) for(unsigned pc=0;pc<4;++pc) for(unsigned kind=0;kind<2;++kind) {
        u16 cw=0x7f|(rc<<10)|(pc<<8); u32 v=0x40200000;
        if(kind) __asm__ volatile("fninit; fldcw %2; flds %1; frndint; fnsave %0":"=m"(hw):"m"(v),"m"(cw):"memory");
        else __asm__ volatile("fninit; fldcw %2; flds %1; fsqrt; fnsave %0":"=m"(hw):"m"(v),"m"(cw):"memory");
        emit(hw,108);
    }
    const struct ext_case cases[]=EXT_CASES;
    for(unsigned i=0;i<EXT_COUNT;++i) {
        oracle_ext(cases[i].op,cases[i].a,cases[i].b,cases[i].cw);emit(hw,108);
    }
    const struct ext_case stores[]=STORE_CASES;
    for(unsigned i=0;i<STORE_COUNT;++i) {
        u32 out=0x12345678; u16 cw=stores[i].cw;struct raw80 a=stores[i].a;
        __asm__ volatile("fninit; fldt %2; fldcw %3; fstps %0; fnsave %1"
            :"+m"(out),"=m"(hw):"m"(a),"m"(cw):"memory");
        emit(&out,4);emit(hw,108);
    }
    for(unsigned rc=0;rc<4;++rc) {
        u16 cw=0x37f|(rc<<10);
        __asm__ volatile("fninit; fldcw %1; fld1; fldpi; fnsave %0":"=m"(hw):"m"(cw):"memory");
        emit(hw,108);
    }
    __asm__ volatile("syscall"::"a"(60),"D"(0):"rcx","r11","memory");
    __builtin_unreachable();
}
#else
/* libc-free x87 oracle. Native WSL and qemu use the same instruction fixture. */
#include "x87_emu_internal.h"
#include "x87_emu_reference.h"
#include "paging.h"
#include "gdt.h"

static u8 ram[3 * PAGE_SIZE] __attribute__((aligned(4096)));
static struct addrspace owner;
static int reject_page = -1, reject_write, eligible_owner = 1, calls;
static struct x87_emu_trap trap;
static struct x87_emu_state state;
static u8 hw[108];
static int fail, reenter, nested_result;
static void diagnostic(u32 a, u32 b, u32 c, u32 d)
{
    u32 values[4] = {a,b,c,d}; char out[36];
    for(unsigned i=0;i<4;++i) {
        for(unsigned j=0;j<8;++j) out[i*9+j]="0123456789abcdef"[(values[i]>>(28-j*4))&15];
        out[i*9+8]=i==3?'\n':' ';
    }
    __asm__ volatile("int $0x80"::"a"(4),"b"(2),"c"(out),"d"(36):"memory");
}
#define CHECK(c, n) do { if (!(c)) { fail = n; return; } } while (0)
bool x87_emu_test_eligible(const struct addrspace *as) { return eligible_owner && as == &owner; }
int as_access_page(const struct addrspace *as, u32 va, int write, u32 *pa)
{
    ++calls;
    if (reenter) {
        reenter=0;
        nested_result=x87_emu_dispatch(&trap,&owner,&state);
    }
    if (as != &owner || va < 0x1000 || va >= 0x4000 ||
        (int)(va / PAGE_SIZE) == reject_page || (write && reject_write)) return 0;
    *pa = (u32)(uptr)(ram + va - 0x1000);
    return 1;
}
static void reset(void)
{
    x87_emu_state_init(&state, true);
    for (unsigned i = 0; i < sizeof(ram); ++i) ram[i] = 0;
    for (unsigned i = 0; i < X87_SEGMENTS; ++i) trap.segment[i] = USER_DS;
    trap.segment[X87_CS] = USER_CS;
    for (unsigned i = 0; i < 8; ++i) trap.gpr[i] = 0;
    trap.eflags = 0x202;
    reject_page = -1; reject_write = 0; eligible_owner = 1; calls = 0; reenter=0; nested_result=-1;
}
static enum x87_emu_result run(u8 op, u8 modrm)
{
    ram[0] = op; ram[1] = modrm;
    trap.eip = 0x1000;
    return x87_emu_dispatch(&trap, &owner, &state);
}
static enum x87_emu_result mem(u8 op, unsigned idx, u32 address)
{
    x87_put_le(ram + 2, address, 4);
    return run(op, (idx << 3) | 5);
}
static int same(const u8 *a, const u8 *b, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void oracle(unsigned op, u32 a, u32 b, u16 cw)
{
    (void)a; (void)b;
    unsigned rc=(cw>>10)&3, pc=(cw>>8)&3, k=0;
    const u32 pairs[][2] = ARITH_PAIRS;
    while(k<6 && (pairs[k][0]!=a || pairs[k][1]!=b)) ++k;
    for(unsigned i=0;i<108;++i) hw[i]=reference_arith[(((rc*4+pc)*6+k)*8+op)*108+i];
}
static void arithmetic(void)
{
    const u32 values[][2] = ARITH_PAIRS;
    for (unsigned rc = 0; rc < 4; ++rc) for (unsigned pc = 0; pc < 4; ++pc)
        for (unsigned k = 0; k < sizeof(values)/sizeof(values[0]); ++k)
            for (unsigned op = 0; op < 8; ++op) {
                reset();
                u16 cw = 0x007f | (rc << 10) | (pc << 8);
                state.cw = cw;
                sw_float32_t a = {values[k][0]}, b = {values[k][1]};
                x87_set_top(&state, 6);
                x87_put(&state, 6, f32_to_extF80(a));
                x87_put(&state, 7, f32_to_extF80(b));
                oracle(op, a.v, b.v, cw);
                CHECK(run(0xd8, 0xc1 | (op << 3)) == X87_EMU_OK, 10);
                u8 image[108]; x87_emu_state_encode(&state, image, false, false);
                if ((state.sw & 0x7fff) != (x87_get_le(hw+4,2) & 0x7fff))
                    diagnostic((rc<<24)|(pc<<16)|(k<<8)|op,state.sw,x87_get_le(hw+4,2),a.v);
                CHECK((state.sw & 0x7fff) == (x87_get_le(hw+4,2) & 0x7fff), 11);
                CHECK(same(image+28, hw+28, 20), 12);
                CHECK(trap.eip == 0x1002, 13);
            }
}
EXT_TYPES;
static void extended(void)
{
    const struct ext_case cases[]=EXT_CASES;
    for(unsigned i=0;i<EXT_COUNT;++i) {
        reset();state.cw=cases[i].cw;x87_set_top(&state,6);
        sw_extFloat80_t a={.signif=cases[i].a.sig,.signExp=cases[i].a.exp};
        sw_extFloat80_t b={.signif=cases[i].b.sig,.signExp=cases[i].b.exp};
        x87_put(&state,6,a);x87_put(&state,7,b);
        CHECK(run(0xd8,0xc1|(cases[i].op<<3))==X87_EMU_OK,90);
        const u8 *ref=reference_extended+i*108;
        if(state.sw!=x87_get_le(ref+4,2))diagnostic(i,state.sw,x87_get_le(ref+4,2),0);
        CHECK(state.sw==x87_get_le(ref+4,2),91);
        if(!same(state.reg[6].bytes,ref+28,10))diagnostic(i,x87_get_le(state.reg[6].bytes+8,2),x87_get_le(ref+36,2),0);
        CHECK(same(state.reg[6].bytes,ref+28,10),92);
    }
}
static void stores(void)
{
    const struct ext_case cases[]=STORE_CASES;
    for(unsigned i=0;i<STORE_COUNT;++i) {
        reset();state.cw=cases[i].cw;x87_set_top(&state,7);
        sw_extFloat80_t a={.signif=cases[i].a.sig,.signExp=cases[i].a.exp};
        x87_put(&state,7,a);x87_put_le(ram+4096,0x12345678,4);
        CHECK(mem(0xd9,3,0x2000)==X87_EMU_OK,93);
        const u8 *ref=reference_store+i*112;
        if(state.sw!=x87_get_le(ref+8,2))diagnostic(i,state.sw,x87_get_le(ref+8,2),1);
        CHECK(state.sw==x87_get_le(ref+8,2),94);
        CHECK(state.tw==x87_get_le(ref+12,2),95);
        CHECK(same(ram+4096,ref,4),96);
    }
    for(unsigned rc=0;rc<4;++rc) {
        reset();state.cw=0x37f|(rc<<10);
        CHECK(run(0xd9,0xe8)==X87_EMU_OK && run(0xd9,0xeb)==X87_EMU_OK,97);
        u8 image[108];x87_emu_state_encode(&state,image,false,false);
        const u8 *ref=reference_env+rc*108;
        CHECK(same(image,ref,12) && same(image+28,ref+28,20),98);
    }
}
static void conversion(void)
{
    const u32 values[] = CONVERT_VALUES;
    for (unsigned rc=0; rc<4; ++rc) for (unsigned k=0; k<5; ++k) {
        reset();
        u16 cw = 0x37f | (rc << 10), sw;
        u32 v = values[k], expected;
        expected=x87_get_le(reference_convert+(rc*5+k)*6,4);
        sw=x87_get_le(reference_convert+(rc*5+k)*6+4,2);
        state.cw = cw;
        x87_put_le(ram+4096, v, 4);
        CHECK(mem(0xd9,0,0x2000) == X87_EMU_OK, 20);
        CHECK(mem(0xdb,3,0x2004) == X87_EMU_OK, 21);
        CHECK(x87_get_le(ram+4100,4) == expected, 22);
        CHECK((state.sw & 0x7fff) == (sw & 0x7fff), 23);
    }
    /* ext80 store retains low bits that binary64 would discard. */
    reset();
    sw_extFloat80_t a = {.signif=0x8000000000000001ULL,.signExp=0x3fff};
    struct x87_emu_ext80 wire; x87_pack(&wire,a);
    for (unsigned i=0;i<10;++i) ram[4096+i]=wire.bytes[i];
    CHECK(mem(0xdb,5,0x2000)==X87_EMU_OK,24);
    CHECK(mem(0xdb,7,0x2010)==X87_EMU_OK,25);
    CHECK(same(ram+4112,wire.bytes,10),26);
}
static void unary(void)
{
    for (unsigned rc=0;rc<4;++rc) for(unsigned pc=0;pc<4;++pc) for(unsigned kind=0;kind<2;++kind) {
        reset();
        u16 cw=0x7f|(rc<<10)|(pc<<8);
        u32 v=0x40200000;
        state.cw=cw;
        x87_put_le(ram+4096,v,4);
        CHECK(mem(0xd9,0,0x2000)==X87_EMU_OK,30);
        for(unsigned i=0;i<108;++i) hw[i]=reference_unary[((rc*4+pc)*2+kind)*108+i];
        CHECK(run(0xd9,kind?0xfc:0xfa)==X87_EMU_OK,31);
        CHECK((state.sw&0x7fff)==(x87_get_le(hw+4,2)&0x7fff),32);
        CHECK(same(state.reg[x87_top(&state)].bytes,hw+28,10),33);
    }
}
static void environment(void)
{
    reset();
    CHECK(run(0xd9,0xe8)==X87_EMU_OK,40);
    CHECK(run(0xd9,0xeb)==X87_EMU_OK,41);
    state.cw=0x027e;
    struct x87_emu_state saved=state, restored;
    u8 image[108];
    for(unsigned op16=0;op16<2;++op16) {
        x87_emu_state_encode(&saved,image,op16,false);
        x87_emu_state_init(&restored,true);
        restored.fop=saved.fop;
        x87_emu_state_decode(&restored,image,op16,false);
        CHECK(same((const u8 *)&saved,(const u8 *)&restored,sizeof(saved)),42);
    }
    CHECK(mem(0xd9,6,0x2000)==X87_EMU_OK,43);
    CHECK(x87_get_le(ram+4096,2)==0x027e && state.cw==0x027f,44);
    CHECK(mem(0xd9,4,0x2000)==X87_EMU_OK && state.cw==0x027e,45);
    CHECK(run(0xdb,0xe3)==X87_EMU_OK,46);
    CHECK(state.tw==0xffff && same((u8 *)state.reg,(u8 *)saved.reg,sizeof(state.reg)),47);
}
static void faults(void)
{
    reset();
    CHECK(run(0xd9,0xe8)==X87_EMU_OK,50);
    struct x87_emu_state saved=state;
    for(unsigned i=8188;i<8202;++i) ram[i]=0x55;
    reject_page=3;
    CHECK(mem(0xdb,7,0x2ffc)==X87_EMU_MEMORY_FAULT,51);
    CHECK(same((u8 *)&state,(u8 *)&saved,sizeof(state)) && trap.eip==0x1000,52);
    CHECK(x87_get_le(ram+8188,4)==0x55555555,53);
    reject_page=-1; reject_write=1;
    CHECK(mem(0xd9,3,0x2000)==X87_EMU_MEMORY_FAULT,54);
    CHECK(same((u8 *)&state,(u8 *)&saved,sizeof(state)),55);
    reject_write=0;
    trap.segment[X87_DS]=0;
    CHECK(mem(0xd9,0,0x2000)==X87_EMU_MEMORY_FAULT,56);
    trap.segment[X87_DS]=USER_DS;
    eligible_owner=0;
    CHECK(run(0xd9,0xe8)==X87_EMU_NOT_USER,57);
    eligible_owner=1;
    CHECK(run(0xd9,0xfe)==X87_EMU_UNSUPPORTED,58);
    CHECK(same((u8 *)&state,(u8 *)&saved,sizeof(state)),59);
    reenter=1;
    CHECK(run(0xd9,0xe8)==X87_EMU_OK && nested_result==X87_EMU_REENTERED,99);
}
static void exceptions(void)
{
    reset();
    state.cw &= ~X87_ZE;
    CHECK(run(0xd9,0xee)==X87_EMU_OK,60);
    CHECK(run(0xd9,0xe8)==X87_EMU_OK,61);
    struct x87_emu_ext80 original=state.reg[x87_top(&state)];
    CHECK(run(0xd8,0xf1)==X87_EMU_OK,62);
    CHECK((state.sw & (X87_ESB|X87_ZE))==(X87_ESB|X87_ZE),63);
    CHECK(same(original.bytes,state.reg[x87_top(&state)].bytes,10),64);
    struct x87_emu_state saved=state;
    CHECK(run(0x9b,0)==X87_EMU_PENDING_MF && trap.eip==0x1000,65);
    CHECK(same((u8 *)&state,(u8 *)&saved,sizeof(state)),66);
    CHECK(mem(0xd9,7,0x2000)==X87_EMU_OK,67);
    CHECK(run(0xdb,0xe2)==X87_EMU_OK && !(state.sw&0x80ff),68);
    reset();
    state.cw &= ~X87_IE;
    CHECK(mem(0xdb,3,0x2000)==X87_EMU_OK,69);
    CHECK(state.tw==0xffff && x87_top(&state)==0 && (state.sw&X87_SF),70);
    CHECK(x87_get_le(ram+4096,4)==0,71);
    reset();
    softfloat_roundingMode=softfloat_round_max;
    softfloat_exceptionFlags=softfloat_flag_underflow;
    extF80_roundingPrecision=32;
    CHECK(run(0xd9,0xeb)==X87_EMU_OK,72);
    CHECK(softfloat_roundingMode==softfloat_round_max &&
        softfloat_exceptionFlags==softfloat_flag_underflow && extF80_roundingPrecision==32,73);
}
static void addressing(void)
{
    reset();
    /* FLD m32 [ESP+ECX*4+8], use the user ESP from trap. */
    ram[0]=0xd9;ram[1]=0x44;ram[2]=0x8c;ram[3]=8;
    trap.gpr[X87_ESP]=0x2000;trap.gpr[X87_ECX]=2;
    x87_put_le(ram+4112,0x3f800000,4);
    trap.eip=0x1000;
    CHECK(x87_emu_dispatch(&trap,&owner,&state)==X87_EMU_OK,80);
    CHECK(trap.eip==0x1004 && state.fdp==0x2010 && state.fds==USER_DS,81);
    /* address16 BP+SI-4 defaults to SS. */
    ram[0]=0x67;ram[1]=0xd9;ram[2]=0x42;ram[3]=0xfc;
    trap.gpr[X87_EBP]=0x1ff0;trap.gpr[X87_ESI]=0x24;
    trap.segment[X87_DS]=0;trap.eip=0x1000;
    CHECK(x87_emu_dispatch(&trap,&owner,&state)==X87_EMU_OK && trap.eip==0x1004,82);
    for(unsigned i=0;i<15;++i)ram[i]=0x66;
    trap.eip=0x1000;calls=0;
    CHECK(x87_emu_dispatch(&trap,&owner,&state)==X87_EMU_UNSUPPORTED && calls==15,83);
}
void _start(void)
{
    arithmetic(); if(!fail) extended(); if(!fail) stores(); if(!fail) conversion(); if(!fail) unary();
    if(!fail) environment(); if(!fail) faults(); if(!fail) exceptions(); if(!fail) addressing();
    __asm__ volatile("int $0x80"::"a"(1),"b"(fail):"memory");
    __builtin_unreachable();
}

#endif
