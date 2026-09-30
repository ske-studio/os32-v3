/* F2a: real FD table; only filesystem/TTY/owner boundaries are mocks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fs/vfs_fd.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)
static int current_owner = 2, owner_sets, probes, size_rc, write_rc;
/* vfs_resolve_path が呼ばれたときに立っているべき owner。F2a/F2b は 2 のまま
 * (vfs_open_sqlite が広域 owner を動かさないことの確認)。S0-K の
 * kapi_db_v50_host.c だけが owner を渡り歩くのでここも一緒に動かす。 */
static int resolve_owner = 2;
/* 相対名に前置する cwd。fs/vfs.c の vfs_resolve_path は cwd + "/" + input を
 * VFS_MAX_PATH の作業バッファで連結して**切り詰めてから** `.` / `..` を畳む
 * ので、「入力は短いが解決名は上限」「溢れた入力が別の絶対名に化ける」の
 * どちらも作れる。F2a/F2b は "" のまま (絶対名しか渡さない)。 */
static const char *resolve_cwd = "";
static VfsOps mock_ops;
int res_owner_get(void) { return current_owner; }
void res_owner_set(int owner) { owner_sets++; current_owner = owner; }
/* fs/vfs.c の vfs_resolve_path の移植 (票 TASK_VFS_FD_PATH 以降の形)。
 * 以前の写しは「cwd と input を VFS_MAX_PATH の tmp へ連結して切り詰めてから
 * 畳む」旧実装をなぞり、Codex 往復 3 の反例 (切り詰めた末尾が別の絶対名に
 * 化ける) を作れるようにしていた。実物は切り詰めずに断るようになったので、
 * 写しも同じ規則にする:
 *   (1) 入力は NUL 抜き VFS_MAX_PATH - 1 まで (超えたら NAMETOOLONG)
 *   (2) 相対名は cwd + "/" + input を大きい作業領域で連結してから畳む
 *   (3) 要素表が満杯のところへ積もうとしたら NAMETOOLONG (黙って捨てない)
 *   (4) 結果が size に収まらなければ NAMETOOLONG (out は空) */
const char *vfs_cwd(void) { return resolve_cwd[0] ? resolve_cwd : "/"; }

