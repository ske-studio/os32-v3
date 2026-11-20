/* Fault injection at the VFS, store and SQLite boundaries of static FEP boot. */
#include "fep_static_source.inc"
#include "fep_boundary_runtime.h"
#include "kapi_db.h"
#include "redir_access.h"

enum services_stage services_stage;
int services_public;
volatile u32 services_boot_fault;
const SqliteModuleApi sqlite_stub_api, real_api;
const SqliteModuleApi *sqlite_module_api;
static int row, target, source_fd, dest_fd, eof;
static int present[3], writes[3], stats, deletes, probes, checks, reserves, closes;
static int step_rc, cleanup_rc, probe_rc, reservation_rc, commit_rc, open_rc, abort_rc;
static int mounted, backend, ramfs, identity_calls, bad_identity, replaced, unavailable;
static int after_probe, after_abort, invalid_generation, pre_generation, dbu;
static int check_rc, follow_rc, phase, warnings, opened, short_write;
static int stat_rc, src_rc, dst_rc, read_rc, write_rc;
static void require(int good, const char *what)
{
    if (!good) { printf("FAIL row %d %s\n", row, what); exit(1); }
}
static int edition_index(const char *path)
{
    for (int i = 0; i < 3; i++)
        if (!kstrcmp(path, editions[i].path) || !kstrcmp(path, editions[i].source)) return i;
    require(0, "unexpected path");
    return -1;
}
int vfs_is_mounted(const char *path) { return !kstrcmp(path, "/dbu") ? dbu : mounted; }
const char *vfs_fstype(const char *path) { (void)path; return ramfs ? "ramfs" : "iso9660"; }
u32 vfs_path_mount_generation(const char *path)
{
    if (!kstrcmp(path, "/db")) return invalid_generation ? 0 : 1;
    return pre_generation == edition_index(path) + 1 ? 2 : 1;
}
int vfs_sqlite_backend(const char *path) { (void)path; return backend; }
int vfs_stat(const char *path, OS32_Stat *out)
{
    (void)out;
    int i = edition_index(path);
    stats++;
    if (i == target && stat_rc) return stat_rc;
    return present[i] ? 0 : VFS_ERR_NOTFOUND;
}
int vfs_open(const char *path, int flags)
{
    int i = edition_index(path);
    if (!kstrcmp(path, editions[i].source)) {
        require(flags == O_RDONLY && !source_fd, "source open");
        if (i == target && src_rc) return src_rc;
        eof = 0;
        return source_fd = 10 + i;
    }
    if (flags == O_RDONLY) {
        if (unavailable) return VFS_ERR_IO;
        return dest_fd = 30 + i;
    }
    require(flags == (O_CREAT | O_EXCL | O_WRONLY) && source_fd == 10 + i &&
            !present[i], "exclusive create");
    if (i == target && dst_rc) return dst_rc;
    present[i] = 1;
    return dest_fd = 20 + i;
}
int vfs_read_fd(int fd, void *buf, u32 size)
{
    require(size >= 4, "read buffer");
    require(fd == source_fd && dest_fd, "read descriptors");
    if (eof++ == 0) {
        kmemcpy(buf, "dict", 4);
        return 4;
    }
    return fd == 10 + target ? read_rc : 0;
}
int vfs_write_fd(int fd, const void *buf, u32 size)
{
    require(size == 4 && !memcmp(buf, "dict", 4), "copy contents");
    require(fd == dest_fd && source_fd, "write descriptors");
    writes[fd - 20]++;
    if (fd == 20 + target && write_rc) return write_rc;
    return short_write && fd == 20 + target ? 1 : (int)size;
}
void vfs_close(int fd)
{
    if (fd == source_fd && fd) source_fd = 0;
    else if (fd == dest_fd && fd) dest_fd = 0;
    else require(0, "unexpected close");
}
int vfs_sqlite_fd_identity(int fd, VfsSqliteIdentity *id)
{
    require(fd == dest_fd && fd, "identity descriptor");
    identity_calls++;
    id->mount_generation = 1 + (step_rc == identity_calls || (fd >= 30 && replaced == 2));
    id->file_id = 100 + (fd >= 30 && replaced == 1);
    return bad_identity == identity_calls ? VFS_ERR_NOSYS : 0;
}
int vfs_rm(const char *path)
{
    int i = edition_index(path);
    require(!source_fd && !dest_fd && present[i], "delete after close and create");
    deletes++;
    if (!cleanup_rc || cleanup_rc == VFS_ERR_NOTFOUND) present[i] = 0;
    return cleanup_rc;
}
int fep_meta_probe(const char *path, FepMeta *meta)
{
    require(!kstrcmp(path, IME_DICT_PATH) && present[0], "probe main");
    probes++;
    after_probe = probe_rc == OS32_ERR_BUSY;
    meta->reserve_kb = 1;
    return probe_rc;
}
int os32_sqlite_outer_boot_check(int live, int reserve)
{
    checks++;
    if (live) {
        require(live == 2 && phase == 2 && reserve == FEP_DICT_MIN_RESERVE_KB * 1024, "commit order");
        return commit_rc;
    }
    if (reserve) {
        require(probes && reserve == FEP_DICT_MIN_RESERVE_KB * 1024, "reservation order");
        reserves++;
        phase = !reservation_rc;
        return reservation_rc;
    }
    if (after_probe || after_abort) {
        after_abort = after_probe = 0;
        return follow_rc;
    }
    return check_rc;
}
int ime_display_boot(u32 fault) { (void)fault; require(phase == 1, "prepare before open"); return 0; }
void ime_display_abort(void) {}
int fep_store_open(FepStore *s, const char *path, const char *user)
{
    (void)s;
    require(!kstrcmp(path, IME_DICT_PATH) && !kstrcmp(user, dbu ? FEP_USER_PATH : ""), "snapshot");
    opened++;
    phase = 2;
    return open_rc;
}
int fep_store_close(FepStore *s) { (void)s; closes++; after_abort = 1; return abort_rc; }
u32 fep_store_live_connections(const FepStore *s) { (void)s; return 2; }
int fep_store_switch(FepStore *s, const char *path) { (void)s; (void)path; return 0; }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; warnings++; }
static void reset(int n)
{
    row = n;
    state = IDLE;
    pending = 0;
    for (int i = 0; i < 3; i++) {
        editions[i].done = editions[i].skip = present[i] = writes[i] = 0;
    }
    services_public = backend = mounted = ramfs = dbu = 1;
    services_stage = SERVICES_SQLITE_ONLY;
    sqlite_module_api = &real_api;
    target = source_fd = dest_fd = stats = deletes = probes = checks = reserves = closes = 0;
    step_rc = cleanup_rc = probe_rc = reservation_rc = commit_rc = open_rc = abort_rc = 0;
    identity_calls = bad_identity = replaced = unavailable = after_probe = after_abort = 0;
    invalid_generation = pre_generation = check_rc = follow_rc = phase = warnings = opened = 0;
    short_write = stat_rc = src_rc = dst_rc = read_rc = write_rc = 0;
    ime_suspend_state()->dict_loaded = 0;
    fep_boot_config_clear();
}
static void expect(int rc, enum boot_state next)
{
    int result = ime_dict_boot();
    require(result == rc && state == next && !source_fd && !dest_fd, "result/state/descriptors");
    if (rc) {
        FepMeta meta;
        u32 kb;
        require(fep_boot_config_get(&meta, &kb) == OS32_ERR_AGAIN, "snapshot cleared");
    }
    if (next == FILE_QUARANTINE || next == CONNECTION_QUARANTINE) {
        int before = checks;
        ime_suspend_state()->dict_loaded = 1;
        require(ime_dict_boot() == OS32_ERR_BUSY && checks == before, "gate before loaded");
        require(ime_switch_dict(1) == OS32_ERR_BUSY && ime_dict_suspend(0) == OS32_ERR_BUSY,
                "quarantined switch/suspend");
        require(services_public == (next == FILE_QUARANTINE) &&
                services_stage == (next == FILE_QUARANTINE ? SERVICES_SQLITE_ONLY :
                                   SERVICES_MINIMAL_QUARANTINED) &&
                sqlite_module_api == (next == FILE_QUARANTINE ? &real_api : &sqlite_stub_api),
                "SQLite publication");
        require(kapi_db_error_code(-1) == (next == FILE_QUARANTINE ? 0 : OS32_ERR_NOSYS),
                "DB diagnostic slot follows quarantine kind");
    }
}
static void retry(void)
{
    stat_rc = src_rc = dst_rc = read_rc = write_rc = probe_rc = check_rc = 0;
    reservation_rc = open_rc = commit_rc = short_write = follow_rc = 0;
    expect(0, READY);
}
static void scenario(int n)
{
    reset(n);
    switch (n) {
    case 1: state = FILE_QUARANTINE; expect(OS32_ERR_BUSY, FILE_QUARANTINE); break;
    case 2: state = READY; ime_suspend_state()->dict_loaded = 1; expect(0, READY);
        require(!checks && !opened, "already loaded"); break;
    case 3:
        for (int i = 0; i < 5; i++) {
            reset(n); mounted = i != 0; backend = i == 1 ? 0 : i == 2 ? -1 : 1;
            invalid_generation = i == 3; ramfs = i != 4;
            services_stage = SERVICES_MINIMAL; services_public = 0;
            expect(OS32_ERR_NOMOUNT, IDLE);
            require(!stats && !checks && !deletes && !writes[0] &&
                    services_stage == SERVICES_MINIMAL && !services_public, "no root I/O or publication");
        }
        ramfs = 1; retry(); break;
    case 4: case 5: case 6:
        check_rc = n == 4 ? SQLITE_BUSY : n == 5 ? SQLITE_MISUSE : OS32_ERR_NOSYS;
        services_stage = SERVICES_MINIMAL; services_public = 0;
        expect(n == 6 ? OS32_ERR_NOSYS : OS32_ERR_AGAIN, IDLE);
        require(checks == 1 && !reserves && !closes && !stats && !services_public &&
                services_stage == SERVICES_MINIMAL, "initial check alone"); retry(); break;
    case 7: case 8:
        stat_rc = n == 7 ? VFS_ERR_STALE : VFS_ERR_IO;
        expect(n == 7 ? OS32_ERR_AGAIN : stat_rc, IDLE); retry(); break;
    case 9: case 10:
        src_rc = n == 9 ? VFS_ERR_NOTFOUND : VFS_ERR_NOSPC;
        expect(n == 9 ? OS32_ERR_AGAIN : src_rc, IDLE); retry(); break;
    case 11: dst_rc = VFS_ERR_EXIST; expect(dst_rc, IDLE); require(!deletes, "create failure"); retry(); break;
    case 12: case 13: case 14: case 15: case 16:
        read_rc = n == 12 || n == 16 ? OS32_ERR_HOSTDOWN : 0;
        write_rc = n == 13 ? VFS_ERR_NOSPC : n == 15 ? VFS_ERR_ROFS : 0;
        short_write = n == 14;
        cleanup_rc = n == 16 ? VFS_ERR_IO : n == 12 ? VFS_ERR_NOTFOUND : 0;
        expect(n == 16 ? OS32_ERR_BUSY : n == 12 ? OS32_ERR_AGAIN :
               short_write ? OS32_ERR_IO : write_rc, n == 16 ? FILE_QUARANTINE : IDLE);
        require(deletes == 1, "remove incomplete main"); if (n != 16) retry(); break;
    case 17: bad_identity = 2; probe_rc = OS32_ERR_INVAL; expect(probe_rc, IDLE);
        require(!deletes && present[0] && identity_calls == 2, "no pending identity"); retry(); break;
    case 18: case 19: case 20: case 21:
        probe_rc = n == 18 ? OS32_ERR_IO : OS32_ERR_BUSY;
        follow_rc = n == 20 ? SQLITE_BUSY : n == 21 ? SQLITE_MISUSE : 0;
        expect(n == 18 ? probe_rc : n == 19 ? OS32_ERR_AGAIN : OS32_ERR_BUSY,
               n <= 19 ? IDLE : CONNECTION_QUARANTINE);
        require(!deletes && pending && !reserves, "pending across probe failure");
        if (n <= 19) retry(); break;
    case 22: present[0] = 1; probe_rc = OS32_ERR_INVAL; expect(probe_rc, IDLE);
        require(!deletes && !writes[0] && !identity_calls, "existing main retained"); retry(); break;
    case 23: case 24: case 25:
        probe_rc = OS32_ERR_IO; expect(probe_rc, IDLE);
        replaced = n == 23; cleanup_rc = n == 25 ? VFS_ERR_BUSY : 0;
        probe_rc = OS32_ERR_INVAL;
        expect(n == 25 ? OS32_ERR_BUSY : probe_rc, n == 25 ? FILE_QUARANTINE : IDLE);
        require(deletes == (n != 23) && present[0] == (n != 24), "pending identity cleanup");
        if (n != 25) { retry(); require(writes[0] == 1 + (n == 24), "recopy deleted main"); }
        break;
    case 26: case 27: case 28: case 29: case 30: case 31: case 32:
        target = 1; present[2] = n == 26;
        src_rc = n == 26 ? VFS_ERR_NOTFOUND : n == 27 ? VFS_ERR_STALE : n == 28 ? VFS_ERR_IO : 0;
        dst_rc = n == 29 ? VFS_ERR_EXIST : 0;
        read_rc = n == 30 ? VFS_ERR_STALE : n >= 31 ? VFS_ERR_NOSPC : 0;
        cleanup_rc = n == 32 ? VFS_ERR_BUSY : 0;
        reservation_rc = SQLITE_BUSY;
        expect(n == 32 ? OS32_ERR_BUSY : OS32_ERR_AGAIN, n == 32 ? FILE_QUARANTINE : IDLE);
        require(deletes == (n >= 30) && writes[0] == 1 && warnings == (n == 28 || n == 29 || n == 31),
                "variant cleanup/log");
        require(reserves == (n != 27 && n != 30 && n != 32), "continue or retry variant");
        if (n != 32) {
            int before = writes[1]; retry();
            require(writes[0] == 1 && writes[1] == before + (n != 29 && n != 31), "variant retry policy");
        }
        break;
    case 33: case 34: case 35:
        reservation_rc = n == 33 ? SQLITE_MISUSE : n == 34 ? SQLITE_NOMEM : SQLITE_ERROR;
        expect(n == 33 ? OS32_ERR_AGAIN : n == 34 ? OS32_ERR_NOSPC : OS32_ERR_IO, IDLE);
        require(!closes && checks == 2 && !opened, "reservation failure alone"); retry(); break;
    case 39: dbu = 0; expect(0, READY);
        require(services_public && services_stage == SERVICES_READY && opened == 1 && checks == 3 &&
                kapi_db_error_code(-1) == 0,
                "committed");
        { int db = kapi_db_open(":memory:");
          require(db >= 0 && !kapi_db_close(db), "application DB alongside committed dictionary"); }
        break;
    default: /* 36–38 open and 40–42 commit */
        open_rc = n <= 38 ? SQLITE_ERROR : 0;
        commit_rc = n >= 40 ? SQLITE_BUSY : 0;
        abort_rc = n == 37 || n == 41;
        follow_rc = n == 38 || n == 42 ? SQLITE_BUSY : 0;
        expect(abort_rc || follow_rc ? OS32_ERR_BUSY : OS32_ERR_IO,
               abort_rc || follow_rc ? CONNECTION_QUARANTINE : IDLE);
        require(closes == 1 && checks == 2 + (n >= 40) + !abort_rc, "rollback sequence");
        if (!abort_rc && !follow_rc) retry();
    }
}
int main(void)
{
    for (int i = 1; i <= 42; i++) scenario(i);
    for (int i = 0; i < 3; i++) {
        reset(43); pre_generation = i + 1; expect(OS32_ERR_NOMOUNT, IDLE);
        require(!writes[i] && !deletes, "mount before open");
    }
    for (int i = 0; i < 4; i++) {
        reset(44); bad_identity = i & 1; step_rc = !bad_identity; cleanup_rc = i >= 2 ? VFS_ERR_IO : 0;
        expect(cleanup_rc ? OS32_ERR_BUSY : OS32_ERR_NOMOUNT, cleanup_rc ? FILE_QUARANTINE : IDLE);
        require(deletes == 1 && !writes[0], "mount after open");
    }
    for (int i = 0; i < 3; i++) {
        reset(23); probe_rc = OS32_ERR_NOTFOUND; expect(probe_rc, IDLE);
        probe_rc = OS32_ERR_INVAL;
        replaced = i == 0 ? 2 : 0;
        unavailable = i == 1;
        bad_identity = i == 2 ? identity_calls + 1 : 0;
        expect(probe_rc, IDLE);
        require(!pending && !deletes, "replacement mount, missing fd or unavailable identity");
    }
    reset(12); read_rc = VFS_ERR_STALE; expect(OS32_ERR_AGAIN, IDLE); retry();
    reset(9); src_rc = OS32_ERR_HOSTDOWN; expect(OS32_ERR_AGAIN, IDLE); retry();
    reset(8); stat_rc = VFS_ERR_ROFS; expect(VFS_ERR_ROFS, IDLE); retry();
    reset(28); target = 1; stat_rc = VFS_ERR_IO; expect(0, READY);
    require(!writes[1] && warnings == 1 && writes[2], "optional stat failure");
    reset(33); reservation_rc = SQLITE_BUSY; expect(OS32_ERR_AGAIN, IDLE);
    require(!closes && checks == 2, "busy reservation alone");
    reset(45); open_rc = OS32_ERR_INVAL; expect(open_rc, IDLE);
    return 0;
}

