/* Real adapter/runtime + core session teardown. CPU/PTE edges are explicit
 * substitutes: exact PTEs/ESP0 are covered by shm_user_host, hardware by PM. */
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "io.h"
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
static u32 kcg_restores;
void kcg_v86_restore(void) { kcg_restores++; }
#include "v86_module.c"
#include "v86.h"
#include "paging.h"
#include "module.h"

volatile u32 kctx_irq_depth, kctx_exc_depth;
struct kdebug kdebug;
static u8 mailbox[MEM_AUTOPLAY_MAILBOX_SIZE];
void *v86_call_mailbox(void) { return mailbox; }
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
static u32 mode, restored, detached, audited, calls;
static u32 free_before;
static void body_noop(void) { calls++; }
static void body_detach(void)
{
    CHECK(restored && !v86_session.running && !v86_session.active);
    CHECK(session_ref && !kctx_irq_depth && !kctx_exc_depth);
    CHECK(returned == free_before && !retired);
    detached++;
}
static int init_v86(V86ModuleApi *out)
{
    u32 entries[V86_MODULE_API_SLOTS];
    for (u32 i = 0; i < V86_MODULE_API_SLOTS; i++) entries[i] = (u32)(uptr)body_noop;
    kmemcpy(out, entries, sizeof(entries));
    out->v86_bios_detach_disk = body_detach;
    return 0;
}
void v86_bios_restore_real(void) { CHECK(session_ref); }
void v86_runtime_end(void)
{
    CHECK(session_ref);
    v86_session.active = v86_session.running = 0;
}
int paging_v86_restore(struct v86_session_state *s)
{
    CHECK(s == &v86_session);
    if (mode == 1) {
        /* A rollback requested before PTE return must retain the body/owner.
         * Moving session release before this edge compiles, then fails here. */
        int rc = v86_module_abort();
        CHECK(returned == free_before && !retired);
        CHECK(rc == MODULE_ISOLATED);
    }
    CHECK(session_ref);
    restored++;
    return 0;
}
void gfx_v86_return(void) { CHECK(restored && detached && session_ref); }
void kselftest_audit_v86_return(void) { CHECK(session_ref); audited++; }
/* A restore failure is outside these successful-return lifecycle cases. */
void platform_fault_cell(u32 pos, u16 code, u8 attr)
{
    (void)pos; (void)code; (void)attr;
    CHECK(0);
}
#include "v86_mem.c"

void _start(void)
{
    /* This fixture exercises teardown only; provide the saved host mailbox. */
    u32 args[6] = {MEM_AUTOPLAY_MAILBOX_BASE, PAGE_SIZE, 3, 0x32, ~0U, 0}, mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(args) : "memory");
    CHECK(mapped == MEM_AUTOPLAY_MAILBOX_BASE);
    for (u32 i = 0; i < sizeof(mailbox); i++) mailbox[i] = (u8)i;
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = v86_start, .quiesce = v86_quiesce};
    CHECK(!v86_module_session_acquire());
    v86_io_apply_policy(); CHECK(!calls);
    v86_io_reset_policy(); CHECK(kcg_restores == 1);
    CHECK(!module_load(&q, &v86_handle));
    struct module_record *r = module_record_get(v86_handle);
    r->init = (u32)(uptr)init_v86;
    r->text = (u32)(uptr)body_noop & ~(PAGE_SIZE - 1);
    r->text_size = 16 * PAGE_SIZE;
    int rc = module_start(v86_handle);
    if (mode == 3) {
        CHECK(rc == MODULE_INIT_FAILED && r->state == MODULE_ABSENT);
        empty(); finish(0);
    }
    CHECK(!rc && v86_module_session_acquire());
    CHECK(!v86_module_session_acquire() && r->active_refs == 1);
    /* This teardown-only fixture supplies the normal snapshot cache policy. */
    for (int mi = 0; mi < V86_IDENT_MAP_N; mi++) {
        const struct v86_ident_ent *e = &v86_ident_map[mi];
        for (u32 a = e->start; a < e->end; a += PAGE_SIZE)
            v86_session.low_pte[a / PAGE_SIZE] = a | e->teardown_flags;
    }
    v86_session.open = v86_session.active = v86_session.running = 1;
    free_before = returned;
    v86_io_apply_policy(); CHECK(calls == 1);
    if (mode == 2) kctx_exc_depth = 1;
    v86_session_end();
    CHECK(kcg_restores == 2);
    if (mode == 2) {
        CHECK(!detached && !audited && session_ref && r->active_refs == 1);
        CHECK(v86_session.release_pending && returned == free_before && !retired);
        kctx_exc_depth = 0; /* setjmp.asm trusted landing */
        v86_session_end();
    }
    CHECK(restored == 1 && detached == 1 && audited == 1);
    for (u32 i = 0; i < sizeof(mailbox); i++)
        CHECK(((u8 *)P2V(MEM_AUTOPLAY_MAILBOX_BASE))[i] == mailbox[i]);
    CHECK(!session_ref && !r->active_refs && !v86_session.release_pending);
    CHECK(returned == free_before && !retired);
    v86_session_end(); CHECK(detached == 1);
    if (mode == 1) {
        CHECK(v86_module_abort() == MODULE_ISOLATED);
        CHECK(returned == free_before && !retired);
    } else {
        CHECK(!v86_module_abort()); empty();
    }
    finish(0);
}