int vfs_resolve_path(const char *in, char *out, int size)
{
    static char tmp[VFS_MAX_PATH * 2 + 1];
    const char *parts[VFS_MAX_PATH_DEPTH];
    int plen[VFS_MAX_PATH_DEPTH];
    int num_parts = 0;
    int i, p, o, t = 0, in_len;

    probes++;
    CHECK(current_owner == resolve_owner);
    if (!out || size <= 0) return VFS_ERR_INVAL;
    out[0] = '\0';
    if (!in || !in[0]) {
        if ((int)strlen(vfs_cwd()) + 1 > size) return VFS_ERR_NAMETOOLONG;
        str_cpy(out, vfs_cwd(), size);
        return VFS_OK;
    }
    for (in_len = 0; in_len < VFS_MAX_PATH && in[in_len]; in_len++) { }
    if (in_len >= VFS_MAX_PATH) return VFS_ERR_NAMETOOLONG;

    if (in[0] != '/') {
        const char *c = vfs_cwd();
        while (*c) tmp[t++] = *c++;
        if (t > 0 && tmp[t - 1] != '/') tmp[t++] = '/';
    }
    for (i = 0; i < in_len; i++) tmp[t++] = in[i];
    tmp[t] = '\0';

    p = 0;
    while (tmp[p] != '\0') {
        int start, len;
        char c;
        if (tmp[p] == '/') { p++; continue; }
        start = p;
        while (tmp[p] != '/' && tmp[p] != '\0') p++;
        len = p - start;
        c = tmp[p];
        tmp[p] = '\0';
        if (len == 1 && tmp[start] == '.') {
            /* nop */
        } else if (len == 2 && tmp[start] == '.' && tmp[start + 1] == '.') {
            if (num_parts > 0) num_parts--;
        } else {
            if (len > 255) return VFS_ERR_NAMETOOLONG;
            if (num_parts >= VFS_MAX_PATH_DEPTH) return VFS_ERR_NAMETOOLONG;
            parts[num_parts] = &tmp[start];
            plen[num_parts] = len;
            num_parts++;
        }
        if (c == '\0') break;
        p++;
    }

    o = 1;
    for (i = 0; i < num_parts; i++) o += plen[i] + ((i < num_parts - 1) ? 1 : 0);
    if (o + 1 > size) return VFS_ERR_NAMETOOLONG;
    out[0] = '/';
    o = 1;
    for (i = 0; i < num_parts; i++) {
        int j;
        for (j = 0; j < plen[i]; j++) out[o++] = parts[i][j];
        if (i < num_parts - 1) out[o++] = '/';
    }
    out[o] = '\0';
    return VFS_OK;
}
VfsOps *vfs_route(const char *path, char *out, int size, void **ctx)
{ probes++; str_cpy(out, path, size); *ctx = &mock_ops; return &mock_ops; }
int vfs_path_kind(const char *path) { probes++; return VFS_KIND_FILE; }
/* fs/vfs.c 側の実装 (マウント単位の st_dev)。ここでは固定値で足りる */
u32 vfs_path_dev(const char *path) { return 1; }
static int mock_size(void *ctx, const char *path, u32 *size)
{ probes++; *size = 8; return size_rc; }
static int mock_write(void *ctx, const char *path, const void *buf, u32 size)
{ probes++; return write_rc; }
static int mock_read_stream(void *ctx, const char *path, void *buf,
                            u32 size, u32 offset)
{ probes++; memset(buf, 'R', size); return (int)size; }
static int mock_write_stream(void *ctx, const char *path, const void *buf,
                             u32 size, u32 offset)
{ probes++; return (int)size; }
int fd_is_redirected(int fd) { return 0; }
u16 fd_redirect_ifmt(int fd, int *out_file_fd)
{ (void)fd; if (out_file_fd) *out_file_fd = -1; return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *buf, u32 size) { return VFS_ERR_INVAL; }
int fd_redirect_write(int fd, const void *buf, u32 size) { return VFS_ERR_INVAL; }
int kbd_getchar(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color) { }

static void explicit_owner(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease;
    CHECK(vfs_open_sqlite("/db", O_RDWR, 1, &cookie, 7, &lease) == VFS_OK);
    CHECK(open_files[lease.fd].owner == 1);
    CHECK(current_owner == 2 && owner_sets == 0);
}

static void generic_barrier(const char *action)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease;
    CHECK(vfs_open_sqlite("/db", O_RDWR, 1, &cookie, 7, &lease) == VFS_OK);
    if (!strcmp(action, "direct_close")) vfs_close(lease.fd);
    if (!strcmp(action, "owned_close")) vfs_close_owned(1);
    if (!strcmp(action, "protect")) {
        CHECK(vfs_fd_set_protect(lease.fd, 1) == VFS_ERR_INVAL);
        CHECK(vfs_fd_set_protect(lease.fd, 0) == VFS_ERR_INVAL);
    }
    CHECK(open_files[lease.fd].in_use == 1);
    CHECK(open_files[lease.fd].owner == 1);
    CHECK(open_files[lease.fd].protect == 0);
}

static void verified_close(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease, wrong;
    VfsFile before;
    CHECK(vfs_open_sqlite("/db", O_RDWR, 1, &cookie, 7, &lease) == VFS_OK);
    before = open_files[lease.fd];
    wrong = lease;
    wrong.cookie.generation++;
    CHECK(vfs_close_sqlite(&wrong) == VFS_ERR_INVAL);
    CHECK(!memcmp(&before, &open_files[lease.fd], sizeof(before)));
    wrong = lease;
    wrong.cookie.group_index++;
    CHECK(vfs_close_sqlite(&wrong) == VFS_ERR_INVAL);
    wrong = lease;
    wrong.generation++;
    CHECK(vfs_close_sqlite(&wrong) == VFS_ERR_INVAL);
    CHECK(vfs_close_sqlite(&lease) == VFS_OK);
    CHECK(!open_files[lease.fd].in_use);
    CHECK(vfs_close_sqlite(&lease) == VFS_ERR_INVAL);
}

