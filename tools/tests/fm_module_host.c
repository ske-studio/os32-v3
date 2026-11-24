#include "bootinfo_native.h"
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
#define FM_ADAPTER "../../drivers/sound/fm/fm_module.c"
#endif
#include FM_ADAPTER
#include "tick_order.h"
#include "../../kernel/irq/tick.c"
volatile u32 kctx_irq_depth;
const u32 fm_kexports[] = {0};
const struct module_symbol_contract fm_import_contracts[] = {{0}};
const struct module_symbol_contract fm_export_contracts[] = {{0}};
const struct bootinfo *bootinfo_get(void) { finish(2); }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }


static u32 ticks, silence, mode, public_calls;
static int acquired, deny, detached;
int res_owner_get(void) { return 7; }
int snd2_chip_implicit(int owner) { CHECK(owner == 7); return deny || !acquired ? -1 : 0; }
void snd2_chip_fault(u32 dev) { (void)dev; }
static int body_attach(void (*fault)(u32))
{
    CHECK(fault == snd2_chip_fault);
    acquired = 1;
    return 0;
}
static int body_detach(void)
{
    detached++;
    if (mode == 4) return 0;
    acquired = 0;
    return 1;
}
static void body_tick(void) { ticks++; }
static int body_quiet(void)
{
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
    out->attach = body_attach;
    out->detach = body_detach;
    out->quiet = body_quiet;
    out->fm_note_on = body_note;
    return 0;
}
static int start_then_fail(struct module_record *r, u32 entry, u32 *ops)
{
    int rc = fm_start(r, entry, ops);
    if (!rc && mode == 1) {
        CHECK(!r->tick_refs && !module_exports(fm_handle));
        tick_dispatch();
        fm_note_on(0, 60);
        CHECK(!ticks && !public_calls);
        return MODULE_INIT_FAILED;
    }
    return rc;
}
void _start(void)
{
    CHECK(FM_MODULE_OPS_VERSION == 2);
    fm_handle.generation = 1;
    CHECK(fm_module_load() == MODULE_BUSY);
    fm_handle.generation = 0;
    CHECK(!slots[PC98_TICK_SND].fn);
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = start_then_fail, .quiesce = fm_quiesce, .detach = fm_detach};
    /* Facade stubs must be harmless before load. */
    fm_note_on(0, 60); tick_dispatch();
    CHECK(!ticks && !public_calls);
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
        CHECK(!r->tick_refs && silence == 1);
        CHECK(r->state == MODULE_ABSENT);
        tick_dispatch(); fm_note_on(0, 60);
        CHECK(!ticks && !public_calls);
        empty(); finish(0);
    }
    CHECK(!rc && !r->tick_refs);
    deny = 1;
    fm_note_on(0, 60); fm_note_off(0); fm_set_tone_num(0, 1);
    ssg_volume(0, 1); ssg_tone(0, 1); ssg_all_off();
    CHECK(!ticks && !public_calls);
    deny = 0;
    tick_dispatch(); fm_note_on(0, 60);
    CHECK(!ticks && public_calls == 1);
    u32 before = returned;
    rc = fm_module_abort();
    CHECK(!r->tick_refs && silence == 1);
    tick_dispatch(); fm_note_on(0, 60);
    CHECK(!ticks && public_calls == 1);
    CHECK(detached == 1);
    if (mode == 2 || mode == 4) {
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

#include "../../platform/pc98/module_pair.c"

const struct boot_common *boot_common_get(void) {
    static struct boot_common info;
    const struct bootinfo *bi = bootinfo_get();
    info.image_valid = bi->img_valid; info.image_crc = bi->img_crc; info.image_size = bi->img_size;
    return &info;
}

/* Clock-independent registry fixture. */
void tick_budget_begin(void) { }
void tick_budget_end(void) { }
