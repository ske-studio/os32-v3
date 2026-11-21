/* Real backend + real common initializer; only cache publication is observed. */
#include "syscall_code.h"
#include "ring3_str.h"
#include "ring3_ls.h"
#include "paging.h"
#include "memmap.h"
#include "os32_kapi_slots.h"
#include "kstring.h"
static void die(int n) __attribute__((noreturn));
static void die(int n) {
    const char msg[] = "FAIL: syscall code\n";
    __asm__ volatile("int $0x80" :: "a"(4), "b"(2), "c"(msg), "d"(sizeof(msg)-1) : "memory");
    __asm__ volatile("int $0x80" :: "a"(1), "b"(n));
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) die(__LINE__ % 254 + 1); } while (0)
void *kmemset(void *p, int v, u32 n) { u8 *s=p; while(n--) *s++=v; return p; }
void *kmemcpy(void *d, const void *s, u32 n) {
    u8 *a=d; const u8 *b=s; while(n--) *a++=*b++; return d;
}
#define arch_syscall_publish backend_publish
#define arch_syscall_emit backend_emit
#define arch_syscall_layout backend_layout
#include "code.c"
#undef arch_syscall_layout
#undef arch_syscall_emit
#undef arch_syscall_publish
static u8 page[RING3_TRAMP_BYTES] __attribute__((aligned(PAGE_SIZE)));
static u8 expected[RING3_TRAMP_BYTES];
static KernelAPI api;
static KernelAPI *kapi=&api;
static u32 ring3_tramp_page;
static unsigned published, emitted, reject_emit, reject_layout;
int arch_syscall_layout(unsigned cap, struct syscall_layout *layout) {
    return !reject_layout && backend_layout(cap, layout);
}
u32 exec_tramp_page_addr(void) { return (u32)page; }
#define P2V(a) ((void *)(a))
/* Independent byte oracle for every slot, including reservations. */
static void check_stubs(const u8 *p, unsigned count) {
    for (u32 i=0; i<count; i++) {
        const u8 *s=p+RING3_USTR_STUB_OFF+i*8;
        CHECK(s[0]==0xb8 && s[5]==0xcd && s[6]==0x80 && s[7]==0xc3);
        CHECK(s[1]==(u8)i && s[2]==(u8)(i>>8) && s[3]==(u8)(i>>16) && s[4]==(u8)(i>>24));
    }
}
int arch_syscall_emit(void *d, u32 base, u32 bytes, unsigned cap) {
    emitted++;
    if (reject_emit) return 0;
    return backend_emit(d, base, bytes, cap);
}
void arch_syscall_publish(void *d, u32 bytes) {
    CHECK(emitted==1 && published==0 && ring3_tramp_page==0);
    CHECK(d==page && bytes==RING3_TRAMP_BYTES);
    check_stubs(d, KAPI_FUNC_CAPACITY);
    CHECK(((KernelAPI *)d)->magic == api.magic);
    for (u32 i=0; i<(u32)(ring3_ls_end-ring3_ls_start); i++)
        CHECK(page[RING3_LS_SHIM_OFF+i]==ring3_ls_start[i]);
    backend_publish(d, bytes);
    published++;
}
#define arch_exec_publish(dst, bytes) ((void)0)
#include "trampoline.inc"
static unsigned user_started, map_calls, fail_page, sys_mem_kb;
static u8 user_gate[PAGE_SIZE];
#define KBOOT_MEM_KB 32768
#define exec_kapi_init() ((void)0)
#define kprintf(...) ((void)0)
#define _stop() return
#define kboot_stop(why) return
int paging_boot_user_shared(u32 addr, u32 pages)
{
    map_calls++;
    if (addr == (u32)user_gate) CHECK(pages == 1);
    else CHECK(addr == (u32)page && pages == 2 && published == 1);
    return fail_page && fail_page <= pages ? -1 : 0;
}
#include "boot.inc"

