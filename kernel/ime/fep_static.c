/* Static FEP boot transaction. Module images use the resident facade. */
#ifdef OS32_FEP_STATIC
#include "ime.h"
#include "../sqlite_engine.h"
#include "../../lib/sqlite3/generated/sqlite_facade.h"
#include "kprintf.h"
#include "kstring.h"

enum boot_state { IDLE, PREPARED, READY, FILE_QUARANTINE, CONNECTION_QUARANTINE };
static enum boot_state state;
static int reserve_bytes;
static int pending;
static VfsSqliteIdentity copied;

struct edition {
    const char *path;
    const char *source;
    int done, skip;
};
static struct edition editions[] = {
    {IME_DICT_PATH, "/host" IME_DICT_PATH, 0, 0},
    {FEP_DICT_PATH_S, "/host" FEP_DICT_PATH_S, 0, 0},
    {FEP_DICT_PATH_L, "/host" FEP_DICT_PATH_L, 0, 0},
};

static int failed(int rc)
{
    fep_boot_config_clear();
    return rc;
}

int fep_static_gate(void)
{
    return state == FILE_QUARANTINE || state == CONNECTION_QUARANTINE
        ? failed(OS32_ERR_BUSY) : 0;
}

static int connection_quarantine(void)
{
    state = CONNECTION_QUARANTINE;
    services_public = 0;
    services_stage = SERVICES_MINIMAL_QUARANTINED;
    sqlite_module_api = &sqlite_stub_api;
    return OS32_ERR_BUSY;
}

static int remove_copy(struct edition *edition, int result)
{
    int rc = vfs_rm(edition->path);
    if (rc && rc != VFS_ERR_NOTFOUND) {
        state = FILE_QUARANTINE;
        return OS32_ERR_BUSY;
    }
    edition->done = 0;
    if (edition == editions) pending = 0;
    return result;
}

static int disconnected(int rc)
{
    return rc == OS32_ERR_HOSTDOWN || rc == VFS_ERR_STALE;
}

static int optional_error(struct edition *edition, int rc, int skip)
{
    if (edition == editions) return rc;
    edition->skip = skip;
    kprintf(KLOG_ATTR, "[IME] copy %s rc=%d\n", edition->path, rc);
    return 0;
}

static int transfer(int source, int dest)
{
    char buf[1024];
    int n;
    while ((n = vfs_read_fd(source, buf, sizeof(buf))) > 0) {
        int written = vfs_write_fd(dest, buf, n);
        if (written != n) return written < 0 ? written : VFS_ERR_IO;
    }
    return n;
}

static int copy_edition(struct edition *edition, u32 generation)
{
    OS32_Stat st;
    VfsSqliteIdentity id;
    if (vfs_path_mount_generation(edition->path) != generation)
        return OS32_ERR_NOMOUNT;
    if (edition->done || edition->skip) return 0;
    int rc = vfs_stat(edition->path, &st);
    if (!rc) {
        edition->done = 1;
        return 0;
    }
    if (disconnected(rc)) return OS32_ERR_AGAIN;
    if (rc != VFS_ERR_NOTFOUND) return optional_error(edition, rc, 0);
    int src = vfs_open(edition->source, O_RDONLY);
    if (src == VFS_ERR_NOTFOUND) return edition == editions ? OS32_ERR_AGAIN : 0;
    if (disconnected(src)) return OS32_ERR_AGAIN;
    if (src < 0) return optional_error(edition, src, 0);
    int dst = vfs_open(edition->path, O_WRONLY | O_CREAT | O_EXCL);
    if (dst < 0) {
        vfs_close(src);
        return optional_error(edition, dst, 1);
    }
    if (vfs_sqlite_fd_identity(dst, &id) || id.mount_generation != generation) {
        vfs_close(dst);
        vfs_close(src);
        return remove_copy(edition, OS32_ERR_NOMOUNT);
    }
    rc = transfer(src, dst);
    if (!rc && edition == editions)
        pending = vfs_sqlite_fd_identity(dst, &copied) == 0;
    vfs_close(dst);
    vfs_close(src);
    if (rc) {
        rc = remove_copy(edition, rc);
        if (state == FILE_QUARANTINE) return rc;
        if (disconnected(rc)) return OS32_ERR_AGAIN;
        return optional_error(edition, rc, 1);
    }
    edition->done = 1;
    return 0;
}

