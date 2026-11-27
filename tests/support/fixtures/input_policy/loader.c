#include "sys/kern/exec/exec.c"
#include "sys/input/input_context.h"
#include "shell_loop.h"
#include "endian_le.h"

#include <setjmp.h>
jmp_buf probe_jump;
static KernelAPI probe_api;
unsigned input_test_irq;
volatile u32 font_guard_active;
volatile u32 kctx_irq_depth, kctx_exc_depth;
unsigned char shell_area[0x100000] __attribute__((section(".probe_shell"),used));
static uptr entered[5];
static int observed;
static unsigned round_no, previous_gen, fp_saves, lease_before, saves_before;
static int bad;
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
    } else bad |= !input_context_snapshot(&s);
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
        if (av_focus_set(0) || input_context_snapshot(&foreground) ||
            foreground.owner != APP_ID_SHELL || foreground.exec_gen != shell_gen) return 1;
        if (av_focus_set(id)) return 1;
        exec_kill_one(id);
        if (appslot_get(id) || input_context_owner_select(id, gen) == 0 ||
            input_context_snapshot(&foreground) || foreground.owner != APP_ID_SHELL ||
            foreground.exec_gen != shell_gen) return 1;
        previous = gen;
        previous_id = id;
    }
    return 0;
}

void arch_entry_enter(struct arch_entry_context *entry, u32 pd)
{
    KbdPolicySnapshot s;
    (void)entry;
    int gui = round_no >= 1 && round_no <= 3;
    if (pd || input_context_snapshot(&s) || s.owner != APP_ID_SHELL ||
        !s.exec_gen || s.exec_gen == previous_gen || s.flags != INPUT_CONTEXT_ISIG ||
        s.context != (gui ? INPUT_CONTEXT_GUI : INPUT_CONTEXT_RUNNING)) {
        bad = 1;
        __builtin_longjmp(entered,1);
    }
    if (round_no == 1) bad |= check_park_reclaim(s.exec_gen);
    previous_gen = s.exec_gen;
    ++observed;
    if (gui) exec_fault_recover();
    if (round_no == 4) ring3_abort_kill();
}
int probe(void)
{
    const struct shell_loop_ops ops = {gui_default, ready, loading, result, failure, delay_clear};
    kapi = &probe_api;
    appslot_init();
    if (!__builtin_setjmp(entered)) shell_loop_run(&ops);
    return bad || observed != 6;
}