static void stale_reuse(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease old, fresh;
    VfsFile before;
    int fd;
    CHECK(vfs_open_sqlite("/old", O_RDWR, 1, &cookie, 7, &old) == VFS_OK);
    CHECK(vfs_close_sqlite(&old) == VFS_OK);
    CHECK(vfs_open_sqlite("/new", O_RDWR, 1, &cookie, 7, &fresh) == VFS_OK);
    CHECK(old.fd == fresh.fd);
    before = open_files[fresh.fd];
    CHECK(vfs_close_sqlite(&old) == VFS_ERR_INVAL);
    CHECK(!memcmp(&before, &open_files[fresh.fd], sizeof(before)));
    CHECK(fresh.generation != 0 && old.generation != fresh.generation);
    CHECK(vfs_close_sqlite(&fresh) == VFS_OK);
    fd = vfs_open("/generic", O_RDWR);
    CHECK(fd == old.fd);
    before = open_files[fd];
    CHECK(vfs_close_sqlite(&fresh) == VFS_ERR_INVAL);
    CHECK(!memcmp(&before, &open_files[fd], sizeof(before)));
}

static void validation(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease, bad;
    VfsFile before[VFS_MAX_OPEN_FILES];
    int io;
    CHECK(vfs_open_sqlite("/db", O_RDWR, 1, &cookie, 7, &lease) == VFS_OK);
    memcpy(before, open_files, sizeof(before));
    io = probes;
    CHECK(vfs_validate_sqlite(&lease) == VFS_OK);
    bad = lease; bad.fd = -1;
    CHECK(vfs_validate_sqlite(&bad) == VFS_ERR_INVAL);
    bad.fd = VFS_MAX_OPEN_FILES;
    CHECK(vfs_validate_sqlite(&bad) == VFS_ERR_INVAL);
    bad.fd = 0;
    CHECK(vfs_validate_sqlite(&bad) == VFS_ERR_INVAL);
    bad = lease; bad.generation++;
    CHECK(vfs_validate_sqlite(&bad) == VFS_ERR_INVAL);
    bad = lease; bad.cookie.group_index++;
    CHECK(vfs_validate_sqlite(&bad) == VFS_ERR_INVAL);
    bad = lease; bad.cookie.generation++;
    CHECK(vfs_validate_sqlite(&bad) == VFS_ERR_INVAL);
    CHECK(vfs_validate_sqlite(NULL) == VFS_ERR_INVAL);
    CHECK(vfs_close_sqlite(NULL) == VFS_ERR_INVAL);
    CHECK(probes == io && !memcmp(before, open_files, sizeof(before)));
}

static void count_members(void)
{
    VfsSqliteCookie a = { 1, 1 }, b = { 1, 2 }, c = { 2, 1 };
    VfsSqliteLease x, y, z;
    VfsFile before[VFS_MAX_OPEN_FILES];
    int io;
    CHECK(vfs_open_sqlite("/main", O_RDWR, 1, &a, 7, &x) == VFS_OK);
    CHECK(vfs_open_sqlite("/journal", O_RDWR, 1, &a, 8, &y) == VFS_OK);
    CHECK(vfs_open_sqlite("/other", O_RDWR, 1, &b, 9, &z) == VFS_OK);
    memcpy(before, open_files, sizeof(before)); io = probes;
    CHECK(vfs_count_sqlite(&a) == 2);
    CHECK(vfs_count_sqlite(&b) == 1);
    CHECK(vfs_count_sqlite(&c) == 0);
    CHECK(vfs_count_sqlite(NULL) == VFS_ERR_INVAL);
    CHECK(probes == io && !memcmp(before, open_files, sizeof(before)));
    CHECK(vfs_close_sqlite(&x) == VFS_OK);
    CHECK(vfs_count_sqlite(&a) == 1);
    CHECK(vfs_close_sqlite(&y) == VFS_OK);
    CHECK(vfs_count_sqlite(&a) == 0);
}

