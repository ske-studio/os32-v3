#define HOST_CALLER_COPY_TEST
#include "setup_host_source.c"
#undef used
#include "kapi_print.h"
#include "kprintf.h"
#include "kmalloc.h"
#include "os32_kapi_slots.h"

static char output[8192];
static u32 output_size, output_calls;
static u8 output_attr;
static char heap[KAPI_PRINT_ARG_LIMIT][KAPI_PRINT_STRING_CAP];
static int allocated[KAPI_PRINT_ARG_LIMIT], allocation_count, allocation_fail;

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void emit(const char *s, u32 n, u8 attr)
{
    CHECK(output_size + n < sizeof(output));
    for (u32 i = 0; i < n; i++) output[output_size++] = s[i];
    output[output_size] = 0;
    output_attr = attr;
    output_calls++;
}
void shell_print(const char *s, u8 attr)
{
    u32 n = 0;
    while (s[n]) n++;
    emit(s, n, attr);
}
void shell_print_utf8(const char *s, u8 attr) { shell_print(s, attr); }
void console_write(const char *s, u32 n, u8 attr) { emit(s, n, attr); }
void *kmalloc(u32 size)
{
    CHECK(size == KAPI_PRINT_STRING_CAP);
    if (allocation_fail && ++allocation_count == allocation_fail) return 0;
    for (u32 i = 0; i < KAPI_PRINT_ARG_LIMIT; i++) {
        if (!allocated[i]) { allocated[i] = 1; return heap[i]; }
    }
    CHECK(0);
    return 0;
}
void kfree(void *p)
{
    for (u32 i = 0; i < KAPI_PRINT_ARG_LIMIT; i++) {
        if (p == heap[i]) { CHECK(allocated[i]); allocated[i] = 0; return; }
    }
    CHECK(0);
}

#include "format_host_source.c"
#include "attr_host_source.c"
#include "print_host_source.c"

static void *kill_escape[5];
void ring3_fault_kill(void) { __builtin_longjmp(kill_escape, 1); }
static int dispatch_print(u32 va)
{
    u32 frame[8] = {0}, slot = KAPI_SLOT_KPRINTF;
    const void *args_src = (const void *)(uptr)va;
    if (__builtin_setjmp(kill_escape)) return 0;
    /* Actual USER dispatch branch, including refusal -> kill ordering. */
#include "dispatch_host_source.c"
    CHECK(0);
syscall_complete:
    CHECK(frame[7] == 0);
    return 1;
}

static void reset_output(void)
{
    output_size = output_calls = allocation_count = 0;
    output[0] = 0;
}
static void invoke(u32 args, int accepted, const char *expected)
{
    u32 root = host_cr3, flags = host_arch_if;
    reset_output();
    CHECK(dispatch_print(args) == accepted);
    CHECK(host_cr3 == root && host_arch_if == flags);
    CHECK(equal(output, expected));
    if (!accepted) CHECK(!output_calls);
    else if (*expected) CHECK(output_attr == 0xe1);
    for (u32 i = 0; i < KAPI_PRINT_ARG_LIMIT; i++) CHECK(!allocated[i]);
}

