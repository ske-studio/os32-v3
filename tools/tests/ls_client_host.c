#include "os32/ls.h"
KernelAPI table;
KernelAPI *kapi = &table;
static unsigned int total, seen, calls, legacy_calls, in_window;
static int trusted, error_at, recurse, nested_seen, null_path, positive, error_code = OS32_ERR_IO;
static char original_path[] = "/";

static void finish(int code) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) finish(__LINE__ % 200 + 1); } while (0)
static void nested_cb(const DirEntry_Ext *e, void *ctx) {
    CHECK(ctx == &nested_seen && e->size == (unsigned int)nested_seen);
    nested_seen++;
}
static void cb(const DirEntry_Ext *e, void *ctx) {
    CHECK(!in_window && ctx == &total && e->size == seen);
    CHECK(e->type == OS32_FILE_TYPE_FILE && e->name[0] == 'x');
    original_path[0] = 'z';
    seen++;
    if (recurse && seen == 1) {
        unsigned int outer_seen = seen, outer_calls = calls;
        seen = calls = 0;
        recurse = 0;
        CHECK(os32_ls("/", nested_cb, &nested_seen) == 0);
        CHECK(nested_seen == (int)total);
        seen = outer_seen; calls = outer_calls;
    }
}
static int window(const char *path, u32 skip, OS32_LsPacket *out) {
    unsigned int n, i;
    CHECK(path && path[0] == (null_path ? 0 : '/') &&
          (skip == seen || (unsigned int)nested_seen == skip));
    CHECK(++calls <= 100);
    if (trusted) return OS32_ERR_INVAL;
    in_window = 1;
    for (i = 0; i < sizeof(*out); i++) ((unsigned char *)out)[i] = 0;
    n = total - skip;
    if (n > OS32_LS_BATCH) n = OS32_LS_BATCH;
    out->count = n; out->done = skip + n == total;
    out->result = positive;
    if ((int)calls == error_at) { in_window = 0; return error_code; }
    for (i = 0; i < n; i++) {
        unsigned int j;
        for (j = 0; j < OS32_LS_NAME_SIZE; j++) out->entries[i].name[j] = 0;
        out->entries[i].name[0] = 'x';
        out->entries[i].size = skip + i;
        out->entries[i].type = OS32_FILE_TYPE_FILE;
    }
    in_window = 0;
    return 0;
}
static int legacy(const char *path, void *callback, void *ctx) {
    DirEntry_Ext e = {0};
    unsigned int i;
    CHECK(trusted && path[0] == '/'); legacy_calls++;
    e.name[0] = 'x'; e.type = OS32_FILE_TYPE_FILE;
    for (i = 0; i < total; i++) { e.size = i; ((DirCallback)callback)(&e, ctx); }
    return 0;
}
void _start(void) {
    CHECK(sizeof(OS32_LsPacket) == 3708 && sizeof(OS32_LsEntry) == 264);
    unsigned int sizes[] = {0, 1, 14, 15, 28, 301};
    unsigned int i;
    table.sys_ls_window = window; table.sys_ls = legacy;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        total = sizes[i]; seen = calls = 0; original_path[0] = '/';
        CHECK(os32_ls(original_path, cb, &total) == 0 && seen == total);
        CHECK(calls == (total ? (total + 13) / 14 : 1));
    }
    total = 30; seen = calls = 0; recurse = 1;
    CHECK(os32_ls("/", cb, &total) == 0 && seen == total);
    total = 15; seen = calls = 0; null_path = 1;
    CHECK(os32_ls(0, cb, &total) == 0 && seen == total);
    null_path = 0;
    trusted = 1; seen = calls = 0;
    CHECK(os32_ls("/", cb, &total) == 0 && seen == total && legacy_calls == 1);
    trusted = 0; total = 15; seen = calls = 0; positive = 37;
    CHECK(os32_ls("/", cb, &total) == 37 && seen == total);
    total = 30; seen = calls = 0; positive = OS32_ERR_IO;
    CHECK(os32_ls("/", cb, &total) == OS32_ERR_IO && seen == total && calls == 3);
    positive = 0;
    trusted = 0; total = 30; seen = calls = 0; error_at = 2;
    CHECK(os32_ls("/", cb, &total) == OS32_ERR_IO && seen == 14 && calls == 2);
    total = 30; seen = calls = legacy_calls = 0; error_at = 1;
    CHECK(os32_ls("/", cb, &total) == OS32_ERR_IO && seen == 0 &&
          calls == 1 && legacy_calls == 0);
    seen = calls = legacy_calls = 0; error_at = 2; error_code = OS32_ERR_INVAL;
    CHECK(os32_ls("/", cb, &total) == OS32_ERR_INVAL && seen == 14 &&
          calls == 2 && legacy_calls == 0);
    finish(0);
}
