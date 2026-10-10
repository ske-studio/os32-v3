/* Real gfx lifecycle/backends/ledger; only hardware and caller setup are host. */
unsigned int host_arch_if = 0x202U;
#define run original_gfx_run
#include "gfx_kernel_fb_host.c"
#undef run
#define main shutdown_guest_main
#include "../../userland/tests/shutdown_probe.c"
#undef main
void *memset(void *p, int value, __SIZE_TYPE__ bytes)
{ return kmemset(p, value, bytes); }
int memcmp(const void *a, const void *b, __SIZE_TYPE__ bytes)
{
    const u8 *x = a, *y = b;
    for (__SIZE_TYPE__ i = 0; i < bytes; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
static u32 shutdown_calls, leave_calls;
static int identity_error, query_error, query_empty;
static const char *probe_message;
static void __cdecl probe_print(u8 attr, const char *fmt, ...)
{ (void)attr; probe_message = fmt; }
static int probe_message_is(const char *expected)
{
    if (!probe_message) return 0;
    const char *actual = probe_message;
    while (*actual && *actual == *expected) { actual++; expected++; }
    return *actual == *expected;
}
void host_shutdown_event(int event)
{ if (event) shutdown_calls++; else leave_calls++; }
static int probe_identity(u32 *app, u32 *owner, u32 *generation)
{
    if (identity_error) return OS32_ERR_INVAL;
    *app = 2; *owner = space.owner; *generation = space.generation; return 0;
}
static int probe_query(u32 role, OS32_SurfaceQueryResult *out)
{
    struct surface_query_source source;
    struct caller_access caller;
    if (query_error) return OS32_ERR_INVAL;
    if (query_empty) { kmemset(out, 0, sizeof(*out)); return 0; }
    int rc = gfx_surface_source(role, &source);
    if (rc) return rc;
    rc = surface_query_authorize(&source, &caller);
    if (rc) return rc;
    kmemset(out, 0, sizeof(*out));
    out->count = source.count;
    for (u32 i = 0; i < source.count; i++) {
        const struct ledger_surface *sf = &ledger_surfaces[source.refs[i].sid];
        out->desc[i].ref.sid = source.refs[i].sid;
        out->desc[i].ref.generation = source.refs[i].generation;
        out->desc[i].backend = sf->backend;
    }
    return 0;
}
static void run(void) __attribute__((used));
static void run(void)
{
    u32 owner, gens[LEDGER_MAX_SURFACES];
    CallerAccessFrame previous;
    static KernelAPI api;
    api.kprintf = probe_print;
    api.caller_identity = probe_identity;
    api.gfx_screen_owner = gfx_screen_owner;
    api.surface_query = probe_query;
    api.gfx_shutdown = gfx_shutdown;
    map_host(0x10000, 0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE, WAB_XE10_LINEARWIN_SIZE);
    boot(); gfx_boot_reserve();
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "h3b", &owner));
    CHECK(!paging_addrspace_create_lease(&space, owner));
    slot.state = APP_STATE_RUNNING; slot.cpl3 = 1; slot.as = &space;
    slot.hdr_flags = 0; /* Same ordinary non-fullscreen declaration as the guest. */
    host_cr3 = space.pd_phys;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    for (int pref = GFX_PREF_PC98; pref <= GFX_PREF_CIRRUS; pref++) {
        host_gui = 0; host_gfx_owner = 2;
        gfx_set_backend_pref(pref); gfx_init();
        u32 backend = gfx_sf_backend(), shutdown_before = shutdown_calls, leave_before = leave_calls;
        for (u32 i = 0; i < LEDGER_MAX_SURFACES; i++) gens[i] = ledger_surfaces[i].gen;
        host_gui = 1; host_gfx_owner = 3;
        identity_error = 1;
        probe_message = 0;
        CHECK(shutdown_guest_main(0, 0, &api) == 1);
        CHECK(probe_message_is("shutdown_probe: FAIL (precondition caller_identity)\n"));
        identity_error = 0; query_error = 1;
        probe_message = 0;
        CHECK(shutdown_guest_main(0, 0, &api) == 1);
        CHECK(probe_message_is("shutdown_probe: FAIL (precondition surface_query)\n"));
        query_error = 0; query_empty = 1;
        probe_message = 0;
        CHECK(shutdown_guest_main(0, 0, &api) == 1);
        CHECK(probe_message_is("shutdown_probe: FAIL (precondition count 0)\n"));
        query_empty = 0;
        CHECK(shutdown_calls == shutdown_before && leave_calls == leave_before);
        CHECK(shutdown_guest_main(0, 0, &api) == 0);
        CHECK(gfx_sf_backend() == backend && shutdown_calls == shutdown_before && leave_calls == leave_before);
        for (u32 i = 0; i < LEDGER_MAX_SURFACES; i++) CHECK(ledger_surfaces[i].gen == gens[i]);
        host_gfx_owner = 2;
        gfx_shutdown();
        CHECK(shutdown_calls == shutdown_before + 1 && leave_calls == leave_before + 1);
        struct surface_query_source source;
        CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT, &source) == OS32_ERR_INVAL);
        struct ledger_surface *sf = ledger_surface_find(backend, LEDGER_ROLE_CLIENT);
        CHECK(sf && sf->gen == gens[sf - ledger_surfaces] + 1);
        /* TRUSTED cleanup is still allowed with a different screen owner. */
        host_gui = 0; gfx_init(); host_gui = 1; host_gfx_owner = 3; host_user = 0;
        gfx_shutdown();
        CHECK(shutdown_calls == shutdown_before + 2);
        host_user = 1;
    }
    caller_access_leave(&previous);
    SAY("PASS shutdown_probe: non-owner/owner/trusted on three backends");
    die(0);
}
