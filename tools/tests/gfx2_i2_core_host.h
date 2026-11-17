/* Device and current-slot stubs; gfx_init and ownership use product code. */
#include "display_provider.h"
#include "gfx_internal.h"
GfxBackend gfx_backend_pc98, gfx_backend_pegc, gfx_backend_cirrus;
GfxBackend gfx_backend_geforce, gfx_backend_trident;
static const GfxMode host_mode = {.width=64, .height=4, .pitch=256, .format=GFX2_FMT_XRGB8888};
static void host_device_init(void) {}
GfxBackend *display_provider_select(int pref, int legacy, display_select_mode_fn select)
{
    (void)pref; (void)legacy; (void)select;
    gfx_backend_pc98.selected_mode = &host_mode;
    gfx_backend_pc98.init = host_device_init;
    return &gfx_backend_pc98;
}
u32 display_provider_ledger(const GfxBackend *backend)
{ return backend == &gfx_backend_pc98 ? GFX2_KIND_PC98 : 0; }
void display_provider_each(void (*fn)(GfxBackend *, void *), void *arg)
{ fn(&gfx_backend_pc98, arg); }
void display_provider_raster_reset(void) {}
int display_provider_client_before_init(const GfxBackend *be) { (void)be; return 0; }
int display_provider_init_failed(GfxBackend *be) { (void)be; return 0; }
int display_provider_idle_safe(int pref) { (void)pref; return 1; }
int serial_gate_active(void) { return 1; }
void serial_puts_polled(const char *s) { (void)s; }
void serial_diag_u32(u32 x) { (void)x; }
void serial_diag_hex(u32 x, int n) { (void)x; (void)n; }
void gfx_scroll_init(void) {}
void gfx_clear_planar_pages(u32 n) { (void)n; }
void display_text_hide(void) {}
void display_planar16(void) {}
void display_planar_start(int x) { (void)x; }
void display_flip_pages(int x, int y) { (void)x; (void)y; }
u8 *display_planar_plane(unsigned i) { (void)i; return NULL; }
int gfx_return_emergency(u32 owner, int reason) { (void)owner; return reason; }
int pc98_route_safe(void) { return 1; }
static int g_gfx_owner = GFX_OWNER_WM;
volatile u32 gfx_init_reject_count;
int res_owner_get(void) { return I1_APP_ID; }
AppSlot *appslot_get(int id) { return id == I1_APP_ID ? &slot : NULL; }
int appslot_abort_request(void) { slot.abort_req = 1; return 0; }
#include "gfx2_i2_claim.inc"
int con_sink_is_enabled(void) { return 0; }
int kcg_module_ready(void) { return 1; }
void shell_print(const char *s, u8 attr) { (void)s; (void)attr; }
void kapi_sys_exit(int rc) { die(rc); }
void display_palette4_write(int i, u8 r, u8 g, u8 b)
{ (void)i; (void)r; (void)g; (void)b; }
extern volatile u32 gfx_reinit_fail_count;
int gfx_is_started(void);
