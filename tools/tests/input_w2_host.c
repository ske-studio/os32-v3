#include "input_policy.h"
#include "os32_kapi_shared.h"

/* W[2] exercises set and the compiled lookups. Registry/evaluator providers
 * are unused here; real capture/provider integration is tested by W[3..4]. */
void km_policy_lookup_install(void (*lookup)(const KmBackend *, KmSource *)) { (void)lookup; }
void km_policy_read_install(void (*read)(const KmBackend *, u16, KmPolicyKey *)) { (void)read; }
static int pressed(void) { return 0; }
static const KmBackend pc98 = { .id=1, .count=128 }, hid = { .id=2, .count=256 };
static u8 wire[16 + 16 * 129];
static u32 rows;
static void put16(u8 *p, u32 value) { p[0]=value; p[1]=value>>8; }
static void put32(u8 *p, u32 value) { put16(p,value); put16(p+2,value>>16); }
static void begin(void)
{
    rows=0;
    wire[0]='I'; wire[1]='K'; wire[2]='M'; wire[3]='1';
    put16(wire+4,2); put16(wire+6,16); put32(wire+8,0); put32(wire+12,0);
}
static void add(IkmRow r)
{
    u8 *p=wire+16+16*rows++;
    p[0]=r.kind; p[1]=r.profile; p[2]=r.context; p[3]=r.flags;
    put16(p+4,r.index); put16(p+6,r.selector); put32(p+8,r.value); put32(p+12,r.aux);
    put32(wire+8,rows);
}
static int set(void) { return ikm_policy_set(wire,16+16*rows,0,0); }
static int single(IkmRow r) { begin(); add(r); return set(); }
#define REQUIRE(x) do { if (!(x)) return __LINE__ % 120 + 1; } while (0)
static int test(void)
{
    u8 byte=99, action=99;
    IkmRow r = {1,0,1,0,0,0,128,0};
    REQUIRE(!ikm_policy_init(1,&pc98,&hid,pressed));
    REQUIRE(!single(r)); add(r); REQUIRE(set()==OS32_ERR_INVAL);
    r.flags=1; REQUIRE(single(r)==OS32_ERR_INVAL);
    r.flags=0; r.context=2; REQUIRE(single(r)==OS32_ERR_INVAL); /* wrong META size */
    r=(IkmRow){2,0,2,0,255,4,0xf500,0}; REQUIRE(!single(r));
    r.index=256; REQUIRE(single(r)==OS32_ERR_INVAL);
    r.index=1; r.value=0x300; REQUIRE(single(r)==OS32_ERR_INVAL);
    r=(IkmRow){3,0,1,0,0,15,0xf500,0x8001}; REQUIRE(!single(r));
    r.index=1; r.aux=0x8901; REQUIRE(single(r)==OS32_ERR_INVAL);
    r.kind=4; REQUIRE(single(r)==OS32_ERR_INVAL);
    r=(IkmRow){5,1,0,0,1,0,0,0}; REQUIRE(!single(r));
    REQUIRE(!ikm_policy_term(1,2,1,&byte) && byte==0); /* NUL is not absent */
    r=(IkmRow){6,1,0,0,0,0xffff,255,0}; REQUIRE(!single(r));
    r.selector=0x10; r.value=18; REQUIRE(!single(r));
    r.selector=8; REQUIRE(single(r)==OS32_ERR_INVAL);
    r=(IkmRow){7,0,1,0,127,0,3,0}; REQUIRE(!single(r));
    r=(IkmRow){8,4,4,0,255,2,18,0x17}; REQUIRE(single(r)==OS32_ERR_INVAL); /* other arch */
    r.profile=2; REQUIRE(!single(r));
    r.aux=0; add(r); REQUIRE(!set()); /* distinct masks coexist */
    add(r); REQUIRE(set()==OS32_ERR_INVAL);
    /* TERM byte conflicts are rejected by set, not an isolated checker.
     * A failed set leaves the previously compiled table untouched. */
    begin(); add((IkmRow){5,1,0,0,1,0,6,0}); REQUIRE(!set());
    add((IkmRow){5,1,0,0,2,0,6,0}); REQUIRE(set()==OS32_ERR_INVAL);
    REQUIRE(!ikm_policy_term(1,2,1,&byte) && byte==6);
    /* Same ID at concrete and common contexts; concrete wins, and another
     * concrete context falls back to the explicit common row, not default. */
    begin(); add((IkmRow){5,1,1,0,1,0,5,0});
    add((IkmRow){5,1,0,0,1,0,6,0}); add((IkmRow){5,2,1,0,1,0,7,0});
    REQUIRE(!set());
    REQUIRE(!ikm_policy_term(1,1,1,&byte) && byte==5);
    REQUIRE(!ikm_policy_term(1,2,1,&byte) && byte==6);
    REQUIRE(!ikm_policy_term(2,1,1,&byte) && byte==7);
    /* Any kind6 row replaces all kind6 defaults for only that profile,
     * including unmentioned EXT actions and COMPAT bytes. */
    begin(); add((IkmRow){6,1,0,0,0,0,18,0}); REQUIRE(!set());
    REQUIRE(!ikm_policy_action(1,2,1,0x60,0x60,0,&action) && action==0);
    REQUIRE(ikm_policy_compat(1,0x37,&byte)==OS32_ERR_NOTFOUND);
    REQUIRE(!ikm_policy_compat(2,0x37,&byte) && byte==3);
    begin(); add((IkmRow){6,2,0,0,0,0xffff,42,0}); REQUIRE(!set());
    REQUIRE(!ikm_policy_action(1,2,1,0x60,0x60,0,&action) && action==3);
    REQUIRE(!ikm_policy_compat(2,0,&byte) && byte==42);
    REQUIRE(ikm_policy_compat(2,0x37,&byte)==OS32_ERR_NOTFOUND);
    begin(); r=(IkmRow){6,1,1,0,0,0,0,0};
    for (r.index=0;r.index<128;r.index++) add(r);
    REQUIRE(!set()); r.context=2; r.index=0; add(r); REQUIRE(set()==OS32_ERR_INVAL);
    r=(IkmRow){2,0,2,0,0,0,0xd800,0}; REQUIRE(single(r)==OS32_ERR_INVAL);
    r.value=0xffff; REQUIRE(single(r)==OS32_ERR_INVAL);
    r.value=0x10000; REQUIRE(single(r)==OS32_ERR_INVAL);
    r.value=0; REQUIRE(!single(r));
    r=(IkmRow){1,0,2,0,1,0,1,0}; REQUIRE(single(r)==OS32_ERR_INVAL);
    r=(IkmRow){8,1,0,0,0,2,19,0}; REQUIRE(single(r)==OS32_ERR_INVAL);
    r.value=0; r.aux=8; REQUIRE(single(r)==OS32_ERR_INVAL);
    return 0;
}
void _start(void)
{
    int r=test();
#ifdef PPC_TEST
    register int call __asm__("r0")=1;
    register int status __asm__("r3")=r;
    __asm__ volatile("sc" : : "r"(call), "r"(status) : "memory");
#else
    __asm__ volatile("int $0x80" : : "a"(1), "b"(r));
#endif
    __builtin_unreachable();
}
