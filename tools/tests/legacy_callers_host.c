/* Link both genuine providers under distinct symbols, with real callers.
 * Bad pointers are deliberately unmapped qemu-user addresses. */
#include "types.h"
#include "os32_kapi_shared.h"
#include "os32_kapi_slots.h"
#include "appslot.h"
#include "paging.h"
#include "pgalloc.h"
#include "exec.h"
#include "v86_mem.h"
#include "v86.h"
#include "x86_flags.h"
#include "v86_call.h"
static void die(int rc) __attribute__((noreturn));
static void die(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
/* Convert a forbidden none-provider read into a defined runtime verdict.
 * The handler never returns, so no signal restorer is executed. */
static void forbidden_read(int signal) { (void)signal; die(1); }
static volatile uptr bad_argument = 1;
static int install_read_guard(void)
{
    struct {
        void (*handler)(int);
        u32 flags;
        void (*restorer)(void);
        u32 mask[2];
    } action = { forbidden_read, 0, 0, {0, 0} };
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(174), "b"(11),
                     "c"(&action), "d"(0), "S"(8) : "memory");
    return rc;
}
#define CHECK(x) do { if (!(x)) die(1); } while (0)
static u32 escape[5];
static int active, pinned, busy, end_count, cleaned, mark, trap_count, keys, exits;
static int seq, end_seq, context_seq, cleanup_seq;
struct v86_session_state v86_session;
int v86_module_load(void) { return 17; }
int v86_is_active(void) { return active; }
int v86_call_pinned(void) { return pinned; }
int paging_lowmap_busy(void) { return busy; }
void v86_session_end(void) { end_count++; end_seq=++seq; }
void v86_call_cleanup(void) { cleaned++; cleanup_seq=++seq; }
void v86_request_exit(void) { exits++; }
int v86_kbd_push(u8 code) { keys=code; return 3; }
void v86_int80(u32 *frame) { CHECK(frame[V86I_EFLAGS]&EFLAGS_VM); trap_count++; }
int v86_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src, unsigned count, u32 *out)
{ CHECK(slot==9 && fn==(void *)8 && src==(void *)7 && count==6); *out=5; return -1; }
#include "on.inc"
#define legacy_init none_init
#define legacy_active none_active
#define legacy_session_open none_session_open
#define legacy_pinned none_pinned
#define legacy_closing none_closing
#define legacy_lowmap_busy none_lowmap
#define legacy_abort_begin none_abort
#define legacy_end none_end
#define legacy_cleanup none_cleanup
#define legacy_request_exit none_exit
#define legacy_trap none_trap
#define legacy_kbd_push none_kbd
#define legacy_call_dispatch none_dispatch
#include "off.inc"
#undef legacy_init
#undef legacy_active
#undef legacy_session_open
#undef legacy_pinned
#undef legacy_closing
#undef legacy_lowmap_busy
#undef legacy_abort_begin
#undef legacy_end
#undef legacy_cleanup
#undef legacy_request_exit
#undef legacy_trap
#undef legacy_kbd_push
#undef legacy_call_dispatch
static int font_guard_active, cursor_x, cursor_y;
static int sink, cursors, shapes;
static int safe(void) { return 1; }
static int (*pc98_console_safe)(void)=safe;
static int con_backend_safe(void) { return !pc98_console_safe || pc98_console_safe(); }
static int con_sink_is_enabled(void) { return sink; }
static void gfx_policy_reeval(void) {}
static void con_sink_enable(void) { sink=1; }
static void con_backend_cursor_at(int x,int y) { CHECK(!x&&!y); cursors++; }
static void con_backend_cursor_shape(int show) { shapes+=show?1:10; }
static void font_guard_fault(int a,int b,int c) { (void)a;(void)b;(void)c;die(1); }
static void exec_stop_mark(void) { mark++; }
static void _stop(void) { __builtin_longjmp(escape,1); }
static void ring3_context_clear(void) { context_seq=++seq; }
#define EXEC_LJ_EXIT 1
static int finish_count, g_longjmp_reason, g_longjmp_id;
static k_jmpbuf g_exit_jmpbuf;
static void exec_finish(int id,int status,int kind) { CHECK(id==1&&status==2&&kind==3); finish_count++; }
void exec_longjmp(k_jmpbuf buf) { CHECK(buf==g_exit_jmpbuf); __builtin_longjmp(escape,1); }
int appslot_cur(void) { return 1; }
static u32 audit_scans;
volatile u32 kctx_irq_depth,kctx_exc_depth;
static int shlib_read_page(u32 a,u32 b) { (void)a;(void)b;return 0; }
static AppSlot slot;
AppSlot *appslot_at(int id) { return id==1?&slot:0; }
static unsigned irq_save(void) { return 0; }
static void irq_restore(unsigned f) { CHECK(!f); }
u32 paging_current_cr3(void) { return 123; }
u32 paging_kernel_pd_phys(void) { return 456; }
void paging_load_cr3(u32 root) { CHECK(root==123||root==456); }
int paging_as_audit(const struct addrspace *as,int (*page)(u32,u32)) { (void)as;(void)page;audit_scans++;return 0; }
static int lease_check(const struct addrspace *as) { (void)as;return 0; }
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
static int alias_cache(const struct ledger_surface *s) { (void)s;return 1; }
int ledger_surface_validate(const struct ledger_surface *s) { (void)s;return 1; }
static u32 memmap_audit_skip,memmap_audit_runs,as_audit_runs,memmap_audit_fail,as_audit_fail,audit_runs,audit_fail;
static char audit_first_fail_tag[32];
u32 exec_tramp_page_addr(void) { return 0; }
int paging_master_audit(u32 p) { (void)p;audit_scans++;return 0; }
int ledger_selfcheck(const char *tag) { (void)tag;return 1; }
static void kstrncpy(char *d,const char *s,u32 n) { while(n-- && (*d++=*s++)) {} }
static int serial_gate_active(void) { return 1; }
static void serial_puts_polled(const char *s) { (void)s; }
#include "callers.inc"

