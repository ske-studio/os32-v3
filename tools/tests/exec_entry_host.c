/* Product ILP32 frame preparation + actual CPL0 stack call. Only iret is observed. */
#include "types.h"
static u32 host_cr3;
#include "cpu.h"
static void observe_user(u32, u32, u32) __attribute__((noreturn));
#define ARCH_ENTRY_USER(pd, sp, ip) observe_user(pd, sp, ip)
#include "entry.c"
#include "context.c"

static void *escape[5];
static struct arch_entry_context contexts[2];
static u8 stacks[2][8192] __attribute__((aligned(16)));
static u32 stored_at, stored_bytes, stores, words[5];
static int store_failure;
static volatile int calls, nesting;
static volatile u32 expected_argc;
static void say(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
static void fail(void) __attribute__((noreturn));
static void fail(void) { say("FAIL: entry contract\n"); __asm__ volatile("int $0x80"::"a"(1),"b"(1)); for (;;) {} }
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); fail(); } } while(0)
struct tss_entry kernel_tss;
u32 tss_get_esp0(void) { return kernel_tss.esp0; }
void tss_set_esp0(u32 sp) { kernel_tss.esp0=sp; }
void ring3_resume(const u32 *p,u32 root,void *tss) { (void)p; (void)root; (void)tss; fail(); }
static int store(void *owner, u32 va, const void *src, u32 bytes)
{
    CHECK(owner == &stores);
    CHECK(va >= (u32)stacks && va <= (u32)stacks + sizeof(stacks) - bytes);
    stores++; stored_at=va; stored_bytes=bytes;
    for (u32 k=0;k<bytes/4;k++) words[k]=((const u32 *)src)[k];
    if (store_failure) return -1;
    for (u32 k=0;k<bytes;k++) ((u8 *)va)[k]=((const u8 *)src)[k];
    return 0;
}
static void observe_user(u32 root,u32 sp,u32 ip)
{
    CHECK(root==0x123000 && ip==0xabcdef && sp==stored_at);
    CHECK(stored_bytes==20 && ((sp+4)&15)==0);
    __builtin_longjmp(escape,1);
}
static void child(u32 argc,u32 argv,u32 api,u32 aux)
{
    CHECK(argc==expected_argc && argv==0x2468 && api==0x3579 && aux==0x468a);
    calls++;
    if (nesting==1) {
        nesting=2;
        arch_entry_enter(&contexts[1],0);
        CHECK(calls==2);
    } else if (nesting==3) __builtin_longjmp(escape,1);
}
void _start(void)
{
    struct exec_entry_args args={3,0x2468,0x3579,0x468a};
    for (int user=0;user<2;user++) for (u32 argc=0;argc<=3;argc+=3) {
        args.argc=argc; expected_argc=argc;
        u32 top=(u32)(stacks[0]+sizeof(stacks[0]))-7;
        CHECK(!arch_entry_prepare(&contexts[0],top,user ? 0xabcdef : (u32)child,user,&args,store,&stores));
        CHECK(stored_bytes==(user?20:16));
        CHECK(stored_at==((top-16)&~15u)-(user?4:0));
        CHECK(words[user?1:0]==argc && words[user?2:1]==args.argv);
        CHECK(words[user?3:2]==args.api && words[user?4:3]==args.aux);
        if (user) {
            CHECK(words[0]==0);
            if (!__builtin_setjmp(escape)) arch_entry_enter(&contexts[0],0x123000);
        } else arch_entry_enter(&contexts[0],0);
    }
    for (u32 top=0;top<20;top++) {
        u32 before=stores;
        CHECK(arch_entry_prepare(&contexts[0],top,0,1,&args,store,&stores));
        CHECK(stores==before);
        if (top<16) CHECK(arch_entry_prepare(&contexts[0],top,0,0,&args,store,&stores));
    }
    store_failure=1;
    for (int user=0;user<2;user++) {
        struct arch_entry_context before=contexts[0];
        CHECK(arch_entry_prepare(&contexts[0],(u32)(stacks[0]+8192),0,user,&args,store,&stores));
        for (u32 k=0;k<sizeof(before);k++) CHECK(before.opaque[k]==contexts[0].opaque[k]);
    }
    store_failure=0; args.argc=expected_argc=3; calls=0;
    for (int k=0;k<2;k++) CHECK(!arch_entry_prepare(&contexts[k],(u32)(stacks[k]+8192),(u32)child,0,&args,store,&stores));
    nesting=1; arch_entry_enter(&contexts[0],0); CHECK(calls==2);
    nesting=3;
    if (!__builtin_setjmp(escape)) { arch_entry_enter(&contexts[0],0); fail(); }
    CHECK(calls==3);
    say("PASS entry: USER/CPL0 argc 0/multiple api aux alignment store failure underflow nested return/longjmp\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(0)); for (;;) {}
}
