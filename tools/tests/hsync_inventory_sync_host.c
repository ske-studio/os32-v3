/* Production main + existing H2 RAM filesystem; only KAPI is replaced. */
#include "fep_boundary_runtime.h"
static char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}
extern int sprintf(char *, const char *, ...);
extern char *strcat(char *, const char *);
#include "hsync_h2_host.c"

#define DESC_FD 1100
#define DATA_FD 1101
static u8 desc[32], tree[8192];
static u32 tree_size, entries, cursor, clock_tick;
static int new_calls, source_ls, source_stat, source_reads, local_reads;
static int initial_error, job_error, short_data, busy, local_error, allocations, fail_alloc;
static int fep_closes, fep_opens;
static void put16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static void put32(u8 *p, u32 v) { put16(p, v); put16(p + 2, v >> 16); }
static u32 tick(void) { return clock_tick++; }
static int suspend_dict(int on)
{ if (on) fep_closes++; else fep_opens++; return 0; }
static void *alloc_mem(u32 n) { allocations++; return allocations == fail_alloc ? 0 : fk_mem_alloc(n); }
static int inv_open(const char *path, int flags)
{
    if (strstr(path, "/.os32-manifest/")) {
        cursor = 0;
        if (strstr(path, "/new")) {
            new_calls++;
            if (initial_error) return initial_error;
            return DESC_FD;
        }
        if (strstr(path, "/status/")) { busy = 0; return DESC_FD; }
        return DATA_FD;
    }
    return fk_sys_open(path, flags);
}
static int inv_stat(const char *path, OS32_Stat *st)
{
    if (strstr(path, "/.os32-manifest/data/")) {
        memset(st, 0, sizeof(*st)); st->st_size = tree_size; return 0;
    }
    if (!strcmp(path, SRC_PATH)) source_stat++;
    return fk_sys_stat(path, st);
}
static int inv_read(int fd, void *out, u32 want)
{
    if (fd == DESC_FD) {
        put16(desc + 6, busy ? 0 : job_error ? 2 : 1);
        put32(desc + 28, (u32)job_error);
        memcpy(out, desc, 32); return 32;
    }
    if (fd == DATA_FD) {
        if (short_data) return 0;
        if (want > tree_size - cursor) want = tree_size - cursor;
        memcpy(out, tree + cursor, want); cursor += want; return (int)want;
    }
    if (fd >= 3 && fd - 3 < FS_MAX_FDS && fs_fds[fd - 3].used) {
        const char *path = fs_nodes[fs_fds[fd - 3].node].path;
        if (!strcmp(path, SRC_PATH)) source_reads++;
        if (!strcmp(path, DST_PATH)) {
            local_reads++;
            if (local_error) return OS32_ERR_IO;
        }
    }
    return fk_sys_read(fd, out, want);
}
static int inv_ls(const char *path, void *cb, void *ctx)
{
    if (!strncmp(path, "/host", 5)) source_ls++;
    return fk_sys_ls(path, cb, ctx);
}
static void record(const char *relative, const char *path, int kind)
{
    int node = fs_find(path);
    u32 n = strlen(relative), size = kind == 1 ? fs_nodes[node].size : 0;
    u8 *r = tree + tree_size;
    memset(r, 0, 16); r[0] = kind; put16(r + 2, n);
    put32(r + 4, size); put32(r + 8, node < 0 ? 0 : fs_nodes[node].mtime);
    if (kind == 1) put32(r + 12, crc32_core_final(crc32_core_update(
        CRC32_INIT, fs_nodes[node].data, size)));
    memcpy(r + 16, relative, n); tree_size += 16 + n; entries++;
}
static void seal(void)
{
    memcpy(tree, "HMT1", 4); put16(tree + 4, 1); put16(tree + 6, 24);
    put32(tree + 8, tree_size); put32(tree + 12, entries);
    put32(tree + 16, crc32_core_final(crc32_core_update(CRC32_INIT, tree + 24, tree_size - 24)));
    memset(desc, 0, sizeof(desc)); memcpy(desc, "HMD1", 4); put16(desc + 4, 1);
    put32(desc + 24, tree_size);
}
static void prepare(u32 src_seed, u32 dst_seed, u32 sm, u32 dm)
{
    fixture_used = 0;
    setup_pair(32, src_seed, sm, 32, dst_seed, dm);
    fake_api_init(); fk_version = KAPI_VERSION;
    g_fake.sys_open = inv_open; g_fake.sys_read = inv_read;
    g_fake.sys_stat = inv_stat; g_fake.sys_ls = inv_ls;
    g_fake.get_tick = tick; g_fake.ime_dict_suspend = suspend_dict;
    g_fake.boot_image_info = fk_boot_image_info; fk_boot_valid = 0;
    g_fake.mem_alloc = alloc_mem;
    initial_error = job_error = short_data = busy = local_error = allocations = fail_alloc = 0;
    new_calls = source_ls = source_stat = source_reads = local_reads = 0;
    fk_mkdir_calls = 0;
    fep_closes = fep_opens = 0; clock_tick = 0;
    tree_size = 24; entries = 0; memset(tree, 0, sizeof(tree));
    record("a.bin", SRC_PATH, 1); seal();
}
int main(void)
{
    int rc;
    prepare(1, 2, 100, 100); rc = run1("bin");
    check(!rc && g_unchanged == 1 && !g_copied && !local_reads && !source_reads &&
          !g_local_crc_bytes && !source_stat && !source_ls && new_calls == 1 &&
          log_has("compare=crc"), "same mtime reads zero despite different CRC");
    prepare(1, 2, 100, 100); rc = run2("--verify", "bin");
    check(!rc && g_copied == 1 && g_local_crc_files == 1 && g_local_crc_bytes == 32 &&
          g_content_compares == 0 && source_reads == 2 && source_stat == 1 &&
          !source_ls && fk_rename_calls == 1 && node_equal_bytes(DST_PATH, fs_nodes[fs_find(SRC_PATH)].data, 32),
          "verify CRC comparison no remote comparison READ");
    prepare(1, 1, 200, 100); busy = 1; rc = run1("bin");
    check(!rc && g_metadata_updated == 1 && !fk_write_calls && !source_reads &&
          !source_stat && !g_copied && node_mtime(DST_PATH) == 200,
          "BUSY then metadata only");
    prepare(1, 1, 100, 100); rc = run2("--verify", "bin");
    check(!rc && g_unchanged == 1 && g_local_crc_bytes == 32 && !source_reads &&
          !fk_write_calls && !source_stat, "verify equal CRC reads only local");
    prepare(1, 1, 0, 100); rc = run1("bin");
    check(!rc && g_mtime_unknown == 1 && !fk_write_calls && g_unchanged == 1,
          "unknown mtime skips metadata");
    prepare(1, 1, 200, 100); fk_set_mtime_rc = OS32_ERR_NOSYS; rc = run1("bin");
    check(!rc && g_mtime_nosys == 1 && !fk_write_calls, "NOSYS metadata");
    prepare(1, 1, 200, 100); rc = run2("-n", "bin");
    check(!rc && g_metadata_updated == 1 && node_mtime(DST_PATH) == 100 &&
          !fk_write_calls && !fk_mkdir_calls && !fk_sync_calls, "dry metadata writes zero");
    prepare(1, 2, 100, 100); rc = run2("-f", "bin");
    check(!rc && g_copied == 1 && !g_local_crc_files, "force skips local comparison");
    prepare(1, 2, 200, 100); fs_nodes[fs_find(SRC_PATH)].data[0] ^= 1;
    rc = run1("bin");
    check(rc && dst_unchanged() && !fk_rename_calls && node_of(TMP_PATH) < 0 &&
          log_has("reason=source_changed"), "changed source CRC never published");
    prepare(1, 2, 200, 100); local_error = 1; rc = run1("bin");
    check(rc && !fk_write_calls && !fk_rename_calls, "local IO fails closed");
    for (int err = 0; err < 5; err++) {
        const int errors[] = {OS32_ERR_NOTFOUND, OS32_ERR_NOTDIR, OS32_ERR_NOSYS,
                             OS32_ERR_NAMETOOLONG, OS32_ERR_INVAL};
        prepare(1, 2, 200, 100); initial_error = errors[err]; rc = run1("bin");
        check(!rc && g_copied == 1 && new_calls == 1 && source_ls == 1 &&
              log_has("compare=legacy"), "old host fallback exactly once");
    }
    prepare(1, 2, 200, 100); fail_alloc = 2; rc = run1("bin");
    check(!rc && new_calls == 1 && source_ls == 1 && g_copied == 1,
          "inventory allocation FULL falls back once");
    prepare(1, 2, 200, 100); tree_size = 24; entries = 0;
    record("a.bin", SRC_PATH, 3); seal(); rc = run1("bin");
    check(rc && !local_reads && !source_reads && !fk_write_calls &&
          log_has("reason=type_conflict"), "other type never read as regular file");
    prepare(1, 2, 200, 100); desc[0] = 'X'; rc = run1("bin");
    check(!rc && source_ls == 1 && g_copied == 1, "real name collision fallback");
    prepare(1, 2, 200, 100); job_error = OS32_ERR_FULL; rc = run1("bin");
    check(!rc && new_calls == 1 && source_ls == 1 && g_copied == 1,
          "descriptor FULL fallback before writing");
    for (int err = 0; err < 3; err++) {
        const int errors[] = {OS32_ERR_HOSTDOWN, OS32_ERR_TIMEOUT, OS32_ERR_STALE};
        prepare(1, 2, 200, 100); job_error = errors[err]; rc = run2("--fep-off", "bin");
        check(rc && !fk_write_calls && !fk_mkdir_calls && !source_ls && !fep_closes,
              "communication failures no fallback or FEP close");
    }
    for (int bad = 0; bad < 3; bad++) {
        prepare(1, 2, 200, 100);
        if (bad == 0) tree[16] ^= 1;
        if (bad == 1) put16(desc + 4, 2);
        if (bad == 2) short_data = 1;
        rc = run2("--fep-off", "bin");
        check(rc && !fk_write_calls && !fk_mkdir_calls &&
              !source_ls && !fep_closes, "invalid inventory changes nothing");
    }
    prepare(1, 2, 200, 100); rc = run1("bin/a.bin");
    check(rc && !new_calls && !fk_write_calls, "single file stays legacy type error");
    prepare(1, 2, 200, 100); fs_add_dir("/host/bin/empty");
    record("empty", "/host/bin/empty", 2); seal(); put32(tree + 20, 7);
    rc = run2("--fep-off", "bin");
    check(!rc && node_of("/bin/empty") >= 0 && !source_ls &&
          fep_closes == 1 && fep_opens == 1 && log_has("skipped_names=7"),
          "empty directory skipped names FEP ordering");
    prepare(1, 2, 200, 100);
    char *av[] = {"hsync", "bin", 0};
    rc = hsync_main(2, av, &g_fake);
    check(rc && !new_calls && !fk_write_calls, "KAPI label gate before acquisition");
    prepare(1, 2, 200, 100);
    fs_add_dir("/etc");
    int protected_node = fs_add_file("/etc/settings.db", (const u8 *)"safe", 4);
    fs_nodes[protected_node].ino = fs_nodes[fs_find(DST_PATH)].ino;
    rc = run1("bin");
    check(!rc && g_protected == 1 && !g_local_crc_files && !local_reads && !fk_write_calls &&
          dst_unchanged(), "protected alias before local CRC");
    prepare(1, 1, 100, 100);
    fs_add_dir("/host/bin/mounted"); fs_add_dir("/bin/mounted");
    record("mounted", "/host/bin/mounted", 2); seal();
    fk_sub_prefix = "/bin/mounted"; fk_sub_dev = "ram0";
    rc = run1("bin");
    check(!rc && g_excluded == 1 && !g_local_crc_files && !fk_mkdir_calls,
          "child mount excluded before traversal");
    fk_sub_prefix = fk_sub_dev = 0;
    prepare(1, 1, 200, 100);
    strcpy(fs_nodes[fs_find("/host/bin")].path, "/host/boot");
    strcpy(fs_nodes[fs_find("/bin")].path, "/boot");
    strcpy(fs_nodes[fs_find(SRC_PATH)].path, "/host/boot/vmkernel.lz4");
    strcpy(fs_nodes[fs_find(DST_PATH)].path, "/boot/vmkernel.lz4");
    tree_size = 24; entries = 0; record("vmkernel.lz4", "/host/boot/vmkernel.lz4", 1); seal();
    rc = run1("boot");
    check(!rc && g_metadata_updated == 1 && !g_copied && !fk_write_calls &&
          !g_touched_boot && node_of("/boot/vmkernel.old") < 0,
          "boot CRC equal updates mtime without backup or reboot note");
    prepare(1, 1, 100, 100);
    fs_add_dir("/host/sys"); fs_add_file("/host/sys/ignored", (const u8 *)"x", 1);
    tree_size = 24; entries = 0;
    record("bin", "/host/bin", 2); record("bin/a.bin", SRC_PATH, 1);
    record("sys", "/host/sys", 2); record("sys/ignored", "/host/sys/ignored", 1); seal();
    rc = run0();
    check(!rc && new_calls == 1 && !source_ls && g_excluded == 1 &&
          !g_local_crc_files && g_unchanged == 1 && !g_copied &&
          node_of("/sys") < 0, "whole tree one job default sys exclusion");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