/* Link the same caller slices to none as well as the native provider. */
#define legacy_init none_init
#define legacy_active none_active
#define legacy_session_open none_session_open
#define legacy_pinned none_pinned
#define legacy_closing none_closing
#define legacy_lowmap_busy none_lowmap
#define legacy_abort_begin none_abort
#define legacy_end none_end
#define legacy_cleanup none_cleanup
#define legacy_request_exit none_exit
#define legacy_trap none_trap
#define legacy_kbd_push none_kbd
#define legacy_call_dispatch none_dispatch
#define exec_launch none_exec_launch
#define exec_exit none_exec_exit
#define console_render_allowed none_console_render_allowed
#define console_hw_cursor_sync none_console_hw_cursor_sync
#define console_hw_cursor_enable none_console_hw_cursor_enable
#define console_text_gdc_stop none_console_text_gdc_stop
#define lease_audit_all none_lease_audit_all
#define kselftest_run_audit none_kselftest_run_audit
#include "callers.inc"
#undef legacy_init
#undef legacy_active
#undef legacy_session_open
#undef legacy_pinned
#undef legacy_closing
#undef legacy_lowmap_busy
#undef legacy_abort_begin
#undef legacy_end
#undef legacy_cleanup
#undef legacy_request_exit
#undef legacy_trap
#undef legacy_kbd_push
#undef legacy_call_dispatch
#undef exec_launch
#undef exec_exit
#undef console_render_allowed
#undef console_hw_cursor_sync
#undef console_hw_cursor_enable
#undef console_text_gdc_stop
#undef lease_audit_all
#undef kselftest_run_audit
void _start(void) {
    u32 result=99, frame[20]={0};
    CHECK(!install_read_guard());
    CHECK(legacy_init()==17);
    for (int bits=0;bits<32;bits++) {
        v86_session.open=bits&1; active=(bits>>1)&1; pinned=(bits>>2)&1;
        v86_session.closing=(bits>>3)&1; busy=(bits>>4)&1;
        CHECK(legacy_session_open()==v86_session.open && !none_session_open());
    }
    v86_session.open=v86_session.closing=0;

    for(int a=0;a<2;a++)for(int b=0;b<2;b++) {
        active=a;sink=b;CHECK(console_render_allowed()==(!a&&!b));
        cursors=shapes=0;console_hw_cursor_enable();CHECK(cursors==!a && shapes==!a);
        console_text_gdc_stop();CHECK(sink);CHECK(shapes==(a?0:11));
    }
    busy=0;pinned=0;CHECK(!exec_launch("",0));
    pinned=1;CHECK(exec_launch("",0)==EXEC_ERR_INVALID);
    pinned=0;busy=1;CHECK(exec_launch("",0)==EXEC_ERR_INVALID);
    CHECK(lease_audit_all()==-1);CHECK(!kselftest_run_audit("skip"));
    CHECK(memmap_audit_skip==1&&!audit_scans);busy=0;
    CHECK(!lease_audit_all());CHECK(!kselftest_run_audit("run"));CHECK(audit_scans==1&&audit_runs==1);
    v86_session.closing=1;
    if(!__builtin_setjmp(escape))exec_exit(2,3);
    CHECK(mark==1&&!end_count&&!cleaned&&!v86_session.aborting);
    v86_session.closing=0;
    if(!__builtin_setjmp(escape))exec_exit(2,3);
    CHECK(v86_session.aborting&&end_count==1&&cleaned==1&&finish_count==1);
    CHECK(end_seq<context_seq&&context_seq<cleanup_seq);
    CHECK(!legacy_trap(frame));frame[V86I_EFLAGS]=EFLAGS_VM;
    CHECK(legacy_trap(frame)&&trap_count==1);
    CHECK(legacy_call_dispatch(9,(void *)8,(void *)7,6,&result)==-1&&result==5);
    CHECK(legacy_kbd_push(0x91)==3&&keys==0x91);legacy_request_exit();CHECK(exits==1);
    CHECK(none_init()==OS32_ERR_NOSYS&&!none_active()&&!none_pinned()&&!none_closing()&&!none_lowmap());
    CHECK(!none_trap((void *)1)&&!none_kbd(255));none_end();none_cleanup();none_abort();none_exit();
    struct kapi_arg_source *unread_args=(struct kapi_arg_source *)bad_argument;
    const u32 slots[]={KAPI_SLOT_V86_SELFTEST,KAPI_SLOT_V86_DISKTEST,KAPI_SLOT_V86_BOOT,KAPI_SLOT_V86_BOOT2,KAPI_SLOT_V86_GDC_CAPTURE};
    for(u32 i=0;i<5;i++) { result=99;CHECK(none_dispatch(slots[i],(void *)1,unread_args,999,&result)==1&&(int)result==OS32_ERR_NOSYS); }
    result=99;CHECK(!none_dispatch(0,(void *)1,unread_args,999,&result)&&result==99);
    CHECK(v86_smoke_test()==OS32_ERR_NOSYS&&v86_disk_test((void *)1)==OS32_ERR_NOSYS);
    CHECK(v86_boot((void *)1)==OS32_ERR_NOSYS&&v86_boot2((void *)1,(void *)1)==OS32_ERR_NOSYS);
    /* Independent none caller link: native state cannot block admission,
     * suppress rendering, skip audits or enter the native teardown. */
    busy=pinned=active=v86_session.closing=1; sink=0;
    CHECK(!none_exec_launch("",0)&&none_console_render_allowed());
    cursors=shapes=0;none_console_hw_cursor_enable();CHECK(cursors==1&&shapes==1);
    none_console_text_gdc_stop();CHECK(sink&&shapes==11);
    CHECK(!none_lease_audit_all()&&!none_kselftest_run_audit("none"));
    end_count=cleaned=finish_count=seq=0;v86_session.aborting=0;
    if(!__builtin_setjmp(escape))none_exec_exit(2,3);
    CHECK(!end_count&&!cleaned&&!v86_session.aborting&&finish_count==1);
    u8 out[sizeof(V86Gcap)];for(u32 i=0;i<sizeof(out);i++)out[i]=0xa5;
    CHECK(v86_gdc_capture(1,(V86Gcap *)out)==OS32_ERR_NOSYS);
    for(u32 i=0;i<sizeof(out);i++)CHECK(out[i]==0xa5);
    static const char msg[]="GREEN legacy callers on/off\n";
    __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(msg),"d"(sizeof(msg)-1));
    die(0);
}