static void quarantine(void)
{
    VfsSqliteCookie a = { 1, 1 }, b = { 1, 2 };
    VfsSqliteLease x, y, z;
    VfsFile saved, other;
    int fd, io;
    CHECK(vfs_open_sqlite("/main", O_RDWR, 1, &a, 7, &x) == VFS_OK);
    CHECK(vfs_open_sqlite("/journal", O_RDWR, 1, &a, 8, &y) == VFS_OK);
    CHECK(vfs_close_sqlite(&x) == VFS_OK);
    fd = vfs_open("/unrelated", O_RDWR);
    CHECK(fd == x.fd);
    other = open_files[fd]; io = probes;
    CHECK(vfs_quarantine_sqlite(&a) == VFS_OK);
    CHECK(vfs_validate_sqlite(&y) == VFS_ERR_INVAL);
    saved = open_files[y.fd];
    CHECK(vfs_close_sqlite(&y) == VFS_ERR_INVAL);
    CHECK(vfs_close_sqlite(&x) == VFS_ERR_INVAL);
    CHECK(vfs_quarantine_sqlite(&a) == VFS_OK);
    CHECK(vfs_quarantine_sqlite(NULL) == VFS_ERR_INVAL);
    vfs_close(y.fd); vfs_close_owned(1);
    CHECK(vfs_fd_set_protect(y.fd, 0) == VFS_ERR_INVAL);
    CHECK(vfs_fd_set_protect(y.fd, 1) == VFS_ERR_INVAL);
    CHECK(!memcmp(&saved, &open_files[y.fd], sizeof(saved)));
    CHECK(!memcmp(&other, &open_files[fd], sizeof(other)));
    CHECK(vfs_count_sqlite(&a) == 1 && probes == io);
    CHECK(vfs_open_sqlite("/next-child", O_RDWR, 1, &b, 9, &z) == VFS_OK);
    vfs_close_owned(1);
    CHECK(vfs_close_sqlite(&z) == VFS_OK);
    CHECK(!memcmp(&saved, &open_files[y.fd], sizeof(saved)));
    CHECK(vfs_count_sqlite(&a) == 1);
}

static void capacity_preflight(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease, sentinel;
    VfsFile before[VFS_MAX_OPEN_FILES];
    int fd;
    for (fd = 3; fd < VFS_MAX_OPEN_FILES; fd++)
        CHECK(vfs_open("/full", O_RDWR) == fd);
    memset(&lease, 0x5a, sizeof(lease)); sentinel = lease;
    memcpy(before, open_files, sizeof(before)); probes = 0;
    CHECK(vfs_open_sqlite("/must-not-probe", O_CREAT | O_TRUNC | O_RDWR,
                          1, &cookie, 7, &lease) == VFS_ERR_NOSPC);
    CHECK(probes == 0);
    CHECK(!memcmp(&lease, &sentinel, sizeof(lease)));
    CHECK(vfs_open("/generic-full", O_RDWR) == VFS_ERR_NOSPC);
    CHECK(probes == 0 && !memcmp(before, open_files, sizeof(before)));
}

static void generation_exhaustion(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease, old;
    VfsFile before[VFS_MAX_OPEN_FILES];
    int fd;
    /* Fault injection only: no production reset/set-generation API. */
    for (fd = 3; fd < VFS_MAX_OPEN_FILES; fd++)
        open_files[fd].generation = 0xffffffffUL;
    memcpy(before, open_files, sizeof(before)); probes = 0;
    CHECK(vfs_open_sqlite("/exhausted", O_CREAT | O_RDWR, 1, &cookie, 7,
                          &lease) == VFS_ERR_NOSPC);
    CHECK(probes == 0 && !memcmp(before, open_files, sizeof(before)));
    CHECK(vfs_open("/generic", O_RDWR) == VFS_ERR_NOSPC);
    CHECK(probes == 0);
    open_files[4].generation = 0xfffffffeUL;
    CHECK(vfs_open_sqlite("/last", O_RDWR, 1, &cookie, 7, &lease) == VFS_OK);
    CHECK(lease.fd == 4 && lease.generation == 0xffffffffUL);
    old = lease;
    CHECK(vfs_close_sqlite(&lease) == VFS_OK);
    probes = 0;
    CHECK(vfs_open_sqlite("/never-wrap", O_RDWR, 1, &cookie, 7,
                          &lease) == VFS_ERR_NOSPC);
    CHECK(probes == 0 && open_files[4].generation == old.generation);
    CHECK(vfs_close_sqlite(&old) == VFS_ERR_INVAL);
}

