/* Real adapter/runtime + core session teardown. CPU/PTE edges are explicit
 * substitutes: exact PTEs/ESP0 are covered by shm_user_host, hardware by PM. */
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "io.h"
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
#include "v86_module.c"
#include "v86.h"
#include "paging.h"
#include "module.h"

volatile u32 kctx_irq_depth, kctx_exc_depth;
struct kdebug kdebug;
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
#include "v86_mem.c"

void _start(void)
{
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = v86_start, .quiesce = v86_quiesce};
    CHECK(!v86_module_session_acquire());
    v86_io_apply_policy(); CHECK(!calls);
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
    if (mode == 2) {
        CHECK(!detached && !audited && session_ref && r->active_refs == 1);
        CHECK(v86_session.release_pending && returned == free_before && !retired);
        kctx_exc_depth = 0; /* setjmp.asm trusted landing */
        v86_session_end();
    }
    CHECK(restored == 1 && detached == 1 && audited == 1);
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
