#include "vfs.h"
#include "ring3_ls.h"
#include "ring3_str.h"
#include "os32_kapi_slots.h"
#include "redir_access.h"
#include "ls_source.inc"
#include "window.inc"
#include "ext2.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc) { __asm__ volatile("int $0x80" :: "a"(1), "b"(rc)); __builtin_unreachable(); }
static void say(const char *s) { u32 n = 0; while (s[n]) n++; __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
#define assert(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(1); } } while (0)

static int user_call, calls, fs_active, fs_calls, total = 19, result = 57;
static int kill_at, reject_input, reject_output, reject_copy;
static const u8 *read_end;
static void *expected_ctx;
static void *killed[5];
int ring3_call_from_user(void) { return user_call; }
void ring3_fault_kill(void) { assert(!fs_active); __builtin_longjmp(killed, 1); }
void *kmemcpy(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *kmemset(void *d, int v, u32 n) { u8 *a = d; while (n--) *a++ = v; return d; }
char *kstrncpy(char *d, const char *s, u32 n) { u32 i = 0; if (n) { while (i + 1 < n && s[i]) { d[i] = s[i]; i++; } d[i] = 0; } return d; }
int caller_access_get_user(struct caller_access *c) { assert(user_call); kmemset(c, 0, sizeof(*c)); return 1; }
int copy_caller_bytes(const struct caller_access *c, const void *s, void *d, u32 n) { (void)c; assert(!fs_active); if (reject_input) return 0; if (read_end) assert((const u8 *)s + n <= read_end); kmemcpy(d, s, n); return 1; }
int check_caller_write_range(const struct caller_access *c, void *d, u32 n) { (void)c; (void)d; assert(!fs_active && n == sizeof(struct ring3_ls_packet)); return !reject_output; }
int copy_to_caller(const struct caller_access *c, void *d, const void *s, u32 n) { (void)c; assert(!fs_active); if (reject_copy) return 0; kmemcpy(d, s, n); return 1; }

static void nested_listing(void);
static int nest;
static char shim_path[2];
static void callback(const VfsDirEntry *entry, void *ctx)
{
    assert(!user_call); /* catches old CPL0 USER callback path */
    assert(ctx == expected_ctx);
    assert((u8)entry->name[0] == (u8)('A' + calls) && !entry->name[1]);
    assert(entry->size == 100u + (u32)calls && entry->type == (calls % 2));
    calls++;
    shim_path[0] = 'x'; /* later batches must use the original snapshot */
    if (nest && calls == 1) nested_listing();
    if (kill_at && calls == kill_at) ring3_fault_kill();
}
int vfs_ls(const char *path, vfs_dir_cb cb, void *ctx)
{
    if (path[0] && path[0] != '/') return OS32_ERR_NAMETOOLONG;
    assert(!path[0] || !path[1]);
    assert(!fs_active);
    fs_active = 1;
    fs_calls++;
    for (int i = 0; i < total; i++) {
        VfsDirEntry entry;
        kmemset(&entry, 0xab, sizeof(entry)); /* padding must never leak */
        entry.name[0] = 'A' + i; entry.name[1] = 0;
        entry.size = 100 + i; entry.type = i % 2;
        cb(&entry, ctx);
    }
    fs_active = 0;
    return result;
}
#define KAPI_HIT(slot) ((void)(slot))
#include "wrapper.inc"

#define PAGE_SIZE 4096u
#define P2V(p) ((void *)(p))
static u8 tramp[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
u32 __bss_end;
static KernelAPI real_kapi;
static KernelAPI *kapi = &real_kapi;
static u32 ring3_tramp_page;
u32 exec_tramp_page_addr(void) { return (u32)tramp; }
#include "trampoline.inc"

/* The test substitutes only the interrupt instruction. The complete real
 * shim is linked, copied by the real initializer, and executed from its new VA.
 * host_gate preserves all registers just like int80_stub. */
int host_dispatch(u32 esp)
{
    assert(!user_call && !fs_active);
    user_call = 1;
    int rc = ring3_ls_dispatch(esp);
    user_call = 0;
    return rc;
}
__asm__(".global host_gate\nhost_gate:\n"
        "pushal\nleal 36(%esp), %eax\npushl %eax\ncall host_dispatch\n"
        "addl $4, %esp\nmovl %eax, 28(%esp)\npopal\nret\n");

static int run_shim(void *ctx)
{
    KernelAPI *table = (KernelAPI *)tramp;
    expected_ctx = ctx; calls = 0;
    shim_path[0] = '/'; shim_path[1] = 0;
    return table->sys_ls(shim_path, callback, ctx);
}
static int nested_count;
static void nested_callback(const VfsDirEntry *entry, void *ctx)
{
    assert(!fs_active && !user_call && ctx == &nested_count);
    assert(entry->size == 100u + (u32)nested_count);
    nested_count++;
}
static void nested_listing(void)
{
    KernelAPI *table = (KernelAPI *)tramp;
    nested_count = 0;
    assert(table->sys_ls("/", nested_callback, &nested_count) == result);
    assert(nested_count == total);
}
typedef struct {
    vfs_dir_cb user_cb;
    void *user_ctx;
    Ext2Ctx *ec;
} Ext2ListCtx;
static int inode_reads;
int ext2_get_size_ino(Ext2Ctx *ctx, u32 ino, u32 *size)
{ (void)ctx; inode_reads++; *size = 100 + ino; return 0; }
#include "ext2_cb.inc"
static void metadata_callback(const VfsDirEntry *entry, void *ctx)
{ (void)entry; (*(int *)ctx)++; }
static void metadata_window_test(void)
{
    int emitted = 0;
    struct vfs_ls_window_ctx w = { 70, 14, metadata_callback, &emitted };
    Ext2ListCtx lc = { vfs_ls_window_cb, &w, 0 };
    inode_reads = 0;
    for (u32 i = 0; i < 100; i++) {
        Ext2DirEntry e;
        kmemset(&e, 0, sizeof(e));
        e.inode = i + 1; e.file_type = EXT2_FT_REG_FILE;
        e.name_len = 1; e.name[0] = 'a';
        ext2_to_vfs_cb(&e, &lc);
    }
    assert(emitted == 30 && inode_reads == 14);
    assert(vfs_dir_needs_metadata(metadata_callback, &emitted));
}
int main(void)
{
    metadata_window_test();
    expected_ctx = &calls;
    assert(wrap_sys_ls("/", callback, &calls) == result && calls == total);
    user_call = 1; calls = 0;
    if (!__builtin_setjmp(killed)) {
        wrap_sys_ls("/", callback, &calls);
        assert(0); /* raw slot 12 must reject even if it returns an error */
    }
    assert(!calls && !fs_active);
    user_call = 0;
    ring3_trampoline_init();
    assert(ring3_tramp_page == (u32)tramp);
    assert((u32)(ring3_ls_end - ring3_ls_start) <= RING3_LS_SHIM_CAP);
    assert(RING3_LS_SHIM_OFF + RING3_LS_SHIM_CAP <= PAGE_SIZE);
    int protect;
    __asm__ volatile("int $0x80" : "=a"(protect) : "a"(125), "b"(tramp), "c"(PAGE_SIZE), "d"(7) : "memory");
    assert(!protect);
    for (total = 0; total <= 100; total++) {
        int before = fs_calls;
        assert(run_shim((void *)0xdeadbeef) == result && calls == total);
        assert(fs_calls - before == (total ? (total + RING3_LS_BATCH - 1) / RING3_LS_BATCH : 1));
        assert(!fs_active);
    }
    total = 100;
    int before_100 = fs_calls;
    assert(run_shim(&calls) == result && calls == 100);
    assert(fs_calls - before_100 == 8); /* fixed performance contract */
    nest = 1; total = 19;
    assert(run_shim(&calls) == result && calls == total);
    nest = 0; result = OS32_ERR_IO;
    assert(run_shim(0) == result && calls == total);
    result = 0; kill_at = 3;
    if (!__builtin_setjmp(killed)) { run_shim(&calls); assert(0); }
    assert(calls == 3 && !fs_active && !user_call);
    kill_at = 0;
    assert(run_shim(&calls) == 0 && calls == total); /* no leftover cursor */
    struct ring3_ls_packet out;
    u32 request[] = {0, (u32)"/", 0, (u32)&out};
    user_call = 1;
    assert(ring3_ls_dispatch((u32)request) == 0);
    for (u32 i = 0; i < out.count; i++) {
        const u8 *p = (const u8 *)&out.entries[i];
        for (u32 j = 2; j < VFS_MAX_PATH; j++) assert(p[j] == 0);
        for (u32 j = VFS_MAX_PATH + 5; j < sizeof(VfsDirEntry); j++) assert(p[j] == 0);
    }
    request[1] = 0;
    assert(ring3_ls_dispatch((u32)request) == 0 && out.count == RING3_LS_BATCH);
    char long_path[VFS_MAX_PATH];
    kmemset(long_path, 'x', sizeof(long_path));
    request[1] = (u32)long_path;
    assert(ring3_ls_dispatch((u32)request) == 0 && !out.count &&
           out.result == OS32_ERR_NAMETOOLONG);
    struct caller_access access;
    char staged[VFS_MAX_PATH + 1];
    read_end = (const u8 *)long_path + sizeof(long_path);
    assert(ls_copy_path(&access, (u32)long_path, staged));
    long_path[0] = 0;
    read_end = (const u8 *)long_path + 1;
    assert(ls_copy_path(&access, (u32)long_path, staged));
    read_end = 0;
    request[1] = (u32)"/";
    request[2] = 0xffffffffu;
    assert(ring3_ls_dispatch((u32)request) == 0 && !out.count && out.done);
    for (int mode = 0; mode < 4; mode++) {
        int before = fs_calls;
        reject_input = mode == 0; reject_output = mode == 1; reject_copy = mode == 2;
        if (!__builtin_setjmp(killed)) {
            ring3_ls_dispatch(mode == 3 ? 0xffffffffu : (u32)request);
            assert(0);
        }
        assert(!fs_active && fs_calls == before + (mode == 2));
    }
    say("PASS: trusted/raw USER, relocated shim order/ctx, 0..100 entries, 100 entries/8 gates, path snapshot, ext2 metadata window, errors, kill/restart, bounds, padding\n");
    return 0;
}
void _start(void) { die(main()); }
