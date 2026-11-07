/* Full exec.c lifecycle with real setjmp.asm; only CPU, VFS and resource
 * boundaries are fake. The shell is resident for the nested CUI case. */
#include "types.h"
static u32 host_cr3;
#include "cpu.h"
static void user_main(void) __attribute__((noreturn));
static void resident_main(void);
#undef arch_enter_user
#define arch_enter_user(pd, sp, entry) do { (void)(pd); (void)(sp); (void)(entry); user_main(); } while (0)
#undef arch_call_on_stack
#define arch_call_on_stack(saved, sp, entry) do { (saved) = 0; (void)(sp); (void)(entry); resident_main(); } while (0)
#include "exec.c"

static void say(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
static void die(void) __attribute__((noreturn));
static void die(void) { __asm__ volatile("int $0x80"::"a"(1),"b"(1)); for (;;) {} }
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(); } } while(0)
enum { SHELL_DEPTH=1, A_DEPTH=2, B_DEPTH=3, TRACE_MAX=8 };
static AppSlot slots[APP_SLOT_COUNT];
static struct addrspace spaces[APP_SLOT_COUNT];
static KernelAPI api;
static u32 tramp[KAPI_DATA_IDX_SHM_BASE+1];
static int cur, trace_count, return_count, cleanup_count;
static const X87State *trace_state[TRACE_MAX];
static char trace_op[TRACE_MAX];
static u8 stack_page[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u8 allocation[MEM_EXEC_STACK_SIZE] __attribute__((aligned(PAGE_SIZE)));
static void record(char op, const X87State *p) { CHECK(trace_count<TRACE_MAX); trace_op[trace_count]=op; trace_state[trace_count++]=p; }
void x87_save(X87State *p) { record('S',p); }
void x87_init(void) { record('I',0); }
void x87_restore(const X87State *p) { record('R',p); }
static void expect(int n, char op, const X87State *p) { CHECK(trace_count==n+1 && trace_op[n]==op && trace_state[n]==p); }
void *kmemset(void *p,int c,u32 n) { u8 *b=p; while(n--) *b++=c; return p; }
void *kmemcpy(void *p,const void *q,u32 n) { u8 *b=p; const u8 *a=q; while(n--) *b++=*a++; return p; }
char *kstrncpy(char *p,const char *s,u32 n) { u32 i=0; while(i+1<n && s[i]) { p[i]=s[i]; i++; } if(n) p[i]=0; return p; }
u32 kstrlen(const char *s) { u32 n=0; while(s[n]) n++; return n; }
char *kstrncat(char *p,const char *s,u32 n) { u32 a=kstrlen(p); kstrncpy(p+a,s,n-a); return p; }
int kstrcmp(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return (u8)*a-(u8)*b; }
int vfs_resolve_path(const char *p,char *out,int n) { kstrncpy(out,p,n); return VFS_OK; }
int vfs_read(const char *p,void *buf,u32 n) { (void)p; CHECK(n==OS32X_HDR_SIZE); OS32Header *h=buf; kmemset(h,0,n); h->header_size=OS32X_HDR_SIZE; h->text_size=PAGE_SIZE; h->code_size=PAGE_SIZE; h->load_addr=exec_nest_level ? MEM_EXEC_LOAD_ADDR : MEM_SHELL_LOAD_ADDR; return n; }
int vfs_open(const char *p,int m) { (void)p; CHECK(!m); return 7; }
int vfs_seek(int fd,int off,int w) { CHECK(fd==7 && off==OS32X_HDR_SIZE && !w); return off; }
int vfs_read_fd(int fd,void *p,u32 n) { CHECK(fd==7 && n==PAGE_SIZE); kmemset(p,0,n); return n; }
void vfs_close(int fd) { CHECK(fd==7); }
#define LEGACY_FAKE_PINNED_ENABLE
#define LEGACY_FAKE_PINNED 0
#define LEGACY_FAKE_LOWMAP_ENABLE
#define LEGACY_FAKE_LOWMAP 0
#define LEGACY_FAKE_END_ENABLE
#define LEGACY_FAKE_END (void)0
#define LEGACY_FAKE_CLEANUP_ENABLE
#define LEGACY_FAKE_CLEANUP (void)0
#define LEGACY_FAKE_CLOSING_ENABLE
#define LEGACY_FAKE_ABORT_ENABLE
#include "legacy_guest_fake.h"


u32 paging_current_cr3(void) { return 1; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 pd) { host_cr3=pd; }
int appslot_cur(void) { return cur; }
AppSlot *appslot_at(int id) { CHECK(id>=0 && id<APP_SLOT_COUNT); return &slots[id]; }
AppSlot *appslot_get(int id) { return appslot_at(id); }
int appslot_start_admit(int gui,u32 n,u32 free) { (void)n; (void)free; CHECK(!gui && (cur==1 || cur==2)); return cur+1; }
int appslot_cui_only_admit(int gui,u32 flags) { (void)gui; (void)flags; return 0; }
void appslot_start_commit(int id,int gui,u32 n) { (void)n; CHECK(!gui && id==cur+1); slots[id].parent=cur; slots[id].depth=slots[cur].depth+1; slots[id].state=APP_STATE_RUNNING; cur=id; }
void appslot_shell_commit(void) { cur=APP_ID_SHELL; slots[cur].depth=SHELL_DEPTH; slots[cur].state=APP_STATE_RUNNING; }
void appslot_mark_scheduled(int id,u32 t) { (void)t; CHECK(id==cur); }
int appslot_return_target(int id) { CHECK(id==cur); return slots[id].parent; }
void appslot_switch_to(int id) { cur=id; }
u32 appslot_reclaim(int id) { CHECK(id==cur); slots[id].state=APP_STATE_FREE; return 0; }
int os32x_layout_check(const OS32Header *h,u32 n,u32 off) { (void)h; (void)n; (void)off; return OS32X_LAYOUT_OK; }
const char *os32x_layout_reason(int rc) { (void)rc; CHECK(0); return 0; }
u32 pgalloc_free_pages(void) { return 1000; }
void exec_heap_save_state(u32 *n) { *n=0; }
void exec_heap_user_init(struct addrspace *as) { CHECK(as==&spaces[cur+1]); }
void exec_heap_init_at(u32 b,u32 n) { (void)b; (void)n; }
void exec_heap_restore_state(u32 b,u32 n,u32 used) { (void)b; (void)n; (void)used; }
int ledger_owner_new(u32 kind,u32 id,const char *name,u32 *out) { (void)kind; (void)name; *out=id; return 1; }
int ledger_owner_retire(u32 owner) { (void)owner; return 1; }
void *kmalloc(u32 n) { CHECK(n==sizeof(struct addrspace)); return &spaces[cur+1]; }
void kfree(void *p) { (void)p; }
int paging_addrspace_create_lease(struct addrspace *as,u32 owner) { as->pd_phys=owner+1; as->owner=owner; return 0; }
void appmem_init(struct addrspace *as,u32 b,u32 e,u32 h,u32 g) { (void)as; (void)b; (void)e; (void)h; (void)g; }
int shlib_addrspace_attach(struct addrspace *as) { (void)as; return 0; }
u32 pgalloc_alloc_phys(u32 owner,int n) { (void)owner; CHECK(n>0 && (u32)n<=sizeof(allocation)/PAGE_SIZE); return (u32)allocation; }
int paging_addrspace_map_user_range_phys(struct addrspace *as,u32 b,u32 e,u32 p,u32 f) { (void)as; (void)b; (void)e; (void)p; (void)f; return 0; }
int paging_addrspace_protect_code_ro(struct addrspace *as,u32 b,u32 e) { (void)as; (void)b; (void)e; return 0; }
int paging_addrspace_map_user(struct addrspace *as,u32 v,u32 p,u32 f) { (void)as; (void)v; (void)p; (void)f; CHECK(0); return -1; }
int pgalloc_free_n_owner(u32 o,u32 p,int n) { (void)o; (void)p; (void)n; CHECK(0); return 0; }
int as_va_to_pa(u32 pd,u32 va,u32 *pa) { (void)pd; *pa=(u32)stack_page+(va & (PAGE_SIZE-1)); return 0; }
void caller_access_save(volatile CallerAccessFrame *p) { kmemset((void *)p,0,sizeof(*p)); }
void caller_access_invalidate(void) {}
void caller_access_leave(const volatile CallerAccessFrame *p) { (void)p; }
u32 tss_get_esp0(void) { return 0; }
void tss_set_esp0(u32 n) { (void)n; }
u32 ledger_owner_pages(u32 owner) { (void)owner; return 0; }
int lease_revoke_all(struct addrspace *as) { (void)as; return 0; }
void shlib_addrspace_detach(struct addrspace *as) { (void)as; }
u32 paging_addrspace_free_user_range(struct addrspace *as,u32 b,u32 e) { (void)as; return (e-b)/PAGE_SIZE; }
void paging_addrspace_destroy(struct addrspace *as) { as->pd_phys=0; }
void paging_addrspace_poison(struct addrspace *as) { (void)as; CHECK(0); }
int ledger_reclaim_owner(u32 owner,u32 *pages) { (void)owner; *pages=0; return 1; }
void serial_puts_polled(const char *s) { (void)s; }
volatile u32 kctx_irq_depth,kctx_exc_depth,font_guard_active,tick_count,ring3_wm_shell_gen;
ShellService g_shell_service;
void shell_service_retire(void) {}
void shell_service_publish(u32 b,u32 n) { (void)b; (void)n; }
int kselftest_run_audit(const char *name) { (void)name; return 0; }
int paging_set_not_present(u32 b,u32 e) { (void)b; (void)e; return 0; }
void shell_print(const char *s,u8 a) { (void)a; say(s); CHECK(0); }
void shell_print_dec(u32 n,u8 a) { (void)n; (void)a; CHECK(0); }
void __cdecl kprintf(u8 a,const char *s,...) { (void)a; (void)s; CHECK(0); }


void font_guard_fault(u32 a,u32 b,u32 c) { (void)a; (void)b; (void)c; CHECK(0); }
void res_owner_set(int id) { (void)id; }
#define CLEANUP(name) void name(int id) { CHECK(id==cur); }
CLEANUP(kapi_strings_cleanup_owned)
CLEANUP(db_cleanup_owned)
CLEANUP(fd_redirect_reset_owned)
CLEANUP(font_lease_close_owned)
CLEANUP(vfs_close_owned)
CLEANUP(pipe_free_owned)
CLEANUP(shm_free_owned)
CLEANUP(snd_owner_exit)
CLEANUP(pcm_reclaim)
#undef CLEANUP
#define NOTICE(name) void name(int id) { (void)id; }
NOTICE(ime_owner_exit)
NOTICE(kbd_inject_owner_exit)
NOTICE(launch_owner_exit)
NOTICE(host_owner_exit)
NOTICE(con_sink_owner_exit)
NOTICE(appslot_gfx_owner_exit)
#undef NOTICE
int gfx_return_emergency(u32 owner,int reason) { CHECK(owner>=1 && owner<=3 && reason==EXEC_KIND_EXITED); return 0; }
void gui_owner_exit(int id,int kind) { CHECK(id>=1 && id<=3 && kind==EXEC_KIND_EXITED); cleanup_count++; }
int con_sink_is_enabled(void) { return 0; }
int con_sink_reader_get(void) { CHECK(0); return 0; }
void con_sink_push_exit(int id) { (void)id; CHECK(0); }
i32 launch_child(i32 id) { (void)id; CHECK(0); return 0; }
void kbd_inject_discard(void) { CHECK(0); }
void input_discard_stop(void) { CHECK(0); }

static void user_main(void)
{
    if (cur==2) {
        CHECK(exec_nest_level==A_DEPTH);
        expect(1,'I',0); CHECK(trace_op[0]=='S' && trace_state[0]==&x87_parent[SHELL_DEPTH]);
        /* A calls the real loader while its own launch frame remains live. */
        CHECK(exec_launch("/B",0)==EXEC_SUCCESS);
        CHECK(cur==2 && exec_nest_level==A_DEPTH && cleanup_count==1);
        expect(4,'R',&x87_parent[A_DEPTH]);
        CHECK(return_count==1);
    } else {
        CHECK(cur==3 && exec_nest_level==B_DEPTH);
        expect(3,'I',0); CHECK(trace_op[2]=='S' && trace_state[2]==&x87_parent[A_DEPTH]);
        return_count++;
    }
    exec_exit(EXEC_SUCCESS,EXEC_KIND_EXITED);
    CHECK(0); for (;;) {}
}
/* CPU stack boundary calls an app main that returns normally, with no exit KAPI.
 * The next statement in the real exec_launch must perform exec_exit. */
static void resident_main(void)
{
    CHECK(cur==APP_ID_SHELL && exec_nest_level==SHELL_DEPTH);
    expect(1,'I',0); CHECK(trace_op[0]=='S' && trace_state[0]==&x87_parent[0]);
    return_count++;
}
static void map(u32 base,u32 size)
{
    u32 args[6]={base,size,3,0x32,~0u,0},r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(90),"b"(args):"memory"); CHECK(r==base);
}
void _start(void)
{
    map(MEM_SHELL_LOAD_ADDR,PAGE_SIZE);
    map(MEM_SHELL_STACK_TOP-PAGE_SIZE,PAGE_SIZE);
    kapi=&api; ring3_tramp_page=(u32)tramp;
    cur=APP_ID_SHELL; slots[cur].depth=SHELL_DEPTH; slots[cur].state=APP_STATE_RUNNING;
    exec_nest_level=SHELL_DEPTH;
    CHECK(exec_launch("/A",0)==EXEC_SUCCESS);
    CHECK(cur==APP_ID_SHELL && exec_nest_level==SHELL_DEPTH && cleanup_count==2);
    expect(5,'R',&x87_parent[SHELL_DEPTH]);
    say("PASS x87 nested: shell -> A -> B -> A -> shell, parent[2] then parent[1]\n");
    trace_count=return_count=cleanup_count=0; exec_nest_level=0;
    CHECK(exec_launch("/shell",0)==EXEC_SUCCESS);
    CHECK(return_count==1 && cleanup_count==1 && exec_nest_level==0);
    expect(2,'R',&x87_parent[0]);
    say("PASS x87 normal return: app main -> exec_exit -> launch landing -> parent[0]\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(0)); for (;;) {}
}

int as_va_to_pa_read(u32 pd,u32 va,u32 *pa) { (void)pd; *pa=va; return 0; }
int shlib_loaded(void) { return 0; }
u32 shlib_data_pages(void) { return 0; }
