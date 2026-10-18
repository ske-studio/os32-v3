/* Real runtime + FM resident adapter. Init functions stand in for relocated
 * body text; prefix-return failure exercises the actual post-registration
 * rollback. Destructive page fixture verifies body/owner/FD reclamation. */
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "io.h"
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
#ifndef FM_ADAPTER
#define FM_ADAPTER "../../kernel/fm_module.c"
#endif
#include FM_ADAPTER

static u32 ticks, silence, mode, public_calls;
static void body_tick(void) { ticks++; }
static void body_stop(int enable) { CHECK(!enable); }
static int body_quiet(void)
{
    CHECK(!tick_target); /* no new Key-ON after the final silence */
    silence++;
    return mode != 2;
}
static void body_note(int ch, int note) { CHECK(ch == 0 && note == 60); public_calls++; }
static int init_fm(FmModuleApi *out)
{
    CHECK(!module_exports(fm_handle));
    u32 entries[FM_MODULE_API_SLOTS];
    for (u32 i = 0; i < FM_MODULE_API_SLOTS; i++) entries[i] = (u32)(uptr)body_tick;
    kmemcpy(out, entries, sizeof(entries));
    out->tick = body_tick;
    out->quiet = body_quiet;
    out->snd_set_master = body_stop;
    out->fm_note_on = body_note;
    return 0;
}
static int start_then_fail(struct module_record *r, u32 entry, u32 *ops)
{
    int rc = fm_start(r, entry, ops);
    if (!rc && mode == 1) {
        CHECK(r->tick_refs == 1 && !module_exports(fm_handle));
        snd_tick();
        fm_note_on(0, 60);
        CHECK(ticks == 1 && !public_calls);
        return MODULE_INIT_FAILED;
    }
    return rc;
}
void _start(void)
{
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = start_then_fail, .quiesce = fm_quiesce, .detach = fm_detach};
    /* Facade stubs must be harmless before load. */
    fm_note_on(0, 60); snd_tick();
    CHECK(!ticks && !public_calls && !snd_bgm_is_playing());
    CHECK(!module_load(&q, &fm_handle));
    struct module_record *r = module_record_get(fm_handle);
    r->init = (u32)(uptr)init_fm;
    /* Explicit host seam: candidate code is host text, rather than body bytes.
     * Product-image checks separately validate the real relocated facade. */
    r->text = (u32)(uptr)body_tick & ~(PAGE_SIZE - 1);
    r->text_size = 16 * PAGE_SIZE;
    int rc = module_start(fm_handle);
    if (mode == 1 || mode == 3) {
        CHECK(rc == MODULE_INIT_FAILED);
        CHECK(!tick_target && !r->tick_refs && silence == 1);
        CHECK(r->state == MODULE_ABSENT);
        snd_tick(); fm_note_on(0, 60);
        CHECK(ticks == (mode == 1 ? 1U : 0U) && !public_calls);
        empty(); finish(0);
    }
    CHECK(!rc && r->tick_refs == 1 && tick_target);
    snd_tick(); fm_note_on(0, 60);
    CHECK(ticks == 1 && public_calls == 1);
    u32 before = returned;
    rc = fm_module_abort();
    CHECK(!tick_target && !r->tick_refs && silence == 1);
    snd_tick(); fm_note_on(0, 60);
    CHECK(ticks == 1 && public_calls == 1);
    if (mode == 2) {
        CHECK(rc == MODULE_ISOLATED && r->state == MODULE_QUARANTINED);
        CHECK(returned == before && mask[0] == 2 && live[0] && !retired);
        CHECK(fm_module_abort() == MODULE_ISOLATED);
    } else {
        CHECK(!rc && r->state == MODULE_ABSENT);
        CHECK(!fm_module_abort());
        empty();
    }
    finish(0);
}
