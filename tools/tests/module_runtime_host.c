/* Actual transaction core + validator, with destructive VFS/page fixtures. */
#include "module_runtime.h"
#include "pgalloc.h"
#include "memmap.h"
#include "vfs.h"
#include "dev.h"
#include "kstring.h"

#define IMAGE_SIZE 4160U
#define OWNER_PAGES 4U
static u8 file[IMAGE_SIZE];
static u8 pool[MODULE_RECORD_CAP][OWNER_PAGES * PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static u32 live[MODULE_RECORD_CAP], mask[MODULE_RECORD_CAP];
static u32 input[2], pos, fd_live, opens, closes, allocations, fail_at;
static u32 starts, cleans, clean_order, returned, retired, fail_cleanup;
static struct module_handle current;
static Device device = {.name = "cd0", .type = DEV_BLOCK};
static Device *cached_device;
static int fs_context;
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); \
    finish(1); \
} } while (0)
static void *mount_fs(int dev)
{
    CHECK(VFS_MOUNT_DEV_TYPE(dev) == VFS_DEV_CD);
    return &fs_context;
}
static void unmount_fs(void *ctx) { CHECK(ctx == &fs_context); }
void vfs_fd_invalidate_mount(void *ctx) { CHECK(ctx == &fs_context); }
static VfsOps fs_ops = {.name = "fixture", .mount = mount_fs, .umount = unmount_fs};
int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
u32 kstrlen(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}
char *kstrncpy(char *d, const char *s, u32 n)
{
    if (!n) return d;
    u32 i = 0;
    while (i + 1 < n && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
    return d;
}
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
static void read_input(void *p, u32 size)
{
    while (size) {
        int rc;
        __asm__ volatile("int $0x80" : "=a"(rc) : "0"(3), "b"(0), "c"(p), "d"(size) : "memory");
        CHECK(rc > 0);
        size -= rc; p = (u8 *)p + rc;
    }
}
int vfs_open(const char *path, int mode)
{
    CHECK(path[0] == '/' && mode == O_RDONLY && !fd_live);
    opens++; fd_live = 1; pos = 0;
    return 3;
}
void vfs_close(int fd) { CHECK(fd == 3 && fd_live); fd_live = 0; closes++; }
int vfs_fstat(int fd, OS32_Stat *st)
{
    CHECK(fd == 3 && fd_live); st->st_size = IMAGE_SIZE; return 0;
}
int vfs_read_fd(int fd, void *dst, u32 n)
{
    CHECK(fd == 3 && fd_live && n <= IMAGE_SIZE - pos);
    kmemcpy(dst, file + pos, n); pos += n; return n;
}
static u32 index_of(u32 owner)
{
    CHECK(owner > LEDGER_OWNER_FIXED_LAST);
    u32 i = owner - LEDGER_OWNER_FIXED_LAST - 1;
    CHECK(i < MODULE_RECORD_CAP && live[i]);
    return i;
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    CHECK(kind == LEDGER_KIND_MODULE && id == MODULE_ID_KDEBUG && name[0]);
    if (++allocations == fail_at) return 0;
    for (u32 i = 0; i < MODULE_RECORD_CAP; i++) if (!live[i]) {
        live[i] = 1; *owner = LEDGER_OWNER_FIXED_LAST + 1 + i; return 1;
    }
    finish(2);
}
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    u32 i = index_of(owner);
    if (++allocations == fail_at) return 0;
    CHECK(n > 0 && (u32)n <= OWNER_PAGES);
    u32 bits = (1U << n) - 1;
    for (u32 p = 0; p + n <= OWNER_PAGES; p++) if (!(mask[i] & (bits << p))) {
        mask[i] |= bits << p;
        kmemset(pool[i] + p * PAGE_SIZE, 0xcc, n * PAGE_SIZE);
        return (u32)(uptr)(pool[i] + p * PAGE_SIZE);
    }
    finish(2);
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    u32 i = index_of(owner);
    u32 first = (u32)(uptr)pool[i] / PAGE_SIZE;
    CHECK(pfn >= first && n > 0 && pfn - first + n <= OWNER_PAGES);
    u32 bits = ((1U << n) - 1) << (pfn - first);
    CHECK((mask[i] & bits) == bits);
    if (input[0] == 10 && !returned) { returned = 1; return 0; }
    mask[i] &= ~bits;
    kmemset(pool[i] + (pfn - first) * PAGE_SIZE, 0xdd, n * PAGE_SIZE);
    returned += n;
    return 1;
}
int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    u32 i = index_of(owner);
    if (input[0] == 11) {
        CHECK(!dev_find("cd0") && !cached_device);
        CHECK(vfs_mount("/fixture", "cd0", "fixture") == VFS_ERR_INVAL);
    }
    if (input[0] == 9) {
        /* Rejection must precede relocation, BSS zeroing and import writes. */
        CHECK(!memcmp(pool[i], file, IMAGE_SIZE));
        for (u32 j = IMAGE_SIZE; j < 2 * PAGE_SIZE; j++) CHECK(pool[i][j] == 0xcc);
    }
    *pages = 0;
    for (u32 p = 0; p < OWNER_PAGES; p++) if (mask[i] & (1U << p)) {
        CHECK(pgalloc_free_n_owner(owner, (u32)(uptr)pool[i] / PAGE_SIZE + p, 1));
        ++*pages;
    }
    return 1;
}
int ledger_owner_retire(u32 owner)
{
    u32 i = index_of(owner);
    CHECK(!mask[i]); live[i] = 0; retired++; return 1;
}
static void empty(void)
{
    CHECK(!fd_live && opens == closes);
    for (u32 i = 0; i < MODULE_RECORD_CAP; i++) CHECK(!live[i] && !mask[i]);
}
static int quiesce(struct module_record *r)
{
    current.slot = (u32)(r - module_records); current.generation = r->generation;
    CHECK(r->state == MODULE_ROLLING_BACK && !module_exports(current));
    CHECK(clean_order == 0); clean_order = 1; cleans++;
    /* The previous module must remain available while a consumer cleans up. */
    for (u32 i = 0; i < r->dependency_count; i++)
        CHECK(module_exports(r->dependencies[i]));
    return fail_cleanup != 1;
}
static int detach(struct module_record *r)
{
    CHECK(clean_order == 1); clean_order = 2;
    if (fail_cleanup != 2) {
        if (input[0] != 8 || input[1] != MODULE_REF_IRQ)
            while (r->irq_refs) CHECK(module_ref_release(current, MODULE_REF_IRQ));
        if (input[0] != 8 || input[1] != MODULE_REF_TICK)
            while (r->tick_refs) CHECK(module_ref_release(current, MODULE_REF_TICK));
        if (input[0] != 8 || input[1] != MODULE_REF_CALLBACK)
            while (r->callback_refs) CHECK(module_ref_release(current, MODULE_REF_CALLBACK));
        if (input[0] != 8 || input[1] != MODULE_REF_ACTIVE)
            while (r->active_refs) CHECK(module_ref_release(current, MODULE_REF_ACTIVE));
        if (input[0] != 8 || input[1] != MODULE_REF_REGISTRATION)
            while (input[0] != 11 && r->registration_refs)
                CHECK(module_ref_release(current, MODULE_REF_REGISTRATION));
    }
    if (input[0] == 11 && r->registration_refs) {
        if (input[1] != 1) vfs_umount("/fixture");
        if (vfs_unregister_fs(&fs_ops) != VFS_OK) return 0;
        CHECK(module_ref_release(current, MODULE_REF_REGISTRATION));
        CHECK(!dev_unregister(&device));
        if (input[1] == 2) return 0; /* A cached Device user could not detach. */
        cached_device = 0;
        CHECK(module_ref_release(current, MODULE_REF_REGISTRATION));
    }
    return fail_cleanup != 2;
}
static int stop_dma(struct module_record *r)
{
    CHECK(clean_order == 2); clean_order = 0;
    if (fail_cleanup != 3 && (input[0] != 8 || input[1] != MODULE_REF_DMA))
        while (r->dma_refs) CHECK(module_ref_release(current, MODULE_REF_DMA));
    return fail_cleanup != 3;
}
static int start(struct module_record *r, u32 entry, u32 *ops)
{
    starts++;
    current.slot = (u32)(r - module_records); current.generation = r->generation;
    CHECK(r->state == MODULE_STARTING && !module_exports(current));
    CHECK(entry == r->init && ops[0] == 4 && ops[5] == r->base + 10);
    CHECK(!fd_live);
    /* Real BSS zeroing/import resolution, and actual body code execution. */
    CHECK(*(u32 *)(uptr)(r->base + 32) == 0x12345678U);
    for (u32 i = 64; i < PAGE_SIZE; i++) CHECK(!((u8 *)(uptr)r->base)[i]);
    CHECK(((int (*)(void))(uptr)entry)() == 0);
    CHECK(!module_ref_acquire(current, r->owner + 1, MODULE_REF_IRQ, r->text));
    CHECK(!module_ref_acquire(current, r->owner, MODULE_REF_TICK, r->text + r->text_size));
    for (u32 kind = MODULE_REF_IRQ; kind <= MODULE_REF_ACTIVE; kind++)
        CHECK(module_ref_acquire(current, r->owner, kind,
                                 kind <= MODULE_REF_CALLBACK ? r->text : 0));
    /* A STARTING IRQ consumer dispatches independently of module_exports. */
    CHECK(((int (*)(void))(uptr)(r->base + 10))() == 42);
    if (input[0] != 11)
        CHECK(module_ref_acquire(current, r->owner, MODULE_REF_REGISTRATION, 0));
    if (!pgalloc_alloc_phys(r->owner, 1)) return 1;
    if (!pgalloc_alloc_phys(r->owner, 1)) return 1;
    if (input[0] == 11 && starts == 3) {
        CHECK(module_ref_acquire(current, r->owner, MODULE_REF_REGISTRATION, 0));
        CHECK(!dev_register(&device));
        cached_device = dev_find("cd0");
        CHECK(cached_device == &device);
        CHECK(module_ref_acquire(current, r->owner, MODULE_REF_REGISTRATION, 0));
        CHECK(vfs_register_fs(&fs_ops) == VFS_OK);
        CHECK(module_ref_acquire(current, r->owner, MODULE_REF_REGISTRATION, 0));
        CHECK(vfs_register_fs(&fs_ops) == VFS_ERR_EXIST);
        CHECK(module_ref_release(current, MODULE_REF_REGISTRATION));
        CHECK(r->registration_refs == 2); /* Failed publication owns no ref. */
        CHECK(vfs_mount("/fixture", "cd0", "fixture") == VFS_OK);
        if (input[1] >= 3) fail_cleanup = input[1] - 2;
        return 1; /* Fail after publication, while still STARTING. */
    }
    if (input[0] == 3) ops[5]++;
    return input[0] == 2;
}
static struct module_symbol_contract imports[] = {{0xd8ef36fcU, 1, 1}};
static struct module_symbol_contract exports[MODULE_OPS_SLOTS];
static u32 addresses[] = {0x12345678U};
static struct module_request request(void)
{
    /* Hashes are supplied from the independent Python fixture's contracts. */
    struct module_request q = {
        .path = "/boot/test.mod", .name = "fixture",
        .policy = {75, 3, 0x12345678, 0xabcdef01, 4, 1, 1,
                   imports, exports, MODULE_ID_KDEBUG},
        .imports = addresses,
        .lifecycle = {start, quiesce, detach, stop_dma}
    };
    return q;
}
static void abort_one(struct module_handle h)
{
    current = h;
    CHECK(!module_abort(h));
    u32 before = returned;
    CHECK(!module_abort(h) && returned == before);
}
static void registry_limits(void)
{
    VfsOps ops[VFS_MAX_FS + 1];
    Device devices[MAX_DEVICES + 1];
    char names[MAX_DEVICES + 1][2];
    kmemset(ops, 0, sizeof(ops));
    kmemset(devices, 0, sizeof(devices));
    for (u32 i = 0; i <= MAX_DEVICES; i++) {
        names[i][0] = 'A' + i; names[i][1] = 0;
        devices[i].name = names[i];
        CHECK(dev_register(&devices[i]) == (i < MAX_DEVICES ? 0 : -1));
    }
    CHECK(dev_count() == MAX_DEVICES && !dev_find(names[MAX_DEVICES]));
    CHECK(dev_register(&devices[0]) == -1);
    CHECK(dev_unregister(&devices[MAX_DEVICES]) == -1);
    CHECK(!dev_unregister(&devices[1]));
    CHECK(!dev_find(names[1]) && dev_get(1) == &devices[2]);
    CHECK(!dev_register(&devices[MAX_DEVICES]));
    for (u32 i = 0; i <= MAX_DEVICES; i++)
        if (i != 1) CHECK(!dev_unregister(&devices[i]));
    CHECK(!dev_count() && !dev_get(0));
    for (u32 i = 0; i <= VFS_MAX_FS; i++) {
        ops[i] = fs_ops; ops[i].name = names[i];
        CHECK(vfs_register_fs(&ops[i]) == (i < VFS_MAX_FS ? VFS_OK : VFS_ERR_FULL));
    }
    CHECK(vfs_register_fs(&ops[0]) == VFS_ERR_EXIST);
    CHECK(vfs_unregister_fs(&ops[VFS_MAX_FS]) == VFS_ERR_NOTFOUND);
    CHECK(vfs_mount("/fixture", "cd0", names[VFS_MAX_FS]) == VFS_ERR_INVAL);
    CHECK(vfs_mount("/fixture", "cd0", names[1]) == VFS_OK);
    CHECK(vfs_unregister_fs(&ops[1]) == VFS_ERR_BUSY);
    vfs_umount("/fixture");
    CHECK(vfs_unregister_fs(&ops[1]) == VFS_OK);
    CHECK(vfs_mount("/fixture", "cd0", names[1]) == VFS_ERR_INVAL);
    CHECK(vfs_register_fs(&ops[VFS_MAX_FS]) == VFS_OK);
    for (u32 i = 0; i <= VFS_MAX_FS; i++) if (i != 1) {
        CHECK(vfs_mount("/fixture", "cd0", names[i]) == VFS_OK);
        vfs_umount("/fixture");
        CHECK(vfs_unregister_fs(&ops[i]) == VFS_OK);
    }
}
void _start(void)
{
    CHECK(MODULE_RECORD_CAP == 11);
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    if (input[0] == 12) { registry_limits(); finish(0); }
    /* Decode contract hashes independently from the file's known records. */
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(pool), "c"(sizeof(pool)), "d"(7) : "memory");
    CHECK(!rc);
    struct module_request q = request();
    struct module_handle h = {0, 0};
    fail_at = input[0] == 1 ? input[1] : 0;
    if (input[0] == 10) fail_cleanup = input[1];
    rc = module_load(&q, &h);
    if (input[0] == 9) {
        CHECK(rc == (int)input[1] && !starts && !module_exports(h));
        CHECK(module_record_get(h)->state == MODULE_ABSENT);
        empty(); finish(0);
    }
    if (!rc) {
        CHECK(!module_exports(h) && !starts);
        rc = module_start(h);
    }
    if (input[0] >= 1 && input[0] <= 3) {
        CHECK(rc == (fail_at && fail_at <= 2 ? MODULE_NO_PAGES : MODULE_INIT_FAILED));
        CHECK(module_record_get(h)->state == MODULE_ABSENT);
        abort_one(h); empty(); finish(0);
    }
    if (input[0] == 10) {
        CHECK(rc == (input[1] ? MODULE_ISOLATED : MODULE_INIT_FAILED));
        CHECK(!module_exports(h) && starts == 1 && cleans == 1);
        if (input[1]) {
            CHECK(module_record_get(h)->state == MODULE_QUARANTINED);
            CHECK(live[0] && mask[0] == 15 && returned == 1 && !retired);
        } else {
            CHECK(module_record_get(h)->state == MODULE_ABSENT && retired == 1);
            abort_one(h); empty();
        }
        finish(0);
    }
    CHECK(!rc && module_exports(h));
    struct module_record *r = module_record_get(h);
    CHECK(r->pages == 1 && !r->prefix_pages);
    CHECK(((int (*)(void))(uptr)module_exports(h)[5])() == 42);
    for (u32 i = 0; i < PAGE_SIZE; i++) CHECK(pool[0][i] == 0xdd);
    CHECK(module_start(h) == MODULE_BUSY);
    if (input[0] == 11) {
        struct module_handle chain[3] = {h, {0, 0}, {0, 0}};
        q.dependencies = &chain[0]; q.dependency_count = 1;
        CHECK(!module_load(&q, &chain[1]) && !module_start(chain[1]));
        q.dependencies = &chain[1];
        CHECK(!module_load(&q, &chain[2]));
        CHECK(module_start(chain[2]) == MODULE_INIT_FAILED);
        CHECK(!module_exports(chain[2]));
        u32 before = returned;
        fail_cleanup = 0;
        CHECK(module_abort_reverse(chain, 3) == (input[1] ? MODULE_ISOLATED : 0));
        if (input[1]) {
            CHECK(returned == before && returned == 2 && !retired);
            for (u32 i = 0; i < 3; i++) {
                CHECK(module_record_get(chain[i])->state == MODULE_QUARANTINED);
                CHECK(live[i] && mask[i] == (i == 2 ? 15U : 14U));
            }
        } else empty();
        finish(0);
    }
    if (input[0] == 4 || input[0] == 8) {
        /* Each incomplete stop stage must retain body and all owner pages. */
        fail_cleanup = input[0] == 4 ? input[1] : 0; current = h;
        u32 before = returned;
        CHECK(module_abort(h) == MODULE_ISOLATED);
        CHECK(r->state == MODULE_QUARANTINED && !module_exports(h));
        CHECK(live[0] && mask[0] == 14 && returned == before && !retired);
        CHECK(module_abort(h) == MODULE_ISOLATED && returned == before && cleans == 1);
        finish(0);
    }
    if (input[0] == 5) {
        /* Record saturation must not open a file or allocate an owner/page. */
        struct module_handle all[MODULE_RECORD_CAP]; all[0] = h;
        for (u32 i = 1; i < MODULE_RECORD_CAP; i++) {
            CHECK(!module_load(&q, &all[i]));
            CHECK(!module_start(all[i]));
        }
        u32 old_opens = opens, old_alloc = allocations;
        struct module_handle unchanged = h;
        CHECK(module_load(&q, &unchanged) == MODULE_RECORD_FULL);
        CHECK(unchanged.slot == h.slot && unchanged.generation == h.generation);
        CHECK(opens == old_opens && allocations == old_alloc);
        for (u32 i = MODULE_RECORD_CAP; i; i--) abort_one(all[i - 1]);
        empty(); finish(0);
    }
    if (input[0] == 6 || input[0] == 7) {
        struct module_handle chain[2] = {h, {0, 0}};
        q.dependencies = &h; q.dependency_count = 1;
        CHECK(!module_load(&q, &chain[1]));
        /* The request dependency table is temporary; record must own its copy. */
        h.generation++; q.dependencies = 0;
        CHECK(!module_start(chain[1]));
        CHECK(module_exports(module_record_get(chain[1])->dependencies[0]));
        if (input[0] == 7) {
            current = chain[0]; u32 before = returned;
            CHECK(module_abort(chain[0]) == MODULE_ISOLATED && returned == before);
            CHECK(live[0] && live[1]); finish(0);
        }
        /* Exercise actual reverse helper (cleanup derives current from record). */
        CHECK(!module_abort_reverse(chain, 2));
        empty(); finish(0);
    }
    abort_one(h); empty();
    /* Slot reuse invalidates old handles, including release and abort. */
    struct module_handle next;
    CHECK(!module_load(&q, &next));
    CHECK(next.slot == h.slot && next.generation == h.generation + 1);
    CHECK(!module_record_get(h) && !module_exports(h));
    CHECK(!module_ref_release(h, MODULE_REF_IRQ));
    CHECK(!module_abort(h));
    abort_one(next); empty();
    /* Refuse dependency storage overflow and stale dependencies before open. */
    u32 old_opens = opens, old_alloc = allocations;
    q.dependencies = &h; q.dependency_count = MODULE_DEPENDENCY_CAP + 1;
    CHECK(module_load(&q, &next) == MODULE_BAD_DEPENDENCY);
    q.dependency_count = 1;
    CHECK(module_load(&q, &next) == MODULE_BAD_DEPENDENCY);
    CHECK(opens == old_opens && allocations == old_alloc);
    finish(0);
}
