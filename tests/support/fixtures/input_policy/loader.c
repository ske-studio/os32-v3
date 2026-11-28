#include "sys/kern/exec/exec.c"
#include "sys/input/input_context.h"
#include "shell_loop.h"
#include "in2_int.h"
#include "kbd_contract.h"
#include "endian_le.h"

#include <setjmp.h>
static jmp_buf resident_jump, child_jump;
void *probe_jump = resident_jump;
static struct addrspace child_as;
static u8 child_pages[MEM_EXEC_STACK_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u32 trampoline[PAGE_SIZE / sizeof(u32)];
static u32 active_pd, heap_used, child_entries, stop_discards;
static int child_kind;
static unsigned lease_closes;
#if OS32_HOOK_FONT_LEASE
void font_lease_close_shell(void) { ++lease_closes; }
#else
void exec_font_lease_close_shell(void) { ++lease_closes; }
#endif
static u8 lost[4];
static unsigned lost_count;
int input_key_pressed(u8 sc) { (void)sc; return 0; }
u32 input_key_release_gen(u8 sc) { (void)sc; return 0; }
void in2_ring_clear(void) { lost_count = 0; }
void in2_ring_lost(u8 reason) { if (lost_count < sizeof lost) lost[lost_count++] = reason; }
void mouse_ext_read(i32 *x, i32 *y) { *x = *y = 0; }
i32 mouse_ext_read_wheel(void) { return 0; }
u32 pgalloc_free_pages(void) { return 1024; }
u32 pgalloc_alloc_phys(u32 owner, int pages)
{
    return owner == 2 && pages > 0 && (u32)pages <= sizeof child_pages / PAGE_SIZE ?
           (u32)child_pages : 0;
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{ (void)kind; (void)id; (void)name; *owner = 2; return 1; }
void *kmalloc(u32 size) { return size == sizeof child_as ? &child_as : 0; }
int paging_addrspace_create_lease(struct addrspace *as, u32 owner)
{ as->owner = owner; as->pd_phys = 2; return 0; }
int as_va_to_pa(u32 pd, u32 va, u32 *pa)
{ *pa = (u32)child_pages + (va & (PAGE_SIZE - 1)); return pd != 2; }
int as_va_to_pa_read(u32 pd, u32 va, u32 *pa)
{ (void)pd; *pa = va; return 0; }
u32 paging_current_cr3(void) { return active_pd; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 pd) { active_pd = pd; }
void exec_heap_save_state(u32 *used) { *used = heap_used; }
void exec_heap_restore_state(u32 base, u32 size, u32 used)
{ (void)base; (void)size; heap_used = used; }
void exec_heap_user_init(struct addrspace *as) { (void)as; heap_used = 0; }
static u32 dispatches;
u32 io_dispatch_total_count(void) { return dispatches; }
static void discard_stop(void)
{
    ++stop_discards;
    kbd_discard_stop();
}
static KernelAPI probe_api;
unsigned input_test_irq;
volatile u32 font_guard_active;
volatile u32 kctx_irq_depth, kctx_exc_depth;
unsigned char shell_area[0x100000] __attribute__((section(".probe_shell"),used));
static uptr entered[5];
static int observed;
static unsigned round_no, previous_gen, fp_saves, lease_before, saves_before;
static int bad;
static int no_keys(void) { return 0; }
static const u8 baseline[] = {'I','K','M','1',2,0,16,0,1,0,0,0,0,0,0,0,
                              2,0,2,0,37,0,0,0,65,0,0,0,0,0,0,0};
static int restored(void)
{
    u8 bytes[sizeof baseline];
    u32 needed;
    if (ikm_policy_get(bytes, sizeof bytes, &needed) || needed != sizeof baseline) return 0;
    for (unsigned i = 0; i < needed; ++i)
        if (bytes[i] != baseline[i]) return 0;
    return 1;
}
#if OS32_HOOK_GUI
int gui_registered(void)
#else
int exec_gui_registered(void)
#endif
{
    return round_no >= 1 && round_no <= 3;
}
void arch_fp_save(struct arch_fp_context *ctx) { (void)ctx; ++fp_saves; }
static int gui_default(void) { return 1; }
int gui_take_next_shell(char *path, int cap)
{
    if (round_no != 1) return 0;
    kstrncpy(path, SYS_GSHELL_BIN, cap);
    return 1;
}
static int ready(int gui)
{
    if (round_no == 5) {
        lease_before = g_shell_service.generation;
        saves_before = fp_saves;
        if (input_context_owner_register(APP_ID_SHELL, 999, 0, 0) ||
            input_context_owner_select(APP_ID_SHELL, 999)) bad = 1;
    }
    return !round_no && gui;
}
static void loading(const char *path, int gui) { (void)path; (void)gui; }
static void failure(int gui,int rc,int streak) { (void)gui; (void)rc; (void)streak; }
static void delay_clear(void) {}
static void result(const char *path, int rc, int gui, int streak)
{
    KbdPolicySnapshot s;
    (void)path; (void)gui; (void)streak;
    if (round_no == 5) {
        bad |= rc != EXEC_ERR_INVALID || exec_nest_level || lease_before != g_shell_service.generation ||
               fp_saves != saves_before || input_context_snapshot(&s) || s.exec_gen != 999;
        input_context_owner_release(APP_ID_SHELL, 999);
    } else bad |= !input_context_snapshot(&s) || !restored();
    if (++round_no == 7) __builtin_longjmp(entered,1);
}
int vfs_read(const char *path, void *buf, u32 size)
{
    (void)path;
    u32 hdr[16] = {OS32X_MAGIC, OS32X_HDR_SIZE, OS32X_HDR_VERSION,
#ifdef __PPC__
        OS32X_FLAG_PPC32,
#else
        0,
#endif
        0,4096,0,0,0,KAPI_VERSION,MEM_SHELL_LOAD_ADDR,KAPI_DATA_FIELDS_OFF,
        OS32_KAPI_ABI_GENERATION,OS32_MEMORY_LAYOUT_GENERATION,0,4096};
    if (probe_jump == child_jump) {
        hdr[3] |= OS32X_FLAG_RING3;
        hdr[10] = MEM_EXEC_LOAD_ADDR;
    }
    if (size != sizeof hdr) return -1;
    for (unsigned i=0;i<16;++i) le32_wr((u8*)buf + i*4,hdr[i]);
    return sizeof hdr;
}
int vfs_read_fd(int fd, void *dst, u32 size) { (void)fd; (void)dst; return size; }
int vfs_resolve_path(const char *path, char *out, int size)
{ kstrncpy(out,path,size); return VFS_OK; }
static int check_park_reclaim(unsigned shell_gen)
{
    unsigned previous = 0;
    int previous_id = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        int id = appslot_start_reserve(1);
        AppSlot *child = appslot_at(id);
        if (id < APP_ID_MIN || !child || (attempt && id != previous_id)) return 1;
        unsigned gen = child->exec_gen;
        if (!gen || gen == previous ||
            input_context_owner_register(id, gen, APP_ID_SHELL, shell_gen)) return 1;
        appslot_start_commit(id, 1, 0);
        appslot_park_commit();
        KbdPolicySnapshot foreground;
        if (av_focus_set(id) || av_focus_get() != id || !av_focus_set(APP_ID_MAX + 1) ||
            input_context_snapshot(&foreground) || foreground.owner != (unsigned)id ||
            foreground.exec_gen != gen) return 1;
        KbdPolicyKey tab = { .map_generation = ikm_policy_generation(),
                             .sc_down_repeat = KEY_TAB | KBD_POLICY_SC_DOWN };
        if (!input_delivery_key(&foreground, &tab) || !av_focus_set(APP_ID_MAX + 1) ||
            kapi->kbd_policy_key_read(&tab, 1) != 1) return 1;
        if (!input_delivery_key(&foreground, &tab) || av_focus_set(id) ||
            kapi->kbd_policy_key_read(&tab, 1) != 0) return 1;
        appslot_resume_commit(id);
        if (kapi->kbd_policy_delivery_set(KBD_POLICY_DELIVERY_BYTE)) return 1;
        appslot_park_commit();
        if (av_focus_set(0) || input_context_snapshot(&foreground) ||
            foreground.owner != APP_ID_SHELL || foreground.exec_gen != shell_gen ||
            kapi->kbd_policy_delivery_set(KBD_POLICY_DELIVERY_BYTE)) return 1;
        if (av_focus_set(id)) return 1;
        if (ikm_policy_reset(id, gen) || restored()) return 1;
        kbd_set_gui_mode(1);
        if (in2_excl_acquire(id, IN2_EXCL_KBD)) return 1;
        legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
        unsigned stops = stop_discards;
        if (exec_abort_clear() < 0 || exec_kill(id) < 0 || stop_discards != stops + 1 ||
            legacy_raw_take() != -1 || in2_excl_acquired(id) ||
            lost_count != 2 || lost[0] != IN2_LOST_FOCUS || lost[1] != IN2_LOST_EXCL) return 1;
        kbd_set_gui_mode(0);
        if (!restored()) return 1;
        if (appslot_get(id) || input_context_owner_select(id, gen) == 0 ||
            input_context_snapshot(&foreground) || foreground.owner != APP_ID_SHELL ||
            foreground.exec_gen != shell_gen) return 1;
        previous = gen;
        previous_id = id;
    }
    return 0;
}

static int check_child(unsigned shell_gen)
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        child_kind = attempt;
        unsigned entries = child_entries, saves = fp_saves, stops = stop_discards;
        if (!attempt && input_context_owner_register(APP_ID_MIN, 999, 0, 0)) return 1;
        unsigned parent_closes = lease_closes;
        unsigned parent_lease = g_shell_service.generation;
        int parent_av = av_focus_get();
        active_pd = 1;
        heap_used = 1234;
        io_syscall_begin(APP_ID_SHELL);
        ++dispatches;
        probe_jump = child_jump;
        int rc = exec_run("/bin/child");
        probe_jump = resident_jump;
        KbdPolicySnapshot after;
        if (active_pd != 1 || heap_used != 1234 || io_syscall_suspend(APP_ID_SHELL) != 1 ||
            input_context_snapshot(&after) || after.owner != APP_ID_SHELL ||
            after.exec_gen != shell_gen || !restored() || appslot_get(APP_ID_MIN)) return 1;
        if (!attempt) {
            if (parent_closes != lease_closes || parent_lease != g_shell_service.generation || parent_av != av_focus_get() ||
                rc != EXEC_ERR_INVALID || fp_saves != saves || child_entries != entries ||
                input_context_owner_release(APP_ID_MIN, 999)) return 1;
        } else if (child_entries != entries + 1 || stop_discards != stops + (attempt == 3)) return 1;
    }
    if (legacy_raw_take() != -1 || !legacy_stop_filter(KEY_STOP, 0, SHIFT_CTRL)) return 1;
    kbd_set_gui_mode(0);
    return 0;
}

