/* Real core CLIENT binding, driven by a provider unknown to the old PC98 list. */
#include "gfx_internal.h"
#include "display_provider.h"
#include "pgalloc.h"
#include "os32_kapi_shared.h"
static u8 shadow[4096];
static struct ledger_surface client;
static GfxBackend of_backend;
GfxBackend gfx_backend_pc98;
GfxBackend *g_backend = &of_backend;
u8 *bb[SURFACE_QUERY_MAX], *bb_b, *bb_r, *bb_g, *bb_i;
int gfx_current_height = 4;
static int gfx_mode_ready=1, gfx_started=1;
u32 display_provider_ledger(const GfxBackend *be) { return be == &of_backend ? LEDGER_SF_OF_FB : LEDGER_SF_PC98; }
struct ledger_surface *ledger_surface_find(u32 backend, u32 role)
{ return backend==LEDGER_SF_OF_FB && role==LEDGER_ROLE_CLIENT ? &client : NULL; }
int ring3_call_from_user(void) { return 0; }
void gfx_framebuffer_bridge(void *fb) { (void)fb; }
#include "core_surface_kind.inc"
#include "core_framebuffer.inc"
static int test(void)
{
    struct gfx_kernel_fb fb;
    static const GfxMode mode = {7, 4, GFX_BB_PACKED8, 8, 0, 0, 64};
    of_backend.selected_mode=&mode;
    client=(struct ledger_surface){.first=V2P(shadow)/PAGE_SIZE, .npages=1,
        .backing=LEDGER_SB_RAM, .backend=LEDGER_SF_OF_FB, .role=LEDGER_ROLE_CLIENT,
        .width=7, .height=4, .pitch=64, .format=GFX_BB_PACKED8, .planes=1};
    if (gfx_sf_backend()!=6) return 1;
    gfx_bind_client();
    if (bb[0]!=(u8 *)P2V(client.first*PAGE_SIZE) || bb[1] || bb[2] || bb[3] ||
        of_backend.bb_base!=bb[0] || of_backend.bb_size!=4096 ||
        of_backend.bb_pitch!=64 || of_backend.bb_format!=GFX_BB_PACKED8) return 2;
    if (gfx_kernel_framebuffer(&fb) || fb.width!=7 || fb.height!=4 ||
        fb.pitch!=64 || fb.planes[0]!=bb[0]) return 3;
    client.closing=1; gfx_bind_client();
    if (bb[0] || of_backend.bb_base || of_backend.bb_size) return 4;
    client.closing=0; gfx_mode_ready=0; gfx_bind_client();
    if (bb[0] || of_backend.bb_base) return 5;
    gfx_mode_ready=1; gfx_bind_client();
    if (!bb[0] || bb_b!=bb[0] || bb_r || bb_g || bb_i) return 6;
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
