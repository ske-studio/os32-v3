/* Use the real exception decoder and teardown. The guest and saved mappings
 * belong to this process. Hardware, page-table updates and module pins are models. */
#include "sys/platform/pc98/legacy/v86_mem.c"
#include "check.h"

__attribute__((section(".guest"), aligned(16)))
static const u8 guest_outs[16] = {0x6e};
struct kdebug kdebug;
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
volatile u32 kctx_irq_depth, kctx_exc_depth;
static unsigned resets, detaches, releases;
int v86_gp_handler(u32 *frame);

void *kmemcpy(void *dst, const void *src, u32 bytes) { return dst; }
void *v86_call_mailbox(void) { return 0; }
void v86_bios_restore_real(void) {}
int paging_v86_restore(struct v86_session_state *s) { return 0; }
void platform_fault_cell(u32 pos, u16 code, u8 attr) {}
void _stop(void) { check(0, "restore mapping"); }
void v86_io_reset_policy(void) { ++resets; }
void v86_bios_detach_disk(void) { ++detaches; }
void gfx_v86_return(void) {}
void kselftest_audit_v86_return(void) {}
void v86_module_session_release(void)
{
    check(!kctx_exc_depth && !kctx_irq_depth && detaches == 1, "foreground release");
    ++releases;
}
int platform_irq_reserved(unsigned irq) { return 0; }
void irq_disable(unsigned irq) {}
void tss_set_esp0(u32 value) {}
u32 v86_io_in(u16 port, int size) { return 0; }
void v86_io_out(u16 port, int size, u32 value) {}
int v86_bios_is_hle(u32 vector) { return 0; }
void v86_bios_dispatch(u32 *frame, u32 vector) {}

void _start(void)
{
    u32 frame[V86F_SS + 1] = {0};
    frame[V86F_CS] = (u32)guest_outs >> 4;
    v86_session.open = v86_session.running = v86_session.active = 1;
    for (unsigned i = 0; i < V86_IDENT_MAP_N; ++i) {
        const struct v86_ident_ent *e = &v86_ident_map[i];
        for (u32 a = e->start; a < e->end; a += PAGE_SIZE)
            v86_session.low_pte[a / PAGE_SIZE] = e->teardown_flags;
    }
    ++kctx_exc_depth;
    check(v86_gp_handler(frame) && v86_get_exit_reason() == V86_EXIT_UNKNOWN_OP &&
          !frame[V86F_EIP], "unsupported instruction");
    v86_session_end();
    check(v86_session.release_pending && !v86_session.open && !v86_session.active &&
          resets == 1 && !releases && !detaches, "defer release");
    --kctx_exc_depth;
    v86_session_end();
    v86_session_end();
    check(!v86_session.release_pending && resets == 1 && releases == 1 && detaches == 1,
          "foreground release once");
    passed();
}
