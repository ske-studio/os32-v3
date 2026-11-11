#include "gfx2_bridge_fake.h"
/* Declaration checks evaluate types at runtime so type mutants compile RED. */
#include "gfx_hal.h"
#include "gfx_internal.h"
#include "gfx_policy.h"
#include "gfx_hw_mono_cpu.h"
#include "../../userland/lib/gfx/gfx_direct_internal.h"
#include "../../drivers/video/geforce/geforce_module.h"
#include "../../hwdev/gf/accel.h"

KernelAPI *gfx_api;
int gfx_lost;
int gfx_direct;
int gfx_ready, gfx_dirty_suppress, gfx_packed;
void *memmove(void *dst, const void *src, unsigned int n) { (void)src; (void)n; return dst; }
u8 gfx_get_pixel(int x, int y) { (void)x; (void)y; return 0; }
void gfx_pixel_nodirty(int x, int y, u8 c) { (void)x; (void)y; (void)c; }
GFX_Framebuffer gfx_fb;
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
__typeof__(gfx_gen) gfx_gen;

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg) - 1) : "memory"); \
    finish(1); \
} } while (0)
#define TYPE(symbol, type) CHECK(__builtin_types_compatible_p(__typeof__(symbol), type))

__typeof__(g_backend) g_backend;

typedef int (*predicate_fn)(void);
typedef void (*void_fn)(void);
typedef int (*role_fn)(u32);
typedef void (*reinit_fn)(int, int);
typedef int (*fill_fn)(int, int, int, int, u8, u8);
typedef int (*copy_fn)(int, int, int, int, int, int);
typedef int (*put_fn)(int, int, int, int, const u8 *, u32, int);
typedef int (*mono_fn)(int, int, int, int, const u8 *, u32, u8, int, int, int, int, int);
typedef int (*get_fn)(int, int, int, int, u8 *, u32);

/* No GUI/backend in the declaration fixture; policy behavior is tested in H4. */
__typeof__(g_backend) g_backend;
int con_sink_is_enabled(void) { return 0; }
int appslot_gfx_owner(void) { return 1; }
void gfx_reinit_surface_roles(int finish, int display_only)
{ (void)finish; (void)display_only; }

void _start(void)
{
    TYPE(g_backend, GfxBackend *);
    TYPE(&gfx_policy_gen, u16 *);
    TYPE(&gfx_scan_changed, int *);
    TYPE(&gfx_direct_wanted, predicate_fn);
    TYPE(&gfx_fullscreen_owner, predicate_fn);
    TYPE(&gfx_policy_reeval, void_fn);
    TYPE(&gfx_policy_blocks_map, role_fn);
    TYPE(&gfx_reinit_surface_roles, reinit_fn);
    TYPE(&gfx_hw_mono_cpu, mono_fn);
    TYPE(&gfx_direct, int *);
    TYPE(&gfx_gen, u16 *);
    TYPE(&gfx_lost, int *);
    TYPE(&gfx_cmd_enqueue_fill, fill_fn);
    TYPE(&gfx_cmd_enqueue_copy, copy_fn);
    TYPE(&gfx_cmd_enqueue_put, put_fn);
    TYPE(&gfx_cmd_enqueue_mono, mono_fn);
    TYPE(&gfx_cmd_get, get_fn);
    TYPE(&gfx_flush, predicate_fn);
    TYPE(&gfx_cmd_drop, void_fn);
    TYPE(&libos32gfx_take_regen, predicate_fn);
    TYPE(&gfx_copy_rect, copy_fn);
    TYPE(&geforce_fill_rop, fill_fn);
    TYPE(&gf_direct_ok, predicate_fn);
    TYPE(&gf_direct_ops, const GfxDirectOps *);
    CHECK(gfx_policy_gen == 1 && !gfx_scan_changed);
    gfx_policy_reeval();
    CHECK(!gfx_direct_wanted() && !gfx_fullscreen_owner());
    CHECK(!gfx_policy_blocks_map(0xffffffffU));
    CHECK(gfx_hw_mono_cpu(0, 0, 1, 1, 0, 1, 7, -1, 0, 0, 1, 1) == OS32_ERR_NOSYS);
    CHECK(!gfx_flush());
    gfx_cmd_drop();
    CHECK(gfx_copy_rect(0, 0, 1, 1, 1, 1) == OS32_ERR_NOSYS);
    finish(0);
}
