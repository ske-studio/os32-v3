/* ILP32 fake module owners; the production broker and hit macro run unchanged. */
#include "debug_probe.h"
#include "kstring.h"

static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); \
    finish(1); \
} } while (0)

struct fixture {
    struct { struct probe_decl table[PROBE_POINT_MAX]; char names[PROBE_POINT_MAX][PROBE_NAME_SIZE + 1]; } ro;
    struct probe_slot slots[PROBE_POINT_MAX];
    u8 padding[4096];
} __attribute__((aligned(4096)));
static struct fixture images[2];
struct module_record module_records[MODULE_RECORD_CAP];
static struct module_handle owners[] = {{0, 1}, {1, 1}, {2, 1}};
static u32 calls, last_id, acquire_calls, fail_acquire, check_stop;
static struct probe_list listing;
static void capture(u32 id) { calls++; last_id = id; }
struct module_record *module_record_get(struct module_handle h)
{
    if (h.slot >= MODULE_RECORD_CAP || !h.generation ||
        module_records[h.slot].generation != h.generation) return 0;
    return &module_records[h.slot];
}
int module_ref_acquire(struct module_handle h, u32 owner,
                       enum module_ref_kind kind, u32 handler)
{
    struct module_record *r = module_record_get(h);
    (void)handler;
    acquire_calls++;
    if (fail_acquire == acquire_calls) return 0;
    CHECK(r && r->owner == owner && kind == MODULE_REF_REGISTRATION);
    CHECK(r->state == MODULE_STARTING || r->state == MODULE_RUNNING);
    r->registration_refs++;
    return 1;
}
int module_ref_release(struct module_handle h, enum module_ref_kind kind)
{
    struct module_record *r = module_record_get(h);
    CHECK(r && r->registration_refs && kind == MODULE_REF_REGISTRATION);
    if (check_stop) {
        for (u32 i = 0; i < 2; i++)
            for (u32 j = 0; j < PROBE_POINT_MAX; j++) CHECK(!images[i].slots[j].hit);
    }
    r->registration_refs--;
    return 1;
}
static void setup(void)
{
    for (u32 i = 0; i < 3; i++) {
        module_records[i].generation = 1;
        module_records[i].owner = i + 1;
        module_records[i].state = MODULE_RUNNING;
    }
    for (u32 i = 0; i < 2; i++) {
        struct module_record *r = &module_records[i];
        r->base = r->backing = (u32)(uptr)&images[i];
        r->pages = sizeof(images[i]) / 4096;
        r->ro = r->base;
        r->ro_size = sizeof(images[i].ro);
        for (u32 j = 0; j < PROBE_POINT_MAX; j++) {
            char *name = images[i].ro.names[j];
            name[0] = 'a' + i; name[1] = '.';
            name[2] = '0' + j / 10; name[3] = '0' + j % 10; name[4] = 0;
            images[i].ro.table[j] = (struct probe_decl){name, &images[i].slots[j]};
        }
    }
    module_records[2].module_id = MODULE_ID_KDEBUG;
    module_records[2].text = (u32)(uptr)capture;
    module_records[2].text_size = 1;
}
static void list(u32 count)
{
    CHECK(!debug_probe_control(PROBE_LIST, &listing, sizeof(listing)));
    CHECK(listing.count == count);
}
static void empty(void)
{
    check_stop = 1;
    CHECK(!debug_probe_unregister(owners[0]));
    CHECK(!debug_probe_unregister(owners[1]));
    check_stop = 0;
    for (u32 i = 0; i < 3; i++) CHECK(!module_records[i].registration_refs);
    list(0);
}
static struct probe_attach attachment(u32 count)
{
    struct probe_attach a = {.owner = {2, 1}, .callback = capture, .count = count};
    for (u32 i = 0; i < PROBE_ACTIVE_MAX; i++) a.ids[i] = i;
    return a;
}
static i32 arm(struct probe_attach *a)
{
    return debug_probe_control(PROBE_ATTACH, a, sizeof(*a));
}
void _start(void)
{
    setup();
    struct probe_decl *table = images[0].ro.table;
    struct probe_decl saved = table[0];
    PROBE_HIT(images[0].slots[0]); CHECK(!calls);
    CHECK(!debug_probe_register(owners[0], 0, 0));
    CHECK(debug_probe_register(owners[0], table, 33) == PROBE_ERR_LIMIT);
    CHECK(debug_probe_register((struct module_handle){0, 2}, table, 1) == PROBE_ERR_ARG);
    CHECK(debug_probe_register(owners[0], (void *)(uptr)1, 1) == PROBE_ERR_ARG);
    CHECK(debug_probe_register(owners[0], (void *)(uptr)(module_records[0].ro + module_records[0].ro_size - 4), 1) == PROBE_ERR_ARG);
    table[0].slot = &images[1].slots[0];
    CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    table[0].slot = (void *)&images[0].ro;
    CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    table[0].slot = (void *)((u8 *)&images[0].slots[0] + 1);
    CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    table[0].slot = (void *)(uptr)(module_records[0].backing + module_records[0].pages * 4096 - 4);
    CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    table[0] = saved;
    module_records[0].text = (u32)(uptr)saved.slot;
    module_records[0].text_size = sizeof(*saved.slot);
    CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    module_records[0].text_size = 0;
    table[0].name = "outside.ro";
    CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    table[0].name = (char *)(uptr)(module_records[0].ro + module_records[0].ro_size - 1);
    *(char *)table[0].name = 'a';
    CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    table[0] = saved;
    const char *bad[] = {"", "A", "a-b", "a b", "a/b", "a\x80", "12345678901234567890123456789012"};
    for (u32 i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        kstrncpy(images[0].ro.names[0], bad[i], sizeof(images[0].ro.names[0]));
        CHECK(debug_probe_register(owners[0], table, 1) == PROBE_ERR_ARG);
    }
    kstrncpy(images[0].ro.names[0], "1234567890123456789012345678901", PROBE_NAME_SIZE);
    CHECK(!debug_probe_register(owners[0], table, 1));
    list(1); CHECK(!kstrcmp(listing.points[0].name, images[0].ro.names[0]));
    /* Names are value copies, not borrowed strings. */
    images[0].ro.names[0][0] = 'x';
    list(1); CHECK(listing.points[0].name[0] == '1');
    empty();
    setup();
    struct probe_decl second = table[1];
    table[1].name = table[0].name;
    CHECK(debug_probe_register(owners[0], table, 2) == PROBE_ERR_ARG);
    table[1] = second; table[1].slot = table[0].slot;
    CHECK(debug_probe_register(owners[0], table, 2) == PROBE_ERR_ARG);
    table[1] = second;
    table[1].slot = (void *)((u8 *)table[0].slot + 4);
    CHECK(debug_probe_register(owners[0], table, 2) == PROBE_ERR_ARG);
    table[1] = second;
    fail_acquire = acquire_calls + 2;
    CHECK(debug_probe_register(owners[0], table, 3) == PROBE_ERR_ARG);
    CHECK(!module_records[0].registration_refs); list(0);
    fail_acquire = 0;
    /* STARTING is retained but neither listed nor attachable. */
    module_records[0].state = MODULE_STARTING;
    CHECK(!debug_probe_register(owners[0], table, 8));
    CHECK(module_records[0].registration_refs == 8); list(0);
    struct probe_attach a = attachment(1);
    CHECK(arm(&a) == PROBE_ERR_ARG);
    check_stop = 1; CHECK(!debug_probe_unregister(owners[0])); check_stop = 0;
    CHECK(!module_records[0].registration_refs);
    module_records[0].state = MODULE_RUNNING;
    CHECK(!debug_probe_register(owners[0], table, 32)); list(32);
    CHECK(debug_probe_register(owners[1], images[1].ro.table, 1) == PROBE_ERR_LIMIT);
    a = attachment(9); CHECK(arm(&a) == PROBE_ERR_LIMIT);
    a = attachment(0); CHECK(arm(&a) == PROBE_ERR_ARG);
    a = attachment(2); a.ids[1] = a.ids[0]; CHECK(arm(&a) == PROBE_ERR_ARG);
    a.ids[1] = 32; CHECK(arm(&a) == PROBE_ERR_ARG);
    for (u32 i = 0; i < 32; i++) CHECK(!images[0].slots[i].hit);
    a = attachment(8); a.callback = 0; CHECK(arm(&a) == PROBE_ERR_ARG);
    a.callback = (probe_callback_fn)(uptr)1; CHECK(arm(&a) == PROBE_ERR_ARG);
    a = attachment(8); a.owner.generation++; CHECK(arm(&a) == PROBE_ERR_ARG);
    a = attachment(8); module_records[2].state = MODULE_STARTING;
    CHECK(arm(&a) == PROBE_ERR_ARG); module_records[2].state = MODULE_RUNNING;
    module_records[2].module_id = MODULE_ID_KDEBUG + 1;
    CHECK(arm(&a) == PROBE_ERR_ARG); module_records[2].module_id = MODULE_ID_KDEBUG;
    fail_acquire = acquire_calls + 1; CHECK(arm(&a) == PROBE_ERR_ARG); fail_acquire = 0;
    CHECK(!arm(&a)); CHECK(module_records[2].registration_refs == 1);
    list(32);
    for (u32 i = 0; i < 32; i++) CHECK(listing.points[i].enabled == (i < 8));
    PROBE_HIT(images[0].slots[7]); CHECK(calls == 1 && last_id == 7);
    CHECK(arm(&a) == PROBE_ERR_BUSY);
    CHECK(debug_probe_register(owners[1], images[1].ro.table, 1) == PROBE_ERR_BUSY);
    CHECK(debug_probe_control(PROBE_DETACH, &owners[1], sizeof(owners[1])) == PROBE_ERR_ARG);
    check_stop = 1;
    CHECK(!debug_probe_control(PROBE_DETACH, &owners[2], sizeof(owners[2])));
    check_stop = 0;
    CHECK(!module_records[2].registration_refs);
    PROBE_HIT(images[0].slots[7]); CHECK(calls == 1);
    CHECK(!arm(&a)); empty();
    /* Cross-owner duplicate names, stale generation and reused id. */
    CHECK(!debug_probe_register(owners[0], table, 1));
    kstrncpy(images[1].ro.names[0], table[0].name, PROBE_NAME_SIZE);
    CHECK(debug_probe_register(owners[1], images[1].ro.table, 1) == PROBE_ERR_ARG);
    CHECK(module_records[1].registration_refs == 0); list(1);
    CHECK(debug_probe_unregister((struct module_handle){0, 2}) == PROBE_ERR_ARG); list(1);
    CHECK(!debug_probe_unregister(owners[0]));
    module_records[0].generation++; owners[0].generation++;
    CHECK(!debug_probe_register(owners[0], table, 1));
    list(1); CHECK(listing.points[0].id == 0 && !listing.points[0].enabled);
    a = attachment(1); CHECK(!arm(&a));
    check_stop = 1;
    CHECK(!debug_probe_unregister(owners[2]));
    check_stop = 0;
    CHECK(!module_records[2].registration_refs && !images[0].slots[0].hit);
    empty();
    a = attachment(1); CHECK(arm(&a) == PROBE_ERR_ARG);
    CHECK(debug_probe_control(PROBE_LIST, &listing, sizeof(listing) - 1) == PROBE_ERR_ARG);
    CHECK(debug_probe_control(PROBE_ATTACH, &a, sizeof(a) - 1) == PROBE_ERR_ARG);
    CHECK(debug_probe_control(PROBE_DETACH, &owners[2], 0) == PROBE_ERR_ARG);
    CHECK(debug_probe_control(PROBE_LIST, 0, sizeof(listing)) == PROBE_ERR_ARG);
    CHECK(debug_probe_control(0, &listing, sizeof(listing)) == PROBE_ERR_ARG);
    finish(0);
}
