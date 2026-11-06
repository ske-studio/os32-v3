/* Paired API: exercise the production full-slot validator and private boot relay. */
#include "kernel/ime/fep_module.c"
static IME_Dict dict;
const IME_Dict *ime_current_dict(void) { return &dict; }
u32 body_boot_connections(void);
int services_public;
enum services_stage services_stage;
volatile u32 services_boot_fault;
static int reject_slot;
static int exported;
const u32 *module_exports(struct module_handle h)
{ (void)h; return exported ? (const u32 *)1 : 0; }
static u32 live;
static u32 mock_connections(void) { return live; }
static int mock_init(FepModuleApi *out)
{
    u32 entries[FEP_MODULE_API_SLOTS];
    for (u32 i = 0; i < FEP_MODULE_API_SLOTS; i++) entries[i] = (u32)(uptr)mock_connections;
    if (reject_slot >= 0) entries[reject_slot] = 0;
    kmemcpy(out, entries, sizeof(entries));
    return 0;
}
static int test(void)
{
    struct module_record record = {.text = (u32)(uptr)mock_connections, .text_size = 1};
    if (ime_boot_connections()) return 1;
    for (int i = 0; i < (int)FEP_MODULE_API_SLOTS; i++) {
        candidate_valid = 0; reject_slot = i;
        if (fep_start(&record, (u32)(uptr)mock_init, 0) != MODULE_INIT_FAILED ||
            candidate_valid || ime_boot_connections()) return 2;
    }
    reject_slot = -1;
    if (fep_start(&record, (u32)(uptr)mock_init, 0) || !candidate_valid) return 3;
    /* services_public and module_exports are still false at boot_check time. */
    for (live = 0; live <= 2; live++)
        if (ime_boot_connections() != live) return 4;
    services_public = exported = 1;
    if (ime_boot_connections() != live) return 5;
    dict.db = 0;
    if (body_boot_connections()) return 6;
    dict.db = (void *)1;
    if (body_boot_connections() != 1) return 7;
    dict.isolated = 1; /* Failed close still owns its connection. */
    if (body_boot_connections() != 1) return 8;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
