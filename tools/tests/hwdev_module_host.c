/* Actual facade + runtime + validator; synthetic M2 or built GF body. */
#include "../../kernel/hwdev_module.c"
#include "pgalloc.h"
#include "memmap.h"
#include "vfs.h"
#ifdef HWDEV_REAL_IMAGE
#include "hwdev_pair.h"
#include "kdebug/gfinit.h"
static struct recover image_owner;
static u32 image_owner_calls, image_end_calls;
static struct recover *image_recover_owner(void) { image_owner_calls++; return &image_owner; }
static int image_trial_end(struct recover *r, const struct recover_token *t, enum recover_reason why)
{ (void)r; (void)t; (void)why; image_end_calls++; return -1; }
#endif
static u8 file[MODULE_IMAGE_MAX], pool[MODULE_IMAGE_MAX] __attribute__((aligned(4096)));
static u32 input[3], pos, opens, closes, live, logs;
static struct hwdev_core_api fixture_core;
static struct bootinfo bi;
struct kdebug kdebug;
static void finish(u32 rc) __attribute__((noreturn));
static void finish(u32 rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    const char s[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(sizeof(s)-1) : "memory"); \
    finish(1); \
} } while (0)
void *kmemcpy(void *d, const void *s, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = ((const u8 *)s)[i];
    return d;
}
void *kmemset(void *d, int c, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = c;
    return d;
}
int memcmp(const void *a, const void *b, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (((const u8 *)a)[i] != ((const u8 *)b)[i]) return 1;
    return 0;
}
void kprintf(u8 c, const char *f, ...) { (void)c; (void)f; logs++; }
const char *kdebug_state_name(int s) { (void)s; return "fixture"; }
const struct bootinfo *bootinfo_get(void) { return &bi; }
int vfs_open(const char *p, int flags) { (void)p; (void)flags; opens++; return input[0] == 1 ? -1 : 3; }
void vfs_close(int fd) { CHECK(fd == 3); closes++; }
int vfs_fstat(int fd, OS32_Stat *st) { CHECK(fd == 3); st->st_size = input[1]; return 0; }
int vfs_read_fd(int fd, void *p, u32 n)
{
    CHECK(fd == 3 && n <= input[1] - pos);
    kmemcpy(p, file + pos, n);
    pos += n;
    if (input[0] == 4 && pos == input[1]) module_records[0].generation++;
    return n;
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *out)
{
    (void)name;
    CHECK(kind == LEDGER_KIND_MODULE && id == MODULE_ID_HWDEV && !live);
    *out = 100;
    live = 1;
    return 1;
}
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    CHECK(owner == 100 && n > 0 && (u32)n <= sizeof(pool) / PAGE_SIZE);
    return (u32)(uptr)pool;
}
int pgalloc_free_n_owner(u32 owner, u32 page, int n)
{
    (void)page;
    CHECK(owner == 100 && n > 0);
    return 1;
}
int ledger_reclaim_owner(u32 owner, u32 *count) { CHECK(owner == 100); *count = 0; return 1; }
int ledger_owner_retire(u32 owner) { CHECK(owner == 100); live = 0; return 1; }
/* Kernel imports are only installed, never called by initialize. */
void fixture_kernel(void) { finish(90); }
static void core_function(void) { }
static const struct hwdev_core_api *get_core(void) { return &fixture_core; }
static void read_input(void *p, u32 size)
{
    while (size) {
        int n;
        __asm__ volatile("int $0x80" : "=a"(n) : "0"(3), "b"(0), "c"(p), "d"(size) : "memory");
        CHECK(n > 0);
        size -= n;
        p = (u8 *)p + n;
    }
}
void _start(void)
{
    int protect;
    __asm__ volatile("int $0x80" : "=a"(protect) : "0"(125), "b"(pool), "c"(sizeof(pool)), "d"(7) : "memory");
    CHECK(!protect);
    read_input(input, sizeof(input));
    CHECK(input[1] <= sizeof(file));
    read_input(file, input[1]);
    bi.img_valid = 1;
    bi.img_crc = ((u32 *)file)[MI_PAIR_CRC];
    if (input[0] == 9) bi.img_crc ^= 1;
    fixture_core.abi = HWDEV_ABI;
    fixture_core.size = sizeof(fixture_core);
    fixture_core.contract_crc = HWDEV_CONTRACT_CRC;
    fixture_core.layout_crc = HWDEV_LAYOUT_CRC;
    for (u32 i = 0; i < HWDEV_CORE_SLOTS; i++) ((u32 *)&fixture_core)[4 + i] = (u32)(uptr)core_function;
#ifdef HWDEV_REAL_IMAGE
    fixture_core.recover_owner = image_recover_owner;
    fixture_core.trial_end = image_trial_end;
#endif
    struct module_record *c = &module_records[0];
    c->state = MODULE_RUNNING;
    c->generation = 1;
    c->module_id = MODULE_ID_KDEBUG;
#ifdef HWDEV_REAL_IMAGE
    c->image_crc = HWDEV_CORE_IMAGE_CRC;
#else
    c->image_crc = 0x11223344;
#endif
    c->ro = (u32)(uptr)&fixture_core;
    c->ro_size = sizeof(fixture_core);
    c->text = (u32)(uptr)core_function;
    c->text_size = 1;
#ifdef HWDEV_REAL_IMAGE
    c->text = 0x08048000; c->text_size = 0x100000;
#endif
    kdebug.state = KDEBUG_RUNNING;
    kdebug.ops.hwdev_core_get = get_core;
    struct module_handle dep = {0, 1};
    switch (input[0]) {
    case 2: kdebug.state = KDEBUG_OFF; break;
    case 3: dep.generation++; break;
    case 5: fixture_core.contract_crc ^= 1; break;
    case 6: fixture_core.layout_crc ^= 1; break;
    case 7: fixture_core.size--; break;
    case 8: fixture_core.trial_begin = 0; break;
    case 10: kdebug.ops.hwdev_core_get = 0; break;
    case 11: c->ro_size--; break;
    case 12: fixture_core.abi++; break;
    case 13: ((u32 *)&fixture_core)[4]++; break;
    }
    int rc = hwdev_module_load(dep);
    CHECK((u32)rc == input[2]);
    if (rc || input[0] == 2) {
        CHECK(!hwdev_module_ops() && !live);
    } else {
        const struct hwdev_ops *ops = hwdev_module_ops();
        CHECK(ops && live);
#ifdef HWDEV_REAL_IMAGE
        image_owner.state = REC_PAUSED;
        for (u32 op = GF_OP_INSPECT; op <= GF_OP_ABORT; op++) {
            struct gfinit_request q = {.version=GF_INIT_VERSION, .size=sizeof(q),
                .flags=GF_ALLOW_WRITE, .max_steps=GF_INIT_SCHEDULE};
            CHECK(ops->call(op, &q, sizeof(q), 0, 0, 0) == -1);
            CHECK(image_owner_calls == op - GF_OP_INSPECT + 1 && q.status);
        }
        CHECK(image_end_calls == 1);
        ops->release_work();
        CHECK(image_owner_calls == 7);
#else
        CHECK(ops->call(0, 0, 0, 0, 0, 0) == 42);
        ops->release_work();
#endif
        struct module_record *r = module_record_get(hwdev_handle);
        CHECK(r && r->dependency_count == 1);
        /* Every core/kernel address was installed before initialize. */
        u32 *slots = (u32 *)(uptr)(r->base + ((u32 *)file)[MI_DATA_OFF]);
        u32 expected[HWDEV_IMPORT_COUNT];
        hwdev_fill_imports(expected, &fixture_core);
        CHECK(!memcmp(slots, expected, sizeof(expected)));
        c->generation++;
        CHECK(!hwdev_module_ops());
        c->generation--;
        CHECK(module_abort(hwdev_handle) == 0 && !hwdev_module_ops() && !live);
    }
    CHECK(c->state == MODULE_RUNNING); /* HWDEV failure never rolls back S0's body. */
    if (input[0] == 2) CHECK(!opens && !logs);
    else CHECK(opens == closes + (input[0] == 1));
    finish(0);
}
