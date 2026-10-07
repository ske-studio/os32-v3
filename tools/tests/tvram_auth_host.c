/* Real loader policy, saved USER identity and generated disk wrappers. */
#define HOST_CALLER_COPY_TEST
#define appslot_get walk_appslot_get
#include "setup_host_source.c"
#undef appslot_get
#undef used
static AppSlot shell_slot = { .state = APP_STATE_RUNNING };
AppSlot *appslot_get(int id)
{
    return id == APP_ID_SHELL ? &shell_slot : walk_appslot_get(id);
}
#include "ring3_str.h"
#include "ring3_str.c"
volatile int ring3_in_syscall = 1;
#define RING3_USTACK_TOP MEM_APP_STACK_TOP
#define RING3_STACK_BOTTOM (MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE)
#define RING3_HEAP_TOP (RING3_STACK_BOTTOM - PAGE_SIZE)
#include "exec_host_source.c"
#include "config.h"
static int gui_mode;
int con_sink_is_enabled(void) { return gui_mode; }
int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
char *kstrncpy(char *d, const char *s, u32 n)
{
    u32 i = 0;
    if (n) { while (i + 1 < n && s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
    return d;
}
#include "resolve_host_source.c"
/* Keep integrated caller checks by default. The focused policy case isolates
 * the ring3 CPL guard from redir_live's independent check of the same bit. */
static int assume_valid_user;
static int tvram_caller_access_get_user(struct caller_access *caller)
{
    return assume_valid_user ? 1 : caller_access_get_user(caller);
}
#define caller_access_get_user tvram_caller_access_get_user
#include "disk_host_source.c"
#undef caller_access_get_user
#include "generated_host_source.c"
static int reached;
void ring3_fault_kill(void) { SAY("FAIL: unexpected pointer fault"); die(1); }
void tvram_readchar_at(int x, int y, u16 *code, u8 *attr)
{ (void)x; (void)y; reached++; *code = 0x2341; *attr = 0xe1; }
static void reads(int allowed)
{
    u16 *code = (void *)MEM_EXEC_LOAD_ADDR;
    u8 *attr = (void *)(MEM_EXEC_LOAD_ADDR + 8);
    *code = 0xffff; *attr = 0xff; reached = 0;
    wrap_tvram_readchar_at(2, 3, code, attr);
    CHECK(*code == (allowed ? 0x2341 : 0));
    CHECK(*attr == (allowed ? 0xe1 : 0));
    CHECK(reached == allowed);
}
static void caller_copy_tests(void)
{
    u32 args[6] = {MEM_EXEC_LOAD_ADDR, PAGE_SIZE, 3, 0x32, 0xffffffffU, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == MEM_EXEC_LOAD_ADDR);
    (void)exec_disk_write_path_allowed;
    CallerAccessFrame previous;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    reads(1);
    slot.gui = 1; reads(0); slot.gui = 0;
    gui_mode = 1; reads(0); gui_mode = 0;
    ring3_wm_depth = 1; reads(0); ring3_wm_depth = 0;
    slot.state = APP_STATE_PARKED;
    CHECK(!exec_tvram_read_allowed()); slot.state = APP_STATE_RUNNING;
    slot.cpl3 = 0; CHECK(!exec_tvram_read_allowed()); slot.cpl3 = 1;
    assume_valid_user = 1;
    CHECK(exec_tvram_read_allowed());
    slot.cpl3 = 0; CHECK(!exec_tvram_read_allowed()); slot.cpl3 = 1;
    assume_valid_user = 0;
    resource_owner = APP_ID_SHELL; CHECK(!exec_tvram_read_allowed()); resource_owner = 2;
    host_cr3++; CHECK(!exec_tvram_read_allowed()); host_cr3--;
    space.generation++; CHECK(!exec_tvram_read_allowed()); space.generation--;
    caller_access_invalidate(); CHECK(!exec_tvram_read_allowed());
    CHECK(caller_access_enter(&previous, CALLER_USER)); reads(1);
    /* Trusted paths can validate outputs without a USER frame. */
    ring3_in_syscall = 0; reads(0);
    wrap_tvram_readchar_at(0, 0, 0, 0); /* trusted optional outputs */
    CHECK(reached == 0);
    current = resource_owner = APP_ID_SHELL; reads(1);
    /* Isolate current identity and resource ownership in the CPL0 branch. */
    resource_owner = 2; reads(0); resource_owner = APP_ID_SHELL;
    current = 2; slot.cpl3 = 0; reads(0); slot.cpl3 = 1;
    current = APP_ID_SHELL;
    shell_slot.cpl3 = 1; reads(0); shell_slot.cpl3 = 0;
    shell_slot.state = APP_STATE_PARKED; reads(0); shell_slot.state = APP_STATE_RUNNING;
    gui_mode = 1; reads(0); gui_mode = 0;
    ring3_wm_depth = 1; reads(0); ring3_wm_depth = 0;
    current = resource_owner = 2; ring3_in_syscall = 1;
    caller_access_leave(&previous);
    SAY("PASS: TVRAM public CUI authorization and zero fill");
}
