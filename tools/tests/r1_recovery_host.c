/* Real paging/ledger/V86 and exec lifecycle slices; CPU and device edges only. */
#include "types.h"
#include "appslot.h"
#include "exec.h"
#include "r1_fixture.h"
#include "v86.h"
#include "tvram.h"
static u32 host_cr3;
static int forbid_app_pd;
static void check_cr3(u32 root);
#define HOST_MMU_LOAD_CHECK(root) check_cr3(root)
#include "paging_source.c"
#include "pgalloc_source.c"
#include "pgalloc_host_fixture.h"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int rc) __attribute__((noreturn));
static void die(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static void say(const char *s) {
    u32 n = 0; while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { say("FAIL " #x "\n"); die(1); } } while (0)
static void check_cr3(u32 root) {
    CHECK(!forbid_app_pd || root == paging_kernel_pd_phys());
}
void *kmemset(void *p, int c, u32 n) { u8 *b=p; while (n--) *b++=c; return p; }
void *kmemcpy(void *p, const void *q, u32 n) { u8 *a=p; const u8 *b=q; while(n--) *a++=*b++; return p; }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void kfree(void *p) { (void)p; }
static char serial[4096];
static u32 serial_len;
void serial_puts_polled(const char *s) {
    while (*s) { CHECK(serial_len+1 < sizeof(serial)); serial[serial_len++]=*s++; }
    serial[serial_len]=0;
}
static int contains(const char *s) {
    for (u32 i=0; i<serial_len; i++) {
        u32 j=0; while (s[j] && serial[i+j]==s[j]) j++;
        if (!s[j]) return 1;
    }
    return 0;
}
static AppSlot g_slot[APP_SLOT_COUNT], *g_cur_app;
static int g_cur=APP_ID_SHELL, g_cur_op_is_wait, res_owner=APP_ID_SHELL;
volatile u32 ring3_switch_count, ring3_transition_count;
volatile u32 ring3_abort_count, fault_kill_count, ring3_wm_fault_count;
volatile int ring3_in_syscall, ring3_wm_depth, exec_nest_level;
int exec_exit_status;
static int g_last_kind, g_last_code, g_pending_id, g_pending_kind;
static int g_longjmp_reason, g_longjmp_id;
#define EXEC_LJ_EXIT 1
#define EXEC_LJ_PENDING 3
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN];
u32 exec_as_leftover_pages;
volatile u32 exec_stop_count;
static void *jump[5];
static int transferred, finished, last_kind;
static int expect_parent_poison;
int appslot_cur(void) { return g_cur; }
void res_owner_set(int id) { res_owner=id; }
int res_owner_get(void) { return res_owner; }
static void redir_switch_in(int id) {
    if (forbid_app_pd) {
        CHECK(id==2 && g_slot[id].state==APP_STATE_PARKED && g_slot[id].parked_from_wait);
        CHECK(g_slot[id].as->appmem_poisoned && g_slot[id].abort_req);
        CHECK(!r1_fixture_arm[R1_FIXTURE_PARKED_POISON]);
    }
}
#define appslot_return_target real_return_target
#include "slot_source.c"
#undef appslot_return_target
int appslot_return_target(int id) {
    if (expect_parent_poison && id==3) {
        CHECK(g_slot[2].as->appmem_poisoned && g_slot[2].abort_req);
        CHECK(!r1_fixture_arm[R1_FIXTURE_PARENT_POISON]);
    }
    return real_return_target(id);
}
static struct addrspace spaces[APP_SLOT_COUNT];
static void ring3_context_clear(void) { ring3_in_syscall=0; }
void exec_longjmp(u32 *buf) {
    if (buf != g_exit_jmpbuf) {
        CHECK(kctx_exc_depth == 1 && !kctx_irq_depth);
        CHECK(v86_session.release_pending && v86_session.backing_phys);
        CHECK(!v86_session.open && g_pending_id == g_cur);
        CHECK(g_slot[g_cur].state == APP_STATE_FAULT_PENDING);
        transferred++;
    }
    kctx_exc_depth=kctx_irq_depth=0; /* setjmp.asm restores the trusted depths. */
    __builtin_longjmp(jump,1);
}
static void ring3_band_set(u32 n) { (void)n; }
static void exec_heap_reset(void) {}
static void exec_restore_context(int id) {
    AppSlot *a=appslot_at(id);
    g_cur_app=a->cpl3 ? a : 0;
    paging_load_cr3(a->cpl3 ? a->as->pd_phys : paging_kernel_pd_phys());
}
u32 appslot_reclaim(int id) { g_slot[id]=(AppSlot){0}; return 0; }
static void exec_reclaim_resources(int id) {
    (void)id;
    CHECK(!kctx_exc_depth && !kctx_irq_depth && !v86_session.release_pending);
}
static void exec_notify_owned(int id,int kind) { (void)id; last_kind=kind; finished++; }
static void exec_reclaim_owned(int id,int kind) { (void)id; (void)kind; CHECK(0); }
int lease_revoke_all(struct addrspace *as) { (void)as; return 0; }
void shlib_addrspace_detach(struct addrspace *as) { (void)as; }
#include "owner_diag.h"
#include "exec_source.c"
static void caller_access_leave(int *p) { (void)p; }
static u32 *g_cur_frame;
static void exec_park_stop(u32 *f) { (void)f; }
static u32 tick_count;
static int kernel_tss;
void ring3_resume(const u32 *f,u32 pd,void *tss) { (void)f;(void)pd;(void)tss; CHECK(0); }
#include "tails_source.c"

V86Gcap *v86_gcap_rec;
static u32 releases;
void v86_bios_save_real(void) {}
void v86_bios_restore_real(void) {}
void v86_bios_setup(void) {}
void v86_io_apply_policy(void) {}
void v86_io_reset_policy(void) {}
void v86_runtime_end(void) { v86_session.active=0; }
void v86_bios_detach_disk(void) {
    CHECK(!kctx_exc_depth && !kctx_irq_depth && _irq_enabled()); releases++;
}
void v86_gcap_release(void) { CHECK(!kctx_exc_depth && !kctx_irq_depth); }
void gfx_v86_return(void) { CHECK(!v86_session.backing_phys); }
void kselftest_audit_v86_return(void) { CHECK(!v86_restore_mismatch); }
#include "v86_source.c"
static void sputs(const char *s) { (void)s; }
static void sput_hex32(u32 n) { (void)n; }
#include "entry_source.c"
static void host_raise_ud(void) {
    u32 regs[13]={0}; /* CS.RPL=0: actual syscall fault branch. */
    kctx_exc_depth++;
    exception_handler(0,R1_FIXTURE_UD_VECTOR,0,regs);
    CHECK(0);
}
#include "fixture_source.c"

static u32 start(int id,int parent) {
    u32 owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS,id,"r1",&owner));
    struct addrspace *as=&spaces[id];
    CHECK(!paging_addrspace_create(as,owner));
    u32 phys=pgalloc_alloc_phys(owner,1);
    CHECK(phys && !paging_addrspace_map_user(as,MEM_EXEC_LOAD_ADDR,phys,PAGE_RW|PTE_USER));
    g_slot[id]=(AppSlot){.state=APP_STATE_RUNNING,.parent=parent,.cpl3=1,.as=as,
        .load_addr=MEM_EXEC_LOAD_ADDR,.sbrk_heap_limit=MEM_EXEC_LOAD_ADDR+PAGE_SIZE};
    return owner;
}
static void arm(u32 hook,int id) {
    r1_fixture_id[hook]=id;
    r1_fixture_generation[hook]=spaces[id].generation;
    r1_fixture_arm[hook]=R1_FIXTURE_ARM;
}
static void select_app(int id) {
    appslot_switch_to(id); exec_restore_context(id); ring3_in_syscall=1;
}
static void check_exit(u32 pages,u32 leftover) {
    char wanted[64], *p=wanted;
    const char *labels[]={" pages="," leftover="};
    u32 values[]={pages,leftover};
    for (u32 i=0;i<2;i++) {
        const char *s=labels[i]; while (*s) *p++=*s++;
        char digits[11]; u32 n=0,v=values[i];
        do { digits[n++]=(char)('0'+v%10); v/=10; } while(v);
        while(n) *p++=digits[--n];
    }
    *p=0;
    CHECK(contains("OS32: owner-exit") && contains(wanted) && contains(" irq=0 exc=0"));
}
static void normal_exit(int id,u32 owner) {
    select_app(id); serial_len=0;
    u32 pages=ledger_owner_pages(owner), left=exec_as_leftover_pages;
    exec_finish(id,0,EXEC_KIND_EXITED);
    CHECK(!ledger_owner_pages(owner) && exec_as_leftover_pages==left);
    check_exit(pages,0);
}
static void v86_case(void) {
    u32 owner=start(2,APP_ID_SHELL); select_app(2);
    u32 left=exec_as_leftover_pages;
    for (u32 value=0;value<2;value++) {
        r1_fixture_arm[R1_FIXTURE_V86_FAULT]=value;
        CHECK(!v86_session_begin(owner)); v86_session_end();
    }
    CHECK(!transferred && !v86_restore_mismatch);
    r1_fixture_arm[R1_FIXTURE_V86_FAULT]=R1_FIXTURE_ARM;
    r1_fixture_v86_ready(); /* Closed session. */
    v86_session.open=1; ring3_in_syscall=0; r1_fixture_v86_ready();
    ring3_in_syscall=1; v86_session.closing=1; r1_fixture_v86_ready();
    CHECK(r1_fixture_arm[R1_FIXTURE_V86_FAULT]==R1_FIXTURE_ARM);
    v86_session.open=v86_session.closing=0;
    u32 release0=releases;
    r1_fixture_arm[R1_FIXTURE_V86_FAULT]=R1_FIXTURE_ARM;
    serial_len=0;
    u32 pages=ledger_owner_pages(owner);
    if (!__builtin_setjmp(jump)) { v86_session_begin(owner); CHECK(0); }
    CHECK(transferred==1 && releases==release0 && !r1_fixture_arm[R1_FIXTURE_V86_FAULT]);
    CHECK(ledger_owner_pages(owner)>pages && g_pending_kind==EXEC_KIND_FAULT);
    exec_pending_finish();
    CHECK(releases==release0+1 && !ledger_owner_pages(owner) && !g_pending_id);
    CHECK(exec_as_leftover_pages==left && last_kind==EXEC_KIND_FAULT && !v86_restore_mismatch);
    check_exit(pages,0);
    exec_pending_finish(); CHECK(releases==release0+1);
    owner=start(2,APP_ID_SHELL); select_app(2);
    CHECK(!v86_session_begin(owner)); v86_session_end(); normal_exit(2,owner);
}
static void poison_case(int parked) {
    u32 hook=parked ? R1_FIXTURE_PARKED_POISON : R1_FIXTURE_PARENT_POISON;
    u32 owner=start(2,APP_ID_SHELL), child=0;
    if (!parked) child=start(3,2);
    g_slot[2].state=parked ? APP_STATE_PARKED : APP_STATE_RUNNING;
    g_slot[2].parked_from_wait=parked;
    u32 pages=ledger_owner_pages(owner), left=exec_as_leftover_pages;
    /* Unarmed, invalid magic, wrong id, stale generation, wrong state/mark. */
    for (u32 bad=0;bad<6;bad++) {
        arm(hook,2);
        if (bad==0) r1_fixture_arm[hook]=0;
        if (bad==1) r1_fixture_arm[hook]=1;
        if (bad==2) r1_fixture_id[hook]=3;
        if (bad==3) r1_fixture_generation[hook]++;
        if (bad==4) g_slot[2].state=APP_STATE_WAIT_POLL;
        if (bad==5) {
            if (parked) g_slot[2].parked_from_wait=0;
            else g_slot[3].parent=APP_ID_SHELL;
        }
        if (parked) r1_fixture_resume(2); else r1_fixture_parent(3);
        CHECK(!spaces[2].appmem_poisoned && !g_slot[2].abort_req);
        CHECK(ledger_owner_pages(owner)==pages && exec_as_leftover_pages==left);
        if(bad>=2) CHECK(r1_fixture_arm[hook]==R1_FIXTURE_ARM);
        g_slot[2].state=parked ? APP_STATE_PARKED : APP_STATE_RUNNING;
        g_slot[2].parked_from_wait=parked;
        if(!parked) g_slot[3].parent=2;
    }
    arm(hook,2); serial_len=0;
    if (parked) {
        g_cur=APP_ID_SHELL; g_cur_app=0; res_owner=APP_ID_SHELL;
        paging_load_cr3(paging_kernel_pd_phys()); forbid_app_pd=1;
        if (!__builtin_setjmp(jump)) { host_resume_tail(2); CHECK(0); }
        forbid_app_pd=0;
        CHECK(!g_slot[2].parked_from_wait);
    } else {
        select_app(3); expect_parent_poison=1; exec_finish(3,0,EXEC_KIND_EXITED);
        expect_parent_poison=0;
        CHECK(!ledger_owner_pages(child) && g_cur==2 && g_slot[2].abort_req);
        CHECK(spaces[2].appmem_poisoned && g_slot[2].state==APP_STATE_RUNNING);
        serial_len=0;
        if (!__builtin_setjmp(jump)) { host_syscall_tail(g_slot[2].frame); CHECK(0); }
    }
    CHECK(!r1_fixture_arm[hook] && last_kind==EXEC_KIND_ABORTED);
    CHECK(ledger_owner_pages(owner)==pages && exec_as_leftover_pages==left+pages);
    CHECK(!g_slot[2].as && !g_slot[2].cpl3 && contains("owner pages retained"));
    check_exit(pages,pages);
    /* Slot reuse does not re-arm and never reclaims the quarantined owner. */
    u32 next=start(2,APP_ID_SHELL); CHECK(next!=owner); normal_exit(2,next);
    CHECK(ledger_owner_pages(owner)==pages && exec_as_leftover_pages==left+pages);
}
void _start(void) {
    u32 args[6]={KERNEL_LOAD_ADDR,16*1024*1024,3,0x32,~0U,0}, result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(90),"b"(args):"memory");
    CHECK(result==KERNEL_LOAD_ADDR);
    host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    g_slot[APP_ID_SHELL].state=APP_STATE_RUNNING;
    v86_case(); poison_case(0); poison_case(1);
    CHECK(!ledger_irq_ops && !ledger_exc_ops && !exec_stop_count);
    say("r1 recovery: K1, parent syscall exit, PARKED pre-commit, quarantine/owner-exit PASS\n");
    die(0);
}
