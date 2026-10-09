/* Execute the actual guest fixtures with a scripted KAPI transport. */
#define main fixture_main
#define malloc fixture_malloc
#define free fixture_free
#ifdef TEST_BACK
#include "trim_back_source.c"
#else
#include "trim_front_source.c"
#endif
#undef main
#undef malloc
#undef free

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
#define REQUIRE(x) do { if (!(x)) finish(1); } while (0)
static int contains(const char *s, const char *part)
{
    for (; *s; s++) {
        const char *a = s, *b = part;
        while (*b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
char *strcpy(char *d, const char *s)
{
    char *p = d;
    while ((*p++ = *s++)) { }
    return d;
}
unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long n = 0;
    (void)base;
    while (*s >= '0' && *s <= '9') n = n * 10 + (unsigned)(*s++ - '0');
    *end = (char *)s;
    return n;
}
static KernelAPI api;
#ifdef TEST_BACK
static unsigned focus_calls, ticks, focus_printed, trims, frees;
static int focus_rc;
static unsigned char live[NANO_KEEP][BLOCK_BYTES];
static int gui_mock(u32 op, u32 arg)
{
    REQUIRE(op == GUI_OP_WIN_SET_FOCUS && arg == 42 && !ticks);
    focus_calls++;
    return focus_rc;
}
static u32 tick_mock(void)
{
    REQUIRE(!stop_focus || (focus_calls == 1 && focus_printed == 1));
    return ticks++;
}
static void print_mock(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (contains(fmt, "focus ok")) { REQUIRE(!focus_rc); focus_printed++; }
}
void fixture_free(void *p) { (void)p; frees++; }
uint32_t os32_nano_trim(void) { trims++; return 1; }
static void check_hook(int focus, int error)
{
    stop_focus = focus; focus_rc = error;
    focus_calls = ticks = focus_printed = trims = frees = 0;
    armed = 1; released = bad = hooks = 0; slow_ticks = 3; own_window = 42;
    keep = count = 0;
    for (u32 i = 0; i < NANO_KEEP; i++) nano[i] = live[i];
    baseline_crc = data_crc();
    REQUIRE(trim_hook() == (error ? 0U : 1U));
    REQUIRE(hooks == 1 && focus_calls == (unsigned)focus);
    REQUIRE(error ? (bad && !released && !ticks && !trims && !frees) :
                   (!bad && released && ticks >= 4 && trims == 1 && frees == NANO_COUNT - NANO_KEEP));
}
void _start(void)
{
    api.gui_call = gui_mock; api.get_tick = tick_mock; api.kprintf = print_mock;
    back_api = &api;
    check_hook(0, 0); check_hook(1, 0); check_hook(1, -1);
    finish(0);
}
#else
static unsigned char shared[GUI_SLOT_SIZE];
static volatile GuiSlotHeader *header = (volatile GuiSlotHeader *)shared;
static volatile GuiEvent *events = (volatile GuiEvent *)(shared + GUI_SLOT_RING_OFF);
static unsigned polls, waits, preps, done, unsettled, target_calls, retry_count;
static int in_prepare, after_wait;
static void print_mock(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (contains(fmt, "prep begin")) { preps++; in_prepare = 1; after_wait = 0; }
    if (contains(fmt, "PREP OK")) in_prepare = 0;
    if (contains(fmt, "prep done")) { REQUIRE(after_wait && waits == 3); done++; }
    if (contains(fmt, "prep unsettled")) unsettled++;
}
static i32 stat_mock(i32 id, void *out, u32 bytes)
{
    MemStat *s = out;
    REQUIRE(id == -1 && bytes == sizeof(*s));
    *s = (MemStat){0};
    s->phys_free_pages = after_wait && waits == 1 ? 30 : 0;
    s->trim_pending_mask = after_wait && waits == 2 ? 8 : 0;
    return sizeof(*s);
}
static void *map_mock(u32 bytes, void *hint, u32 flags)
{
    (void)bytes; (void)hint; (void)flags;
    REQUIRE(in_prepare);
    return 0;
}
void *fixture_malloc(size_t bytes)
{
    REQUIRE(bytes == BLOCK_BYTES);
    if (in_prepare) return 0;
    REQUIRE(done == 1 && header->ring_head == header->ring_tail);
    target_calls++; retry_count++;
    return shared;
}
void os32_gui_retry_enable(void) { }
struct os32_gui_retry_stats os32_gui_retry_stats(void)
{
    struct os32_gui_retry_stats s = {0}; s.retry_count = retry_count; return s;
}
static void key(unsigned ch, unsigned down)
{
    GuiEvent ev = {0}; ev.kind = GUI_EV_KEY; ev.sub = down; ev.payload.key.ch = ch;
    events[header->ring_tail++ % GUI_RING_CAPACITY] = ev;
}
static int gui_mock(u32 op, u32 arg)
{
    if (op == GUI_OP_INIT) return 0;
    if (op == GUI_OP_WIN_CREATE) return 42;
    if (op == GUI_OP_WIN_DESTROY) return 0;
    if (op == GUI_OP_WAIT) { waits++; after_wait = 1; REQUIRE(waits > 3 || arg == 1); return 0; }
    REQUIRE(op == GUI_OP_POLL);
    polls++;
    if (polls == 2 || polls == 3) { key('p', 1); key('p', 0); }
    if (polls == 4) { key('t', 1); key('t', 0); key('x', 1); key('x', 0); }
    if (polls == 5) key('q', 1);
    REQUIRE(polls <= 5);
    return 0;
}
void _start(void)
{
    api.gui_call = gui_mock; api.mem_stat = stat_mock; api.mem_map = map_mock;
    api.kprintf = print_mock; api.shm_base = (u32)shared - GUI_SHM_OFFSET;
    front_api = &api;
    REQUIRE(run_gui() == 0);
    REQUIRE(preps == 3 && done == 1 && unsettled == 2 && target_calls == 1 && retry_count == 1);
    finish(0);
}
#endif