static int probe_main(FepMeta *meta)
{
    int rc = fep_meta_probe(IME_DICT_PATH, meta);
    if (!rc) pending = 0;
    if (rc == OS32_ERR_BUSY)
        return os32_sqlite_outer_boot_check(0, 0) ? connection_quarantine() : OS32_ERR_AGAIN;
    if (rc != OS32_ERR_INVAL || !pending) return rc;
    int fd = vfs_open(IME_DICT_PATH, O_RDONLY);
    VfsSqliteIdentity id;
    int same = 0;
    if (fd >= 0) {
        same = !vfs_sqlite_fd_identity(fd, &id) &&
            id.mount_generation == copied.mount_generation && id.file_id == copied.file_id;
        vfs_close(fd);
    }
    pending = 0;
    return same ? remove_copy(editions, rc) : rc;
}

int fep_static_prepare(void)
{
    FepMeta meta;
    state = IDLE;
    u32 generation = vfs_path_mount_generation("/db");
    if (!generation || !vfs_is_mounted("/db") || kstrcmp(vfs_fstype("/db"), "ramfs") ||
        vfs_sqlite_backend(IME_DICT_PATH) <= 0) return failed(OS32_ERR_NOMOUNT);
    int rc = os32_sqlite_outer_boot_check(0, 0);
    if (rc == SQLITE_BUSY || rc == SQLITE_MISUSE) return failed(OS32_ERR_AGAIN);
    if (rc) return failed(OS32_ERR_NOSYS);
    rc = copy_edition(editions, generation);
    if (!rc) rc = probe_main(&meta);
    for (unsigned i = 1; !rc && i < sizeof(editions) / sizeof(editions[0]); i++)
        rc = copy_edition(&editions[i], generation);
    if (rc) return failed(rc);
    u32 kb = meta.reserve_kb < FEP_DICT_MIN_RESERVE_KB ? FEP_DICT_MIN_RESERVE_KB : meta.reserve_kb;
    reserve_bytes = kb * 1024;
    rc = os32_sqlite_outer_boot_check(0, reserve_bytes);
    if (rc == SQLITE_BUSY || rc == SQLITE_MISUSE) return failed(OS32_ERR_AGAIN);
    if (rc) return failed(rc == SQLITE_NOMEM ? OS32_ERR_NOSPC : OS32_ERR_IO);
    fep_boot_config_set(&meta, kb);
    rc = fep_boot_user_path_set(FEP_USER_PATH);
    fep_boot_user_rc_set(vfs_is_mounted("/dbu") ? rc : OS32_ERR_NOSYS);
    state = PREPARED;
    return 0;
}

u32 ime_boot_connections(void)
{
    return fep_store_live_connections(ime_current_store());
}

int fep_static_commit(int open_rc)
{
    if (!open_rc && !os32_sqlite_outer_boot_check(ime_boot_connections(), reserve_bytes)) {
        state = READY;
        services_stage = SERVICES_READY;
        services_public = 1;
        return 0;
    }
    if (ime_boot_abort() || os32_sqlite_outer_boot_check(0, 0))
        return failed(connection_quarantine());
    state = IDLE;
    services_stage = SERVICES_SQLITE_ONLY;
    services_public = 1;
    return failed(open_rc < 0 ? open_rc : OS32_ERR_IO);
}

void fep_static_boot(void)
{
    ime_init();
    (void)ime_dict_boot();
}
#endif
