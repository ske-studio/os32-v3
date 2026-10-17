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
#include "appslot.h"

static KernelAPI backend;
KernelAPI *kapi = &backend;
static struct _reent reent;
struct _reent *_impure_ptr = &reent;
static struct os32_nano_arena primary;
static struct appmem_table table;
static struct appmem_layout layout;
static unsigned backend_maps, backend_unmaps, backend_exact_fail;
/* Host backing/owner model, independent of both extent and observer lists.
 * The real exec teardown walker runs against this model; guest PFNs/MMU and
 * ledger reclamation still require the NP21/W parent-mode acceptance run. */
static unsigned char owned[(MEM_APP_BAND_MAX_TOP - MEM_APP_BAND_BASE) / PAGE];
static u32 owner_pages, first_full_base, full_checks;
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
    for (uintptr_t va = base; va < base + bytes; va += PAGE) {
        unsigned i = (va - MEM_APP_BAND_BASE) / PAGE;
        CHECK(i < sizeof(owned) && !owned[i]);
        owned[i] = 1; owner_pages++;
    }
}
static unsigned release_backing(uintptr_t base, unsigned bytes)
{
    unsigned pages = 0;
    int result;
    if (!bytes) return 0;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(91), "b"(base), "c"(bytes) : "memory");
    CHECK(!result);
    for (uintptr_t va = base; va < base + bytes; va += PAGE) {
        unsigned i = (va - MEM_APP_BAND_BASE) / PAGE;
        CHECK(i < sizeof(owned));
        if (owned[i]) { owned[i] = 0; owner_pages--; pages++; }
    }
    return pages;
}
static int equal(const void *a, const void *b, unsigned bytes)
{
    const unsigned char *x = a, *y = b;
    while (bytes--) if (*x++ != *y++) return 0;
    return 1;
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
    int rc;
    struct appmem_table before = table;
    u32 owned_before = owner_pages;
    backend_unmaps++;
    rc = appmem_unmap_prepare(&table, (u32)base, (u32)base + bytes, APPMEM_PUBLIC_UNMAP_MASK, &p);
    if (rc) {
        CHECK(equal(&table, &before, sizeof(table)));
        CHECK(owner_pages == owned_before);
        if (rc == APPMEM_EFULL) {
            if (!first_full_base) first_full_base = (u32)base;
            full_checks++;
        }
        return rc == APPMEM_EFULL ? OS32_ERR_FULL : OS32_ERR_INVAL;
    }
    CHECK(release_backing((uintptr_t)base, bytes) == bytes / PAGE);
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
    CHECK(s.owner_pages == owner_pages);
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

/* Only the exit dependencies are modeled; exec_teardown_app is extracted
 * verbatim from exec.c by the runner. No allocator-list-driven cleanup. */
static u32 exec_as_leftover_pages, exit_diagnostics, exit_before;
static int retired, destroyed;
static u32 ledger_owner_pages(u32 owner) { CHECK(owner == 2); return owner_pages; }
static int lease_revoke_all(struct addrspace *as) { (void)as; return 0; }
static void shlib_addrspace_detach(struct addrspace *as) { (void)as; }
u32 paging_addrspace_free_user_range(struct addrspace *as, u32 base, u32 end)
{
    CHECK(as->owner == 2 && end >= base);
    return release_backing(base, end - base);
}
void paging_addrspace_destroy(struct addrspace *as) { as->pd_phys = 0; destroyed++; }
void paging_addrspace_poison(struct addrspace *as) { as->appmem_poisoned = 1; }
static int ledger_reclaim_owner(u32 owner, u32 *left)
{
    CHECK(owner == 2); *left = owner_pages; return 1;
}
static int ledger_owner_retire(u32 owner) { CHECK(owner == 2); retired++; return 1; }
static void exec_owner_diag(AppSlot *a, int exiting, u32 pages, u32 leftover)
{
    CHECK(a->as && exiting && !leftover);
    exit_before = pages; exit_diagnostics++;
}
static void kfree(void *p) { CHECK(p != NULL); }
static void serial_puts_polled(const char *s) { (void)s; CHECK(0); }
#define kprintf print
#include "allocator_teardown.inc"
#undef kprintf

static void retained_and_exit(void)
{
    if (first_full_base) {
        void *retained = NULL;
        for (unsigned i = 0; i < LARGE_COUNT; i++)
            if (blocks[i] && (uintptr_t)blocks[i] / PAGE == first_full_base / PAGE)
                retained = blocks[i];
        CHECK(retained != NULL);
        unsigned before = backend_unmaps, failures = full_checks;
        u32 pages_before = owner_pages;
        errno = 0;
        free(retained);
        CHECK(backend_unmaps == before + 1 && full_checks == failures + 1);
        CHECK(errno == ENOMEM);
        CHECK(owner_pages == pages_before);
    }
    struct addrspace as = {0};
    AppSlot app = {0};
    as.owner = 2; as.pd_phys = PAGE; as.appmem = table;
    app.cpl3 = 1; app.as = &as;
    app.load_addr = MEM_EXEC_LOAD_ADDR + PAGE;
    app.sbrk_heap_limit = MEM_EXEC_LOAD_ADDR + 2 * PAGE;
    app.exec_heap_base = MEM_EXEC_HEAP_BASE;
    app.exec_heap_size = MEM_EXEC_HEAP_MIN;
    u32 pages_before = owner_pages;
    CHECK(pages_before > 0);
    exec_teardown_app(&app);
    CHECK(!app.as && !app.cpl3 && destroyed == 1 && retired == 1);
    CHECK(exit_diagnostics == 1 && exit_before == pages_before);
    CHECK(!exec_as_leftover_pages && !owner_pages);
    for (unsigned i = 0; i < sizeof(owned); i++) CHECK(!owned[i]);
    say("HOST F9 retained-list/owner/exec-teardown PASS\n",
        sizeof("HOST F9 retained-list/owner/exec-teardown PASS\n") - 1);
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
    backing(table.e[1].base, table.e[1].end - table.e[1].base);
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
    if (!rc) retained_and_exit();
    die(rc);
}
__asm__(".global _start\n_start:\n mov %esp,%eax\n push %eax\n call host_start\n");
