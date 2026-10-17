/* Test the real guest fixture's verdicts with controlled KAPI observations.
 * This checks the observer, not kernel reclamation (PM runs that in NP21/W). */
#define main guest_main
#include "../../userland/tests/layout_cycle_test.c"
#undef main
#include "../../userland/lib/rt/dbgserial.c"

static u8 libc_bytes[PROBE_BYTES], exec_bytes[PROBE_BYTES];
static KernelAPI api;
static int calls, kind, code, corrupt, leak, wrong_kind;
static const char *want_command;
static int command_bad;
void *malloc(size_t size) { return size == PROBE_BYTES ? libc_bytes : 0; }
void free(void *p) { (void)p; }
void *memset(void *p, int c, size_t n) { u8 *q = p; while (n--) *q++ = c; return p; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
int strncmp(const char *a, const char *b, size_t n) { while (n && *a && *a == *b) { a++; b++; n--; } return n ? (u8)*a - (u8)*b : 0; }
static void *__cdecl alloc(u32 size) { return size == PROBE_BYTES ? exec_bytes : 0; }
static void __cdecl release(void *p) { (void)p; }
static void __cdecl print(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
static int __cdecl write_result(int fd, const void *buf, u32 size) { (void)fd; (void)buf; return size; }
static int __cdecl exec(const char *command)
{
    calls++;
    if (strcmp(command, want_command)) command_bad = 1;
    if (corrupt == 1) bss_probe[1]++;
    if (corrupt == 2) libc_bytes[1]++;
    if (corrupt == 3) exec_bytes[1]++;
    return code;
}
static int __cdecl result(int *k, int *c) { *k = wrong_kind ? EXEC_KIND_INVALID : kind; *c = code; return 0; }
static i32 __cdecl stat(i32 id, void *out, u32 size)
{
    MemStat *s = out;
    if (id != -1 || size != sizeof(*s)) return -1;
    memset(s, 0, sizeof(*s));
    s->phys_free_pages = 1000;
    s->owner_pages = 30;
    s->exec_heap_used = PROBE_BYTES;
    s->resident_heap_used = 500;
    s->kheap_used = 100;
    if (calls) {
        switch (leak) {
        case 1: s->phys_free_pages--; break;
        case 2: s->owner_pages++; break;
        case 3: s->exec_heap_used++; break;
        case 4: s->resident_heap_used++; break;
        case 5: s->kheap_used++; break;
        case 6: s->extents_total++; break;
        case 7: s->leftover_pages++; break;
        case 8: s->lease_active++; break;
        }
    }
    return sizeof(*s);
}
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define VERIFY(c) do { if (!(c)) finish(1); } while (0)
void _start(void)
{
    VERIFY(_dbg_is_blacklisted(MEM_STACK_GUARD));
    VERIFY(_dbg_is_blacklisted(MEM_SHELL_GUARD));
    VERIFY(_dbg_is_blacklisted(MEM_FIXED_PAGING_GUARD_BASE));
    VERIFY(!_dbg_is_blacklisted(MEM_SHELL_HEAP_BASE));
    VERIFY(!_dbg_is_blacklisted(MEM_SHLIB_BASE));
    api.mem_alloc = alloc; api.mem_free = release; api.kprintf = print;
    api.sys_write = write_result;
    api.exec_run = exec; api.exec_last_result = result; api.mem_stat = stat;
    char *argv[] = {"layout_cycle_test", "exit", 0};
    const char *modes[] = {"exit", "fault", "stop", "nested-exit", "nested-fault", "nested-stop"};
    const char *commands[] = {SELF " --leaf", "/usr/bin/faulttest.bin ud", "/usr/bin/faulttest.bin loop",
        SELF " --once exit", SELF " --once fault", SELF " --once stop"};
    for (int m = 0; m < 6; m++) {
        argv[1] = (char *)modes[m]; want_command = commands[m];
        kind = m == 1 ? EXEC_KIND_FAULT : m == 2 ? EXEC_KIND_ABORTED : EXEC_KIND_EXITED;
        code = m == 1 || m == 2 ? EXEC_ERR_FAULT : 0;
        calls = 0;
        VERIFY(guest_main(2, argv, &api) == 0 && calls == 20 && !command_bad);
    }
    argv[1] = "exit"; want_command = commands[0]; kind = EXEC_KIND_EXITED; code = 0;
    for (corrupt = 1; corrupt <= 3; corrupt++) {
        calls = 0;
        VERIFY(guest_main(2, argv, &api) == 1 && calls == 1);
    }
    corrupt = 0;
    for (leak = 1; leak <= 8; leak++) {
        calls = 0;
        VERIFY(guest_main(2, argv, &api) == 1 && calls == 1);
    }
    leak = 0; wrong_kind = 1; calls = 0;
    VERIFY(guest_main(2, argv, &api) == 1 && calls == 1);
    wrong_kind = 0; calls = 0;
    char *once[] = {"layout_cycle_test", "--once", "exit", 0};
    VERIFY(guest_main(3, once, &api) == 0 && calls == 1);
    finish(0);
}