static void check_boot(void (*boot)(void), int gate)
{
    for (unsigned failure = 0; failure <= 3; failure++) {
        reject_layout = failure == 1;
        reject_emit = failure == 2;
        fail_page = failure == 3 ? (gate ? 1 : 2) : 0;
        user_started = map_calls = emitted = published = ring3_tramp_page = 0;
        boot();
        CHECK(map_calls == (gate || failure == 0 || failure == 3));
        CHECK(user_started == (failure == 0 || (gate && failure < 3)));
    }
}
void _start(void) {
    struct syscall_layout l;
    CHECK(!arch_syscall_layout(0, 0));
    CHECK(!arch_syscall_layout(KAPI_FUNC_CAPACITY+1, &l));
    CHECK(!arch_syscall_layout(~0u, &l));
    for (unsigned cap=0; cap<=KAPI_FUNC_CAPACITY; cap++) {
        CHECK(arch_syscall_layout(cap, &l));
        CHECK(l.stub_off==RING3_USTR_STUB_OFF && l.stub_stride==KAPI_STUB_SIZE && l.stub_count==cap);
        CHECK(l.scratch_off==RING3_USTR_OFF && l.scratch_bytes==RING3_USTR_CAP);
        CHECK(l.ls_off==RING3_LS_SHIM_OFF && l.ls_bytes==192);
        CHECK(l.stub_off>=sizeof(KernelAPI) && l.stub_off+cap*l.stub_stride<=l.ls_off);
        CHECK(l.scratch_off+l.scratch_bytes<=PAGE_SIZE && l.ls_off+l.ls_bytes<=l.total_bytes && l.total_bytes==RING3_TRAMP_BYTES);
        for (u32 i=0; i<RING3_TRAMP_BYTES; i++) page[i]=0xa5;
        CHECK(!backend_emit(page, 0x1000, l.total_bytes-1, cap));
        CHECK(!backend_emit(page, ~0u-l.total_bytes+1, RING3_TRAMP_BYTES, cap));
        CHECK(!backend_emit(0, 0x1000, RING3_TRAMP_BYTES, cap));
        CHECK(!backend_emit(page, 0x1000, RING3_TRAMP_BYTES, ~0u));
        for (u32 i=0; i<RING3_TRAMP_BYTES; i++) CHECK(page[i]==0xa5);
        CHECK(backend_emit(page, 0x1000, RING3_TRAMP_BYTES, cap));
        check_stubs(page, cap);
        for (u32 i=0; i<RING3_TRAMP_BYTES; i++) {
            int stub=i>=l.stub_off && i<l.stub_off+cap*l.stub_stride;
            int shim=i>=l.ls_off && i<l.ls_off+(u32)(ring3_ls_end-ring3_ls_start);
            if (shim) CHECK(page[i]==ring3_ls_start[i-l.ls_off]);
            if (!stub && !shim) CHECK(page[i]==0xa5);
        }
    }
    for (u32 i=0; i<RING3_TRAMP_BYTES; i++) page[i]=0;
    reject_layout=1;
    CHECK(ring3_trampoline_init()<0 && !emitted && !published && !ring3_tramp_page);
    reject_layout=0;
    reject_emit=1;
    CHECK(ring3_trampoline_init()<0);
    CHECK(!published && !ring3_tramp_page);
    for (u32 i=0; i<RING3_TRAMP_BYTES; i++) CHECK(!page[i]);
    reject_emit=0; emitted=0;
    api.magic=0x12345678; api.version=123; api.shm_base=MEM_SHM_BASE;
    ring3_trampoline_init();
    CHECK(published==1 && emitted==1 && ring3_tramp_page==(u32)page);
    u32 *tbl=(u32 *)expected;
    tbl[0]=api.magic; tbl[1]=api.version;
    for (u32 i=0; i<KAPI_FUNC_CAPACITY; i++) {
        tbl[KAPI_FN_TABLE_FIRST+i]=(u32)page+RING3_USTR_STUB_OFF+i*8;
        u8 *s=expected+RING3_USTR_STUB_OFF+i*8;
        s[0]=0xb8; s[1]=i; s[2]=i>>8; s[3]=i>>16; s[4]=i>>24;
        s[5]=0xcd; s[6]=0x80; s[7]=0xc3;
    }
    tbl[KAPI_FN_TABLE_FIRST+KAPI_SLOT_SYS_LS]=(u32)page+RING3_LS_SHIM_OFF;
    tbl[KAPI_DATA_IDX_SHM_BASE]=(u32)MEM_SHM_BASE;
    for (u32 i=0; i<(u32)(ring3_ls_end-ring3_ls_start); i++)
        expected[RING3_LS_SHIM_OFF+i]=ring3_ls_start[i];
    for (u32 i=0; i<RING3_TRAMP_BYTES; i++) CHECK(page[i]==expected[i]);
    check_boot(pc98_boot, 0);
    check_boot(ppc_exec_boot, 0);
    check_boot(ppc_gate_boot, 1);
    __asm__ volatile("int $0x80" :: "a"(1), "b"(0));
    __builtin_unreachable();
}
