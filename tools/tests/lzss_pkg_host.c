/* Real guest PKG parser/extractor; only KAPI I/O is replaced. [C1] GNU11. */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}
static size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
#include "userland/lib/rt/pkg.c"

_Static_assert(sizeof(u32) == 4, "guest ILP32 required");
_Static_assert(sizeof(PkgHeader) == PKG_HEADER_SIZE, "wire header layout");
static int linux_call(int n, u32 a, u32 b, u32 c)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}
static void die(int code)
{
    linux_call(1, (u32)code, 0, 0);
    for (;;) {}
}
static void say(const char *s) { linux_call(4, 2, (u32)s, strlen(s)); }
#define CHECK(x) do { if (!(x)) { \
    say("FAIL: " #x "\n"); say(pkg_path); say("\n"); die(1); \
} } while (0)

static const char *pkg_path = "arguments", *expected_dir;
static int input = -1, expected = -1;
static unsigned files_written;
static unsigned char heap[32 * 1024 * 1024];
static u32 heap_used;

static int f_open(const char *path, int mode)
{
    if (mode == KAPI_O_RDONLY) {
        CHECK(input == -1 && strcmp(path, pkg_path) == 0);
        input = linux_call(5, (u32)path, 0, 0);
        CHECK(input >= 0);
        return 1;
    }
    char host_path[4096];
    size_t n = strlen(expected_dir), m = strlen(path);
    CHECK(expected == -1 && (mode & KAPI_O_WRONLY));
    CHECK(n + m < sizeof(host_path));
    memcpy(host_path, expected_dir, n);
    memcpy(host_path + n, path, m + 1);
    expected = linux_call(5, (u32)host_path, 0, 0);
    CHECK(expected >= 0);
    files_written++;
    return 2;
}

static void f_close(int fd)
{
    if (fd == 1) {
        CHECK(input >= 0 && linux_call(6, input, 0, 0) == 0);
        input = -1;
    } else {
        unsigned char byte;
        CHECK(fd == 2 && expected >= 0);
        CHECK(linux_call(3, expected, (u32)&byte, 1) == 0);
        CHECK(linux_call(6, expected, 0, 0) == 0);
        expected = -1;
    }
}

static int f_read(int fd, void *buf, u32 size)
{
    CHECK(fd == 1 && input >= 0);
    return linux_call(3, input, (u32)buf, size);
}

static int f_write(int fd, const void *buf, u32 size)
{
    const unsigned char *bytes = buf;
    unsigned char want[4096];
    CHECK(fd == 2 && expected >= 0);
    for (u32 off = 0; off < size;) {
        u32 n = size - off;
        if (n > sizeof(want)) n = sizeof(want);
        CHECK(linux_call(3, expected, (u32)want, n) == (int)n);
        for (u32 j = 0; j < n; j++) CHECK(want[j] == bytes[off + j]);
        off += n;
    }
    return (int)size;
}

static int f_seek(int fd, int off, int whence)
{
    CHECK(fd == 1 && input >= 0);
    return linux_call(19, input, (u32)off, (u32)whence);
}

static int f_mkdir(const char *path) { (void)path; return 0; }
static void *f_alloc(u32 size)
{
    CHECK(size <= sizeof(heap) - heap_used);
    void *p = heap + heap_used;
    heap_used += size;
    return p;
}
static void f_free(void *p) { (void)p; }
static void f_print(u8 color, const char *fmt, ...)
{
    (void)color; (void)fmt;
    CHECK(0); /* Allocation diagnostics must never be reached. */
}

int main(int argc, char **argv)
{
    KernelAPI api = {0};
    CHECK(argc >= 3 && argc % 2 == 1);
    api.sys_open = f_open;
    api.sys_close = f_close;
    api.sys_read = f_read;
    api.sys_write = f_write;
    api.sys_lseek = f_seek;
    api.sys_mkdir = f_mkdir;
    api.mem_alloc = f_alloc;
    api.mem_free = f_free;
    api.kprintf = f_print;
    for (int i = 1; i < argc; i += 2) {
        PkgInfo info;
        unsigned want = 0;
        pkg_path = argv[i];
        expected_dir = argv[i + 1];
        files_written = 0;
        heap_used = 0;
        CHECK(pkg_parse(&api, pkg_path, &info) == PKG_OK);
        for (int j = 0; j < info.entry_count; j++)
            if (info.entries[j].type == PKG_TYPE_FILE) want++;
        CHECK(pkg_extract(&api, pkg_path, &info) == PKG_OK);
        CHECK(input == -1 && expected == -1 && files_written == want);
    }
    say("PASS guest pkg_parse/pkg_extract\n");
    return 0;
}

/* Linux i386 entry, preserve argc/argv before aligning the C call stack. */
__asm__(".text\n.globl _start\n_start:\n"
        "mov %esp,%eax\n"
        "and $-16,%esp\n"
        "sub $8,%esp\n"
        "lea 4(%eax),%edx\n"
        "push %edx\n"
        "push (%eax)\n"
        "call main\n"
        "mov %eax,%ebx\n"
        "mov $1,%eax\n"
        "int $0x80\n");
