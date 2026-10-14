/* Real module.c + module_image.c, fake VFS/ledger and executable i386 body. */
#include "kernel/module.h"
#include "kernel/module_image.h"
#include "kernel/pgalloc.h"
#include "kernel/sysconfig.h"
#include "bootinfo.h"
#include "config.h"
#include "fs/vfs.h"
#include "lib/kstring.h"
#include "kprintf.h"
#include "memmap.h"
#include "kdebug_imports.h"

#define CAP (MODULE_IMAGE_MAX + MODULE_PAGE_SIZE)
static u8 file[CAP];
static u8 pool[CAP] __attribute__((aligned(MODULE_PAGE_SIZE)));
static u32 input[5]; /* config, failure injection, expected state, image size, pair */
static unsigned pos, opens, closes, owners, retires, pages, reads, logs, imports;
static struct bootinfo bi;
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) finish(__LINE__ % 200 + 20); } while (0)
static void read_input(void *ptr, unsigned size)
{
    while (size) {
        int rc;
        __asm__ volatile("int $0x80" : "=a"(rc) : "0"(3), "b"(0), "c"(ptr), "d"(size) : "memory");
        CHECK(rc > 0);
        size -= rc; ptr = (u8 *)ptr + rc;
    }
}
void *kmemcpy(void *dst, const void *src, unsigned long n)
{
    for (unsigned long i = 0; i < n; i++) ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}
void *kmemset(void *dst, int c, unsigned long n)
{
    for (unsigned long i = 0; i < n; i++) ((u8 *)dst)[i] = c;
    return dst;
}
int memcmp(const void *a, const void *b, unsigned long n)
{
    for (unsigned long i = 0; i < n; i++)
        if (((const u8 *)a)[i] != ((const u8 *)b)[i]) return 1;
    return 0;
}
int sysconfig_get_int(const char *path, const char *key, int def)
{
    (void)path; (void)key; (void)def;
    return input[0];
}
const struct bootinfo *bootinfo_get(void) { return &bi; }
int vfs_open(const char *path, int mode)
{
    CHECK(!memcmp(path, SYS_KDEBUG_BIN, sizeof(SYS_KDEBUG_BIN)) && mode == O_RDONLY);
    opens++;
    return input[1] == 1 ? -1 : 3;
}
void vfs_close(int fd) { CHECK(fd == 3 && !closes); closes++; }
int vfs_fstat(int fd, OS32_Stat *st)
{
    CHECK(fd == 3);
    st->st_size = input[3];
    return input[1] == 2 ? -1 : 0;
}
int vfs_read_fd(int fd, void *dst, u32 n)
{
    CHECK(fd == 3 && !closes && n <= input[3] - pos);
    reads++;
    if ((input[1] == 3 && reads == 1) || (input[1] == 6 && reads == 2)) return n - 1;
    kmemcpy(dst, file + pos, n); pos += n;
    return n;
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    CHECK(kind == LEDGER_KIND_MODULE && id == MODULE_ID_KDEBUG && name[0] == 'k');
    if (input[1] == 4) return 0;
    owners++; *owner = (LEDGER_OWNER_FIXED_LAST + 1);
    return 1;
}
int ledger_owner_retire(u32 owner)
{
    CHECK(owner == (LEDGER_OWNER_FIXED_LAST + 1) && owners == 1 && !pages && !retires);
    retires++;
    return 1;
}
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    CHECK(owner == (LEDGER_OWNER_FIXED_LAST + 1) && !pages && (u32)n * PAGE_SIZE <= CAP);
    if (input[1] == 5) return 0;
    pages = n;
    return (u32)pool;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    CHECK(owner == (LEDGER_OWNER_FIXED_LAST + 1) && pfn == (u32)pool / PAGE_SIZE && n > 0 && (u32)n <= pages);
    pages -= n;
    /* Reclaim really destroys prefix: published pointers must target the body. */
    kmemset(pool, 0xdd, n * PAGE_SIZE);
    return 1;
}
void kprintf(unsigned char color, const char *format, ...)
{
    (void)color;
    if (!memcmp(format, "[kdebug] rejected: %s\n", sizeof("[kdebug] rejected: %s\n"))) logs++;
    else imports++;
}
void _start(void)
{
    read_input(input, sizeof(input)); CHECK(input[3] <= CAP);
    read_input(file, input[3]);
    bi.img_valid = input[1] != 7; bi.img_crc = input[4];
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(pool), "c"(CAP), "d"(7) : "memory");
    CHECK(!rc);
    kdebug_load();
    CHECK(kdebug.state == input[2] && imports == 0);
    CHECK(opens == (input[0] == 1) && kdebug.open_count == opens);
    CHECK(closes == (opens && input[1] != 1));
    if (input[2] == KDEBUG_RUNNING) {
        CHECK(pages == kdebug.pages && pages > 0 && retires == 0 && logs == 0);
        CHECK(kdebug.pair_crc == bi.img_crc && kdebug.base == (u32)pool + MODULE_PAGE_SIZE);
        CHECK(kdebug.ops.version == KDEBUG_OPS_VERSION && kdebug.ops.crt_diag_bootlog);
        CHECK((u32)kdebug.ops.crt_diag_bootlog >= kdebug.base &&
              (u32)kdebug.ops.crt_diag_bootlog < kdebug.base + pages * PAGE_SIZE);
        for (u32 i = 0; i < MODULE_PAGE_SIZE; i++) CHECK(pool[i] == 0xdd);
    } else {
        CHECK(!pages && retires == owners && logs == (input[2] != 0));
        CHECK(!kdebug.base && !kdebug.pages);
        for (u32 i = 0; i < sizeof(kdebug.ops); i++) CHECK(!((u8 *)&kdebug.ops)[i]);
        if (!input[0]) CHECK(!owners && !kdebug.owner);
    }
    finish(0);
}
