/* Real module.c + module_image.c, fake VFS/ledger and executable i386 body. */
#include "kernel/module/module.h"
#include "kernel/kselftest.h"
#include "kernel/module/module_image.h"
#include "mm/pgalloc.h"
#include "init/sysconfig.h"
#include "bootinfo.h"
#include "config.h"
#include "fs/vfs.h"
#include "lib/kstring.h"
#include "kprintf.h"
#include "memmap.h"
#include "kdebug_imports.h"
#include <stdarg.h>
#include "kernel_selftest_dispatch.h"
#include "kdebug/recover_wire.h"

#define CAP (MODULE_IMAGE_MAX + MODULE_PAGE_SIZE)
/* File limit excludes the separately zeroed resident BSS. */
#define POOL_CAP (4U * MODULE_IMAGE_MAX)
static u8 file[CAP];
static u8 pool[POOL_CAP] __attribute__((aligned(MODULE_PAGE_SIZE)));
static u32 input[5]; /* config, failure injection, expected state, image size, pair */
static unsigned pos, opens, closes, owners, retires, pages, reads, logs, imports, hwdev_loads;
static struct bootinfo bi;
static u32 running_logs, skipped_logs, not_run_logs, dispatch;
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg) - 1) : "memory"); \
    finish(1); \
} } while (0)
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
    CHECK(owner == (LEDGER_OWNER_FIXED_LAST + 1) && !pages && (u32)n * PAGE_SIZE <= POOL_CAP);
    if (input[1] == 5) return 0;
    pages = n;
    return (u32)pool;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    CHECK(owner == (LEDGER_OWNER_FIXED_LAST + 1) && pfn == (u32)pool / PAGE_SIZE && n > 0 && (u32)n <= pages);
    if (input[1] == 8) return 0;
    pages -= n;
    /* Reclaim really destroys prefix: published pointers must target the body. */
    kmemset(pool, 0xdd, n * PAGE_SIZE);
    return 1;
}
int ledger_reclaim_owner(u32 owner, u32 *reclaimed)
{
    CHECK(owner == (LEDGER_OWNER_FIXED_LAST + 1));
    *reclaimed = pages;
    pages = 0;
    kmemset(pool, 0xdd, POOL_CAP);
    return 1;
}
void kprintf(unsigned char color, const char *format, ...)
{
    (void)color;
    va_list ap;
    va_start(ap, format);
    if (!memcmp(format, "[kdebug] rejected: %s\n", sizeof("[kdebug] rejected: %s\n"))) {
        logs++;
        CHECK(va_arg(ap, const char *) == kdebug_state_name(input[2]));
    } else if (!memcmp(format, "[kdebug] running pair %08x\n", sizeof("[kdebug] running pair %08x\n"))) {
        running_logs++;
        CHECK(va_arg(ap, u32) == input[4]);
    } else if (!memcmp(format, "[selftest] skipped (KDEBUG=0)\n", sizeof("[selftest] skipped (KDEBUG=0)\n"))) {
        skipped_logs++;
        CHECK(kdebug.state == KDEBUG_OFF);
    } else if (!memcmp(format, "[selftest] not run (kdebug rejected: %s) — ON の受入は未成立\n",
                       sizeof("[selftest] not run (kdebug rejected: %s) — ON の受入は未成立\n"))) {
        not_run_logs++;
        CHECK(kdebug.state >= KDEBUG_NOT_FOUND);
        CHECK(va_arg(ap, const char *) == kdebug_state_name(kdebug.state));
    } else imports++;
    va_end(ap);
}
void kdebug_forbidden_import(void) { imports++; finish(199); }
u32 kdebug_test_fail(void) { imports++; return 0; }
int kdebug_test_skipped(int tag)
{
    CHECK(tag == KAUDIT_POST_EXEC);
    imports++;
    return 1;
}
static void dispatch_run(void) { dispatch |= 1; }
static void dispatch_exec(void) { dispatch |= 2; }
static void dispatch_unicode(void) { dispatch |= 4; }
static void dispatch_probe(void) { dispatch |= 8; }
static void check_boot_dispatch(void)
{
    static const char *const names[] = {
        "OFF", "RUNNING", "NOT_FOUND", "BAD_HEADER", "PAIR_MISMATCH",
        "NO_IMAGE_CRC", "ABI_MISMATCH", "NO_PAGES", "SHORT_READ",
        "BAD_CRC", "INIT_FAILED", "RELOC_INVALID", "IMPORT_INVALID",
        "RECORD_FULL", "BAD_DEPENDENCY", "BUSY", "QUARANTINED"
    };
    kdebug.ops.selftest_run = dispatch_run;
    kdebug.ops.selftest_post_exec = dispatch_exec;
    kdebug.ops.selftest_post_unicode = dispatch_unicode;
    kdebug.ops.selftest_post_probe = dispatch_probe;
    for (u32 state = 0; state < sizeof(names) / sizeof(names[0]); state++) {
        kdebug.state = state;
        dispatch = skipped_logs = not_run_logs = 0;
        const char *got = kdebug_state_name(state), *want = names[state];
        while (*want) CHECK(*want++ == *got++);
        CHECK(!*got);
        /* All ops deliberately remain non-NULL even in OFF/rejected states. */
        host_boot_dispatch();
        CHECK(dispatch == (state == KDEBUG_RUNNING ? 15U : 0));
        CHECK(skipped_logs == (state == KDEBUG_OFF));
        CHECK(not_run_logs == (state >= KDEBUG_NOT_FOUND));
    }
}
/* KAPI kdebug_call (kernel/module/kdebug_call.c): NOSYS without slot 19, else pass-through. */
static u32 call_op, call_size;
static void *call_buf;
static i32 fake_dbg_call(u32 op, void *buf, u32 size)
{
    call_op = op; call_buf = buf; call_size = size;
    return 0x5a;
}
static void check_kdebug_call(void)
{
    static u8 req[8];
    i32 (*saved)(u32, void *, u32) = kdebug.ops.dbg_call;
    kdebug.ops.dbg_call = 0;
    CHECK(kdebug_call(KDEBUG_OP_PEEK, req, sizeof(req)) == OS32_ERR_NOSYS);
    /* Private route-test also stays dormant with KDEBUG disabled. */
    CHECK(kdebug_call(KDEBUG_OP_RECOVER_ROUTE_TEST, req, sizeof(req)) == OS32_ERR_NOSYS);
    kdebug.ops.dbg_call = fake_dbg_call;
    CHECK(kdebug_call(KDEBUG_OP_PEEK_LAN_PROBE, req, sizeof(req)) == 0x5a);
    CHECK(call_op == KDEBUG_OP_PEEK_LAN_PROBE && call_buf == req && call_size == sizeof(req));
    kdebug.ops.dbg_call = saved;
}
void _start(void)
{
    read_input(input, sizeof(input)); CHECK(input[3] <= CAP);
    read_input(file, input[3]);
    bi.img_valid = input[1] != 7; bi.img_crc = input[4];
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(pool), "c"(POOL_CAP), "d"(7) : "memory");
    CHECK(!rc);
    kdebug_load();
    CHECK(kdebug.state == input[2] && imports == 0);
    CHECK(running_logs == (input[2] == KDEBUG_RUNNING));
    CHECK(hwdev_loads == (input[2] == KDEBUG_RUNNING));
    CHECK(opens == (input[0] == 1) && kdebug.open_count == opens);
    CHECK(closes == (opens && input[1] != 1));
    if (input[2] == KDEBUG_RUNNING) {
        CHECK(pages == kdebug.pages && pages > 0 && retires == 0 && logs == 0);
        u32 prefix = ((const u32 *)file)[11];
        CHECK(kdebug.pair_crc == bi.img_crc && kdebug.base == (u32)pool + prefix);
        CHECK(kdebug.base_pa == V2P(pool + prefix));
        CHECK(kdebug.ops.version == KDEBUG_OPS_VERSION && kdebug.ops.crt_diag_bootlog);
        const u32 *ops = (const u32 *)&kdebug.ops;
        for (u32 slot = 1; slot <= 5; slot++)
            CHECK(ops[slot] >= kdebug.base && ops[slot] < kdebug.base + pages * PAGE_SIZE);
        CHECK(!ops[17] && ops[18] >= kdebug.base &&
              ops[18] < kdebug.base + pages * PAGE_SIZE);
        /* The actual relocated post-exec returns before every later test
         * when the kernel audit reports an open V86 session. */
        kdebug.ops.selftest_post_exec();
        CHECK(imports == 2);
        for (u32 i = 0; i < prefix; i++) CHECK(pool[i] == 0xdd);
    } else {
        CHECK(!pages && retires == owners && logs == (input[2] != 0));
        CHECK(!kdebug.base && !kdebug.base_pa && !kdebug.pages);
        for (u32 i = 0; i < sizeof(kdebug.ops); i++) CHECK(!((u8 *)&kdebug.ops)[i]);
        if (!input[0]) CHECK(!owners && !kdebug.owner);
    }
    if (input[2] == KDEBUG_NOT_FOUND) {
        CHECK(logs == 1 && !owners && !reads);
        CHECK(kdebug_call(KDEBUG_OP_PEEK, 0, 0) == OS32_ERR_NOSYS);
    }
    check_boot_dispatch();
    check_kdebug_call();
    finish(0);
}

/* HWDEV facade is separately exercised with real synthetic images. */
int hwdev_module_load(struct module_handle core)
{ CHECK(kdebug.state == KDEBUG_RUNNING && module_exports(core) != 0);
  hwdev_loads++; return MODULE_NOT_FOUND; }