void arch_entry_enter(struct arch_entry_context *entry, u32 pd)
{
    KbdPolicySnapshot s;
    (void)entry;
    if (probe_jump == child_jump) {
        ++child_entries;
        if (pd != 2 || active_pd != pd || io_syscall_suspend(APP_ID_SHELL) || input_context_snapshot(&s) ||
            s.owner != APP_ID_MIN || !s.exec_gen || ikm_policy_reset(s.owner, s.exec_gen) ||
            restored()) bad = 1;
        if (child_kind == 2) exec_fault_recover();
        if (child_kind == 3) {
            kbd_set_gui_mode(1);
            legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
            ring3_abort_kill();
        }
        return;
    }
    int gui = round_no >= 1 && round_no <= 3;
    if (pd || input_context_snapshot(&s) || s.owner != APP_ID_SHELL ||
        !s.exec_gen || s.exec_gen == previous_gen || s.flags != INPUT_CONTEXT_ISIG ||
        s.context != (gui ? INPUT_CONTEXT_GUI : INPUT_CONTEXT_RUNNING)) {
        bad = 1;
        __builtin_longjmp(entered,1);
    }
    if (round_no == 0) bad |= check_child(s.exec_gen);
    if (round_no == 1) bad |= check_park_reclaim(s.exec_gen);
    bad |= input_context_owner_generation(APP_ID_SHELL) != s.exec_gen;
    bad |= kapi->kbd_policy_delivery_set(KBD_POLICY_DELIVERY_BYTE) != 0;
    bad |= ikm_policy_reset(s.owner, s.exec_gen) != 0 || restored();
    previous_gen = s.exec_gen;
    ++observed;
    if (gui) exec_fault_recover();
    if (round_no == 4) ring3_abort_kill();
}
int probe(void)
{
    const struct shell_loop_ops ops = {gui_default, ready, loading, result, failure, delay_clear};
    kapi = &probe_api;
#include "exec_kapi_init.inc"
    ring3_tramp_page = (u32)trampoline;
    static const InputOps ops_input = {.discard_stop = discard_stop};
    input_register(&ops_input);
    appslot_init();
#ifdef __PPC__
    if (ikm_policy_init(IKM_PLATFORM_MAC, 0, &hid_jp_backend, no_keys)) return 1;
#else
    if (ikm_policy_init(IKM_PLATFORM_PC98, &pc98_jp_backend, &hid_jp_backend, no_keys)) return 1;
#endif
    if (ikm_policy_set(baseline, sizeof baseline, 0, 0)) return 1;
    if (!__builtin_setjmp(entered)) shell_loop_run(&ops);
    return bad || observed != 6;
}