static void caller_copy_tests(void)
{
    CallerAccessFrame previous;
    u32 va = MEM_EXEC_LOAD_ADDR, tail = va + PAGE_SIZE;
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 index = (va >> PAGE_SHIFT) % PTE_COUNT;
    u8 *data = P2V(payload);
    u32 *args = (u32 *)data;
    char *fmt = (char *)data + 512;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    /* Deliberately different bytes at the host VA and checked physical page.
     * A raw-pointer mutant compiles and reaches an assertion, not a fault. */
    u32 map_args[6] = {va, PAGE_SIZE * 2, 3, 0x32, ~0U, 0}, mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(map_args) : "memory");
    CHECK(mapped == va);
    for (u32 i = 0; i < PAGE_SIZE * 2; i++) ((u8 *)(uptr)va)[i] = 0;
    kmemcpy((void *)(uptr)(va + 512), "RAW", sizeof("RAW"));
    kmemcpy((void *)(uptr)(va + 1024), "RAW", sizeof("RAW"));
    ((u32 *)(uptr)va)[0] = 7;
    ((u32 *)(uptr)va)[1] = va + 512;
    ((u32 *)(uptr)va)[2] = 99;

    for (u32 f = 0; f < 2; f++) {
        host_arch_if = f ? 0x202 : 2;
        args[0] = 7; args[1] = va + 512; args[2] = 42;
        kmemcpy(fmt, "literal %d %%", sizeof("literal %d %%"));
        invoke(va, 1, "literal 42 %");
        kmemcpy(fmt, "prefix %s", sizeof("prefix %s"));
        args[2] = va + 1024;
        kmemcpy(data + 1024, "UTF-8: \xe3\x81\x82", sizeof("UTF-8: \xe3\x81\x82"));
        invoke(va, 1, "prefix UTF-8: \xe3\x81\x82");
        /* Page-final NUL: neither format nor %s probes the missing page. */
        args[1] = tail - 4;
        kmemcpy(data + PAGE_SIZE - 4, "%s!", sizeof("%s!"));
        invoke(va, 1, "UTF-8: \xe3\x81\x82!");
        args[1] = va + 512;
        kmemcpy(fmt, "[%s]", sizeof("[%s]"));
        args[2] = tail - 4;
        kmemcpy(data + PAGE_SIZE - 4, "abc", sizeof("abc"));
        invoke(va, 1, "[abc]");
        data[PAGE_SIZE - 1] = 'x';
        invoke(va, 0, "");
        args[1] = tail - 4;
        invoke(va, 0, "");
        /* NUL at format cap-1 succeeds; cap bytes without NUL fail. */
        args[1] = va + 512;
        for (u32 i = 0; i < KAPI_PRINT_FORMAT_CAP; i++) fmt[i] = 'x';
        fmt[KAPI_PRINT_FORMAT_CAP - 1] = 0;
        reset_output();
        CHECK(kapi_print_call((void *)(uptr)va));
        CHECK(output_size == KAPI_PRINT_FORMAT_CAP - 1);
        fmt[KAPI_PRINT_FORMAT_CAP - 1] = 'x';
        invoke(va, 0, "");
        /* A string can span physically separate checked pages. */
        CHECK(!paging_addrspace_map_user(&space, va + PAGE_SIZE,
                                         payload + PAGE_SIZE, PAGE_RW | PTE_USER));
        args[1] = va + 512; args[2] = va + 1024;
        kmemcpy(fmt, "%s", sizeof("%s"));
        for (u32 i = 0; i < KAPI_PRINT_STRING_CAP; i++) data[1024 + i] = 's';
        data[1024 + KAPI_PRINT_STRING_CAP - 1] = 0;
        reset_output();
        CHECK(kapi_print_call((void *)(uptr)va));
        CHECK(output_size == KAPI_PRINT_STRING_CAP - 1);
        for (u32 i = 0; i < output_size; i++) CHECK(output[i] == 's');
        for (u32 i = 0; i < KAPI_PRINT_ARG_LIMIT; i++) CHECK(!allocated[i]);
        data[1024 + KAPI_PRINT_STRING_CAP - 1] = 's';
        invoke(va, 0, "");
        pt[index + 1] = 0;
        /* No argument beyond 64B; %% and a final % use no word. */
        kmemcpy(fmt, "%d%d%d%d%d%d%d%d%d%d%d%d%d%d%%", sizeof("%d%d%d%d%d%d%d%d%d%d%d%d%d%d%%"));
        for (u32 i = 0; i < 14; i++) args[2 + i] = 1;
        invoke(va, 1, "11111111111111%");
        kmemcpy(fmt + 28, "%d", sizeof("%d"));
        invoke(va, 0, "");
        /* Fixed words and the last used variable word may end at page end. */
        u32 *end_args = (u32 *)(data + PAGE_SIZE - 12);
        kmemcpy(fmt, "%d%%", sizeof("%d%%"));
        end_args[0] = 7; end_args[1] = va + 512; end_args[2] = 9;
        invoke(tail - 12, 1, "9%");
        kmemcpy(fmt, "ok%", sizeof("ok%"));
        end_args[1] = 7; end_args[2] = va + 512;
        invoke(tail - 8, 1, "ok%");
        kmemcpy(fmt, "%d", sizeof("%d"));
        invoke(tail - 8, 0, "");
        invoke(tail - 4, 0, "");
        invoke(~0U - 3, 0, "");
        /* NULL %s is printable, NULL format is refused. */
        args[1] = va + 512; args[2] = 0;
        kmemcpy(fmt, "%s", sizeof("%s"));
        invoke(va, 1, "(null)");
        args[1] = 0;
        invoke(va, 0, "");
        args[1] = va + 512;
        /* A later bad string cannot emit an earlier literal/valid string. */
        kmemcpy(fmt, "prefix %s %s", sizeof("prefix %s %s"));
        args[2] = va + 1024; args[3] = tail;
        invoke(va, 0, "");
        args[3] = va + 1024;
        allocation_fail = 2;
        invoke(va, 0, "");
        allocation_fail = 0;
        /* Existing renderer grammar, including unknown conversion slots. */
        kmemcpy(fmt, "%02u|%4d|%-4s|%lX|%c|%q|%d|%%|%", sizeof("%02u|%4d|%-4s|%lX|%c|%q|%d|%%|%"));
        kmemcpy(data + 1024, "hi", sizeof("hi"));
        args[2] = 5; args[3] = (u32)-42; args[4] = va + 1024;
        args[5] = 0xab; args[6] = 'C'; args[7] = 0; args[8] = 3;
        invoke(va, 1, "05| -42|hi  |AB|C|q|3|%|%");
        /* Incomplete modifiers must not read past the staged format. */
        kmemcpy(fmt, "x%0", sizeof("x%0"));
        invoke(va, 1, "x");
        space.generation++;
        invoke(va, 0, "");
        space.generation--;
    }
    caller_access_leave(&previous);
    invoke(va, 0, "");
    /* Internal/CPL0 logging uses the original variadic entry without USER
     * identity, staging allocation, or the public input bounds. */
    allocation_fail = 1;
    reset_output();
    kprintf(7, "trusted %02u %s %%", 5u, "ok");
    CHECK(equal(output, "trusted 05 ok %") && !allocation_count);
    allocation_fail = 0;
    SAY("PASS kapi print: B1 format/string/words, no partial output, cleanup, IF/CR3, renderer");
}