static void invalid_open(void)
{
    VfsSqliteCookie cookie = { 1, 0 };
    VfsSqliteLease lease, sentinel;
    VfsFile before[VFS_MAX_OPEN_FILES];
    memset(&lease, 0x5a, sizeof(lease)); sentinel = lease;
    memcpy(before, open_files, sizeof(before));
    CHECK(vfs_open_sqlite("/invalid", O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_INVAL);
    cookie.generation = 1; cookie.group_index = -1;
    CHECK(vfs_open_sqlite("/invalid", O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_INVAL);
    cookie.group_index = 1;
    CHECK(vfs_open_sqlite("/invalid", O_RDWR, -1, &cookie, 7, &lease) == VFS_ERR_INVAL);
    CHECK(vfs_open_sqlite(NULL, O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_INVAL);
    CHECK(vfs_open_sqlite("/invalid", O_RDWR, 1, NULL, 7, &lease) == VFS_ERR_INVAL);
    CHECK(vfs_open_sqlite("/invalid", O_RDWR, 1, &cookie, 7, NULL) == VFS_ERR_INVAL);
    CHECK(probes == 0 && !memcmp(before, open_files, sizeof(before)));
    CHECK(!memcmp(&lease, &sentinel, sizeof(lease)));
}

static void generic_regression(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease;
    int child, protected_fd, reused;
    char buf[4];
    child = vfs_open("/child", O_RDWR);
    protected_fd = vfs_open("/protected", O_RDWR);
    CHECK(child == 3 && protected_fd == 4);
    CHECK(open_files[child].owner == current_owner);
    CHECK(open_files[child].lifetime == VFS_FD_GENERIC);
    CHECK(vfs_fd_set_protect(protected_fd, 1) == VFS_OK);
    CHECK(vfs_fd_is_protected(protected_fd) == 1);
    vfs_close_owned(1);
    CHECK(vfs_tell(child) == 0);
    vfs_close_owned(2);
    CHECK(vfs_tell(child) == VFS_ERR_INVAL);
    CHECK(vfs_tell(protected_fd) == 0);
    vfs_close(protected_fd);
    CHECK(vfs_tell(protected_fd) == VFS_ERR_INVAL);
    CHECK(vfs_open_sqlite("/managed", O_RDWR, 1, &cookie, 7, &lease) == VFS_OK);
    CHECK(vfs_close_sqlite(&lease) == VFS_OK);
    reused = vfs_open("/reused", O_RDWR);
    CHECK(reused == lease.fd);
    CHECK(open_files[reused].lifetime == VFS_FD_GENERIC);
    CHECK(open_files[reused].cookie.generation == 0);
    CHECK(open_files[reused].sqlite_flags == 0 && !open_files[reused].quarantined);
    CHECK(vfs_seek(reused, 4, SEEK_SET) == 4 && vfs_tell(reused) == 4);
    CHECK(vfs_get_size(reused) == 8);
    CHECK(vfs_read_fd(reused, buf, sizeof(buf)) == sizeof(buf));
    CHECK(buf[0] == 'R' && vfs_tell(reused) == 8);
    CHECK(vfs_read_fd(reused, buf, sizeof(buf)) == 0);
    CHECK(vfs_write_fd(reused, buf, sizeof(buf)) == sizeof(buf));
    CHECK(vfs_get_size(reused) == 12 && vfs_tell(reused) == 12);
    CHECK(vfs_fd_set_protect(reused, 1) == VFS_OK);
    CHECK(vfs_fd_set_protect(reused, 0) == VFS_OK);
    vfs_close_owned(2);
    CHECK(vfs_tell(reused) == VFS_ERR_INVAL);
    CHECK(vfs_isatty(0) == 1 && vfs_isatty(1) == 1);
    CHECK(vfs_fd_set_protect(-1, 1) == VFS_ERR_INVAL);
}

static void failed_open(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease, sentinel;
    VfsFile before[VFS_MAX_OPEN_FILES];
    memset(&lease, 0x5a, sizeof(lease)); sentinel = lease;
    memcpy(before, open_files, sizeof(before));
    size_rc = VFS_ERR_NOTFOUND;
    CHECK(vfs_open_sqlite("/missing", O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_NOTFOUND);
    CHECK(!memcmp(before, open_files, sizeof(before)));
    CHECK(!memcmp(&lease, &sentinel, sizeof(lease)));
    CHECK(vfs_count_sqlite(&cookie) == 0);
    CHECK(current_owner == 2 && owner_sets == 0);
    write_rc = VFS_ERR_IO;
    CHECK(vfs_open_sqlite("/create-fails", O_CREAT | O_RDWR, 1, &cookie, 7,
                          &lease) == VFS_ERR_IO);
    CHECK(!memcmp(before, open_files, sizeof(before)));
    CHECK(!memcmp(&lease, &sentinel, sizeof(lease)));
    mock_ops.get_file_size = NULL;
    CHECK(vfs_open_sqlite("/unsupported", O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_INVAL);
    CHECK(!memcmp(before, open_files, sizeof(before)));
}

static void quarantine_capacity(void)
{
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease lease;
    VfsFile before[VFS_MAX_OPEN_FILES];
    int fd;
    for (fd = 3; fd < VFS_MAX_OPEN_FILES; fd++)
        CHECK(vfs_open_sqlite("/isolated", O_RDWR, 1, &cookie, fd, &lease) == VFS_OK);
    CHECK(vfs_quarantine_sqlite(&cookie) == VFS_OK);
    memcpy(before, open_files, sizeof(before)); probes = 0;
    vfs_close_owned(1);
    for (fd = 3; fd < VFS_MAX_OPEN_FILES; fd++) vfs_close(fd);
    CHECK(vfs_open_sqlite("/full", O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_NOSPC);
    CHECK(vfs_open("/full", O_RDWR) == VFS_ERR_NOSPC);
    CHECK(vfs_count_sqlite(&cookie) == VFS_MAX_OPEN_FILES - 3);
    CHECK(probes == 0 && !memcmp(before, open_files, sizeof(before)));
}

int main(int argc, char **argv)
{
    mock_ops.get_file_size = mock_size;
    mock_ops.write_file = mock_write;
    mock_ops.read_stream = mock_read_stream;
    mock_ops.write_stream = mock_write_stream;
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "explicit_owner")) explicit_owner();
    else if (!strcmp(argv[1], "direct_close") || !strcmp(argv[1], "owned_close") ||
             !strcmp(argv[1], "protect")) generic_barrier(argv[1]);
    else if (!strcmp(argv[1], "verified_close")) verified_close();
    else if (!strcmp(argv[1], "stale_reuse")) stale_reuse();
    else if (!strcmp(argv[1], "validation")) validation();
    else if (!strcmp(argv[1], "count_members")) count_members();
    else if (!strcmp(argv[1], "quarantine")) quarantine();
    else if (!strcmp(argv[1], "capacity_preflight")) capacity_preflight();
    else if (!strcmp(argv[1], "generation_exhaustion")) generation_exhaustion();
    else if (!strcmp(argv[1], "invalid_open")) invalid_open();
    else if (!strcmp(argv[1], "generic_regression")) generic_regression();
    else if (!strcmp(argv[1], "failed_open")) failed_open();
    else if (!strcmp(argv[1], "quarantine_capacity")) quarantine_capacity();
    else return 2;
    printf("PASS %s\n", argv[1]);
    return 0;
}
