/* Execute the real guest fixture + real nano + real extent policy under
 * Linux/qemu-user. Only page backing, MemStat and the clock are stand-ins.
 * Host times are format tests and MUST NOT be used as F-9 measurements. */
#include "nano_adapter.h"
#include "appmem.h"
#include "memmap.h"
#define main guest_main
#include "../../userland/tests/allocator_bench.c"
#undef main
#include <stdarg.h>

static KernelAPI backend;
KernelAPI *kapi = &backend;
static struct _reent reent;
struct _reent *_impure_ptr = &reent;
static struct os32_nano_arena primary;
static struct appmem_table table;
static struct appmem_layout layout;
static unsigned backend_maps, backend_unmaps, backend_exact_fail;
static uint64_t host_time = 0xffffff80ULL;
static int no_clock;
static int parent_mode, parent_step;
static const char *parent_case;
int *__errno(void) { return &reent._errno; }
void os32_nano_prefix_check(const void *p) { (void)p; }
void *memset(void *p, int c, size_t n) { unsigned char *q = p; while (n--) *q++ = c; return p; }
void *memcpy(void *p, const void *s, size_t n) { unsigned char *q = p; const unsigned char *r = s; while (n--) *q++ = *r++; return p; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static void say(const char *s, unsigned n)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void die(int code) __attribute__((noreturn));
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
#define CHECK(c) do { if (!(c)) { say("FAIL " #c "\n", sizeof("FAIL " #c "\n") - 1); die(1); } } while (0)
static void number(unsigned value)
{
    char buf[16]; unsigned pos = sizeof(buf);
    do { buf[--pos] = '0' + value % 10; value /= 10; } while (value);
    say(buf + pos, sizeof(buf) - pos);
}
static void print(u8 attr, const char *format, ...)
{
    va_list ap;
    unsigned arg_words = 2; /* attr + format in the real 64-byte CPL3 window */
    (void)attr;
    va_start(ap, format);
    while (*format) {
        if (*format != '%') { say(format++, 1); continue; }
        format++;
        CHECK(++arg_words <= 16);
        if (*format == 's') {
            const char *s = va_arg(ap, const char *);
            unsigned n = 0; while (s[n]) n++;
            say(s, n);
        } else if (*format == 'u') number(va_arg(ap, unsigned));
        else if (*format == 'd') {
            int n = va_arg(ap, int);
            if (n < 0) say("-", 1);
            number(n < 0 ? 0u - (unsigned)n : (unsigned)n);
        } else CHECK(0);
        format++;
    }
    va_end(ap);
}
static void backing(uintptr_t base, unsigned bytes)
{
    unsigned args[6] = {base, bytes, 3, 0x32, ~0u, 0};
    unsigned result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(90), "b"(args) : "memory");
    CHECK(result == base);
}
static void *map_pages(u32 bytes, void *hint, u32 flags)
{
    struct appmem_plan p;
    int rc;
    backend_maps++;
    rc = appmem_prepare(&table, &layout, bytes, (u32)hint, flags, APPMEM_ANON, 0, &p);
    if (rc) {
        if (flags & OS32_MEM_MAP_EXACT) backend_exact_fail++;
        return NULL;
    }
    backing(p.base, p.end - p.base);
    appmem_publish(&table, &p);
    return (void *)p.base;
}
static int unmap_pages(void *base, u32 bytes)
{
    struct appmem_unmap_plan p;
    int result, rc;
    backend_unmaps++;
    rc = appmem_unmap_prepare(&table, (u32)base, (u32)base + bytes, APPMEM_PUBLIC_UNMAP_MASK, &p);
    if (rc) return rc == APPMEM_EFULL ? OS32_ERR_FULL : OS32_ERR_INVAL;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(91), "b"(base), "c"(bytes) : "memory");
    CHECK(!result);
    appmem_unmap_publish(&table, &p);
    return 0;
}
static i32 stat_pages(i32 id, void *out, u32 bytes)
{
    MemStat s = {0};
    CHECK(id == -1 && bytes == sizeof(s));
    s.size = sizeof(s);
    s.app_id = 2;
    s.flags = MEMSTAT_HAS_AS;
    s.primary_mapped_end = layout.primary_mapped_end;
    s.sbrk_heap_limit = MEM_EXEC_LOAD_ADDR + 2 * PAGE;
    if (parent_mode) {
        s.phys_free_pages = 10000;
        s.owner_pages = 10;
        s.kheap_used = 100;
        s.resident_heap_used = 200;
        s.extents_total = 2;
        s.reclaim_count = parent_step;
        if (parent_step && !strcmp(parent_case, "parent-leak")) s.phys_free_pages--;
        if (parent_step && !strcmp(parent_case, "parent-leftover")) s.leftover_pages++;
        if (parent_step && !strcmp(parent_case, "parent-no-reclaim")) s.reclaim_count = 0;
        memcpy(out, &s, sizeof(s));
        return sizeof(s);
    }
    for (unsigned i = 0; i < APPMEM_EXTENT_MAX; i++) if (table.e[i].base) {
        s.extents_total++;
        s.extents[table.e[i].kind - 1]++;
        s.owner_pages += (table.e[i].end - table.e[i].base) / PAGE;
    }
    s.extents_free = APPMEM_EXTENT_MAX - s.extents_total;
    memcpy(out, &s, sizeof(s));
    return sizeof(s);
}
static int time_now(u32 *lo, u32 *hi)
{
    if (no_clock) return OS32_ERR_NOSYS;
    host_time += 64;
    *lo = host_time; *hi = host_time >> 32;
    return 0;
}
static int grow(void *opaque, uintptr_t base, size_t bytes)
{
    (void)opaque;
    return kapi->mem_map(bytes, (void *)base, OS32_MEM_MAP_EXACT) == (void *)base ? 0 : -1;
}
static void *map_arena(void *opaque, size_t bytes, unsigned flags)
{
    (void)opaque;
    return kapi->mem_map(bytes, NULL, flags);
}
static int unmap_arena(void *opaque, uintptr_t base, size_t bytes)
{
    (void)opaque;
    return kapi->mem_unmap((void *)base, bytes);
}
static int child_run(const char *command)
{
    CHECK(command[0] == '/');
    parent_step++;
    return !strcmp(parent_case, "parent-failure") ? 1 : 0;
}
static int last_result(int *kind, int *code)
{
    *kind = EXEC_KIND_EXITED;
    *code = !strcmp(parent_case, "parent-failure") ? 1 : 0;
    return 0;
}
void host_start(unsigned *stack)
{
    int argc = stack[0];
    char **argv = (char **)(stack + 1);
    CHECK(argc == 2 || argc == 3);
    no_clock = argc == 3;
    parent_case = argv[1];
    parent_mode = parent_case[0] == 'p';
    backend.mem_stat = stat_pages;
    backend.kprintf = print;
    if (parent_mode) {
        backend.exec_run = child_run;
        backend.exec_last_result = last_result;
        die(guest_main(1, argv, &backend));
    }
    layout = (struct appmem_layout){MEM_EXEC_LOAD_ADDR + PAGE,
        MEM_EXEC_LOAD_ADDR + PAGE, MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN,
        MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE};
    table.e[0] = (struct appmem_extent){MEM_EXEC_LOAD_ADDR + PAGE, MEM_EXEC_LOAD_ADDR + 2 * PAGE, APPMEM_LIBC_INITIAL, 0};
    table.e[1] = (struct appmem_extent){MEM_EXEC_HEAP_BASE, layout.exec_heap_cur_end, APPMEM_EXEC_INITIAL, 0};
    backing(table.e[0].base, PAGE);
    primary.initial = primary.brk = table.e[0].base;
    primary.mapped_end = table.e[0].end;
    primary.limit = UINTPTR_MAX;
    primary.grow_exact = grow;
    CHECK(os32_nano_configure(&primary, map_arena, unmap_arena, NULL));
    backend.mem_map = map_pages;
    backend.mem_unmap = unmap_pages;
    backend.mem_stat = stat_pages;
    backend.sys_time_now = time_now;
    backend.kprintf = print;
    int rc = guest_main(2, argv, &backend);
    /* The only bypassed map is the explicit small-case primary obstacle. */
    unsigned setup = !strcmp(argv[1], "small16") || !strcmp(argv[1], "small64") || !strcmp(argv[1], "small256");
    CHECK(backend_maps == map_calls + setup);
    CHECK(backend_unmaps == unmap_calls && backend_exact_fail == exact_fail);
    CHECK(!observer_bad);
    die(rc);
}
__asm__(".global _start\n_start:\n mov %esp,%eax\n push %eax\n call host_start\n");
