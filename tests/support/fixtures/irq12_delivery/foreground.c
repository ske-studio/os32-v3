/* Use the real exception decoder and teardown. The guest and saved mappings
 * belong to this process. The page table is host memory. Privileged operations and module pins are modeled. */
#include "sys/platform/pc98/legacy/v86_mem.h"
static u8 low_bytes[V86_REMAP_END];
#define P2V(address) (low_bytes + (address))
#include "sys/platform/pc98/legacy/v86_mem.c"
#include "kbd.h"
#include "legacy_guest.h"
#include "check.h"

__attribute__((section(".guest"), aligned(16)))
static const u8 guest_outs[16] = {0x6e};
struct kdebug kdebug;
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
volatile u32 kctx_irq_depth, kctx_exc_depth;
static unsigned resets, detaches, releases;
int v86_gp_handler(u32 *frame);

struct tss_entry kernel_tss;
int v86_soft_out(u16 port, u8 val) { return 0; }
void paging_load_cr3(u32 pd) {}
static u32 pte0[PTE_COUNT], pd[PTE_COUNT];
static u32 *page_directory = pd, *page_tables[] = {pte0};
static int pg_enabled = 1, flushes;
u32 paging_current_cr3(void) { return (u32)pd; }
#define arch_mmu_flush_tlb() (++flushes)
static void set_page_noflush(u32 a, u32 phys, u32 flags) { pte0[a / PAGE_SIZE] = phys | flags; }
#include "sys/kern/mm/paging_lowmap.inc"
void kfree(void *ptr) {}
static u8 host_bios[PAGE_SIZE], host_mailbox[MEM_AUTOPLAY_MAILBOX_SIZE];
static struct slice_memory storage;
void *kzalloc(u32 bytes) { return bytes == sizeof storage ? &storage : 0; }
void tss_iomap_deny_all(void)
{
    volatile u8 *map = kernel_tss.iomap;
    for (unsigned i = 0; i <= TSS_IOMAP_SIZE; ++i) map[i] = 255;
}
void *kmemcpy(void *dst, const void *src, u32 bytes)
{
    for (u32 i = 0; i < bytes; ++i) ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}
const u8 *v86_bios_real_lowmem(void) { return host_bios; }
void *v86_call_mailbox(void) { return host_mailbox; }
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
    lowmap_token = &v86_session.lowmap;
    lowmap_token->root = paging_current_cr3();
    lowmap_token->master_pde = 3;
    lowmap_token->page_count = V86_GUEST_MAP_END / PAGE_SIZE;
    pd[0] = 7;
    pte0[0] = 7;
    check(!v86_mem_enable_slices(), "allocate context");
    host_bios[99] = 87;
    low_bytes[99] = 41;
    host_mailbox[3] = 9;
    low_bytes[MEM_AUTOPLAY_MAILBOX_BASE + 3] = 7;
    kernel_tss.iomap[40] = 6;
    v86_mem_suspend();
    check(!pte0[0] && pd[0] == 3 && flushes == 1, "suspend page table");
    check(low_bytes[99] == 87 && low_bytes[MEM_AUTOPLAY_MAILBOX_BASE + 3] == 9 &&
          kernel_tss.iomap[40] == 255, "suspend host state");
    v86_session.active = 0;
    v86_session.soft_input = 1;
    check(legacy_active(), "pump input owner");
    v86_pic_reset();
    v86_kbd_reset();
    u32 keyboard_frame[V86I_EFLAGS + 1] = {0}, vector;
    v86_pic_set_imr(0, 0xfd);
    check(!legacy_kbd_push(KEY_CTRL), "control make");
    v86_reflect_irq(keyboard_frame, 1);
    check(v86_pic_candidate(&vector) == 1 && v86_kbd_in(V86_KBD_DATA) == KEY_CTRL,
          "pump keyboard delivery");
    check(legacy_kbd_push(KEY_STOP), "control stop");
    legacy_request_exit();
    check(v86_session.soft_stop, "pump stop retained");
    low_bytes[MEM_AUTOPLAY_MAILBOX_BASE + 3] = 8;
    v86_mem_resume();
    check(pte0[0] == 7 && pd[0] == 7 && flushes == 2, "resume page table");
    check(low_bytes[99] == 41 && low_bytes[MEM_AUTOPLAY_MAILBOX_BASE + 3] == 7 &&
          host_mailbox[3] == 8 && kernel_tss.iomap[40] == 6, "resume DOS state");
    v86_session.active = 1;
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
    check(!legacy_active() && !v86_session.soft_stop && !slice_mem, "session input release");
    passed();
}
