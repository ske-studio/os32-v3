/* Real guest main, freestanding ILP32 Linux pipe boundary. No host libc. */
#include <stdarg.h>
#define main h5_guest_main
#include "h5_fixture.c"
#undef main

static u8 backing[H5_TARGET_PAGES * MEM_PAGE_SIZE];
static void *held;
static u32 held_bytes, initial_free, initial_used;
static int phase, reads, allocs, frees, failure, bad_snapshot, invariant_failed;

static int starts(const char *s, const char *prefix)
{
    while (*prefix) if (*s++ != *prefix++) return 0;
    return 1;
}

static i32 stat_api(i32 id, void *out, u32 bytes)
{
    MemStat *s = out;
    if (id != -1 || bytes != sizeof(*s)) invariant_failed = 1;
    for (u32 i = 0; i < bytes; i++) ((u8 *)out)[i] = 0;
    s->size = sizeof(*s);
    s->phys_free_pages = initial_free;
    s->exec_heap_used = initial_used + held_bytes;
    s->extents[H5_LARGE_INDEX] = held != 0 && held_bytes >= MEM_EXEC_HEAP_MIN && !bad_snapshot;
    return sizeof(*s);
}

static void *alloc_api(u32 bytes)
{
    allocs++;
    if (phase != 1 || held || bytes % MEM_PAGE_SIZE || !reads || bytes > sizeof(backing))
        invariant_failed = 1;
    if (failure || bytes > sizeof(backing)) return 0;
    held = backing;
    held_bytes = bytes;
    return held;
}

static void free_api(void *ptr)
{
    frees++;
    if (phase != 2 || ptr != held) invariant_failed = 1;
    held = 0;
    held_bytes = 0;
}

static int io(int op, int fd, void *buffer, u32 bytes)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(op), "b"(fd), "c"(buffer), "d"(bytes) : "memory");
    return rc;
}

static void print_api(u8 attr, const char *fmt, ...)
{
    char buffer[256], digits[16];
    u32 length = 0;
    va_list ap;
    (void)attr;
    if (starts(fmt, "PREP free1=")) phase = 1;
    if (starts(fmt, "ALLOC ")) {
        phase = 2;
        if (held && held_bytes >= MEM_EXEC_HEAP_MIN && !bad_snapshot) {
            for (u32 i = 0; i < held_bytes; i += MEM_PAGE_SIZE)
                if (((u8 *)held)[i] != H5_PATTERN) invariant_failed = 1;
        }
    }
    va_start(ap, fmt);
    for (; *fmt && length < sizeof(buffer) - 16; fmt++) {
        if (*fmt != '%') buffer[length++] = *fmt;
        else {
            u32 n = va_arg(ap, u32), count = 0;
            fmt++;
            if (*fmt == 'd' && (i32)n < 0) {
                buffer[length++] = '-';
                n = 0U - n;
            }
            do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
            while (count) buffer[length++] = digits[--count];
        }
    }
    va_end(ap);
    if (io(4, 1, buffer, length) != (int)length) invariant_failed = 1;
}

static int read_api(int fd, void *out, u32 bytes)
{
    if (fd != 0 || bytes != 1 || !phase) invariant_failed = 1;
    if (phase == 1 && (held || allocs)) invariant_failed = 1;
    if (phase == 2 && allocs && !failure && !held) invariant_failed = 1;
    reads++;
    return io(3, fd, out, bytes);
}

int host_main(int argc, char **argv)
{
    KernelAPI api = {0};
    char **environment = argv + argc + 1;
    int rc;
    initial_free = 600;
    initial_used = MEM_EXEC_HEAP_MIN;
    for (; *environment; environment++) {
        const char *s = *environment;
        if (starts(s, "H5_FREE1=")) h5_number(s + 9, &initial_free);
        if (starts(s, "H5_ALLOC_FAIL=")) failure = 1;
        if (starts(s, "H5_BAD_SNAPSHOT=")) bad_snapshot = 1;
    }
    api.mem_stat = stat_api;
    api.mem_alloc = alloc_api;
    api.mem_free = free_api;
    api.kprintf = print_api;
    api.sys_read = read_api;
    rc = h5_guest_main(argc, argv, &api);
    if (held || allocs > 1 || frees != (allocs && !failure) || invariant_failed) {
        char message[] = "host invariant failed\n";
        io(4, 2, message, sizeof(message) - 1);
        return 2;
    }
    return rc;
}

__asm__(".global _start\n_start:\n"
        "mov (%esp),%eax\nlea 4(%esp),%edx\npush %edx\npush %eax\n"
        "call host_main\nmov %eax,%ebx\nmov $1,%eax\nint $0x80\n");
