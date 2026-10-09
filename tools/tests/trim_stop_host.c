/* g4: Rust Box + real SDK loop/allocator, kernel map/heap/DONE/kill, WM forget.
 * The reused fixture supplies host MMU/IRQ and absent devices. */
#include "mem_fixture.c"
#include "wm_source.c"
void *kmemcpy(void *d, const void *s, u32 n) { heap_copy(d, s, n); return d; }
void *memcpy(void *d, const void *s, u32 n) { heap_copy(d, s, n); return d; }
void *memset(void *d, int c, u32 n) { u8 *p=d; while(n--) *p++=c; return d; }
void *memmove(void *d, const void *s, u32 n) {
    u8 *a=d; const u8 *b=s;
    if (a < b) while(n--) *a++=*b++;
    else while(n) { n--; a[n]=b[n]; }
    return d;
}
int memcmp(const void *a, const void *b, u32 n) {
    const u8 *x=a,*y=b;
    while(n--) { if (*x != *y) return *x-*y; x++; y++; } return 0;
}
#include "mem_stat_source.c"
void appslot_trim_request_as(struct addrspace *as) { trim_product_request(as); }
#include "trim_exec_source.c"
void kbd_set_gui_mode(int on) { (void)on; }
void ime_set_render(void *table) { (void)table; }
#include "trim_gui_source.c"
extern void g4_forget(int id);
extern void g4_rust_run(void);
static u32 reclaimed, notified, raw_maps, yields;
static void exec_reclaim_resources(int id) { CHECK("reclaim target", id == 2); reclaimed++; }
static void exec_notify_owned(int id, int kind) {
    CHECK("STOP notification", id == 2 && kind == EXEC_KIND_ABORTED);
    notified++; g4_forget(id);
}
static int launch_chain(int id, int *chain, int max) { (void)id; (void)chain; (void)max; return 0; }
#include "stop_source.c"
static void line(const char *s) { u32 n=0; while(s[n]) n++; say(s,n); say("\n",1); }
void g4_check(int ok, const char *label) { if (!ok) { say("FAIL: ",6); line(label); die(1); } }
u32 g4_case(void) { return G4_CASE; }
void *g4_alloc(u32 n) { return wrap_mem_alloc(n); }
void g4_free(void *p) { wrap_mem_free(p); }
int g4_stat(int id, void *p, u32 n) { return kapi_mem_stat(id,p,n); }
i32 g4_state(i32 id) { return id == 2 ? APP_STATE_PARKED : appslot_state(id); }
i32 g4_yield(void) { yields++; return 0; }
static int raw_failure;
void *g4_map(u32 n, void *p, u32 flags) {
    raw_maps++;
    if (raw_failure) return 0;
    return wrap_mem_map(n,p,flags);
}
int g4_unmap(void *p, u32 n) { return wrap_mem_unmap(p,n); }
int g4_gui(u32 op, u32 arg) { return gui_call(op,arg); }
void g4_unexpected(void) { line("FAIL: unexpected KAPI/panic"); die(1); }
extern void g4_rust_raw_map(void);
void g4_raw_map_test(void) {
    u32 calls=raw_maps;
    raw_failure=1; g4_rust_raw_map(); raw_failure=0;
    CHECK("raw map no retry", raw_maps == calls+1 && yields == 0);
}
u32 g4_epoch(void) {
    g_slot[2].state=APP_STATE_PARKED;
    trim_product_request(&b);
    CHECK("pending mark", g_slot[2].trim_pending && g_slot[2].trim_epoch);
    g_slot[2].state=APP_STATE_RUNNING;
    return g_slot[2].trim_epoch;
}
void g4_unarmed_done(void) {
    u32 epoch=g4_epoch();
    CHECK("unarmed natural tail", gui_call(GUI_OP_TRIM_DONE,epoch) > 0);
}
void g4_consume_returned(void) {
    heap_select(3);
    CHECK("front consumes returned pages", wrap_mem_alloc(128*1024));
    heap_select(2);
}
void g4_pass(void) {
    line("PASS Rust run_vt TRIM=1 hook=1 DONE=1 ARENA tail+whole LARGE=0 INITIAL unchanged DATA OK EXACT regrow; raw map retry=0");
    if (G4_CASE == 3) line("PASS G-2 natural tail baseline; front consumption does not change returned-page accounting");
    die(0);
}
void g4_stop(void) {
    u32 owner=a.owner, count=appslot_reclaim_count;
    u32 bad=ledger_bad_free, retire=ledger_retire_refused, claim=ledger_claim_refused;
    CHECK("STOP pending phase", !!g_slot[2].trim_pending == (G4_CASE == 2));
    CHECK("STOP DONE phase", appslot_trim_done_count == (G4_CASE == 1 ? 1U : 0U));
    // IRQ request then cooperative park: host has no iret/context switch.
    appslot_stop_request();
    CHECK("STOP requested", g_slot[2].stop_wm_req);
    u32 parks=ring3_stop_park_count;
    appslot_park_stop_commit();
    CHECK("STOP park once", ring3_stop_park_count == parks+1 && g_slot[2].parked_from_stop);
    paging_load_cr3(paging_kernel_pd_phys()); ring3_in_syscall=0;
    caller_access_invalidate();
    CHECK("WM clears keyboard STOP", appslot_abort_clear() == 0);
    CHECK("STOP kill", exec_kill(2) == 0);
    CHECK("STOP reclaimed once", appslot_reclaim_count == count+1 && reclaimed == 1 && notified == 1);
    CHECK("STOP owner zero", ledger_owner_pages(owner) == 0 && !g_slot[2].as && !exec_as_leftover_pages);
    CHECK("STOP slot trim cleared", !g_slot[2].trim_pending && !g_slot[2].trim_epoch);
    MemStat s;
    CHECK("STOP snapshot", kapi_mem_stat(0,&s,sizeof(s)) == sizeof(s));
    CHECK("STOP mask bit cleared", !(s.trim_pending_mask & (1U<<2)));
    CHECK("STOP ledger errors zero", ledger_bad_free == bad && ledger_retire_refused == retire && ledger_claim_refused == claim);
    CHECK("STOP repeat rejected", exec_kill(2) < 0 && appslot_reclaim_count == count+1 && reclaimed == 1);
    line(G4_CASE == 1 ? "PASS STOP after DONE" : "PASS STOP inside hook");
    line("reclaim=1 owner=0 trim_pending=0 trim_epoch=0 mask-bit=0 WM-sent=0 ledger-errors=0");
    die(0);
}
void _start(void) {
    heap_ram(); host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    roots[0]=paging_kernel_pd_phys(); integration=heap_alias_on=1;
    heap_mmap(MEM_SHELL_HEAP_BASE,MEM_SHELL_HEAP_SIZE,0);
    exec_heap_init_at(MEM_SHELL_HEAP_BASE,MEM_SHELL_HEAP_SIZE);
    appslot_init(); ring3_in_syscall=1;
    heap_create(&a,2); roots[1]=a.pd_phys;
    heap_create(&b,3); roots[2]=b.pd_phys;
    g_slot[2].gui=g_slot[3].gui=1;
    heap_select(2);
    g4_rust_run();
    CHECK("Rust entry must finish", 0);
}
