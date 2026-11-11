#include "gfx2_bridge_fake.h"
/* v1-only harnesses keep HAL v2 at the A0 NOSYS boundary.
 * C1's real registry and core callers are tested by gfx2-backend-reg-host. */
#ifndef TEST_GFX2_CORE_FAKE_H
#define TEST_GFX2_CORE_FAKE_H
#include "../../gfx/v2/gfx2_int.h"
int gfx2_adapter_register(GfxBackend *v1, u32 kind, u32 flags)
{ (void)v1; (void)kind; (void)flags; return OS32_ERR_NOSYS; }
struct gfx2_adapter *gfx2_active(void) { return NULL; }
int gfx2_head_restore_v1(struct gfx2_adapter *a, u32 head)
{ (void)a; (void)head; return OS32_ERR_NOSYS; }
void gfx2_head_detach(struct gfx2_adapter *a, u32 head) { (void)a; (void)head; }
void gfx2_pal_reset16(void) { }
void gfx2_pal_set_v1(int first, int count, const u8 *rgb)
{ (void)first; (void)count; (void)rgb; }
#endif