const IME_Render g_ime_render_tvram;
int ime_display_cols(void) { return 80; }
int ime_display_preedit_row(void) { return 24; }
int ime_display_restore(int top, int rows) { (void)top; (void)rows; return 0; }
void tvram_set_scroll_reserve(int rows) { (void)rows; }
int res_owner_get(void) { return 1; }
int ring3_call_from_user(void) { return 0; }
int sqlite_engine_available(int io) { (void)io; return 1; }

/* The real DB slot opens and closes an application group after FEP commit. */
u32 __kernel_resident_end;
int redir_access_capture(RedirAccess *out) { (void)out; return 1; }
int copy_caller_cstr(const struct caller_access *c, const char *src, char *dst, u32 cap)
{
    (void)c;
    if (kstrlen(src) >= cap) return 0;
    kstrcpy(dst, src);
    return 1;
}
int copy_caller_bytes(const struct caller_access *c, const void *src, void *dst, u32 len)
{ (void)c; kmemcpy(dst, src, len); return 1; }
int vfs_resolve_path(const char *src, char *dst, int cap)
{ return copy_caller_cstr(0, src, dst, cap) ? 0 : VFS_ERR_INVAL; }
int os32_sqlite_outer_group_acquire(int kind, int owner, Os32SqliteGroup *group)
{
    require(kind == OS32_SQLITE_EXEC && owner == 1 && state == READY, "application group");
    group->generation = 1;
    return SQLITE_OK;
}
int os32_sqlite_outer_group_open(const Os32SqliteGroup *group, const char *path, int flags, sqlite3 **db)
{
    require(group->generation && !kstrcmp(path, ":memory:") &&
            flags == (SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE), "application open");
    *db = (sqlite3 *)group;
    return SQLITE_OK;
}
int os32_sqlite_exec(sqlite3 *db, const char *sql, SqliteExecCallback cb, void *arg, char **error)
{ (void)db; (void)sql; (void)cb; (void)arg; (void)error; return SQLITE_OK; }
int os32_sqlite_get_autocommit(sqlite3 *db) { (void)db; return 1; }
int os32_sqlite_close(sqlite3 *db) { (void)db; return SQLITE_OK; }
int os32_sqlite_outer_group_begin_close(const Os32SqliteGroup *group)
{ (void)group; return SQLITE_OK; }
int os32_sqlite_outer_group_finish(const Os32SqliteGroup *group, int close_rc)
{ (void)group; return close_rc; }
int os32_sqlite_outer_group_quarantine(const Os32SqliteGroup *group, int stage, int rc)
{ (void)group; (void)stage; require(0, "unexpected DB quarantine"); return rc; }
int os32_sqlite_finalize(sqlite3_stmt *stmt)
{ (void)stmt; require(0, "unexpected DB statement"); return SQLITE_ERROR; }
int os32_sqlite_extended_errcode(sqlite3 *db) { (void)db; return SQLITE_ERROR; }
const char *os32_sqlite_errmsg(sqlite3 *db) { (void)db; return "error"; }
const char *os32_sqlite_errstr(int rc) { (void)rc; return "error"; }
