/* ========================================================================= */
/*  CFG_HOST.C — 票 S2-C §4 のホスト TDD                                     */
/*                                                                           */
/*  実物だけを組む: 実 `userland/lib/cfg/` 一式 + 実 `userland/cmds/cfg.c` +  */
/*  実 `kapi/kapi_db.c` + 実 `lib/sqlite3/sqlite3.c` + 実 `os32_sqlite_vfs.c` */
/*  + 実 `fs/vfs_fd.c` + RAM バックエンド。模型は exec 側のポインタ検証、     */
/*  SHM の置き場、VFS の rename / open / read / stat と「KAPI の close が     */
/*  失敗する」の注入だけ — どれもカーネル番地か障害注入でしか作れない。       */
/*  ホストのファイルシステムには 1 バイトも触らない (tsv は stdin から)。     */
/*                                                                           */
/*  実行: python3 -B tools/tests/test_cfg.py                                 */
/* ========================================================================= */

#define main f2a_main
#include "vfs_fd_sqlite_host.c"
#undef main

/* ホストの size_t はカーネルの u32 と違うので libc の実体を使い回す。 */
#define __KSTRING_H
char *kstrncpy(char *dst, const char *src, u32 size)
{ str_cpy(dst, src, (int)size); return dst; }
char *kstrncat(char *dst, const char *src, u32 size)
{
    u32 used = (u32)strlen(dst);
    if (used + 1u < size) str_cpy(dst + used, src, (int)(size - used));
    return dst;
}
u32 kstrlen(const char *s) { return (u32)strlen(s); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
int kstrncmp(const char *a, const char *b, u32 n) { return strncmp(a, b, n); }
void *kmemcpy(void *dst, const void *src, u32 n) { return memcpy(dst, src, n); }
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }

#include "../../lib/sqlite3/sqlite3.h"
#include "../../lib/sqlite3/os32_sqlite_vfs.c"
#include "sqlite_groups_backend.h"

volatile u32 tick_count;
u32 sys_time(void) { return 0; }
void kprintf(u8 color, const char *fmt, ...) { (void)color; (void)fmt; }
int vfs_sync(void) { probes++; return 0; }

static const char *host_resolve(const char *path)
{
    static char resolved[VFS_MAX_PATH];
    vfs_resolve_path(path, resolved, (int)sizeof(resolved));
    return resolved;
}
int vfs_rm(const char *path) { probes++; return fixture_rm(host_resolve(path)); }

/* stat の障害注入。`stat_fail_on` は部分一致、`stat_fail_exact` は完全一致で、
 * `stat_fail_skip` 回だけ素通ししてから失敗させる (cfg_init の入口検査を
 * 通したうえで、後段の stat だけを落とすため)。 */
static int inj_no_ino;            /* 1 = st_ino を供給しない FS の模型 */
static const char *stat_fail_on;
static const char *stat_fail_exact;
static int stat_fail_skip;
static int stat_fail_rc = OS32_ERR_IO;
int vfs_stat(const char *path, OS32_Stat *st)
{
    FixtureFile *f;
    probes++;
    if (stat_fail_on && strstr(path, stat_fail_on)) return stat_fail_rc;
    if (stat_fail_exact && !strcmp(path, stat_fail_exact)) {
        if (stat_fail_skip > 0) stat_fail_skip--;
        else return stat_fail_rc;
    }
    f = fixture_find(host_resolve(path), 0);
    if (!f) return OS32_ERR_NOTFOUND;
    if (st) {
        memset(st, 0, sizeof(*st));
        st->st_size = f->size;
        st->st_nlink = 1;
        st->st_dev = 1;
        /* fixture の並び順を inode 代わりにする (同一性の判定に使う)。
         * `inj_no_ino` は FAT (`fs/fatfs_vfs.c` は st_ino を 0 のまま返す)
         * の模型 — inode を供給しない FS。 */
        if (!inj_no_ino) st->st_ino = (u32)(f - fixture_files) + 1u;
    }
    return 0;
}

/* ---- exec 側の模型 (kapi_db.c が唯一使う口) ---------------------------- */
int ring3_user_range_ok(u32 p, u32 len)
{
    (void)len;
    return p != 0;                   /* CPL=0 の直呼びと同じ扱い */
}

#define SHM_CANARY 256
static unsigned char test_shm[DB_SHM_BLOCK_SIZE + SHM_CANARY];
#include "../../kapi/kapi_db.c"

/* ========================================================================= */
/*  libos32cfg の KAPI 境界 (差し替え先)                                     */
/* ========================================================================= */

#include "../../userland/lib/cfg/cfg_internal.h"

/* 障害注入 */
static int inj_close_fail;        /* != 0 = db_close が失敗し、この値を返す */
static const char *inj_prep_fail; /* 部分一致した SQL の prepare_only を失敗 */
static int inj_bind_fail;         /* != 0 = db_bind_text を失敗させる */
static int inj_rename_mode;       /* 0=普通 1=新名あり旧名残る 2=何も起きない
                                     3=旧名は消えたが失敗を返す */
/* S3-C: import の入力側の注入 */
static int inj_seek_fail;         /* != 0 = sys_lseek が失敗する */
static int inj_fread_fail_at;     /* n 回目の sys_read を失敗させる (0 = しない) */
static int inj_fread_n;
static int inj_close_after_commit;/* COMMIT の直後に db_close を壊す */
/* db_exec の差し替え (最大 2 組)。本物の SQLite に別の SQL を流して
 * **実在する診断コード**を作る — 単に -1 を返すと last_sqlite が 0 のままで
 * 「診断が保たれたか」を試験できない。 */
#define INJ_EXEC_SUBS 2
static const char *inj_exec_match[INJ_EXEC_SUBS];
static const char *inj_exec_sub[INJ_EXEC_SUBS];

static int be_open_existing(const char *p, int w) { return kapi_db_open_existing(p, w); }
static int inj_prep_skip;         /* 一致しても最初の N 回は素通しさせる */
static int be_prepare_only(int h, const char *s)
{
    if (inj_prep_fail && strstr(s, inj_prep_fail)) {
        if (inj_prep_skip > 0) {
            inj_prep_skip--;
        } else {
            /* 実在しない表を引かせて、本物の SQLite の失敗と診断を作る。 */
            return kapi_db_prepare_only(h, "SELECT 1 FROM no_such_table_for_tdd");
        }
    }
    return kapi_db_prepare_only(h, s);
}
static int be_bind_int(int h, int i, int v)       { return kapi_db_bind_int(h, i, v); }
static int be_bind_text(int h, int i, const char *s, int n)
{
    if (inj_bind_fail) {
        /* 実在しない index で本物の SQLITE_RANGE を作る。 */
        return kapi_db_bind_text(h, 99, s, n);
    }
    return kapi_db_bind_text(h, i, s, n);
}
static int be_bind_blob(int h, int i, const void *p, int n)
{ return kapi_db_bind_blob(h, i, p, n); }
static int be_bind_null(int h, int i)             { return kapi_db_bind_null(h, i); }
static int be_error_code(int h)
{
    if (inj_close_fail && h >= 0 && h < DB_MAX_CONNECTIONS &&
        db_slots[h].last_error == SQLITE_OK)
        return inj_close_fail;
    return kapi_db_error_code(h);
}
static int be_open(const char *p)                 { return kapi_db_open(p); }
static int be_close(int h)
{
    int rc = kapi_db_close(h);        /* 実際には閉じる (資源を漏らさない) */
    if (inj_close_fail) return -1;    /* ライブラリからは「失敗」に見せる */
    return rc;
}
static int be_exec(int h, const char *s)
{
    int i, rc;
    for (i = 0; i < INJ_EXEC_SUBS; i++) {
        if (inj_exec_match[i] && strstr(s, inj_exec_match[i]))
            return kapi_db_exec(h, inj_exec_sub[i]);
    }
    rc = kapi_db_exec(h, s);
    /* S3-C: **commit の後に**だけ close を壊す。cfg_open の中の
     * RO -> RW 切り替えを巻き込むと open 自体が ERROR になってしまう。 */
    if (inj_close_after_commit && !strcmp(s, "COMMIT")) {
        inj_close_fail = inj_close_after_commit;
        inj_close_after_commit = 0;
    }
    return rc;
}
static int be_step(int h)                         { return kapi_db_step(h); }
static int be_finalize(int h)                     { return kapi_db_finalize(h); }
static unsigned char *be_shm(void)                { return test_shm; }
static int be_stat(const char *p, OS32_Stat *st)  { return vfs_stat(p, st); }

static int be_rename(const char *o, const char *n)
{
    FixtureFile *src = fixture_find(host_resolve(o), 0);
    FixtureFile *dst;
    if (!src) return OS32_ERR_NOTFOUND;
    if (inj_rename_mode == 2) return OS32_ERR_IO;   /* 何も起きなかった */
    dst = fixture_find(host_resolve(n), 1);
    if (!dst) return OS32_ERR_NOSPC;
    memcpy(dst->data, src->data, src->size);
    dst->size = src->size;
    if (inj_rename_mode == 1) return OS32_ERR_IO;   /* 旧名削除も巻き戻しも失敗 */
    src->exists = 0;
    if (inj_rename_mode == 3) return OS32_ERR_IO;   /* 旧名は消えたが失敗を返す */
    return 0;
}

static int be_unlink(const char *p) { return vfs_rm(p); }

/* ---- S3-C: 生成入力 (fixture の 128KB に入らない件数の JSON) ----------- */
/*  8192 件の JSON は 360KB あって FixtureFile に収まらないので、`gen_path`
 *  に一致する open だけ**その場で組み立てる**仮想ファイルにする。行長は
 *  固定なので lseek の当たり先も算術で出せる。`gen_rows2` / `gen_dup` /
 *  `gen_ver2` は **2 回目以降の open** にだけ効く = 巡回の間の差し替え。 */
#define GEN_REC 46
#define GENFD_BASE 300
#define GENFD_MAX 2

static const char *gen_path;      /* NULL = 無効 */
static int gen_rows;              /* 1 巡目の件数 */
static int gen_rows2;             /* 2 巡目の件数 (0 = 同じ) */
static int gen_dup;               /* 2 巡目: この行を 1 行目と同じ key にする */
static int gen_ver2;              /* 2 巡目のヘッダ版 (0 = 1 のまま) */
static int gen_opens;
static struct { int used; int rows; int dup; int ver; u32 off; } genfd[GENFD_MAX];

static int gen_is_fd(int fd)
{
    return fd >= GENFD_BASE && fd < GENFD_BASE + GENFD_MAX;
}

static int gen_hdr(int ver, char *out)
{
    sprintf(out, "{\"schema_version\":%d,\"exported\":\"0\"}\n", ver);
    return (int)strlen(out);
}

static void gen_rec(int idx, int dup, char *out)
{
    int k = (dup && idx == dup) ? 0 : idx;
    /* 値は常に 1 — writer の fmt_int は先頭ゼロを出さないので `%04d` の値は
     * reader が正しく弾く。行長を固定したいのは key 側だけ。 */
    sprintf(out, "{\"scope\":\"user\",\"key\":\"k%04d\",\"type\":0,\"v\":1}\n", k);
    CHECK((int)strlen(out) == GEN_REC);
}

static int gen_open(void)
{
    int i;
    for (i = 0; i < GENFD_MAX; i++) {
        if (!genfd[i].used) {
            gen_opens++;
            genfd[i].used = 1;
            genfd[i].off = 0;
            if (gen_opens >= 2) {
                genfd[i].rows = gen_rows2 ? gen_rows2 : gen_rows;
                genfd[i].dup = gen_dup;
                genfd[i].ver = gen_ver2 ? gen_ver2 : 1;
            } else {
                genfd[i].rows = gen_rows;
                genfd[i].dup = 0;
                genfd[i].ver = 1;
            }
            return GENFD_BASE + i;
        }
    }
    return OS32_ERR_NOSPC;
}

static void gen_close(int fd) { genfd[fd - GENFD_BASE].used = 0; }

static int gen_seek(int fd, int off, int whence)
{
    int i = fd - GENFD_BASE;
    long n;
    if (!genfd[i].used) return OS32_ERR_INVAL;
    if (inj_seek_fail) return OS32_ERR_IO;
    if (whence == SEEK_SET) n = off;
    else if (whence == SEEK_CUR) n = (long)genfd[i].off + off;
    else n = off;
    if (n < 0) return OS32_ERR_INVAL;
    genfd[i].off = (u32)n;
    return (int)n;
}

static int gen_read(int fd, unsigned char *buf, u32 size)
{
    int i = fd - GENFD_BASE, hlen, idx, pos;
    u32 n = 0, off;
    char hdr[64], rec[80];
    int built = -1;

    if (!genfd[i].used) return OS32_ERR_INVAL;
    hlen = gen_hdr(genfd[i].ver, hdr);
    while (n < size) {
        off = genfd[i].off;
        if (off < (u32)hlen) {
            buf[n] = (unsigned char)hdr[off];
        } else {
            idx = (int)((off - (u32)hlen) / GEN_REC);
            pos = (int)((off - (u32)hlen) % GEN_REC);
            if (idx >= genfd[i].rows) break;
            if (built != idx) { gen_rec(idx, genfd[i].dup, rec); built = idx; }
            buf[n] = (unsigned char)rec[pos];
        }
        genfd[i].off++;
        n++;
    }
    return (int)n;
}

/* tsv 読み出し用の最小 FD 表 (fixture の上、読み取り専用)。 */
#define HOSTFD_MAX 4
static struct { int used; FixtureFile *f; u32 off; } hostfd[HOSTFD_MAX];

static int be_fopen(const char *p, int mode)
{
    int i;
    FixtureFile *f;
    (void)mode;
    if (gen_path && !strcmp(host_resolve(p), gen_path)) return gen_open();
    f = fixture_find(host_resolve(p), 0);
    if (!f) return OS32_ERR_NOTFOUND;
    for (i = 0; i < HOSTFD_MAX; i++) {
        if (!hostfd[i].used) {
            hostfd[i].used = 1;
            hostfd[i].f = f;
            hostfd[i].off = 0;
            return 100 + i;
        }
    }
    return OS32_ERR_NOSPC;
}

static int be_fread(int fd, void *buf, u32 size)
{
    int i = fd - 100;
    u32 n;
    if (inj_fread_fail_at && ++inj_fread_n == inj_fread_fail_at)
        return OS32_ERR_IO;
    if (gen_is_fd(fd)) return gen_read(fd, (unsigned char *)buf, size);
    if (i < 0 || i >= HOSTFD_MAX || !hostfd[i].used) return OS32_ERR_INVAL;
    if (hostfd[i].off >= hostfd[i].f->size) return 0;
    n = hostfd[i].f->size - hostfd[i].off;
    if (n > size) n = size;
    memcpy(buf, hostfd[i].f->data + hostfd[i].off, n);
    hostfd[i].off += n;
    return (int)n;
}

static int be_fseek(int fd, int off, int whence)
{
    int i = fd - 100;
    long n;
    if (gen_is_fd(fd)) return gen_seek(fd, off, whence);
    if (i < 0 || i >= HOSTFD_MAX || !hostfd[i].used) return OS32_ERR_INVAL;
    if (inj_seek_fail) return OS32_ERR_IO;
    if (whence == SEEK_SET) n = off;
    else if (whence == SEEK_CUR) n = (long)hostfd[i].off + off;
    else n = (long)hostfd[i].f->size + off;
    if (n < 0) return OS32_ERR_INVAL;
    hostfd[i].off = (u32)n;
    return (int)n;
}

static void be_fclose(int fd)
{
    int i = fd - 100;
    if (gen_is_fd(fd)) { gen_close(fd); return; }
    if (i >= 0 && i < HOSTFD_MAX) hostfd[i].used = 0;
}

static u32 host_tick = 7;
static u32 be_tick(void) { return host_tick; }

static const CfgBackend host_backend = {
    be_open_existing, be_prepare_only, be_bind_int, be_bind_text, be_bind_blob,
    be_bind_null, be_error_code, be_open, be_close, be_exec, be_step,
    be_finalize, be_shm, be_stat, be_rename, be_unlink, be_fopen, be_fread,
    be_fseek, be_fclose, be_tick
};

const CfgBackend *cfg_backend_platform(void) { return &host_backend; }

/* ========================================================================= */
/*  試験対象 (そのまま載せる)                                                */
/* ========================================================================= */

#include "../../userland/lib/cfg/libos32cfg.c"
#include "../../userland/lib/cfg/cfg_enum.c"
#include "../../userland/lib/cfg/cfg_tsv.c"
#include "../../userland/lib/cfg/cfg_init.c"
#include "../../userland/lib/cfg/cfg_json.c"
#include "../../userland/lib/cfg/cfg_import.c"

/* cfg コマンドは main を差し替えて丸ごと載せる (出力は捕まえる)。 */
static char cap_out[65536];
static int cap_len;
static int cap_yields;
/* S5: 出力の 1KB ごとの yield は許すが、**接続を持ったまま**の yield は
 * 契約違反 (票 §7 の直列化)。g_db.in_use がその判定。 */
static int cap_yields_open;
static int inj_write_fail;      /* != 0 = sys_write が失敗する */
static int inj_short_write;     /* != 0 = 1 回に書ける最大バイト数 */
static int host_write(int fd, const void *buf, u32 n)
{
    if (inj_write_fail) return -1;
    if (inj_short_write > 0 && n > (u32)inj_short_write) n = (u32)inj_short_write;
    if (fd == 1 || fd == 2) {
        if (cap_len + (int)n < (int)sizeof(cap_out)) {
            memcpy(cap_out + cap_len, buf, n);
            cap_len += (int)n;
        }
        return (int)n;
    }
    {   /* export の書き出し先も fixture の中 */
        int i = fd - 200;
        FixtureFile *f;
        if (i < 0 || i >= HOSTFD_MAX) return -1;
        f = hostfd[i].f;
        if (!f || hostfd[i].off + n > FIXTURE_BYTES) return -1;
        memcpy(f->data + hostfd[i].off, buf, n);
        hostfd[i].off += n;
        if (f->size < hostfd[i].off) f->size = hostfd[i].off;
        return (int)n;
    }
}
static int host_open_w(const char *path, int mode)
{
    int i;
    FixtureFile *f;
    (void)mode;
    f = fixture_find(host_resolve(path), 1);
    if (!f) return OS32_ERR_NOSPC;
    f->size = 0;
    for (i = 0; i < HOSTFD_MAX; i++) {
        if (!hostfd[i].used) {
            hostfd[i].used = 1;
            hostfd[i].f = f;
            hostfd[i].off = 0;
            return 200 + i;
        }
    }
    return OS32_ERR_NOSPC;
}
static void host_close_w(int fd)
{
    int i = fd - 200;
    if (i >= 0 && i < HOSTFD_MAX) hostfd[i].used = 0;
}
static i32 host_yield(void)
{
    cap_yields++;
    if (g_db.in_use) cap_yields_open++;
    return 0;
}
/* S5: cfg_bench は tick の**差**を測る。既定の step は 0 なので S2 までの
 * 試験の見え方は変わらない。cfg_bench の試験だけが step を立てる。 */
static u32 host_tick_step;
static u32 host_gettick(void)
{
    u32 now = host_tick;
    host_tick += host_tick_step;
    return now;
}
static int host_unlink(const char *p) { return vfs_rm(p); }
static int host_stat(const char *p, OS32_Stat *st) { return vfs_stat(p, st); }
static const char *host_cwd = "/";
static const char *host_getcwd(void) { return host_cwd; }

static KernelAPI host_api;

#define main cfg_main
#include "../../userland/cmds/cfg.c"
#undef main

/* 計測プログラムも同じ土台に載せる (票 S5-C)。純関数を直接叩けるよう、
 * 静的関数の名前は cfg.c と重ならない bn_ / bo_ 接頭辞にしてある。 */
#define main bench_main
#include "../../userland/tests/cfg_bench.c"
#undef main

/* ========================================================================= */
/*  下ごしらえ                                                               */
/* ========================================================================= */

static int cases_run;

static void reset_all(void)
{
    int i;
    memset(test_shm, 0, sizeof(test_shm));
    memset(db_slots, 0, sizeof(db_slots));
    memset(db_open_fail, 0, sizeof(db_open_fail));
    memset(fixture_files, 0, sizeof(fixture_files));
    memset(hostfd, 0, sizeof(hostfd));
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].in_use = 0;
    inj_no_ino = 0;
    stat_fail_on = NULL;
    stat_fail_exact = NULL;
    stat_fail_skip = 0;
    stat_fail_rc = OS32_ERR_IO;
    inj_close_fail = 0;
    inj_prep_fail = NULL;
    inj_prep_skip = 0;
    inj_bind_fail = 0;
    inj_write_fail = 0;
    inj_short_write = 0;
    memset(inj_exec_match, 0, sizeof(inj_exec_match));
    memset(inj_exec_sub, 0, sizeof(inj_exec_sub));
    inj_rename_mode = 0;
    inj_seek_fail = 0;
    inj_fread_fail_at = 0;
    inj_fread_n = 0;
    inj_close_after_commit = 0;
    gen_path = NULL;
    gen_rows = 0;
    gen_rows2 = 0;
    gen_dup = 0;
    gen_ver2 = 0;
    gen_opens = 0;
    memset(genfd, 0, sizeof(genfd));
    host_cwd = "/";
    resolve_cwd = "";
    resolve_owner = current_owner = 2;
    cap_len = 0;
    cap_yields = 0;
    cap_yields_open = 0;
    host_tick = 7;
    fixture_init();
    /* cfg 側の静的状態を毎回真っさらに戻す */
    memset(&g_db, 0, sizeof(g_db));
    memset(&g_new, 0, sizeof(g_new));
    g_close_error = 0;
    g_init_reason = 0;
    g_backend = NULL;
    memset(&host_api, 0, sizeof(host_api));
    host_api.sys_write = host_write;
    host_api.sys_yield = host_yield;
    host_api.sys_open = host_open_w;
    host_api.sys_close = host_close_w;
    host_api.sys_unlink = host_unlink;
    host_api.sys_stat = host_stat;
    host_api.sys_getcwd = host_getcwd;
    host_api.get_tick = host_gettick;
    host_api.db_mem_used = kapi_db_mem_used;   /* 実 sqlite3_memory_used */
    host_api.version = 50;
    host_tick_step = 0;
}

static void canary_check(const char *where)
{
    int i;
    for (i = 0; i < SHM_CANARY; i++) {
        if (test_shm[DB_SHM_BLOCK_SIZE + i] != 0) {
            fprintf(stderr, "FAIL %s: wrote past the 16KB block\n", where);
            exit(1);
        }
    }
}

/* 使い捨ての DB を legacy db_open (CREATE 付き) で作る。 */
static void raw_db(const char *path, const char *const *stmts)
{
    int h = kapi_db_open(path);
    int i;
    CHECK(h >= 0);
    for (i = 0; stmts[i]; i++) CHECK(kapi_db_exec(h, stmts[i]) == 0);
    CHECK(kapi_db_close(h) == 0);
}

static const char *DDL[] = {
    "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
    "CREATE TABLE settings (scope TEXT NOT NULL, key TEXT NOT NULL,"
    " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
    " PRIMARY KEY (scope, key)) WITHOUT ROWID",
    "INSERT INTO meta VALUES (1, '0')",
    NULL
};

static void make_good_db(void) { raw_db(CFG_DB_PATH, DDL); }

static void put_file(const char *path, const char *text)
{
    CHECK(fixture_create(NULL, path, text, (u32)strlen(text)) == VFS_OK);
}

/* ========================================================================= */
/*  (1) MISSING                                                              */
/* ========================================================================= */
static void c_missing(void)
{
    CfgDb *db;
    char buf[32];

    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_MISSING);
    CHECK(cfg_schema_version(db) == 0);
    CHECK(cfg_get_int(db, "gshell", "desktop/color", 42) == 42);
    CHECK(cfg_get_text(db, "gshell", "a", buf, 32) == OS32_ERR_NOTFOUND);
    CHECK(cfg_begin(db) == OS32_ERR_INVAL);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == OS32_ERR_INVAL);
    CHECK(cfg_close(db) == 0);
    /* DB も journal も作られていない */
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);
    CHECK(fixture_find(CFG_DB_JOURNAL_PATH, 0) == NULL);

    /* writable=1 でも同じ (RW へ切り替えない) */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_status(db) == CFG_MISSING);
    CHECK(cfg_begin(db) == OS32_ERR_INVAL);
    CHECK(cfg_close(db) == 0);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);
}

/* ========================================================================= */
/*  (2) CORRUPT — 0 バイト / hot journal / meta 表が無い                     */
/* ========================================================================= */
static void c_corrupt(void)
{
    CfgDb *db;
    static const char *empty_ddl[] = { "CREATE TABLE t(x)", NULL };

    CHECK(fixture_find(CFG_DB_PATH, 1) != NULL);      /* 0 バイト */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_status(db) == CFG_CORRUPT);
    CHECK(cfg_begin(db) == OS32_ERR_INVAL);
    CHECK(cfg_get_int(db, "gshell", "a", 9) == 9);
    CHECK(cfg_close(db) == 0);
    CHECK(fixture_find(CFG_DB_PATH, 0)->size == 0);   /* 触っていない */

    reset_all();
    make_good_db();
    put_file(CFG_DB_JOURNAL_PATH, "hot");             /* hot journal */
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_CORRUPT);
    CHECK(cfg_close(db) == 0);
    CHECK(fixture_find(CFG_DB_JOURNAL_PATH, 0) != NULL);  /* 消さない */

    reset_all();
    raw_db(CFG_DB_PATH, empty_ddl);                   /* meta 表が無い */
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_CORRUPT);
    CHECK(cfg_close(db) == 0);
}

/* ========================================================================= */
/*  (3)(12) VERSION — 読めるが書けない。RW 要求でも RO のまま。              */
/* ========================================================================= */
static void c_version(void)
{
    CfgDb *db;
    char buf[64];
    static const char *v2[] = {
        "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
        "CREATE TABLE settings (scope TEXT NOT NULL, key TEXT NOT NULL,"
        " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
        " PRIMARY KEY (scope, key)) WITHOUT ROWID",
        "INSERT INTO meta VALUES (2, '0')",
        "INSERT INTO settings VALUES ('gshell','desktop/color',0,5,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','desktop/name',1,NULL,'hi',NULL)",
        NULL
    };
    raw_db(CFG_DB_PATH, v2);

    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_status(db) == CFG_VERSION);
    CHECK(cfg_schema_version(db) == 2);
    /* 認識できる列はそのまま読める */
    CHECK(cfg_get_int(db, "gshell", "desktop/color", 0) == 5);
    CHECK(cfg_get_text(db, "gshell", "desktop/name", buf, 64) == 2);
    CHECK(!strcmp(buf, "hi"));
    /* 書きは拒否 */
    CHECK(cfg_begin(db) == OS32_ERR_INVAL);
    CHECK(cfg_set_int(db, "gshell", "x", 1) == OS32_ERR_INVAL);
    CHECK(cfg_delete(db, "gshell", "desktop/color") == OS32_ERR_INVAL);
    CHECK(cfg_close(db) == 0);
}

/* ========================================================================= */
/*  (4)(19) 型違い・cap 不足・NULL と空値                                     */
/* ========================================================================= */
static void c_types(void)
{
    CfgDb *db;
    char buf[8];
    unsigned char bb[8];
    static const char *rows[] = {
        "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
        "CREATE TABLE settings (scope TEXT NOT NULL, key TEXT NOT NULL,"
        " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
        " PRIMARY KEY (scope, key)) WITHOUT ROWID",
        "INSERT INTO meta VALUES (1, '0')",
        "INSERT INTO settings VALUES ('gshell','n',0,7,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','t',1,NULL,'abcdefgh',NULL)",
        "INSERT INTO settings VALUES ('gshell','e',1,NULL,'',NULL)",
        "INSERT INTO settings VALUES ('gshell','nul',1,NULL,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','b',2,NULL,NULL,x'0102')",
        "INSERT INTO settings VALUES ('gshell','eb',2,NULL,NULL,x'')",
        NULL
    };
    raw_db(CFG_DB_PATH, rows);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_OK);

    /* 型違いは「無い」扱い */
    CHECK(cfg_get_int(db, "gshell", "t", 3) == 3);
    CHECK(cfg_get_text(db, "gshell", "n", buf, 8) == OS32_ERR_NOTFOUND);
    CHECK(cfg_get_blob(db, "gshell", "t", bb, 8) == OS32_ERR_NOTFOUND);

    /* cap 不足は NOSPC で out を書かない */
    memset(buf, 'Z', sizeof(buf));
    CHECK(cfg_get_text(db, "gshell", "t", buf, 8) == OS32_ERR_NOSPC);
    CHECK(buf[0] == 'Z' && buf[7] == 'Z');
    memset(bb, 'Z', sizeof(bb));
    CHECK(cfg_get_blob(db, "gshell", "b", bb, 1) == OS32_ERR_NOSPC);
    CHECK(bb[0] == 'Z');
    CHECK(cfg_get_blob(db, "gshell", "b", bb, 2) == 2);
    CHECK(bb[0] == 1 && bb[1] == 2);

    /* NULL の text は未設定、空 text は長さ 0 */
    CHECK(cfg_get_text(db, "gshell", "nul", buf, 8) == OS32_ERR_NOTFOUND);
    CHECK(cfg_get_text(db, "gshell", "e", buf, 8) == 0);
    CHECK(buf[0] == '\0');
    CHECK(cfg_get_blob(db, "gshell", "eb", bb, 8) == 0);
    CHECK(cfg_close(db) == 0);
}

/* ========================================================================= */
/*  (5) begin → set → commit → close → reopen                               */
/* ========================================================================= */
static void c_roundtrip(void)
{
    CfgDb *db;
    char buf[300];
    unsigned char big[CFG_BLOB_MAX], out[CFG_BLOB_MAX];
    int i;

    make_good_db();
    for (i = 0; i < CFG_BLOB_MAX; i++) big[i] = (unsigned char)(i & 0xFF);

    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_status(db) == CFG_OK);
    CHECK(cfg_schema_version(db) == 1);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "desktop/color", 5) == 0);
    CHECK(cfg_set_text(db, "gshell", "desktop/name", "hello") == 0);
    CHECK(cfg_set_blob(db, "app:filer", "window/main", big, CFG_BLOB_MAX) == 0);
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);

    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "desktop/color", 0) == 5);
    CHECK(cfg_get_text(db, "gshell", "desktop/name", buf, 300) == 5);
    CHECK(!strcmp(buf, "hello"));
    CHECK(cfg_get_blob(db, "app:filer", "window/main", out, CFG_BLOB_MAX)
          == CFG_BLOB_MAX);
    CHECK(!memcmp(out, big, CFG_BLOB_MAX));
    CHECK(cfg_close(db) == 0);

    /* delete も往復する */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_delete(db, "gshell", "desktop/name") == 0);
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_text(db, "gshell", "desktop/name", buf, 300) == OS32_ERR_NOTFOUND);
    CHECK(cfg_close(db) == 0);
}

/* ========================================================================= */
/*  (6)(7)(15) set 失敗 → commit 拒否 / close で rollback / 診断の保持       */
/* ========================================================================= */
static void c_txn(void)
{
    CfgDb *db;

    make_good_db();
    /* txn 外の set は拒否 */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == OS32_ERR_INVAL);

    /* set 失敗 → txn を failed にし、commit は拒否して rollback する */
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);
    inj_prep_fail = "INSERT OR REPLACE";
    CHECK(cfg_set_int(db, "gshell", "c", 3) == OS32_ERR_IO);
    inj_prep_fail = NULL;
    CHECK(db->txn == 2);
    CHECK(cfg_last_sqlite(db) != 0);
    {
        int saved = cfg_last_sqlite(db);
        CHECK(cfg_commit(db) == OS32_ERR_IO);          /* commit は拒否 */
        /* 成功した ROLLBACK は診断を 0 に戻さない (票 §1-4) */
        CHECK(cfg_last_sqlite(db) == saved);
    }
    CHECK(cfg_close(db) == 0);
    /* rollback されたので何も残っていない */
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    CHECK(cfg_close(db) == 0);

    /* 未 commit のまま close → rollback */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "b", 2) == 0);
    CHECK(cfg_close(db) == 0);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "b", -1) == -1);
    CHECK(cfg_close(db) == 0);

    /* close の失敗はコードを保持して負を返す */
    CHECK(cfg_open(&db, 1) == 0);
    inj_close_fail = SQLITE_IOERR;
    CHECK(cfg_close(db) == OS32_ERR_IO);
    CHECK(cfg_last_close_error() == SQLITE_IOERR);
    inj_close_fail = 0;
    /* 静的 1 本なので close 後も診断が読める */
    CHECK(cfg_last_sqlite(db) == SQLITE_IOERR);
}

/* ========================================================================= */
/*  (8) 上限と規則                                                           */
/* ========================================================================= */
/* 1 件ずつ独立した txn で試す。検証で断られた set も txn を failed にする
 * 契約 (FOUNDATION §2-3、往復 1 の ④) なので、拒否の直後に commit が
 * 通らないことまでここで固定する。 */
static int try_int(CfgDb *db, const char *scope, const char *key, int v)
{
    int rc;
    CHECK(cfg_begin(db) == 0);
    rc = cfg_set_int(db, scope, key, v);
    CHECK(cfg_commit(db) == (rc == 0 ? 0 : OS32_ERR_IO));
    return rc;
}

static int try_text(CfgDb *db, const char *scope, const char *key, const char *s)
{
    int rc;
    CHECK(cfg_begin(db) == 0);
    rc = cfg_set_text(db, scope, key, s);
    CHECK(cfg_commit(db) == (rc == 0 ? 0 : OS32_ERR_IO));
    return rc;
}

static int try_blob(CfgDb *db, const char *scope, const char *key,
                    const void *p, int n)
{
    int rc;
    CHECK(cfg_begin(db) == 0);
    rc = cfg_set_blob(db, scope, key, p, n);
    CHECK(cfg_commit(db) == (rc == 0 ? 0 : OS32_ERR_IO));
    return rc;
}

static void c_limits(void)
{
    CfgDb *db;
    char key64[CFG_KEY_MAX + 2], txt256[CFG_TEXT_MAX + 2];
    unsigned char blob[CFG_BLOB_MAX + 1];
    char scope64[CFG_SCOPE_MAX + 2];
    int i;

    make_good_db();
    for (i = 0; i < CFG_KEY_MAX + 1; i++) key64[i] = 'a';
    key64[CFG_KEY_MAX + 1] = '\0';
    for (i = 0; i < CFG_TEXT_MAX + 1; i++) txt256[i] = 'x';
    txt256[CFG_TEXT_MAX + 1] = '\0';
    memset(blob, 0xAB, sizeof(blob));
    strcpy(scope64, "app:");
    for (i = 4; i < CFG_SCOPE_MAX + 1; i++) scope64[i] = 'n';
    scope64[CFG_SCOPE_MAX + 1] = '\0';

    CHECK(cfg_open(&db, 1) == 0);

    /* key 63B は通り 64B は拒否 */
    key64[CFG_KEY_MAX] = '\0';
    CHECK(try_int(db, "gshell", key64, 1) == 0);
    key64[CFG_KEY_MAX] = 'a';
    CHECK(try_int(db, "gshell", key64, 1) == OS32_ERR_INVAL);

    /* text 255B は通り 256B は拒否 */
    txt256[CFG_TEXT_MAX] = '\0';
    CHECK(try_text(db, "gshell", "t", txt256) == 0);
    txt256[CFG_TEXT_MAX] = 'x';
    CHECK(try_text(db, "gshell", "t", txt256) == OS32_ERR_INVAL);
    CHECK(try_text(db, "gshell", "t", (const char *)0) == OS32_ERR_INVAL);

    /* blob 4096B は通り 4097B は拒否 */
    CHECK(try_blob(db, "gshell", "b", blob, CFG_BLOB_MAX) == 0);
    CHECK(try_blob(db, "gshell", "b", blob, CFG_BLOB_MAX + 1) == OS32_ERR_INVAL);

    /* scope / key の字句規則 */
    CHECK(try_int(db, "SYSTEM", "a", 1) == OS32_ERR_INVAL);
    CHECK(try_int(db, "app:", "a", 1) == OS32_ERR_INVAL);
    CHECK(try_int(db, "app:My App", "a", 1) == OS32_ERR_INVAL);
    scope64[CFG_SCOPE_MAX] = '\0';
    CHECK(try_int(db, scope64, "a", 1) == 0);
    scope64[CFG_SCOPE_MAX] = 'n';
    CHECK(try_int(db, scope64, "a", 1) == OS32_ERR_INVAL);
    CHECK(try_int(db, "gshell", "Desktop", 1) == OS32_ERR_INVAL);
    CHECK(try_int(db, "gshell", "desktop/", 1) == OS32_ERR_INVAL);
    CHECK(try_int(db, "gshell", "/desktop", 1) == OS32_ERR_INVAL);
    CHECK(try_int(db, "gshell", "", 1) == OS32_ERR_INVAL);

    /* 不正 UTF-8 の text は拒否 */
    CHECK(try_text(db, "gshell", "u", "\xff\xfe") == OS32_ERR_INVAL);
    CHECK(try_text(db, "gshell", "u", "\xc0\x80") == OS32_ERR_INVAL);
    CHECK(try_text(db, "gshell", "u", "\xed\xa0\x80") == OS32_ERR_INVAL);
    CHECK(try_text(db, "gshell", "u", "\xe3\x81\x82") == 0);   /* あ */

    /* int32 の両端が往復する (⑰: INT_MIN の単項マイナスを踏まない) */
    CHECK(try_int(db, "gshell", "imin", -2147483647 - 1) == 0);
    CHECK(try_int(db, "gshell", "imax", 2147483647) == 0);
    CHECK(cfg_get_int(db, "gshell", "imin", 0) == -2147483647 - 1);
    CHECK(cfg_get_int(db, "gshell", "imax", 0) == 2147483647);
    CHECK(cfg_close(db) == 0);
}

/* ========================================================================= */
/*  (9)(14) enum — 件数・順序・再入・前方一致・257 件                        */
/* ========================================================================= */
static int en_n;
static char en_seen[CFG_ENUM_MAX][CFG_KEY_MAX + 1];
static int en_types[CFG_ENUM_MAX];
static CfgDb *en_db;
static int en_reenter_rc;

static int en_cb(const char *key, int type, void *ctx)
{
    (void)ctx;
    if (en_n < CFG_ENUM_MAX) {
        strcpy(en_seen[en_n], key);
        en_types[en_n] = type;
    }
    en_n++;
    return 0;
}

static int en_scope_n;
static char en_scopes[CFG_SCOPES_MAX][CFG_SCOPE_MAX + 1];

static int en_scope_cb(const char *scope, void *ctx)
{
    (void)ctx;
    if (en_scope_n < CFG_SCOPES_MAX) strcpy(en_scopes[en_scope_n], scope);
    en_scope_n++;
    return 0;
}

static int en_cb_reenter(const char *key, int type, void *ctx)
{
    (void)key; (void)type; (void)ctx;
    en_reenter_rc = cfg_enum(en_db, "gshell", NULL, en_cb, NULL);
    en_n++;
    return 0;
}

static void c_enum(void)
{
    CfgDb *db;
    int i;
    char key[32];

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a_b", 1) == 0);
    CHECK(cfg_set_int(db, "gshell", "axb", 2) == 0);
    CHECK(cfg_set_int(db, "gshell", "a_c", 3) == 0);
    CHECK(cfg_set_text(db, "gshell", "b_z", "t") == 0);
    CHECK(cfg_set_int(db, "system", "s1", 1) == 0);
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);

    CHECK(cfg_open(&db, 0) == 0);
    en_n = 0;
    CHECK(cfg_enum(db, "gshell", NULL, en_cb, NULL) == 4);
    CHECK(en_n == 4);
    CHECK(!strcmp(en_seen[0], "a_b"));      /* ORDER BY key */
    CHECK(!strcmp(en_seen[1], "a_c"));
    CHECK(!strcmp(en_seen[2], "axb"));
    CHECK(!strcmp(en_seen[3], "b_z"));
    CHECK(en_types[3] == CFG_TYPE_TEXT);

    /* prefix "a_" は LIKE ではなく substr の前方一致 — axb は入らない */
    en_n = 0;
    CHECK(cfg_enum(db, "gshell", "a_", en_cb, NULL) == 2);
    CHECK(!strcmp(en_seen[0], "a_b") && !strcmp(en_seen[1], "a_c"));

    /* scope 一覧 (DISTINCT + ORDER BY) */
    en_scope_n = 0;
    CHECK(cfg_enum_scopes(db, en_scope_cb, NULL) == 2);
    CHECK(!strcmp(en_scopes[0], "gshell") && !strcmp(en_scopes[1], "system"));
    CHECK(cfg_close(db) == 0);

    /* callback の中からの cfg_* は拒否 */
    CHECK(cfg_open(&db, 0) == 0);
    en_db = db;
    en_n = 0;
    en_reenter_rc = 0;
    CHECK(cfg_enum(db, "gshell", NULL, en_cb_reenter, NULL) >= 1);
    CHECK(en_reenter_rc == OS32_ERR_INVAL);
    CHECK(cfg_close(db) == 0);

    /* 257 件目は NOSPC で callback を 1 度も呼ばない */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    for (i = 0; i < CFG_ENUM_MAX + 1; i++) {
        sprintf(key, "k%04d", i);
        CHECK(cfg_set_int(db, "user", key, i) == 0);
    }
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);
    CHECK(cfg_open(&db, 0) == 0);
    en_n = 0;
    CHECK(cfg_enum(db, "user", NULL, en_cb, NULL) == OS32_ERR_NOSPC);
    CHECK(en_n == 0);
    CHECK(cfg_close(db) == 0);
}

/* ========================================================================= */
/*  (13)(18) meta の 0 行 / 2 行 / TEXT / 2^32+1 / 正常行との混在            */
/* ========================================================================= */
static void meta_case(const char *insert1, const char *insert2, int want)
{
    CfgDb *db;
    const char *stmts[6];
    int n = 0;

    reset_all();
    stmts[n++] = "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)";
    stmts[n++] = "CREATE TABLE settings (scope TEXT, key TEXT, type INTEGER,"
                 " ival INTEGER, tval TEXT, bval BLOB, PRIMARY KEY (scope,key))"
                 " WITHOUT ROWID";
    if (insert1) stmts[n++] = insert1;
    if (insert2) stmts[n++] = insert2;
    stmts[n] = NULL;
    raw_db(CFG_DB_PATH, stmts);
    CHECK(cfg_open(&db, 0) == 0);
    if (cfg_status(db) != want) {
        fprintf(stderr, "FAIL meta: %s / %s -> %d (want %d)\n",
                insert1 ? insert1 : "(none)", insert2 ? insert2 : "(none)",
                cfg_status(db), want);
        exit(1);
    }
    CHECK(cfg_close(db) == 0);
}

static void c_meta(void)
{
    meta_case(NULL, NULL, CFG_CORRUPT);                        /* 0 行 */
    meta_case("INSERT INTO meta VALUES (1,'a')",
              "INSERT INTO meta VALUES (1,'b')", CFG_CORRUPT); /* 2 行 */
    /* TEXT — INTEGER 親和性で '1' は整数に化けるので、化けない値で見る。 */
    meta_case("INSERT INTO meta VALUES ('abc','a')", NULL, CFG_CORRUPT);
    meta_case("INSERT INTO meta VALUES (x'01','a')", NULL, CFG_CORRUPT);
    meta_case("INSERT INTO meta VALUES (1.5,'a')", NULL, CFG_CORRUPT);
    meta_case("INSERT INTO meta VALUES (4294967297,'a')", NULL, CFG_CORRUPT);
    meta_case("INSERT INTO meta VALUES (0,'a')", NULL, CFG_CORRUPT);
    meta_case("INSERT INTO meta VALUES (-1,'a')", NULL, CFG_CORRUPT);
    /* 正常行との混在 */
    meta_case("INSERT INTO meta VALUES (1,'a')",
              "INSERT INTO meta VALUES (0,'b')", CFG_CORRUPT);
    meta_case("INSERT INTO meta VALUES (1,'a')",
              "INSERT INTO meta VALUES (4294967297,'b')", CFG_CORRUPT);
    meta_case("INSERT INTO meta VALUES (1,'a')", NULL, CFG_OK);
    meta_case("INSERT INTO meta VALUES (2,'a')", NULL, CFG_VERSION);
}

/* ========================================================================= */
/*  (17) SHM の row は private へ即コピーされている                          */
/* ========================================================================= */
static void c_shm_copy(void)
{
    CfgDb *db;
    char buf[64];
    int other;

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_text(db, "gshell", "t", "private") == 0);
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_get_text(db, "gshell", "t", buf, 64) == 7);
    CHECK(!strcmp(buf, "private"));
    /* 別接続が SHM を丸ごと上書きしても、取り出した値は壊れない */
    other = kapi_db_open_existing(CFG_DB_PATH, 0);
    CHECK(other >= 0);
    CHECK(kapi_db_prepare_only(other, "SELECT 'AAAAAAAAAAAAAAAA'") == 0);
    CHECK(kapi_db_step(other) == DB_STATUS_ROW);
    CHECK(!strcmp(buf, "private"));
    CHECK(kapi_db_finalize(other) == 0);
    CHECK(kapi_db_close(other) == 0);
    CHECK(cfg_close(db) == 0);
}

/* ========================================================================= */
/*  (11)(22) cfg_init                                                        */
/* ========================================================================= */
static const char TSV_OK_TEXT[] =
    "# comment\n"
    "\n"
    "gshell\tdesktop/color\tint\t1\n"
    "gshell\tdesktop/wallpaper\ttext\t\n"
    "gshell\ttaskbar/clock_24h\tint\t1\n";

static void c_init(void)
{
    CfgDb *db;
    char buf[64];

    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);
    CHECK(cfg_last_init_reason() == CFG_INIT_NONE);
    CHECK(fixture_find(CFG_DB_PATH, 0) != NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) == NULL);

    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_OK);
    CHECK(cfg_schema_version(db) == 1);
    CHECK(cfg_get_int(db, "gshell", "desktop/color", 0) == 1);
    CHECK(cfg_get_int(db, "gshell", "taskbar/clock_24h", 0) == 1);
    /* 末尾の空欄は NULL ではなく空 text */
    CHECK(cfg_get_text(db, "gshell", "desktop/wallpaper", buf, 64) == 0);
    CHECK(cfg_close(db) == 0);

    /* 生成した DB の形が mk_settings_db.py と一致すること
     * (user_version / meta の版 / 行数 / settings の列)。 */
    {
        int h = kapi_db_open_existing(CFG_DB_PATH, 0);
        CHECK(h >= 0);
        /* meta は 1 行、版は 1 */
        CHECK(kapi_db_prepare_only(h,
              "SELECT COUNT(*), MIN(schema_version) FROM meta") == 0);
        CHECK(kapi_db_step(h) == DB_STATUS_ROW);
        CHECK((int)cfg_i_col_int(0) == 1 && (int)cfg_i_col_int(1) == CFG_SCHEMA_VERSION);
        CHECK(kapi_db_finalize(h) == 0);
        /* settings は tsv の 3 行、列は 6 本 */
        CHECK(kapi_db_prepare_only(h,
              "SELECT COUNT(*) FROM settings") == 0);
        CHECK(kapi_db_step(h) == DB_STATUS_ROW);
        CHECK((int)cfg_i_col_int(0) == 3);
        CHECK(kapi_db_finalize(h) == 0);
        CHECK(kapi_db_prepare_only(h,
              "SELECT scope,key,type,ival,tval,bval FROM settings") == 0);
        CHECK(kapi_db_finalize(h) == 0);
        /* WITHOUT ROWID — rowid が無いこと */
        CHECK(kapi_db_prepare_only(h, "SELECT rowid FROM settings") != 0);
        CHECK(kapi_db_finalize(h) == 0);
        /* PRAGMA user_version はカーネルの SQLite では省かれている
         * (SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS) — 読みも書きも無効。
         * mk_settings_db.py (ホストの CPython SQLite) が入れる user_version は
         * `cfg init` では 0 のままになる。版の正典は meta.schema_version で、
         * ライブラリは user_version を読まないので動作には影響しない。 */
        CHECK(kapi_db_prepare_only(h, "PRAGMA user_version") == 0);
        CHECK(kapi_db_step(h) == DB_STATUS_DONE);
        CHECK(kapi_db_finalize(h) == 0);
        CHECK(kapi_db_close(h) == 0);
    }

    /* 2 度目は already exists で何も変えない */
    CHECK(cfg_init(NULL) == OS32_ERR_EXIST);
    CHECK(cfg_last_init_reason() == CFG_INIT_EXISTS);

    /* 0 バイトの本体も「ある」 */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(fixture_find(CFG_DB_PATH, 1) != NULL);
    CHECK(cfg_init(NULL) == OS32_ERR_EXIST);

    /* journal の残骸 */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    put_file(CFG_DB_JOURNAL_PATH, "x");
    CHECK(cfg_init(NULL) == OS32_ERR_NOTEMPTY);
    CHECK(cfg_last_init_reason() == CFG_INIT_JOURNAL);
    CHECK(fixture_find(CFG_DB_JOURNAL_PATH, 0) != NULL);   /* 消さない */
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);

    /* .new の残骸 — 拒否して消さない (22) */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    put_file(CFG_DB_NEW_PATH, "stale");
    CHECK(cfg_init(NULL) == OS32_ERR_NOTEMPTY);
    CHECK(cfg_last_init_reason() == CFG_INIT_STALE_NEW);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) != NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0)->size == 5);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);

    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    put_file(CFG_DB_NEW_JOURNAL_PATH, "stale");
    CHECK(cfg_init(NULL) == OS32_ERR_NOTEMPTY);
    CHECK(cfg_last_init_reason() == CFG_INIT_STALE_NEW);
    CHECK(fixture_find(CFG_DB_NEW_JOURNAL_PATH, 0) != NULL);

    /* tsv が無い / 規則違反なら何も作らない */
    reset_all();
    CHECK(cfg_init(NULL) == OS32_ERR_NOTFOUND);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);
    reset_all();
    put_file(CFG_TSV_PATH, "gshell\tDesktop\tint\t1\n");
    CHECK(cfg_init(NULL) == OS32_ERR_INVAL);
    CHECK(cfg_last_init_reason() == CFG_INIT_TSV);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) == NULL);
}

/* ========================================================================= */
/*  (16)(20) rename の失敗                                                   */
/* ========================================================================= */
static void c_rename(void)
{
    /* 新名は出来たが旧名の削除も巻き戻しも失敗 = 同じ inode を 2 名が指す。
     * `.new` を消すと本体が壊れるので、**どちらも消さない**。 */
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    inj_rename_mode = 1;
    CHECK(cfg_init(NULL) == OS32_ERR_NOTEMPTY);
    CHECK(cfg_last_init_reason() == CFG_INIT_AMBIGUOUS);
    CHECK(fixture_find(CFG_DB_PATH, 0) != NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) != NULL);

    /* 何も起きなかった → `.new` を片付けて失敗 */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    inj_rename_mode = 2;
    CHECK(cfg_init(NULL) == OS32_ERR_IO);
    CHECK(cfg_last_init_reason() == CFG_INIT_RENAME);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) == NULL);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);

    /* 旧名は消えたが rename が失敗を返した → 本体を検査して成功にする */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    inj_rename_mode = 3;
    CHECK(cfg_init(NULL) == 0);
    CHECK(fixture_find(CFG_DB_PATH, 0) != NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) == NULL);
}

/* ========================================================================= */
/*  (10)(21) tsv reader — stdin の fixture を判定するだけ                    */
/* ========================================================================= */
/* 判定は **`cfg_init` を通して**出す。重複 (scope, key) の検出は控えを
 * 持たず `settings` の PRIMARY KEY に任せた (往復 1 の ⑯) ので、reader 単体
 * ではなく「その tsv で DB が作れるか」が Python と突き合わせる単位になる。 */
static void c_tsv(void)
{
    static unsigned char blob_in[FIXTURE_BYTES];
    int n = (int)fread(blob_in, 1, sizeof(blob_in), stdin);
    int rc;

    CHECK(fixture_create(NULL, CFG_TSV_PATH, blob_in, (u32)n) == VFS_OK);
    rc = cfg_init(NULL);
    if (rc == 0) {
        CfgDb *db;
        int rows = -1;
        CHECK(cfg_open(&db, 0) == 0);
        CHECK(cfg_status(db) == CFG_OK);
        if (cfg_i_prepare(db, "SELECT COUNT(*) FROM settings") == 0 &&
            cfg_backend()->db_step(db->handle) == DB_STATUS_ROW)
            rows = (int)cfg_i_col_int(0);
        cfg_backend()->db_finalize(db->handle);
        CHECK(cfg_close(db) == 0);
        printf("TSV ACCEPT %d\n", rows);
    } else {
        printf("TSV REJECT %d\n", cfg_last_init_reason());
    }
}

/* ========================================================================= */
/*  cfg コマンド — 引数解釈と整形 (純関数) + 通し                            */
/* ========================================================================= */
static void c_args(void)
{
    CfgArgs a;
    char *av1[] = { "cfg", "status" };
    char *av2[] = { "cfg", "get", "gshell", "k" };
    char *av3[] = { "cfg", "get", "gshell", "k", "9" };
    char *av4[] = { "cfg", "set", "gshell", "k", "int", "5" };
    char *av5[] = { "cfg", "set", "gshell", "k", "bogus", "5" };
    char *av6[] = { "cfg", "list" };
    char *av7[] = { "cfg", "list", "gshell", "desk" };
    char *av8[] = { "cfg", "init", "--tsv", "/x.tsv" };
    char *av9[] = { "cfg", "export", "/out.json" };
    char *av10[] = { "cfg", "nope" };
    char buf[64];
    unsigned char bb[8];

    CHECK(cfg_cmd_parse(2, av1, &a) == 0 && a.cmd == CFG_CMD_STATUS);
    CHECK(cfg_cmd_parse(4, av2, &a) == 0 && a.cmd == CFG_CMD_GET && a.def == NULL);
    CHECK(cfg_cmd_parse(5, av3, &a) == 0 && !strcmp(a.def, "9"));
    CHECK(cfg_cmd_parse(6, av4, &a) == 0 && !strcmp(a.type, "int"));
    CHECK(cfg_cmd_parse(6, av5, &a) == -1);
    CHECK(cfg_cmd_parse(2, av6, &a) == 0 && a.scope == NULL);
    CHECK(cfg_cmd_parse(4, av7, &a) == 0 && !strcmp(a.prefix, "desk"));
    CHECK(cfg_cmd_parse(4, av8, &a) == 0 && !strcmp(a.path, "/x.tsv"));
    CHECK(cfg_cmd_parse(3, av9, &a) == 0 && a.cmd == CFG_CMD_EXPORT);
    CHECK(cfg_cmd_parse(2, av10, &a) == -1);
    CHECK(cfg_cmd_parse(1, av1, &a) == -1);

    CHECK(fmt_int(buf, 64, 0) == 1 && !strcmp(buf, "0"));
    CHECK(fmt_int(buf, 64, -2147483647 - 1) == 11 && !strcmp(buf, "-2147483648"));
    CHECK(fmt_int(buf, 64, 2147483647) == 10 && !strcmp(buf, "2147483647"));
    {
        int v;
        CHECK(parse_int("2147483647", &v) == 0 && v == 2147483647);
        CHECK(parse_int("-2147483648", &v) == 0 && v == -2147483647 - 1);
        CHECK(parse_int("2147483648", &v) == -1);
        CHECK(parse_int("0x10", &v) == -1);
        CHECK(parse_int("", &v) == -1);
        CHECK(parse_int("+1", &v) == -1);
    }
    CHECK(parse_hex("0102ff", bb, 8) == 3 && bb[0] == 1 && bb[2] == 0xFF);
    CHECK(parse_hex("0f0", bb, 8) == -1);
    CHECK(parse_hex("zz", bb, 8) == -1);
    CHECK(parse_hex("", bb, 8) == 0);
    bb[0] = 0xAB; bb[1] = 0xCD;
    CHECK(fmt_hex(buf, 64, bb, 2) == 4 && !strcmp(buf, "abcd"));
    CHECK(fmt_b64(buf, 64, (const unsigned char *)"f", 1) == 4 && !strcmp(buf, "Zg=="));
    CHECK(fmt_b64(buf, 64, (const unsigned char *)"fo", 2) == 4 && !strcmp(buf, "Zm8="));
    CHECK(fmt_b64(buf, 64, (const unsigned char *)"foo", 3) == 4 && !strcmp(buf, "Zm9v"));
    CHECK(fmt_json_str(buf, 64, "a\"b\\c\n", 6) == 9 && !strcmp(buf, "a\\\"b\\\\c\\n"));
    CHECK(!strcmp(status_name(CFG_MISSING), "MISSING"));
    CHECK(!strcmp(type_name(CFG_TYPE_BLOB), "blob"));
}

/* ========================================================================= */
/*  (1)(23) cfg コマンドの通し — status / init / set / get / list / export   */
/* ========================================================================= */
static int ran(const char *a1, const char *a2, const char *a3, const char *a4,
               const char *a5)
{
    char *av[6];
    int n = 1;
    av[0] = (char *)"cfg";
    if (a1) av[n++] = (char *)a1;
    if (a2) av[n++] = (char *)a2;
    if (a3) av[n++] = (char *)a3;
    if (a4) av[n++] = (char *)a4;
    if (a5) av[n++] = (char *)a5;
    cap_len = 0;
    memset(&g_db, 0, sizeof(g_db));
    return cfg_main(n, av, &host_api);
}

static int cap_has(const char *needle)
{
    cap_out[cap_len] = '\0';
    return strstr(cap_out, needle) != NULL;
}

static void c_cmd(void)
{
    /* C1: DB が無ければ MISSING / 終了 1 / ファイルは作られない */
    CHECK(ran("status", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("MISSING"));
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);

    /* C2: init → status → list */
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(ran("init", NULL, NULL, NULL, NULL) == 0);
    CHECK(ran("status", NULL, NULL, NULL, NULL) == 0);
    CHECK(cap_has("OK schema_version 1 pool "));
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 0);
    CHECK(cap_has("gshell\tdesktop/color\tint\t1"));
    CHECK(cap_has("gshell\ttaskbar/clock_24h\tint\t1"));
    CHECK(ran("init", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("already exists"));

    /* C3: set / get */
    CHECK(ran("set", "gshell", "desktop/color", "int", "5") == 0);
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("5\n"));
    CHECK(ran("get", "gshell", "nosuch", NULL, NULL) == 0);
    CHECK(cap_has("(not set)"));
    CHECK(ran("get", "gshell", "nosuch", "42", NULL) == 0);
    CHECK(cap_has("42"));
    CHECK(ran("set", "gshell", "b", "blob", "0102ff") == 0);
    CHECK(ran("get", "gshell", "b", NULL, NULL) == 0);
    CHECK(cap_has("0102ff"));
    CHECK(ran("set", "gshell", "Bad", "int", "1") == 1);
    CHECK(cap_has("set rejected"));
    CHECK(ran("del", "gshell", "b", NULL, NULL) == 0);
    CHECK(ran("get", "gshell", "b", NULL, NULL) == 0);
    CHECK(cap_has("(not set)"));

    /* C4: export — 行数 = 件数 + 1、ヘッダの版が status と一致 */
    CHECK(ran("export", "/s.json", NULL, NULL, NULL) == 0);
    {
        FixtureFile *f = fixture_find("/s.json", 0);
        int lines = 0;
        u32 i;
        CHECK(f != NULL);
        for (i = 0; i < f->size; i++) if (f->data[i] == '\n') lines++;
        CHECK(lines == 4);                 /* ヘッダ + 3 件 */
        f->data[f->size] = '\0';
        CHECK(strstr((char *)f->data, "{\"schema_version\":1,\"exported\":\"7\"}"));
        CHECK(strstr((char *)f->data,
                     "{\"scope\":\"gshell\",\"key\":\"desktop/color\","
                     "\"type\":0,\"v\":5}"));
        CHECK(strstr((char *)f->data,
                     "\"key\":\"desktop/wallpaper\",\"type\":1,\"v\":\"\""));
    }

    /* (23) meta が 2 の DB の export ヘッダは実値 2 */
    reset_all();
    {
        static const char *v2[] = {
            "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
            "CREATE TABLE settings (scope TEXT NOT NULL, key TEXT NOT NULL,"
            " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
            " PRIMARY KEY (scope, key)) WITHOUT ROWID",
            "INSERT INTO meta VALUES (2, '0')",
            "INSERT INTO settings VALUES ('gshell','k',0,3,NULL,NULL)",
            NULL
        };
        FixtureFile *f;
        raw_db(CFG_DB_PATH, v2);
        CHECK(ran("export", "/s2.json", NULL, NULL, NULL) == 0);
        f = fixture_find("/s2.json", 0);
        CHECK(f != NULL);
        f->data[f->size] = '\0';
        CHECK(strstr((char *)f->data, "{\"schema_version\":2,"));
        /* VERSION では書けない */
        CHECK(ran("set", "gshell", "k", "int", "4") == 1);
        CHECK(cap_has("cannot write: VERSION"));
    }

    /* MISSING の export は失敗し、案内を出す */
    reset_all();
    CHECK(ran("export", "/s3.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("cannot export: MISSING"));
    CHECK(cap_has("settings.db missing: run 'cfg init'"));
}

/* ========================================================================= */
/*  list が出力バッファを越える — 閉じて吐いて開き直し、続きから出す          */
/* ========================================================================= */
static void c_list_big(void)
{
    CfgDb *db;
    char key[32], val[201];
    int i, lines = 0, n;

    make_good_db();
    memset(val, 'v', 200);
    val[200] = '\0';
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    for (i = 0; i < 100; i++) {
        sprintf(key, "row/%03d", i);
        CHECK(cfg_set_text(db, "user", key, val) == 0);
    }
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);

    /* 1 行 ~220B x 100 = 22KB > CFG_OUT_MAX (8KB) なので必ず複数回に割れる */
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 0);
    CHECK(cap_len > CFG_OUT_MAX);
    for (i = 0; i < cap_len; i++) if (cap_out[i] == '\n') lines++;
    CHECK(lines == 100);                       /* 落ちも重複もしない */
    cap_out[cap_len] = '\0';
    for (i = 0; i < 100; i++) {
        sprintf(key, "user\trow/%03d\ttext\t", i);
        CHECK(strstr(cap_out, key) != NULL);
        /* 同じ key が 2 度出ていないこと */
        n = 0;
        {
            const char *p = cap_out;
            while ((p = strstr(p, key)) != NULL) { n++; p++; }
        }
        CHECK(n == 1);
    }
    CHECK(cap_yields > 0);                     /* 1KB ごとに間を作っている */
}

/* ========================================================================= */
/*  実装レビュー 往復 1 の blocker (s2_tdd.md §C)                            */
/* ========================================================================= */

/* ② stat の失敗を「不存在」に丸めない */
static void c_stat_fail(void)
{
    /* (a) 既存 `.new` の stat が IOERR — 進まず、消さない */
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    put_file(CFG_DB_NEW_PATH, "stale");
    stat_fail_on = "settings.db.new";
    CHECK(cfg_init(NULL) == OS32_ERR_NOTEMPTY);
    CHECK(cfg_last_init_reason() == CFG_INIT_STALE_NEW);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) != NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0)->size == 5);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);

    /* (b) journal の stat が IOERR — 進まない */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    stat_fail_on = "settings.db-journal";
    CHECK(cfg_init(NULL) == OS32_ERR_NOTEMPTY);
    CHECK(cfg_last_init_reason() == CFG_INIT_JOURNAL);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);

    /* (c) 本体の stat が IOERR — 「無い」と思って作らない */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    stat_fail_exact = CFG_DB_PATH;
    CHECK(cfg_init(NULL) == OS32_ERR_IO);
    CHECK(cfg_last_init_reason() == CFG_INIT_STAT);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) == NULL);

    /* (d) rename が失敗し、本体の stat が判定不能 — `.new` を消さない。
     * 入口の (a) は通す必要があるので 1 回だけ素通しさせる。 */
    reset_all();
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    inj_rename_mode = 2;                 /* 何も起きずに失敗 */
    stat_fail_exact = CFG_DB_PATH;
    stat_fail_skip = 1;
    CHECK(cfg_init(NULL) == OS32_ERR_NOTEMPTY);
    CHECK(cfg_last_init_reason() == CFG_INIT_AMBIGUOUS);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) != NULL);   /* 消していない */
}

/* ③ export の出力先に設定 DB 自身を指定できない */
static void c_export_guard(void)
{
    u32 before;

    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);
    before = fixture_find(CFG_DB_PATH, 0)->size;
    CHECK(before > 0);

    CHECK(ran("export", CFG_DB_PATH, NULL, NULL, NULL) == 1);
    CHECK(cap_has("refusing to write"));
    CHECK(fixture_find(CFG_DB_PATH, 0)->size == before);

    CHECK(ran("export", CFG_DB_JOURNAL_PATH, NULL, NULL, NULL) == 1);
    CHECK(fixture_find(CFG_DB_JOURNAL_PATH, 0) == NULL);   /* 作らせない */
    CHECK(ran("export", CFG_DB_NEW_PATH, NULL, NULL, NULL) == 1);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) == NULL);
    CHECK(ran("export", CFG_DB_NEW_JOURNAL_PATH, NULL, NULL, NULL) == 1);

    /* `.` / `..` / 連続 `/` を畳んでから突き合わせる */
    CHECK(ran("export", "/etc/./settings.db", NULL, NULL, NULL) == 1);
    CHECK(ran("export", "/etc//settings.db", NULL, NULL, NULL) == 1);
    CHECK(ran("export", "/etc/x/../settings.db", NULL, NULL, NULL) == 1);
    CHECK(fixture_find(CFG_DB_PATH, 0)->size == before);

    /* 相対名は cwd を足してから見る */
    host_cwd = "/etc";
    CHECK(ran("export", "settings.db", NULL, NULL, NULL) == 1);
    CHECK(ran("export", "./settings.db", NULL, NULL, NULL) == 1);
    host_cwd = "/etc/sub";
    CHECK(ran("export", "../settings.db", NULL, NULL, NULL) == 1);
    CHECK(fixture_find(CFG_DB_PATH, 0)->size == before);

    /* 別名なら通る (正規化した名前で作られる) */
    host_cwd = "/";
    CHECK(ran("export", "/tmp/../backup.json", NULL, NULL, NULL) == 0);
    CHECK(fixture_find("/backup.json", 0) != NULL);
}

/* ④ 入力検証で断られた set も txn を failed にする */
static void c_txn_poison(void)
{
    CfgDb *db;
    char big[CFG_TEXT_MAX + 2];
    int i;

    for (i = 0; i < CFG_TEXT_MAX + 1; i++) big[i] = 'x';
    big[CFG_TEXT_MAX + 1] = '\0';

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);          /* A は通る */
    CHECK(cfg_set_text(db, "gshell", "b", big) == OS32_ERR_INVAL);
    CHECK(cfg_commit(db) == OS32_ERR_IO);                   /* commit は拒否 */
    CHECK(cfg_close(db) == 0);
    /* A も保存されていない (半端な更新を残さない) */
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    CHECK(cfg_close(db) == 0);

    /* scope / key の規則違反でも同じ */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);
    CHECK(cfg_set_int(db, "SYSTEM", "a", 1) == OS32_ERR_INVAL);
    CHECK(cfg_commit(db) == OS32_ERR_IO);
    CHECK(cfg_close(db) == 0);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    CHECK(cfg_close(db) == 0);

    /* txn の外での拒否は failed にしない (txn 自体が無い) */
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == OS32_ERR_INVAL);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);
}

/* ⑤ list / export が値取得の障害を成功として返さない */
static void c_fetch_fail(void)
{
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);

    /* 値の SELECT (SQL_GET) だけを落とす。列挙 (SQL_ENUM) は通る。 */
    inj_prep_fail = "ival";
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("list failed"));

    CHECK(ran("export", "/out.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("export failed"));
    /* 中途半端なバックアップを残さない */
    CHECK(fixture_find("/out.json", 0) == NULL);
    inj_prep_fail = NULL;

    /* get も同じ — 既定値でごまかさない */
    CHECK(ran("set", "gshell", "k", "int", "3") == 0);
    inj_prep_fail = "ival";
    CHECK(ran("get", "gshell", "k", "9", NULL) == 1);
    CHECK(cap_has("get failed"));
    CHECK(!cap_has("\n9\n"));
}

/* ⑥ NULL の値を空値や 0 に化けさせない */
static void c_nullval(void)
{
    static const char *rows[] = {
        "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
        "CREATE TABLE settings (scope TEXT NOT NULL, key TEXT NOT NULL,"
        " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
        " PRIMARY KEY (scope, key)) WITHOUT ROWID",
        "INSERT INTO meta VALUES (1, '0')",
        "INSERT INTO settings VALUES ('gshell','tn',1,NULL,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','in',0,NULL,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','bn',2,NULL,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','te',1,NULL,'',NULL)",
        "INSERT INTO settings VALUES ('gshell','odd',7,NULL,NULL,NULL)",
        NULL
    };
    CfgDb *db;
    FixtureFile *f;

    raw_db(CFG_DB_PATH, rows);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_info(db, "gshell", "tn") == (CFG_TYPE_TEXT | CFG_INFO_NULL));
    CHECK(cfg_get_info(db, "gshell", "in") == (CFG_TYPE_INT | CFG_INFO_NULL));
    CHECK(cfg_get_info(db, "gshell", "bn") == (CFG_TYPE_BLOB | CFG_INFO_NULL));
    CHECK(cfg_get_info(db, "gshell", "te") == CFG_TYPE_TEXT);
    CHECK(cfg_get_info(db, "gshell", "odd") == OS32_ERR_NOSYS);
    CHECK(cfg_get_info(db, "gshell", "nosuch") == OS32_ERR_NOTFOUND);
    CHECK(cfg_close(db) == 0);

    /* get は既定値を出す (空 text や 0 にしない) */
    CHECK(ran("get", "gshell", "tn", "7", NULL) == 0);
    CHECK(cap_has("7\n"));
    CHECK(ran("get", "gshell", "in", "7", NULL) == 0);
    CHECK(cap_has("7\n"));
    CHECK(ran("get", "gshell", "te", "7", NULL) == 0);
    CHECK(cap_has("\n"));
    CHECK(!cap_has("7"));
    /* 認識できない type 列は失敗 */
    CHECK(ran("get", "gshell", "odd", "7", NULL) == 1);

    /* export は NULL を明示する。認識できない type があると失敗させる。 */
    CHECK(ran("export", "/n.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("export failed"));
    CHECK(fixture_find("/n.json", 0) == NULL);

    /* odd を消せば通り、NULL は "v":null で出る */
    CHECK(ran("del", "gshell", "odd", NULL, NULL) == 0);
    CHECK(ran("export", "/n.json", NULL, NULL, NULL) == 0);
    f = fixture_find("/n.json", 0);
    CHECK(f != NULL);
    f->data[f->size] = '\0';
    /* 宣言型は保ったまま値だけ null (往復 2 の 4) */
    CHECK(strstr((char *)f->data, "\"key\":\"tn\",\"type\":1,\"v\":null"));
    CHECK(strstr((char *)f->data, "\"key\":\"in\",\"type\":0,\"v\":null"));
    CHECK(strstr((char *)f->data, "\"key\":\"bn\",\"type\":2,\"v\":null"));
    CHECK(strstr((char *)f->data, "\"key\":\"te\",\"type\":1,\"v\":\"\""));
}

/* ⑦ 壊れた DB は schema 検査の途中で分かっても CORRUPT */
static void c_corrupt_schema(void)
{
    CfgDb *db;
    static unsigned char junk[4096];
    int i;

    /* 写像そのものを固定する (IOERR / NOMEM / BUSY は ERROR のまま) */
    CHECK(map_sql_failure(SQLITE_NOTADB) == CFG_CORRUPT);
    CHECK(map_sql_failure(SQLITE_CORRUPT) == CFG_CORRUPT);
    CHECK(map_sql_failure(SQLITE_ERROR) == CFG_CORRUPT);
    CHECK(map_sql_failure(SQLITE_BUSY_RECOVERY) == CFG_CORRUPT);
    CHECK(map_sql_failure(SQLITE_IOERR) == CFG_ERROR);
    CHECK(map_sql_failure(SQLITE_NOMEM) == CFG_ERROR);
    CHECK(map_sql_failure(SQLITE_BUSY) == CFG_ERROR);
    CHECK(map_sql_failure(0) == CFG_ERROR);

    /* 非ゼロ長の非 SQLite ファイル */
    for (i = 0; i < (int)sizeof(junk); i++) junk[i] = (unsigned char)(i | 0x40);
    CHECK(fixture_create(NULL, CFG_DB_PATH, junk, (u32)sizeof(junk)) == VFS_OK);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_CORRUPT);
    CHECK(cfg_close(db) == 0);
    CHECK(fixture_find(CFG_DB_PATH, 0)->size == sizeof(junk));  /* 触らない */
}

/* ⑧ COMMIT の失敗は、続く ROLLBACK の失敗で上書きされない */
static void c_commit_diag(void)
{
    CfgDb *db;
    int commit_code;

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);
    /* COMMIT は「表が無い」(SQLITE_ERROR = 1)、後片付けの ROLLBACK は
     * NOT NULL 違反 (SQLITE_CONSTRAINT = 19) にして、別のコードを作る。 */
    inj_exec_match[0] = "COMMIT";
    inj_exec_sub[0] = "SELECT 1 FROM no_such_table_for_tdd";
    inj_exec_match[1] = "ROLLBACK";
    inj_exec_sub[1] = "INSERT INTO settings(scope,key,type) VALUES(NULL,'k',0)";
    CHECK(cfg_commit(db) == OS32_ERR_IO);
    commit_code = cfg_last_sqlite(db);
    CHECK(CFG_SQLITE_PRIMARY(commit_code) == CFG_SQLITE_ERROR);   /* 元の原因 */
    CHECK(CFG_SQLITE_PRIMARY(db->cleanup_sqlite) == CFG_SQLITE_CONSTRAINT);
    memset(inj_exec_match, 0, sizeof(inj_exec_match));
    /* 後片付けの失敗は close の戻り値にも出る */
    CHECK(cfg_close(db) == OS32_ERR_IO);
    CHECK(cfg_last_close_error() != 0);
    CHECK(cfg_last_sqlite(db) == commit_code);
}

/* ⑨ open の途中で捨てた接続の close 失敗が消えない */
static void c_open_close_fail(void)
{
    CfgDb *db;
    static const char *bad[] = {
        "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
        "CREATE TABLE settings (scope TEXT, key TEXT, type INTEGER,"
        " ival INTEGER, tval TEXT, bval BLOB, PRIMARY KEY (scope,key))"
        " WITHOUT ROWID",
        NULL                              /* meta は 0 行 = CORRUPT */
    };
    raw_db(CFG_DB_PATH, bad);

    inj_close_fail = SQLITE_IOERR;
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_CORRUPT);
    /* 内部で捨てた接続の close が失敗したことが公開 close に出る */
    CHECK(cfg_close(db) == OS32_ERR_IO);
    CHECK(cfg_last_close_error() == SQLITE_IOERR);
    inj_close_fail = 0;

    /* RO → RW の切り替えで捨てる接続でも同じ */
    reset_all();
    make_good_db();
    inj_close_fail = SQLITE_IOERR;
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_status(db) == CFG_ERROR);
    CHECK(cfg_begin(db) == OS32_ERR_INVAL);
    CHECK(cfg_close(db) == OS32_ERR_IO);
    CHECK(cfg_last_close_error() == SQLITE_IOERR);
}

/* ⑩ get の bind 失敗も CFG_ERROR にする */
static void c_bind_fail(void)
{
    CfgDb *db;
    char buf[32];

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 5) == 0);
    CHECK(cfg_set_text(db, "gshell", "t", "hi") == 0);
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_status(db) == CFG_OK);

    inj_bind_fail = 1;
    CHECK(cfg_get_text(db, "gshell", "t", buf, 32) == OS32_ERR_IO);
    CHECK(cfg_status(db) == CFG_ERROR);       /* 未設定と取り違えない */
    CHECK(cfg_last_sqlite(db) != 0);
    /* 一度 ERROR になった接続は以後も「無い」ではなく障害を返す */
    CHECK(cfg_get_info(db, "gshell", "a") == OS32_ERR_IO);
    CHECK(cfg_get_int(db, "gshell", "a", 42) == 42);
    inj_bind_fail = 0;
    CHECK(cfg_close(db) == 0);

    /* 最初の呼び出しが get_int でも status は ERROR になる */
    reset_all();
    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 5) == 0);
    CHECK(cfg_commit(db) == 0);
    inj_bind_fail = 1;
    CHECK(cfg_get_int(db, "gshell", "a", 42) == 42);
    CHECK(cfg_status(db) == CFG_ERROR);
    inj_bind_fail = 0;
    CHECK(cfg_close(db) == 0);
}

/* ⑪ 単一 key の get が列挙の上限に引きずられない */
static void c_get_many(void)
{
    CfgDb *db;
    char key[32];
    int i;

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "user", "a", 77) == 0);
    for (i = 0; i < CFG_ENUM_MAX; i++) {          /* 合計 257 件 */
        sprintf(key, "a%03d", i);
        CHECK(cfg_set_int(db, "user", key, i) == 0);
    }
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);

    /* 列挙は NOSPC でも、単一 key の照会は実値を返す */
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_enum(db, "user", "a", en_cb, NULL) == OS32_ERR_NOSPC);
    CHECK(cfg_get_info(db, "user", "a") == CFG_TYPE_INT);
    CHECK(cfg_get_int(db, "user", "a", 99) == 77);
    CHECK(cfg_close(db) == 0);

    CHECK(ran("get", "user", "a", "99", NULL) == 0);
    CHECK(cap_has("77\n"));
    CHECK(!cap_has("99"));
}

/* ⑫ enum の callback の中からの get は再入拒否 (INVAL) */
static int reenter_get_rc_text;
static int reenter_get_rc_blob;
static int reenter_get_rc_type;
static CfgDb *reenter_db;

static int en_cb_get(const char *key, int type, void *ctx)
{
    static char buf[64];
    static unsigned char bb[64];
    (void)key; (void)type; (void)ctx;
    reenter_get_rc_text = cfg_get_text(reenter_db, "gshell", "t", buf, 64);
    reenter_get_rc_blob = cfg_get_blob(reenter_db, "gshell", "t", bb, 64);
    reenter_get_rc_type = cfg_get_info(reenter_db, "gshell", "t");
    return 0;
}

static void c_enum_reenter_get(void)
{
    CfgDb *db;

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_text(db, "gshell", "t", "hi") == 0);
    CHECK(cfg_commit(db) == 0);
    reenter_db = db;
    CHECK(cfg_enum(db, "gshell", NULL, en_cb_get, NULL) == 1);
    CHECK(reenter_get_rc_text == OS32_ERR_INVAL);
    CHECK(reenter_get_rc_blob == OS32_ERR_INVAL);
    CHECK(reenter_get_rc_type == OS32_ERR_INVAL);
    CHECK(cfg_close(db) == 0);
}

/* ⑬ 出力の取りこぼし (溢れ / short write) を終了コードへ */
static void c_out_fail(void)
{
    static char hex[CFG_BLOB_MAX * 2 + 1];
    int i;

    make_good_db();
    for (i = 0; i < CFG_BLOB_MAX * 2; i++) hex[i] = "0123456789abcdef"[i & 15];
    hex[CFG_BLOB_MAX * 2] = '\0';
    CHECK(ran("set", "gshell", "b", "blob", hex) == 0);

    /* 4096B blob の hex (8192 文字) は 1 行まるごと出せて終了 0 */
    CHECK(ran("get", "gshell", "b", NULL, NULL) == 0);
    CHECK(cap_len >= CFG_BLOB_MAX * 2 + 1);
    CHECK(strstr(cap_out, hex) != NULL);

    /* short write でも書き切る (要求長だけ進めない) */
    inj_short_write = 7;
    CHECK(ran("get", "gshell", "b", NULL, NULL) == 0);
    CHECK(cap_len >= CFG_BLOB_MAX * 2 + 1);
    inj_short_write = 0;

    /* 書き込み自体が失敗したら終了 1 */
    inj_write_fail = 1;
    CHECK(ran("get", "gshell", "b", NULL, NULL) == 1);
    inj_write_fail = 0;
}

/* ⑭ list の scope を切り詰めない */
static void c_long_scope(void)
{
    char s63[CFG_SCOPE_MAX + 2], s64[CFG_SCOPE_MAX + 2];
    int i;

    strcpy(s63, "app:");
    for (i = 4; i < CFG_SCOPE_MAX; i++) s63[i] = 'n';
    s63[CFG_SCOPE_MAX] = '\0';
    strcpy(s64, s63);
    s64[CFG_SCOPE_MAX] = 'n';
    s64[CFG_SCOPE_MAX + 1] = '\0';

    make_good_db();
    CHECK(ran("set", s63, "k", "int", "5") == 0);
    CHECK(ran("list", s63, NULL, NULL, NULL) == 0);
    CHECK(cap_has("\tk\tint\t5"));
    /* 64B の scope は切り詰めて **別 scope** の値を出さない */
    CHECK(ran("list", s64, NULL, NULL, NULL) == 1);
    CHECK(cap_has("bad scope"));
    CHECK(!cap_has("\tk\tint\t5"));
}

/* ⑰ INT_MIN の解析 (純関数) */
static void c_intmin(void)
{
    int v = 0;
    CHECK(parse_int("-2147483648", &v) == 0 && v == -2147483647 - 1);
    CHECK(parse_int("-0", &v) == 0 && v == 0);
    CHECK(parse_int("-1", &v) == 0 && v == -1);
    CHECK(parse_int("-2147483649", &v) == -1);
    /* tsv reader 側も同じ (cfg_init の往復で確かめる) */
    put_file(CFG_TSV_PATH,
             "gshell\tmin\tint\t-2147483648\n"
             "gshell\tmax\tint\t2147483647\n"
             "gshell\tnz\tint\t-0000\n");
    CHECK(cfg_init(NULL) == 0);
    {
        CfgDb *db;
        CHECK(cfg_open(&db, 0) == 0);
        CHECK(cfg_get_int(db, "gshell", "min", 0) == -2147483647 - 1);
        CHECK(cfg_get_int(db, "gshell", "max", 0) == 2147483647);
        CHECK(cfg_get_int(db, "gshell", "nz", 9) == 0);
        CHECK(cfg_close(db) == 0);
    }
}

/* ⑯ 63B の名前が 49 行あっても受理する (控えを持たなくなった) */
static void c_tsv_many(void)
{
    static char big[FIXTURE_BYTES];
    CfgDb *db;
    int n = 0, i, j;

    for (i = 0; i < 60; i++) {
        for (j = 0; j < 4; j++) big[n++] = "app:"[j];
        for (j = 4; j < CFG_SCOPE_MAX; j++) big[n++] = 'n';
        big[n++] = '\t';
        n += sprintf(big + n, "k%02d", i);
        for (j = 3; j < CFG_KEY_MAX; j++) big[n++] = 'z';
        big[n++] = '\t';
        big[n++] = 'i'; big[n++] = 'n'; big[n++] = 't'; big[n++] = '\t';
        n += sprintf(big + n, "%d", i);
        big[n++] = '\n';
    }
    CHECK(fixture_create(NULL, CFG_TSV_PATH, big, (u32)n) == VFS_OK);
    CHECK(cfg_init(NULL) == 0);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_OK);
    CHECK(cfg_close(db) == 0);

    /* 重複は PRIMARY KEY が弾き、生成物は残らない */
    reset_all();
    put_file(CFG_TSV_PATH, "gshell\ta\tint\t1\ngshell\ta\tint\t2\n");
    CHECK(cfg_init(NULL) == OS32_ERR_INVAL);
    CHECK(cfg_last_init_reason() == CFG_INIT_TSV);
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);
    CHECK(fixture_find(CFG_DB_NEW_PATH, 0) == NULL);
}

/* ========================================================================= */
/*  実装レビュー 往復 2 の blocker (s2_tdd.md §C2)                           */
/* ========================================================================= */

/* scope / key / tval を **bind で** 入れる (埋込み NUL や不正 UTF-8 の行は
 * SQL リテラルでは作れない。SQLITE_OMIT_CAST なので CAST も使えない)。 */
static void insert_text_row(const char *scope, int slen, const char *key,
                            int klen, const char *tval, int tlen)
{
    int h = kapi_db_open_existing(CFG_DB_PATH, 1);
    CHECK(h >= 0);
    CHECK(kapi_db_prepare_only(h,
          "INSERT INTO settings VALUES(?,?,1,NULL,?,NULL)") == 0);
    CHECK(kapi_db_bind_text(h, 1, scope, slen) == 0);
    CHECK(kapi_db_bind_text(h, 2, key, klen) == 0);
    CHECK(kapi_db_bind_text(h, 3, tval, tlen) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_close(h) == 0);
}

static void exec_on_db(const char *sql)
{
    int h = kapi_db_open_existing(CFG_DB_PATH, 1);
    CHECK(h >= 0);
    CHECK(kapi_db_exec(h, sql) == 0);
    CHECK(kapi_db_close(h) == 0);
}

/* 1. 整数取得**だけ**が落ちたときに 0 を成功として出さない */
static void c_r2_int_fail(void)
{
    CfgDb *db;
    int v = 0;

    make_good_db();
    CHECK(ran("set", "gshell", "n", "int", "5") == 0);

    /* 型照会 (1 回目の SQL_GET) は通し、値取得 (2 回目) だけを落とす */
    CHECK(cfg_open(&db, 0) == 0);
    inj_prep_fail = "ival";
    inj_prep_skip = 1;
    CHECK(cfg_get_info(db, "gshell", "n") == CFG_TYPE_INT);
    CHECK(cfg_read_int(db, "gshell", "n", &v) == OS32_ERR_IO);
    CHECK(cfg_status(db) == CFG_ERROR);
    inj_prep_fail = NULL;
    CHECK(cfg_close(db) == 0);

    /* list は 0 を出さずに失敗する */
    inj_prep_fail = "ival";
    inj_prep_skip = 1;
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("list failed"));
    CHECK(!cap_has("int\t0"));
    inj_prep_fail = NULL;

    /* export も同じ。偽の 0 を書いたファイルを残さない */
    inj_prep_fail = "ival";
    inj_prep_skip = 1;             /* 型照会 (info) は通し、値取得で落とす */
    CHECK(ran("export", "/i.json", NULL, NULL, NULL) == 1);
    CHECK(fixture_find("/i.json", 0) == NULL);
    inj_prep_fail = NULL;

    /* get も 0 を表示しない */
    inj_prep_fail = "ival";
    inj_prep_skip = 1;
    CHECK(ran("get", "gshell", "n", "9", NULL) == 1);
    CHECK(cap_has("get failed"));
    CHECK(!cap_has("\n0\n"));
    inj_prep_fail = NULL;
}

/* 2. 接続が ERROR になった後の set 拒否も txn を failed にする */
static void c_r2_txn_error(void)
{
    CfgDb *db;

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);       /* A は通る */
    /* 途中の get が落ちて接続が ERROR になる */
    inj_prep_fail = "ival";
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    inj_prep_fail = NULL;
    CHECK(cfg_status(db) == CFG_ERROR);
    /* 次の set は writable_now の前提で断られるが、txn は failed になる */
    CHECK(cfg_set_int(db, "gshell", "b", 2) == OS32_ERR_INVAL);
    CHECK(db->txn == 2);
    CHECK(cfg_commit(db) == OS32_ERR_IO);
    CHECK(cfg_close(db) == 0);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);     /* A も残らない */
    CHECK(cfg_close(db) == 0);

    /* set を挟まずに commit しても、ERROR 状態なら拒否する */
    reset_all();
    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);
    inj_prep_fail = "ival";
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    inj_prep_fail = NULL;
    CHECK(cfg_commit(db) == OS32_ERR_IO);
    CHECK(cfg_close(db) == 0);
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    CHECK(cfg_close(db) == 0);
}

/* 3. close の失敗が、保存済みの操作診断を上書きしない */
static void c_r2_close_diag(void)
{
    CfgDb *db;
    int op_code;

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    inj_prep_fail = "INSERT OR REPLACE";
    CHECK(cfg_set_int(db, "gshell", "a", 1) == OS32_ERR_IO);
    inj_prep_fail = NULL;
    op_code = cfg_last_sqlite(db);
    CHECK(CFG_SQLITE_PRIMARY(op_code) == CFG_SQLITE_ERROR);

    inj_close_fail = SQLITE_IOERR;
    CHECK(cfg_close(db) == OS32_ERR_IO);
    CHECK(cfg_last_close_error() != 0);
    CHECK(cfg_last_sqlite(db) == op_code);     /* 元の原因が残っている */
    inj_close_fail = 0;

    /* drop_handle (open の途中で捨てる接続) も同じ */
    reset_all();
    {
        static const char *notab[] = { "CREATE TABLE t(x)", NULL };
        raw_db(CFG_DB_PATH, notab);           /* meta 表が無い = CORRUPT */
    }
    inj_close_fail = SQLITE_IOERR;
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_CORRUPT);
    op_code = cfg_last_sqlite(db);
    CHECK(CFG_SQLITE_PRIMARY(op_code) == CFG_SQLITE_ERROR);   /* no such table */
    CHECK(cfg_close(db) == OS32_ERR_IO);
    CHECK(cfg_last_close_error() != 0);
    CHECK(cfg_last_sqlite(db) == op_code);     /* 元の原因が残っている */
}

/* 4. NULL の export が宣言型を保つ */
static void c_r2_null_type(void)
{
    static const char *rows[] = {
        "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
        "CREATE TABLE settings (scope TEXT NOT NULL, key TEXT NOT NULL,"
        " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
        " PRIMARY KEY (scope, key)) WITHOUT ROWID",
        "INSERT INTO meta VALUES (1, '0')",
        "INSERT INTO settings VALUES ('gshell','i',0,NULL,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','t',1,NULL,NULL,NULL)",
        "INSERT INTO settings VALUES ('gshell','b',2,NULL,NULL,NULL)",
        NULL
    };
    FixtureFile *f;

    raw_db(CFG_DB_PATH, rows);
    CHECK(ran("export", "/t.json", NULL, NULL, NULL) == 0);
    f = fixture_find("/t.json", 0);
    CHECK(f != NULL);
    f->data[f->size] = '\0';
    CHECK(strstr((char *)f->data, "\"key\":\"b\",\"type\":2,\"v\":null"));
    CHECK(strstr((char *)f->data, "\"key\":\"i\",\"type\":0,\"v\":null"));
    CHECK(strstr((char *)f->data, "\"key\":\"t\",\"type\":1,\"v\":null"));
    CHECK(!strstr((char *)f->data, "\"type\":3"));
    /* list も宣言型を出す */
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 0);
    CHECK(cap_has("gshell\ti\tint\t(unset)"));
    CHECK(cap_has("gshell\tt\ttext\t(unset)"));
    CHECK(cap_has("gshell\tb\tblob\t(unset)"));
}

/* 5. 64bit 整数 / 未知 type が SHM で 32bit 化された後に正当値にならない */
static void c_r2_wide(void)
{
    CfgDb *db;
    int v = 0;

    make_good_db();
    /* ival = 2^32+1 は SHM で 1 に化ける */
    exec_on_db("INSERT INTO settings VALUES('gshell','big',0,4294967297,NULL,NULL)");
    /* type = 2^32 は SHM で 0 (int) に化ける */
    exec_on_db("INSERT INTO settings VALUES('gshell','t4',4294967296,7,NULL,NULL)");
    /* 下限・上限の外側 */
    exec_on_db("INSERT INTO settings VALUES('gshell','lo',0,-2147483649,NULL,NULL)");
    exec_on_db("INSERT INTO settings VALUES('gshell','hi',0,2147483648,NULL,NULL)");
    /* 境界そのものは通る */
    exec_on_db("INSERT INTO settings VALUES('gshell','ok',0,-2147483648,NULL,NULL)");
    /* 宣言と違う型の値 (tval TEXT の列に blob が入っている) */
    exec_on_db("INSERT INTO settings VALUES('gshell','mix',1,NULL,x'01',NULL)");
    /* 宣言型の値だけが NULL なら「未設定」扱い (他の列の残骸は見ない) */
    exec_on_db("INSERT INTO settings VALUES('gshell','tn',1,NULL,NULL,x'01')");

    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_info(db, "gshell", "big") == OS32_ERR_NOSYS);
    CHECK(cfg_read_int(db, "gshell", "big", &v) == OS32_ERR_NOSYS);
    CHECK(cfg_get_int(db, "gshell", "big", 99) == 99);     /* 1 を返さない */
    CHECK(cfg_get_info(db, "gshell", "t4") == OS32_ERR_NOSYS);
    CHECK(cfg_get_int(db, "gshell", "t4", 99) == 99);
    CHECK(cfg_get_info(db, "gshell", "lo") == OS32_ERR_NOSYS);
    CHECK(cfg_get_info(db, "gshell", "hi") == OS32_ERR_NOSYS);
    CHECK(cfg_get_info(db, "gshell", "ok") == CFG_TYPE_INT);
    CHECK(cfg_get_int(db, "gshell", "ok", 99) == -2147483647 - 1);
    CHECK(cfg_get_info(db, "gshell", "mix") == OS32_ERR_NOSYS);
    CHECK(cfg_get_info(db, "gshell", "tn") == (CFG_TYPE_TEXT | CFG_INFO_NULL));
    /* 契約外の行を読んでも接続は壊れない (他の key は読める) */
    CHECK(cfg_status(db) == CFG_OK);
    CHECK(cfg_close(db) == 0);

    /* list / export は黙って飛ばさず失敗する */
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("list failed"));
    CHECK(ran("export", "/w.json", NULL, NULL, NULL) == 1);
    CHECK(fixture_find("/w.json", 0) == NULL);
}

/* 6. 保存済みの text / blob の境界と、埋込み NUL の key */
static void c_r2_badval(void)
{
    CfgDb *db;
    char buf[512];
    unsigned char bb[CFG_BLOB_MAX + 8];
    int n;

    make_good_db();
    /* 256B の text (hex(zeroblob(128)) = '0' が 256 文字) */
    exec_on_db("INSERT INTO settings VALUES('gshell','t256',1,NULL,"
               "hex(zeroblob(128)),NULL)");
    /* 4097B の blob */
    exec_on_db("INSERT INTO settings VALUES('gshell','b4097',2,NULL,NULL,"
               "zeroblob(4097))");
    /* 不正 UTF-8 と埋込み NUL の text (bind で作る) */
    insert_text_row("gshell", 6, "bad", 3, "\xff", 1);
    insert_text_row("gshell", 6, "nul", 3, "a\0b", 3);
    /* 境界ちょうどは通る */
    exec_on_db("INSERT INTO settings VALUES('gshell','t255',1,NULL,"
               "substr(hex(zeroblob(128)),1,255),NULL)");
    exec_on_db("INSERT INTO settings VALUES('gshell','b4096',2,NULL,NULL,"
               "zeroblob(4096))");

    CHECK(cfg_open(&db, 0) == 0);
    memset(buf, 'Z', sizeof(buf));
    CHECK(cfg_get_text(db, "gshell", "t256", buf, 512) == OS32_ERR_NOSYS);
    CHECK(buf[0] == 'Z');                       /* out を触らない */
    CHECK(cfg_get_text(db, "gshell", "bad", buf, 512) == OS32_ERR_NOSYS);
    CHECK(buf[0] == 'Z');
    CHECK(cfg_get_text(db, "gshell", "nul", buf, 512) == OS32_ERR_NOSYS);
    CHECK(buf[0] == 'Z');
    memset(bb, 'Z', sizeof(bb));
    CHECK(cfg_get_blob(db, "gshell", "b4097", bb, (int)sizeof(bb)) == OS32_ERR_NOSYS);
    CHECK(bb[0] == 'Z');
    /* 境界ちょうどは読める */
    n = cfg_get_text(db, "gshell", "t255", buf, 512);
    CHECK(n == CFG_TEXT_MAX);
    CHECK(cfg_get_blob(db, "gshell", "b4096", bb, (int)sizeof(bb)) == CFG_BLOB_MAX);
    CHECK(cfg_status(db) == CFG_OK);
    CHECK(cfg_close(db) == 0);

    /* 埋込み NUL の key は列挙で別名に化かさず、障害にする */
    reset_all();
    make_good_db();
    insert_text_row("gshell", 6, "a", 1, "x", 1);
    insert_text_row("gshell", 6, "a\0b", 3, "y", 1);
    CHECK(cfg_open(&db, 0) == 0);
    en_n = 0;
    CHECK(cfg_enum(db, "gshell", NULL, en_cb, NULL) == OS32_ERR_IO);
    CHECK(cfg_close(db) == 0);
    /* list は 1 行黙って落とすのではなく失敗する */
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("list failed"));
}

/* 7. export 先の同一性検査が stat 障害を「別ファイル」にしない */
static void c_r2_alias(void)
{
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);

    /* 出力先の stat だけが IOERR。open は成功する状況 */
    stat_fail_exact = "/etc/alias.db";
    CHECK(ran("export", "/etc/alias.db", NULL, NULL, NULL) == 1);
    CHECK(cap_has("refusing to write"));
    CHECK(fixture_find("/etc/alias.db", 0) == NULL);   /* 開いてもいない */
    stat_fail_exact = NULL;

    /* 障害が無ければ普通に書ける */
    CHECK(ran("export", "/etc/alias.db", NULL, NULL, NULL) == 0);
    CHECK(fixture_find("/etc/alias.db", 0) != NULL);
}

/* ========================================================================= */
/*  実装レビュー 往復 3 の blocker (s2_tdd.md §C3)                           */
/* ========================================================================= */

/* B1: inode を供給しない FS (FAT) では同一性を判定できない → 書かない */
static void c_r3_fat(void)
{
    u32 before;

    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);
    before = fixture_find(CFG_DB_PATH, 0)->size;

    /* 大文字の別名 (FAT は大小を区別しない) は名前だけで断る */
    CHECK(ran("export", "/ETC/SETTINGS.DB", NULL, NULL, NULL) == 1);
    CHECK(cap_has("refusing to write"));
    CHECK(fixture_find("/ETC/SETTINGS.DB", 0) == NULL);
    CHECK(fixture_find(CFG_DB_PATH, 0)->size == before);
    CHECK(ran("export", "/Etc/Settings.DB-Journal", NULL, NULL, NULL) == 1);

    /* inode が無い FS では、既存ファイルへの上書きは判定不能として断る */
    put_file("/out.json", "old");
    inj_no_ino = 1;
    CHECK(ran("export", "/out.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("refusing to write"));
    CHECK(fixture_find("/out.json", 0)->size == 3);   /* 触っていない */
    /* 新しい名前 (まだ無い) は NOTFOUND で「別物」と分かるので書ける */
    CHECK(ran("export", "/new.json", NULL, NULL, NULL) == 0);
    CHECK(fixture_find("/new.json", 0) != NULL);
    inj_no_ino = 0;

    /* inode がある FS なら既存ファイルにも書ける */
    CHECK(ran("export", "/out.json", NULL, NULL, NULL) == 0);
    CHECK(fixture_find("/out.json", 0)->size > 3);
}

/* B2: 列挙 callback からの set/delete 拒否も txn を failed にする */
static CfgDb *ew_db;
static int ew_set_rc;
static int ew_del_rc;

static int ew_cb(const char *key, int type, void *ctx)
{
    (void)key; (void)type; (void)ctx;
    ew_set_rc = cfg_set_int(ew_db, "gshell", "b", 2);
    ew_del_rc = cfg_delete(ew_db, "gshell", "a");
    return 0;
}

static void c_r3_enum_write(void)
{
    CfgDb *db;

    make_good_db();
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_int(db, "gshell", "a", 1) == 0);       /* A は通る */
    ew_db = db;
    CHECK(cfg_enum(db, "gshell", NULL, ew_cb, NULL) == 1);
    CHECK(ew_set_rc == OS32_ERR_INVAL);                  /* 再入は拒否 */
    CHECK(ew_del_rc == OS32_ERR_INVAL);
    CHECK(db->txn == 2);                                 /* failed になった */
    CHECK(cfg_commit(db) == OS32_ERR_IO);
    CHECK(cfg_close(db) == 0);
    /* A も残らない */
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    CHECK(cfg_close(db) == 0);
}

/* B3: 縮小前に type の値域を見る (列挙も cfg_get_info と同じ判定) */
static int seen_type;
static int type_cb(const char *key, int type, void *ctx)
{
    (void)key; (void)ctx;
    seen_type = type;
    return 0;
}

static void c_r3_enum_type(void)
{
    CfgDb *db;

    make_good_db();
    /* type = 2^32 は SHM で 0 (int) に化ける */
    exec_on_db("INSERT INTO settings VALUES('gshell','t4',4294967296,7,NULL,NULL)");
    seen_type = -99;
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_enum(db, "gshell", NULL, type_cb, NULL) == OS32_ERR_NOSYS);
    CHECK(seen_type == -99);                     /* callback へ渡っていない */
    CHECK(cfg_get_info(db, "gshell", "t4") == OS32_ERR_NOSYS);  /* 判定が一致 */
    CHECK(cfg_close(db) == 0);

    /* 0/1/2 は通る */
    reset_all();
    make_good_db();
    exec_on_db("INSERT INTO settings VALUES('gshell','k',2,NULL,NULL,x'01')");
    seen_type = -99;
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_enum(db, "gshell", NULL, type_cb, NULL) == 1);
    CHECK(seen_type == CFG_TYPE_BLOB);
    CHECK(cfg_close(db) == 0);

    /* list / export は黙って飛ばさず失敗する */
    reset_all();
    make_good_db();
    exec_on_db("INSERT INTO settings VALUES('gshell','t4',4294967296,7,NULL,NULL)");
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("list failed"));
}

/* B4: ERROR 状態の列挙を「0 件」にしない */
static void c_r3_enum_err(void)
{
    CfgDb *db;

    make_good_db();
    CHECK(ran("set", "gshell", "a", "int", "1") == 0);

    /* get を落として接続を ERROR にしてから列挙する */
    CHECK(cfg_open(&db, 0) == 0);
    inj_prep_fail = "ival";
    CHECK(cfg_get_int(db, "gshell", "a", -1) == -1);
    inj_prep_fail = NULL;
    CHECK(cfg_status(db) == CFG_ERROR);
    en_n = 0;
    CHECK(cfg_enum(db, "gshell", NULL, en_cb, NULL) == OS32_ERR_IO);
    CHECK(en_n == 0);
    en_scope_n = 0;
    CHECK(cfg_enum_scopes(db, en_scope_cb, NULL) == OS32_ERR_IO);
    CHECK(en_scope_n == 0);
    CHECK(cfg_close(db) == 0);

    /* MISSING は従来どおり 0 件 (fallback) */
    reset_all();
    CHECK(cfg_open(&db, 0) == 0);
    CHECK(cfg_status(db) == CFG_MISSING);
    CHECK(cfg_enum(db, "gshell", NULL, en_cb, NULL) == 0);
    CHECK(cfg_enum_scopes(db, en_scope_cb, NULL) == 0);
    CHECK(cfg_close(db) == 0);

    /* 列挙の bind 失敗も CFG_ERROR にする */
    reset_all();
    make_good_db();
    CHECK(cfg_open(&db, 0) == 0);
    inj_bind_fail = 1;
    CHECK(cfg_enum(db, "gshell", NULL, en_cb, NULL) == OS32_ERR_IO);
    CHECK(cfg_status(db) == CFG_ERROR);
    inj_bind_fail = 0;
    CHECK(cfg_close(db) == 0);
}

/* B5: 過長フィールドでカウンタを飽和させる (int を溢れさせない) */
#define R3_LONG 2000000

static int long_pos;
static int long_bad_at;           /* >0 ならその位置に不正 UTF-8 を混ぜる */

static int long_get(void *ctx)
{
    (void)ctx;
    long_pos++;
    if (long_bad_at > 0 && long_pos == long_bad_at) return 0xFF;
    if (long_pos <= R3_LONG) return 'a';
    if (long_pos == R3_LONG + 1) return '\t';
    if (long_pos == R3_LONG + 2) return 'k';
    if (long_pos == R3_LONG + 3) return '\t';
    if (long_pos == R3_LONG + 4) return 'i';
    if (long_pos == R3_LONG + 5) return 'n';
    if (long_pos == R3_LONG + 6) return 't';
    if (long_pos == R3_LONG + 7) return '\t';
    if (long_pos == R3_LONG + 8) return '1';
    if (long_pos == R3_LONG + 9) return '\n';
    return -1;
}

static void c_r3_tsv_long(void)
{
    static CfgTsvRow row;
    CfgTsvErr err;

    /* 200 万バイトの scope 列。飽和していないと `n` が伸び続け、
     * 2GB 級の入力で `buf[n]` が負の添字になる (ASan でも踏めない領域)。 */
    long_pos = 0;
    long_bad_at = 0;
    CHECK(cfg_tsv_parse(long_get, NULL, &row,
                        (int (*)(const CfgTsvRow *, void *))0, NULL, &err) == -1);
    CHECK(err.code == CFG_TSV_E_SCOPE);
    CHECK(err.lineno == 1);
    /* 区切りまで読み切っている (途中で投げ出していない) */
    CHECK(long_pos >= R3_LONG + 1);

    /* カウンタが **飽和** している = 数え続けていない。ここが伸び続けると
     * 2GB 級の入力で `int` が溢れ `buf[n]` が負の添字になる (B5)。
     * 溢れそのものはホストでは踏めないので、飽和の方を固定する。 */
    {
        TsvIn in;
        char buf[CFG_SCOPE_MAX + 1];
        int len = -1, over = 0, nul = 0, c;
        long_pos = 0;
        long_bad_at = 0;
        in_init(&in, long_get, NULL);
        c = read_field(&in, buf, (int)sizeof(buf), &len, &over, &nul);
        CHECK(c == '\t');
        CHECK(over == 1);
        CHECK(len == CFG_SCOPE_MAX);            /* 飽和値 (cap - 1) */
        CHECK(long_pos >= R3_LONG + 1);         /* 区切りまでは読んでいる */
    }

    /* 超過の後ろでも UTF-8 の検査は続く */
    long_pos = 0;
    long_bad_at = R3_LONG - 1;
    CHECK(cfg_tsv_parse(long_get, NULL, &row,
                        (int (*)(const CfgTsvRow *, void *))0, NULL, &err) == -1);
    CHECK(err.code == CFG_TSV_E_UTF8);

    /* 過長の text 列も同じ (値の列でも飽和する) */
    {
        static char big[8192];
        TsvFile f;
        int n = 0, i;
        n += sprintf(big + n, "gshell\tk\ttext\t");
        for (i = 0; i < 4000; i++) big[n++] = 'x';
        big[n++] = '\n';
        CHECK(fixture_create(NULL, CFG_TSV_PATH, big, (u32)n) == VFS_OK);
        CHECK(tsv_open(&f, CFG_TSV_PATH) >= 0);
        CHECK(cfg_tsv_parse(tsv_getc, &f, &row,
                            (int (*)(const CfgTsvRow *, void *))0, NULL, &err) == -1);
        tsv_shut(&f);
        CHECK(err.code == CFG_TSV_E_VALUE);
    }
}


/* ========================================================================= */
/*  S5-C (1) cfg_bench — 集計・引数・整形の純関数                             */
/* ========================================================================= */
static void c_s5_pure(void)
{
    BnArgs a;
    BnStats st;
    char buf[BN_SUM_MAX];
    char small[8];
    int v;

    /* ---- 引数 ---- */
    {
        char *av0[] = { "cfg_bench" };
        char *av1[] = { "cfg_bench", "7" };
        char *av2[] = { "cfg_bench", "7", "3" };
        char *aw0[] = { "cfg_bench", "-w" };
        char *aw1[] = { "cfg_bench", "-w", "5" };
        char *bad1[] = { "cfg_bench", "0" };
        char *bad2[] = { "cfg_bench", "-x" };
        char *bad3[] = { "cfg_bench", "-" };
        char *bad4[] = { "cfg_bench", "7", "3", "9" };
        char *bad5[] = { "cfg_bench", "1x" };
        char *bad6[] = { "cfg_bench", "" };
        char *bad7[] = { "cfg_bench", "-w", "5", "5" };
        char *bad8[] = { "cfg_bench", "-w", "0" };
        char *bad9[] = { "cfg_bench", "7", "0" };

        CHECK(bn_args(1, av0, &a) == 0 && !a.write_mode && a.n == 50 && a.m == 20);
        CHECK(bn_args(2, av1, &a) == 0 && !a.write_mode && a.n == 7 && a.m == 20);
        CHECK(bn_args(3, av2, &a) == 0 && a.n == 7 && a.m == 3);
        CHECK(bn_args(2, aw0, &a) == 0 && a.write_mode && a.n == 20);
        CHECK(bn_args(3, aw1, &a) == 0 && a.write_mode && a.n == 5);
        CHECK(bn_args(2, bad1, &a) == -1);
        CHECK(bn_args(2, bad2, &a) == -1);
        CHECK(bn_args(2, bad3, &a) == -1);
        CHECK(bn_args(4, bad4, &a) == -1);
        CHECK(bn_args(2, bad5, &a) == -1);
        CHECK(bn_args(2, bad6, &a) == -1);
        CHECK(bn_args(4, bad7, &a) == -1);
        CHECK(bn_args(3, bad8, &a) == -1);
        CHECK(bn_args(3, bad9, &a) == -1);
    }
    /* 上限を越えた桁でも int の桁あふれを踏まない */
    CHECK(bn_parse_u32("4294967295", 1, BN_MAX_N, &v) == -1);
    CHECK(bn_parse_u32("99999999999999999999", 1, BN_MAX_N, &v) == -1);
    CHECK(bn_parse_u32("0000012", 1, BN_MAX_N, &v) == 0 && v == 12);
    CHECK(bn_parse_u32("10000", 1, BN_MAX_N, &v) == 0 && v == 10000);
    CHECK(bn_parse_u32("10001", 1, BN_MAX_N, &v) == -1);
    CHECK(bn_parse_u32("+1", 1, BN_MAX_N, &v) == -1);
    CHECK(bn_parse_u32("-1", 1, BN_MAX_N, &v) == -1);
    /* **受け付ける量が集計の幅に収まる** こと (レビュー往復 1 の B3)。
     * 1 回で数えうる失敗は open 1 + get m + 状態 1 + begin/set/commit 3 +
     * close 1 = m + 6 なので、総数の上限は n * (m + 6)。 */
    CHECK(BN_MAX_N == 10000 && BN_MAX_M == 1000);
    CHECK((double)BN_MAX_N * (BN_MAX_M + 6) < 4294967295.0);
    {
        char *big1[] = { "cfg_bench", "10001", "1000" };
        char *big2[] = { "cfg_bench", "10000", "1001" };
        char *big3[] = { "cfg_bench", "-w", "10001" };
        CHECK(bn_args(3, big1, &a) == -1);
        CHECK(bn_args(3, big2, &a) == -1);
        CHECK(bn_args(3, big3, &a) == -1);
    }

    /* ---- 引く先の巡回: 4 件に 1 件が「無いキー」、残りは 3 キーの巡回 ---- */
    {
        static const int want[12] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };
        int j, miss = 0, seen[BN_KEYS];
        for (j = 0; j < BN_KEYS; j++) seen[j] = 0;
        for (j = 0; j < 12; j++) CHECK(bn_pick(j) == want[j]);
        for (j = 0; j < 20; j++) {
            int w = bn_pick(j);
            CHECK(w >= 0 && w <= BN_PICK_MISS);
            if (w == BN_PICK_MISS) miss++; else seen[w]++;
        }
        CHECK(miss == 5);                      /* 20 件のうち 4 件に 1 件 */
        CHECK(seen[0] == 5 && seen[1] == 5 && seen[2] == 5);
    }
    CHECK(!strcmp(bn_key_of(BN_PICK_COLOR), "desktop/color"));
    CHECK(!strcmp(bn_key_of(BN_PICK_WALL), "desktop/wallpaper"));
    CHECK(!strcmp(bn_key_of(BN_PICK_CLOCK), "taskbar/clock_24h"));
    CHECK(!strcmp(bn_key_of(BN_PICK_MISS), "nosuch/key"));

    /* ---- 失敗の数え方: NOTFOUND は数えない ---- */
    CHECK(bn_get_failed(0) == 0);
    CHECK(bn_get_failed(5) == 0);                       /* text の長さ */
    CHECK(bn_get_failed(OS32_ERR_NOTFOUND) == 0);
    CHECK(bn_get_failed(OS32_ERR_IO) == 1);
    CHECK(bn_get_failed(OS32_ERR_NOSYS) == 1);
    CHECK(bn_get_failed(OS32_ERR_INVAL) == 1);

    /* ---- 何回目に 1 行出すか ---- */
    CHECK(bn_should_log(0, 50) == 0);
    CHECK(bn_should_log(8, 50) == 0);
    CHECK(bn_should_log(9, 50) == 1);                   /* 10 回目 */
    CHECK(bn_should_log(19, 50) == 1);
    CHECK(bn_should_log(49, 50) == 1);                  /* 最終回 */
    CHECK(bn_should_log(2, 3) == 1);                    /* n < 10 でも 1 行 */
    CHECK(bn_should_log(1, 3) == 0);

    /* ---- 集計 ---- */
    bn_reset(&st, 1000);
    CHECK(st.n == 0 && st.pool_start == 1000 && st.pool_peak == 1000 &&
          st.pool_end == 1000 && st.failures == 0);
    CHECK(bn_avg(&st) == 0);
    bn_sample(&st, 5, 4000, 1000);
    CHECK(st.t_min == 5 && st.t_max == 5 && bn_avg(&st) == 5);
    bn_sample(&st, 2, 9000, 1200);
    bn_sample(&st, 9, 3000, 1000);
    CHECK(st.n == 3);
    CHECK(st.t_min == 2 && st.t_max == 9 && st.t_sum == 16);
    CHECK(bn_avg(&st) == 5);                            /* 16/3 = 5.33 → 5 */
    CHECK(st.pool_peak == 9000);                        /* open 中の最大 */
    CHECK(st.pool_end == 1000);                         /* 最後の close 後 */
    /* close 後の方が大きい回があればピークはそちらを採る */
    bn_sample(&st, 1, 100, 12000);
    CHECK(st.pool_peak == 12000 && st.pool_end == 12000);
    /* 四捨五入 (切り捨てではない)。商と余りで丸めるので途中で溢れない */
    bn_reset(&st, 0);
    bn_sample(&st, 1, 0, 0);
    bn_sample(&st, 2, 0, 0);
    CHECK(st.t_sum == 3 && bn_avg(&st) == 2);           /* 1.5 → 2 */
    bn_reset(&st, 0);
    bn_sample(&st, 4294967295u, 0, 0);                  /* 平均の丸めで溢れない */
    CHECK(st.t_sum == 4294967295u && bn_avg(&st) == 4294967295u);
    CHECK(!st.saturated && bn_bad(&st) == 0);
    /* `(t_sum + n/2) / n` だと途中で巻き戻って 0 になる組み合わせ */
    bn_reset(&st, 0);
    st.t_sum = 4294967295u;
    st.n = 2;
    CHECK(bn_avg(&st) == 2147483648u);                  /* 2147483647.5 → 上へ */

    /* ---- 飽和 (レビュー往復 1 の B3) ---- */
    {
        int sat = 0;
        CHECK(bn_add_sat(1, 2, &sat) == 3 && sat == 0);
        CHECK(bn_add_sat(4294967294u, 1, &sat) == 4294967295u && sat == 0);
        CHECK(bn_add_sat(4294967295u, 1, &sat) == 4294967295u && sat == 1);
        sat = 0;
        CHECK(bn_add_sat(4000000000u, 400000000u, &sat) == 4294967295u &&
              sat == 1);
    }
    /* tick の合計が溢れたら「飽和した」印が立ち、失敗扱いになる */
    bn_reset(&st, 0);
    bn_sample(&st, 4294967295u, 0, 0);
    CHECK(!st.saturated);
    bn_sample(&st, 1, 0, 0);
    CHECK(st.saturated && st.t_sum == 4294967295u);
    CHECK(bn_bad(&st) == 1);                            /* 失敗 0 でも 1 */
    CHECK(bn_summary(buf, (int)sizeof(buf), &st, CFG_OK) > 0);
    CHECK(strstr(buf, "saturated 1\n") != NULL);
    /* 失敗数も飽和する (戻り値を int へ縮めない) */
    bn_reset(&st, 0);
    st.failures = 4294967295u;
    st.failures = bn_add_sat(st.failures, 1, &st.saturated);
    CHECK(st.failures == 4294967295u && st.saturated && bn_bad(&st) == 1);
    /* 飽和していなければ普段の書式のまま (saturated 行は出ない) */
    bn_reset(&st, 0);
    bn_sample(&st, 1, 0, 0);
    CHECK(bn_summary(buf, (int)sizeof(buf), &st, CFG_OK) > 0);
    CHECK(strstr(buf, "saturated") == NULL);
    CHECK(bn_bad(&st) == 0);

    /* ---- 整形 ---- */
    CHECK(bn_iter_line(buf, (int)sizeof(buf), 10, 3, 123456) > 0);
    CHECK(!strcmp(buf, "iter 10 ticks 3 pool 123456 B\n"));
    CHECK(bn_iter_line(small, (int)sizeof(small), 10, 3, 123456) == -1);

    bn_reset(&st, 4096);
    bn_sample(&st, 2, 200704, 4096);
    bn_sample(&st, 9, 200704, 4096);
    st.failures = 0;
    CHECK(bn_summary(buf, (int)sizeof(buf), &st, CFG_OK) > 0);
    CHECK(!strcmp(buf,
                  "ticks min 2 max 9 avg 6 total 11 n 2\n"
                  "pool start 4096 peak 200704 end 4096 B\n"
                  "status 0 OK\n"
                  "failures 0\n"));
    st.failures = 3;
    CHECK(bn_summary(buf, (int)sizeof(buf), &st, CFG_CORRUPT) > 0);
    CHECK(strstr(buf, "status 2 CORRUPT\n") != NULL);
    CHECK(strstr(buf, "failures 3\n") != NULL);
    CHECK(bn_summary(small, (int)sizeof(small), &st, CFG_OK) == -1);
    CHECK(!strcmp(bn_status_name(CFG_MISSING), "MISSING"));
    CHECK(!strcmp(bn_status_name(CFG_VERSION), "VERSION"));
    CHECK(!strcmp(bn_status_name(CFG_ERROR), "ERROR"));
    /* u32 の上端が負に化けない。数字だけが溢れる幅でも 1 バイトも書かない
     * (bo_str の検査に隠れない大きさをわざと選ぶ)。 */
    {
        BnOut o;
        bo_init(&o, buf, (int)sizeof(buf));
        bo_u32(&o, 4294967295u);
        CHECK(bo_end(&o) == 10 && !strcmp(buf, "4294967295"));

        memset(small, 0x7F, sizeof(small));
        bo_init(&o, small, 4);
        bo_u32(&o, 12345);                       /* 5 桁 + NUL > 4 */
        CHECK(bo_end(&o) == -1);
        CHECK(small[4] == 0x7F && small[5] == 0x7F);  /* 越えて書いていない */
        bo_init(&o, small, 4);
        bo_u32(&o, 123);                         /* 3 桁 + NUL = 4 は入る */
        CHECK(bo_end(&o) == 3 && !strcmp(small, "123"));
    }
}

/* ========================================================================= */
/*  S5-C (1) cfg_bench — 通し (実 DB の上で。接続保持中は yield しない)     */
/* ========================================================================= */
static int benched(const char *a1, const char *a2)
{
    char *av[3];
    int n = 1;
    av[0] = (char *)"cfg_bench";
    if (a1) av[n++] = (char *)a1;
    if (a2) av[n++] = (char *)a2;
    cap_len = 0;
    cap_yields = 0;
    cap_yields_open = 0;
    memset(&g_db, 0, sizeof(g_db));
    return bench_main(n, av, &host_api);
}

/* cap_out の中の "<key>" に続く 10 進数。無ければ -1。 */
static long cap_u32_after(const char *key)
{
    const char *p;
    long v = 0;
    cap_out[cap_len] = '\0';
    p = strstr(cap_out, key);
    if (!p) return -1;
    p += strlen(key);
    if (*p < '0' || *p > '9') return -1;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    return v;
}

static void c_s5_bench(void)
{
    long start, peak, end;

    /* ---- DB が無いとき: MISSING を数え、終了コードは 1 ---- */
    host_tick_step = 1;
    CHECK(benched("2", "4") == 1);
    CHECK(cap_has("cfg_bench read n 2 m 4\n"));
    CHECK(cap_has("status 1 MISSING\n"));
    CHECK(cap_u32_after("failures ") == 2);   /* 1 回につき status != OK が 1 */
    CHECK(cap_yields_open == 0);

    /* ---- 実 DB の上を 3 回 x 8 件 ---- */
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);
    host_tick_step = 1;
    CHECK(benched("3", "8") == 0);
    CHECK(cap_has("cfg_bench read n 3 m 8\n"));
    CHECK(cap_has("status 0 OK\n"));
    CHECK(cap_has("failures 0\n"));
    CHECK(cap_has("iter 3 ticks "));          /* 最終回は必ず 1 行出る */
    CHECK(!cap_has("iter 1 ticks "));         /* 10 回に満たない回は出さない */
    CHECK(!cap_has("saturated"));             /* 集計は溢れていない */
    /* **接続を持っている間は yield しない** (票 §7 の直列化の契約)。
     * 出力は close の後なので、そこでの yield は契約に触れない
     * (レビュー往復 1 の B4 で「総数 0」からこの形に変えた)。 */
    CHECK(cap_yields_open == 0);
    /* 1KB に満たない出力なので、yield は最後の吐き切り 1 回だけ */
    CHECK(cap_yields == 1);
    /* tick は毎回進む (get_tick を窓の前後で 1 回ずつ = 差は 1 以上) */
    CHECK(cap_u32_after("ticks min ") >= 1);
    /* プールは開いている間に膨らみ、閉じたら戻る (受入 M1) */
    start = cap_u32_after("pool start ");
    peak = cap_u32_after(" peak ");
    end = cap_u32_after(" end ");
    CHECK(start >= 0 && peak >= 0 && end >= 0);
    CHECK(peak > start);
    CHECK(end == start);

    /* ---- 10 回以上なら 10 回ごとの行が出る ---- */
    host_tick_step = 2;
    CHECK(benched("11", "4") == 0);
    CHECK(cap_has("iter 10 ticks "));
    CHECK(cap_has("iter 11 ticks "));
    CHECK(!cap_has("iter 9 ticks "));
    CHECK(cap_u32_after("ticks min ") >= 2);
    CHECK(cap_yields_open == 0);

    /* ---- GUI 端末 (con_sink 8KB、満杯で古い行を捨てる) 向けの間 (B4) ----
     * 1KB を越える出力では途中でも yield が入る。入らないと端末アプリが
     * 読み出す前に iter 行が押し出される。 */
    host_tick_step = 1;
    CHECK(benched("400", "1") == 0);
    CHECK(cap_len > 1024);
    CHECK(cap_yields >= 2);                   /* 途中 1 回以上 + 最後の 1 回 */
    CHECK(cap_yields_open == 0);              /* ただし接続保持中は 0 のまま */

    /* ---- 書き ---- */
    host_tick_step = 1;
    CHECK(benched("-w", "2") == 0);
    CHECK(cap_has("cfg_bench write n 2\n"));
    CHECK(cap_has("status 0 OK\n"));
    CHECK(cap_has("failures 0\n"));
    CHECK(cap_has("iter 2 ticks "));
    CHECK(cap_yields_open == 0);
    CHECK(cap_u32_after(" end ") == cap_u32_after("pool start "));
    CHECK(cap_u32_after(" peak ") > cap_u32_after("pool start "));
    {   /* 最後の i (= n-1 = 1) が書き戻っている */
        CfgDb *db;
        CHECK(cfg_open(&db, 0) == 0);
        CHECK(cfg_get_int(db, "app:bench", "counter", -1) == 1);
        CHECK(cfg_close(db) == 0);
    }

    /* ---- 引数が不正なら usage と終了コード 1 ---- */
    CHECK(benched("-x", NULL) == 1);
    CHECK(cap_has("usage: cfg_bench"));
    CHECK(cap_yields_open == 0);

    /* ---- 出力が書けなければ終了コード 1 ---- */
    inj_write_fail = 1;
    CHECK(benched("1", "2") == 1);
    inj_write_fail = 0;

    /* ---- 取った整数が INT_MAX でも桁あふれしない (B1) ----
     * `bn_sink += iv` だと 2 件目で INT_MAX + INT_MAX の signed overflow
     * (未定義動作)。符号なしの XOR に畳んであることを、値を実際に
     * INT_MAX にしてから踏んで確かめる (--sanitize で UBSan が見る)。 */
    CHECK(ran("set", "gshell", "desktop/color", "int", "2147483647") == 0);
    CHECK(ran("set", "gshell", "taskbar/clock_24h", "int", "2147483647") == 0);
    host_tick_step = 1;
    CHECK(benched("2", "3") == 0);            /* color / wallpaper / clock */
    CHECK(cap_has("status 0 OK\n"));
    CHECK(cap_has("failures 0\n"));
    CHECK(ran("set", "gshell", "desktop/color", "int", "1") == 0);
    CHECK(ran("set", "gshell", "taskbar/clock_24h", "int", "1") == 0);

    /* ---- get の途中で ERROR へ遷移したら最終行も ERROR (B2) ----
     * open の schema 検査は通るが、値取得の prepare (SQL_GET = ival を含む)
     * だけが落ちる。open 直後の状態を覚えていると `status 0 OK` を出す。 */
    inj_prep_fail = "ival";
    host_tick_step = 1;
    CHECK(benched("1", "2") == 1);
    inj_prep_fail = NULL;
    CHECK(cap_has("status 4 ERROR\n"));
    /* get 2 件の失敗 + 状態が OK でない 1 = 3 */
    CHECK(cap_u32_after("failures ") == 3);
    CHECK(cap_yields_open == 0);

    /* ---- 古いカーネルは断る ---- */
    host_api.version = 49;
    CHECK(benched("1", "2") == 1);
    CHECK(cap_has("older than KAPI v50"));
    host_api.version = 50;
}

/* ========================================================================= */
/*  S5-C (2) cfg status の pool 表示                                         */
/* ========================================================================= */
static void c_s5_pool(void)
{
    char buf[16];

    /* ---- 符号なし整形 (u32 を fmt_int に通すと 2GB 超が負に化ける) ---- */
    CHECK(fmt_u32(buf, (int)sizeof(buf), 0) == 1 && !strcmp(buf, "0"));
    CHECK(fmt_u32(buf, (int)sizeof(buf), 12345) == 5 && !strcmp(buf, "12345"));
    CHECK(fmt_u32(buf, (int)sizeof(buf), 4294967295u) == 10 &&
          !strcmp(buf, "4294967295"));
    CHECK(fmt_u32(buf, 3, 12345) == -1);
    CHECK(fmt_u32(buf, 1, 0) == -1);

    /* ---- MISSING でも pool は出る ---- */
    CHECK(ran("status", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("MISSING pool "));
    CHECK(cap_has(" B\n"));

    /* ---- OK のときは schema_version の後ろ ---- */
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(ran("init", NULL, NULL, NULL, NULL) == 0);
    CHECK(ran("status", NULL, NULL, NULL, NULL) == 0);
    CHECK(cap_has("OK schema_version 1 pool "));
    CHECK(cap_has(" B\n"));
    /* **接続を持っている間**の値なので、閉じた後の素の値より大きい */
    CHECK(cap_u32_after("pool ") > (long)kapi_db_mem_used());
}


/* ========================================================================= */
/*  票 S3-C §2 — `cfg import` (記録は tools/tests/s3_tdd.md §C)              */
/* ========================================================================= */

static CfgJsonRow jr;

/* 構文だけ (対象外の scope の行にかかるのはこれだけ) */
static int jsyn(const char *line)
{
    return cfg_json_record(line, (int)strlen(line), &jr);
}

/* 構文 + 意味 (対象行にかかる分) */
static int jrec(const char *line)
{
    int rc = cfg_json_record(line, (int)strlen(line), &jr);
    if (rc != CFG_JSON_OK) return rc;
    return cfg_json_check(&jr);
}

static int jhdr(const char *line, int *v)
{
    return cfg_json_header(line, (int)strlen(line), v);
}

/* --- (C1) reader 単体の受理 / 拒否 ---------------------------------- */
static void c_s3_json(void)
{
    static char big[CFG_JSON_LINE_MAX * 2];
    static unsigned char raw[CFG_BLOB_MAX];
    int v = 0, i, n;

    /* ---- ヘッダ ---- */
    CHECK(jhdr("{\"schema_version\":1,\"exported\":\"7\"}", &v) == CFG_JSON_OK);
    CHECK(v == 1);
    CHECK(jhdr("{\"schema_version\":2,\"exported\":\"7\"}", &v) ==
          CFG_JSON_E_VERSION);
    CHECK(v == 2);                                  /* 文言に出す実値 */
    CHECK(jhdr("{\"schema_version\":0,\"exported\":\"7\"}", &v) ==
          CFG_JSON_E_VALUE);
    CHECK(jhdr("{\"schema_version\":1}", &v) == CFG_JSON_E_SYNTAX);
    CHECK(jhdr("{\"schema_version\": 1,\"exported\":\"7\"}", &v) ==
          CFG_JSON_E_VALUE);        /* 空白 (数値の位置なので E_VALUE) */
    CHECK(jhdr("{\"schema_version\":1,\"exported\":\"7\"} ", &v) ==
          CFG_JSON_E_SYNTAX);                       /* 余分な尻尾 */
    CHECK(jhdr("", &v) == CFG_JSON_E_SYNTAX);
    CHECK(jhdr("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":1}", &v) ==
          CFG_JSON_E_SYNTAX);                       /* レコードはヘッダでない */

    /* ---- 骨組み ---- */
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":5}")
          == CFG_JSON_OK);
    CHECK(jr.type == CFG_TYPE_INT && jr.ival == 5 && !jr.is_null);
    CHECK(!strcmp(jr.scope, "gshell") && !strcmp(jr.key, "a"));
    /* 順序違い / 空白 / 余分なキー / 尻尾 */
    CHECK(jrec("{\"key\":\"a\",\"scope\":\"gshell\",\"type\":0,\"v\":5}")
          == CFG_JSON_E_SYNTAX);
    CHECK(jrec("{\"scope\":\"gshell\", \"key\":\"a\",\"type\":0,\"v\":5}")
          == CFG_JSON_E_SYNTAX);
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":5,\"x\":1}")
          == CFG_JSON_E_SYNTAX);
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":5}}")
          == CFG_JSON_E_SYNTAX);
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":5")
          == CFG_JSON_E_SYNTAX);

    /* ---- 型 ---- */
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a\",\"type\":3,\"v\":5}")
          == CFG_JSON_E_TYPE);
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a\",\"type\":\"0\",\"v\":5}")
          == CFG_JSON_E_TYPE);
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a\",\"type\":10,\"v\":5}")
          == CFG_JSON_E_SYNTAX);                    /* 2 桁 = ,"v": が来ない */

    /* ---- scope / key ---- */
    CHECK(jrec("{\"scope\":\"SYSTEM\",\"key\":\"a\",\"type\":0,\"v\":5}")
          == CFG_JSON_E_SCOPE);
    CHECK(jrec("{\"scope\":\"app:\",\"key\":\"a\",\"type\":0,\"v\":5}")
          == CFG_JSON_E_SCOPE);
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"Bad\",\"type\":0,\"v\":5}")
          == CFG_JSON_E_KEY);
    CHECK(jrec("{\"scope\":\"gshell\",\"key\":\"a/\",\"type\":0,\"v\":5}")
          == CFG_JSON_E_KEY);
    /* 63B は受理、64B は拒否 (上限は復号後に見る) */
    strcpy(big, "{\"scope\":\"gshell\",\"key\":\"");
    for (i = 0; i < 63; i++) strcat(big, "a");
    strcat(big, "\",\"type\":0,\"v\":5}");
    CHECK(jrec(big) == CFG_JSON_OK);
    strcpy(big, "{\"scope\":\"gshell\",\"key\":\"");
    for (i = 0; i < 64; i++) strcat(big, "a");
    strcat(big, "\",\"type\":0,\"v\":5}");
    CHECK(jrec(big) == CFG_JSON_E_KEY);

    /* ---- int の境界 ---- */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":2147483647}")
          == CFG_JSON_OK && jr.ival == 2147483647);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":-2147483648}")
          == CFG_JSON_OK && jr.ival == (-2147483647 - 1));
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":2147483648}")
          == CFG_JSON_E_RANGE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":-2147483649}")
          == CFG_JSON_E_RANGE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":+1}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":01}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":-0}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":0}")
          == CFG_JSON_OK && jr.ival == 0);

    /* ---- エスケープ 6 種 ---- */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,"
               "\"v\":\"q\\\"w\\\\e\\nr\\rt\\ty\"") == CFG_JSON_E_SYNTAX);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,"
               "\"v\":\"q\\\"w\\\\e\\nr\\rt\\ty\"}") == CFG_JSON_OK);
    CHECK(jr.tlen == 11);
    CHECK(!memcmp(jr.tval, "q\"w\\e\nr\rt\ty", 11));
    /* \uXXXX は BMP だけ。サロゲートと \u0000 は拒否 */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\u0041\"}")
          == CFG_JSON_OK && jr.tlen == 1 && jr.tval[0] == 'A');
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\u00e3\"}")
          == CFG_JSON_OK && jr.tlen == 2);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\u3042\"}")
          == CFG_JSON_OK && jr.tlen == 3 &&
          !memcmp(jr.tval, "\xe3\x81\x82", 3));
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\u001f\"}")
          == CFG_JSON_OK && jr.tlen == 1 && jr.tval[0] == 0x1F);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\u0000\"}")
          == CFG_JSON_E_NUL);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\ud800\"}")
          == CFG_JSON_E_UTF8);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\udfff\"}")
          == CFG_JSON_E_UTF8);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\u00g0\"}")
          == CFG_JSON_E_SYNTAX);
    /* writer が出さないエスケープ */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\b\"}")
          == CFG_JSON_E_SYNTAX);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\/\"}")
          == CFG_JSON_E_SYNTAX);
    /* 生の制御文字と不正 UTF-8 */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\x01\"}")
          == CFG_JSON_E_SYNTAX);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\xff\"}")
          == CFG_JSON_E_UTF8);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\xe3\x81\"}")
          == CFG_JSON_E_UTF8);
    /* 4 バイト UTF-8 は生で通す (writer が生で出す) */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,"
               "\"v\":\"\xf0\x9f\x98\x80\"}") == CFG_JSON_OK && jr.tlen == 4);

    /* ---- text の上限 (255B / 256B) ---- */
    strcpy(big, "{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"");
    for (i = 0; i < 255; i++) strcat(big, "x");
    strcat(big, "\"}");
    CHECK(jrec(big) == CFG_JSON_OK && jr.tlen == 255);
    strcpy(big, "{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"");
    for (i = 0; i < 256; i++) strcat(big, "x");
    strcat(big, "\"}");
    CHECK(jrec(big) == CFG_JSON_E_VALUE);

    /* ---- 名前も値も最長の text 行 (1,693B + LF + NUL = 1,695B) ----
     * scope 63B (`app:` + 59) / key 63B / 全制御文字 255B の `\u00XX`。 */
    strcpy(big, "{\"scope\":\"app:");
    for (i = 0; i < 59; i++) strcat(big, "n");
    strcat(big, "\",\"key\":\"");
    for (i = 0; i < 63; i++) strcat(big, "k");
    strcat(big, "\",\"type\":1,\"v\":\"");
    for (i = 0; i < 255; i++) strcat(big, "\\u0001");
    strcat(big, "\"}");
    CHECK((int)strlen(big) == 1693);
    CHECK(jrec(big) == CFG_JSON_OK && jr.tlen == 255);

    /* ---- blob (base64) ---- */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"\"}")
          == CFG_JSON_OK && jr.blen == 0);          /* 空 blob */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AP8=\"}")
          == CFG_JSON_OK && jr.blen == 2 &&
          jr.bval[0] == 0x00 && jr.bval[1] == 0xFF);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AAAA\"}")
          == CFG_JSON_OK && jr.blen == 3);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AAA\"}")
          == CFG_JSON_E_VALUE);                     /* 4 の倍数でない */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"A===\"}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"====\"}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"A?==\"}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AA==AA==\"}")
          == CFG_JSON_E_VALUE);                     /* 詰めの後に本体 */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"\\u0041A==\"}")
          == CFG_JSON_E_VALUE);                     /* blob にエスケープは無い */
    /* blob に UTF-8 / NUL の検査はかけない (00 / FF を含む列が通る) */
    for (i = 0; i < 6; i++) raw[i] = (unsigned char)(i & 1 ? 0xFF : 0x00);
    {
        char b6[16], line[128];
        n = fmt_b64(b6, (int)sizeof(b6), raw, 6);
        CHECK(n == 8);
        sprintf(line, "{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"%s\"}",
                b6);
        CHECK(jrec(line) == CFG_JSON_OK && jr.blen == 6);
        CHECK(jr.bval[0] == 0x00 && jr.bval[1] == 0xFF);
    }

    /* ---- 最長行 (名前 63B/63B + blob 4096B = 5,627B + LF + NUL = 5,629B) -- */
    for (i = 0; i < CFG_BLOB_MAX; i++) raw[i] = (unsigned char)(i & 0xFF);
    strcpy(big, "{\"scope\":\"app:");
    for (i = 0; i < 59; i++) strcat(big, "n");
    strcat(big, "\",\"key\":\"");
    for (i = 0; i < 63; i++) strcat(big, "k");
    strcat(big, "\",\"type\":2,\"v\":\"");
    n = (int)strlen(big);
    CHECK(n == 161);
    CHECK(fmt_b64(big + n, (int)sizeof(big) - n, raw, CFG_BLOB_MAX) == 5464);
    strcat(big, "\"}");
    CHECK((int)strlen(big) == 5627);
    CHECK(jrec(big) == CFG_JSON_OK && jr.blen == CFG_BLOB_MAX);
    CHECK(!memcmp(jr.bval, raw, CFG_BLOB_MAX));
    /* 4096B を越える base64 は上限超過 */
    big[strlen(big) - 6] = '\0';                  /* 末尾の 1 組と `"}` を外す */
    strcat(big, "AAAAAAAA\"}");
    CHECK(jrec(big) == CFG_JSON_E_VALUE);

    /* ---- base64 の未使用ビットは 0 でなければならない (正準形) ---- */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AB==\"}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AAB=\"}")
          == CFG_JSON_E_VALUE);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AA==\"}")
          == CFG_JSON_OK && jr.blen == 1 && jr.bval[0] == 0x00);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AAA=\"}")
          == CFG_JSON_OK && jr.blen == 2);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"/w==\"}")
          == CFG_JSON_OK && jr.blen == 1 && jr.bval[0] == 0xFF);

    /* ---- 構文と意味の切り分け (往復 1 の B3) ----
     * 上限 / 値域 / 名前の規則は `cfg_json_check` の側。構文解析は通る。 */
    CHECK(jsyn("{\"scope\":\"SYSTEM\",\"key\":\"a\",\"type\":0,\"v\":5}")
          == CFG_JSON_OK);
    CHECK(jsyn("{\"scope\":\"gshell\",\"key\":\"Bad\",\"type\":0,\"v\":5}")
          == CFG_JSON_OK);
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":2147483648}")
          == CFG_JSON_OK && jr.val_range);
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\u0000\"}")
          == CFG_JSON_OK && jr.val_nul);
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\xff\"}")
          == CFG_JSON_OK);
    /* 構文そのものの違反は「対象外」でも拒否される */
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":01}")
          == CFG_JSON_E_VALUE);
    /* base64 の非正準形は**意味**の傷。構文解析は通る (往復 2 の B3 残存) */
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AB==\"}")
          == CFG_JSON_OK && jr.val_b64);
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AAB=\"}")
          == CFG_JSON_OK && jr.val_b64);
    /* 形そのもの (4 文字単位 / 詰めの位置 / 字種) は構文のまま */
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AAA\"}")
          == CFG_JSON_E_VALUE);
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"A?==\"}")
          == CFG_JSON_E_VALUE);
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AA==AA==\"}")
          == CFG_JSON_E_VALUE);
    /* 4096B 超過も意味の傷 */
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":\"AB==\"}")
          == CFG_JSON_OK);
    CHECK(jsyn("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"\\b\"}")
          == CFG_JSON_E_SYNTAX);
    /* 切り詰めた scope は対象判定に使えない (63B の対象名と取り違えない) */
    strcpy(big, "{\"scope\":\"app:");
    for (i = 0; i < 59; i++) strcat(big, "n");
    CHECK((int)strlen(big) == 14 + 59);
    strcat(big, "extra\",\"key\":\"a\",\"type\":0,\"v\":1}");
    CHECK(jsyn(big) == CFG_JSON_OK);
    CHECK(jr.scope_over && !cfg_json_scope_usable(&jr));
    CHECK(jrec(big) == CFG_JSON_E_SCOPE);
    /* 255B / 4096B ちょうどは意味の検証も通る */
    strcpy(big, "{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":\"");
    for (i = 0; i < 256; i++) strcat(big, "x");
    strcat(big, "\"}");
    CHECK(jsyn(big) == CFG_JSON_OK && jr.val_over);
    CHECK(jrec(big) == CFG_JSON_E_VALUE);

    /* ---- v:null は宣言型を保つ ---- */
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":1,\"v\":null}")
          == CFG_JSON_OK && jr.is_null && jr.type == CFG_TYPE_TEXT);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":2,\"v\":null}")
          == CFG_JSON_OK && jr.is_null && jr.type == CFG_TYPE_BLOB);
    CHECK(jrec("{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":nul}")
          == CFG_JSON_E_VALUE);
}

/* --- 下ごしらえ: 往復に使う値を入れた DB ---------------------------- */
static void s3_seed(void)
{
    CfgDb *db;
    static unsigned char blob[CFG_BLOB_MAX];
    static char ctl[CFG_TEXT_MAX + 1];
    int i;

    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);
    for (i = 0; i < CFG_TEXT_MAX; i++) ctl[i] = (char)(1 + (i % 31));
    ctl[CFG_TEXT_MAX] = '\0';
    for (i = 0; i < CFG_BLOB_MAX; i++) blob[i] = (unsigned char)(i * 7);
    CHECK(cfg_open(&db, 1) == 0);
    CHECK(cfg_status(db) == CFG_OK);
    CHECK(cfg_begin(db) == 0);
    CHECK(cfg_set_text(db, "gshell", "t/cr", "a\rb") == 0);
    CHECK(cfg_set_text(db, "gshell", "t/ctl", ctl) == 0);
    CHECK(cfg_set_blob(db, "gshell", "b/empty", "", 0) == 0);
    CHECK(cfg_set_blob(db, "gshell", "b/bytes", "\x00\xff\x01\xfe", 4) == 0);
    CHECK(cfg_set_blob(db, "gshell", "b/big", blob, CFG_BLOB_MAX) == 0);
    CHECK(cfg_set_null(db, "user", "n/null", CFG_TYPE_TEXT) == 0);
    CHECK(cfg_set_int(db, "app:demo", "x", 7) == 0);
    CHECK(cfg_commit(db) == 0);
    CHECK(cfg_close(db) == 0);
}

static char snap_a[FIXTURE_BYTES];
static u32  snap_a_len;
static char snap_list[65536];
static int  snap_list_len;

static void snap_file(const char *path, char *out, u32 *len)
{
    FixtureFile *f = fixture_find(path, 0);
    CHECK(f != NULL);
    memcpy(out, f->data, f->size);
    *len = f->size;
}

static void snap_the_list(void)
{
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 0);
    CHECK(cap_len < (int)sizeof(snap_list));
    memcpy(snap_list, cap_out, (size_t)cap_len);
    snap_list_len = cap_len;
}

static int list_matches(void)
{
    CHECK(ran("list", NULL, NULL, NULL, NULL) == 0);
    return cap_len == snap_list_len && !memcmp(cap_out, snap_list, (size_t)cap_len);
}

/* --- (C2) 実 writer の出力を import に通す往復 ----------------------- */
static void c_s3_roundtrip(void)
{
    FixtureFile *f;

    s3_seed();
    CHECK(ran("export", "/b.json", NULL, NULL, NULL) == 0);
    CHECK(cap_has("cfg: exported 10 records"));
    snap_file("/b.json", snap_a, &snap_a_len);
    /* 最長行 (blob 4096B) が実際に出ている */
    CHECK(snap_a_len > 5627);
    snap_the_list();

    /* 値を壊す — 変更 / 削除 / 追加 */
    CHECK(ran("set", "gshell", "desktop/color", "int", "99") == 0);
    CHECK(ran("del", "user", "n/null", NULL, NULL) == 0);
    CHECK(ran("set", "user", "extra", "int", "1") == 0);
    CHECK(!list_matches());

    /* 置換で戻る */
    CHECK(ran("import", "/b.json", NULL, NULL, NULL) == 0);
    CHECK(cap_has("imported 10 records (all scopes), replaced\n"));
    CHECK(list_matches());

    /* export し直すとバイト単位で一致する (CR / 制御文字 / 00・FF / 空 blob /
     * NULL 行 / 4096B blob がすべて往復した) */
    CHECK(ran("export", "/c.json", NULL, NULL, NULL) == 0);
    f = fixture_find("/c.json", 0);
    CHECK(f != NULL);
    CHECK(f->size == snap_a_len);
    CHECK(!memcmp(f->data, snap_a, snap_a_len));

    /* 接続を持ったままの yield は無い (票 §7 の直列化) */
    CHECK(cap_yields_open == 0);
}

/* --- (C3) --scope / --merge ----------------------------------------- */
static void c_s3_scope(void)
{
    s3_seed();
    CHECK(ran("export", "/b.json", NULL, NULL, NULL) == 0);

    /* gshell を壊し、scope 外にも値を置く */
    CHECK(ran("set", "gshell", "desktop/color", "int", "42") == 0);
    CHECK(ran("set", "user", "extra", "int", "5") == 0);
    CHECK(ran("import", "/b.json", "--scope", "gshell", NULL) == 0);
    CHECK(cap_has("imported 8 records (gshell), replaced\n"));
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("1\n"));                       /* gshell は戻った */
    CHECK(ran("get", "user", "extra", NULL, NULL) == 0);
    CHECK(cap_has("5\n"));                       /* scope 外の値は不変 */

    /* 置換は対象 scope の**余分な行だけ**を消す */
    CHECK(ran("set", "gshell", "junk", "int", "3") == 0);
    CHECK(ran("import", "/b.json", "--scope", "gshell", NULL) == 0);
    CHECK(ran("get", "gshell", "junk", NULL, NULL) == 0);
    CHECK(cap_has("(not set)"));
    CHECK(ran("get", "user", "extra", NULL, NULL) == 0);
    CHECK(cap_has("5\n"));

    /* --merge は消さない */
    CHECK(ran("set", "user", "extra", "int", "9") == 0);
    CHECK(ran("del", "user", "n/null", NULL, NULL) == 0);
    CHECK(ran("import", "/b.json", "--scope", "user", "--merge") == 0);
    CHECK(cap_has("imported 1 records (user), merged\n"));
    CHECK(ran("get", "user", "extra", NULL, NULL) == 0);
    CHECK(cap_has("9\n"));                       /* merge は既存を消さない */
    CHECK(ran("list", "user", NULL, NULL, NULL) == 0);
    CHECK(cap_has("user\tn/null\ttext\t(unset)"));  /* NULL 行が戻った */

    /* 全 scope の --merge も消さない */
    CHECK(ran("import", "/b.json", "--merge", NULL, NULL) == 0);
    CHECK(cap_has("imported 10 records (all scopes), merged\n"));
    CHECK(ran("get", "user", "extra", NULL, NULL) == 0);
    CHECK(cap_has("9\n"));

    /* 無効な scope は入口で断る (DB を触らない) */
    CHECK(ran("import", "/b.json", "--scope", "BAD", NULL) == 1);
    CHECK(cap_has("bad scope"));

    /* --- 往復 1 の B3: 対象外の scope の行は**構文検証だけ** --- */
    reset_all();
    s3_seed();
    CHECK(ran("set", "user", "keep", "int", "4") == 0);
    snap_the_list();
    {
        static char buf[8192];
        int i;
        strcpy(buf, "{\"schema_version\":1,\"exported\":\"0\"}\n"
                    "{\"scope\":\"gshell\",\"key\":\"desktop/color\","
                    "\"type\":0,\"v\":7}\n"
                    /* 値域外の int / 大文字 scope / 64B key / 不正 UTF-8 /
                     * \u0000 — どれも `user` 側なので無視されるべき */
                    "{\"scope\":\"user\",\"key\":\"a\","
                    "\"type\":0,\"v\":2147483648}\n"
                    "{\"scope\":\"SYSTEM\",\"key\":\"a\",\"type\":0,\"v\":1}\n"
                    "{\"scope\":\"user\",\"key\":\"Bad\",\"type\":0,\"v\":1}\n"
                    "{\"scope\":\"user\",\"key\":\"b\",\"type\":1,"
                    "\"v\":\"\\u0000\"}\n"
                    "{\"scope\":\"user\",\"key\":\"c\",\"type\":1,\"v\":\"");
        /* 構文は正しいが 256B の text (票 §2 の反例そのもの) */
        for (i = 0; i < 256; i++) strcat(buf, "x");
        strcat(buf, "\"}\n");
        put_file("/mix.json", buf);
    }
    CHECK(ran("import", "/mix.json", "--scope", "gshell", NULL) == 0);
    CHECK(cap_has("imported 1 records (gshell), replaced\n"));
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("7\n"));
    CHECK(ran("get", "user", "keep", NULL, NULL) == 0);
    CHECK(cap_has("4\n"));                    /* scope 外は 1 行も動かない */
    /* 同じファイルを全 scope で取り込もうとすると、その行で止まる */
    CHECK(ran("import", "/mix.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("bad line 3: range"));
    /* 対象外の行の base64 が非正準形 (未使用ビット非ゼロ) — 往復 2 の B3 */
    put_file("/b64.json",
             "{\"schema_version\":1,\"exported\":\"0\"}\n"
             "{\"scope\":\"gshell\",\"key\":\"desktop/color\","
             "\"type\":0,\"v\":8}\n"
             "{\"scope\":\"user\",\"key\":\"b\",\"type\":2,\"v\":\"AB==\"}\n");
    CHECK(ran("import", "/b64.json", "--scope", "gshell", NULL) == 0);
    CHECK(cap_has("imported 1 records (gshell), replaced\n"));
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("8\n"));
    CHECK(ran("get", "user", "keep", NULL, NULL) == 0);
    CHECK(cap_has("4\n"));                    /* scope 外は動かない */
    /* 同じ入力を全 scope / その scope で取り込むと、その行で止まる */
    CHECK(ran("import", "/b64.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("bad line 3: value"));
    CHECK(ran("import", "/b64.json", "--scope", "user", NULL) == 1);
    CHECK(cap_has("bad line 3: value"));
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("8\n"));                    /* 失敗側は 1 行も書いていない */
    /* 2 巡目だけに現れても同じ (検証は両巡で同じ) — 1 巡目を通った入力の
     * 対象外の行が 2 巡目で非正準形になっても gshell は書けるまま */
    CHECK(ran("import", "/b64.json", "--scope", "gshell", "--merge") == 0);
    CHECK(cap_has("imported 1 records (gshell), merged\n"));

    /* 対象外の行の blob が 4096B を超えていても対象 scope は通る */
    {
        static char buf[16384];
        static unsigned char raw[CFG_BLOB_MAX];
        int i, n;
        for (i = 0; i < CFG_BLOB_MAX; i++) raw[i] = (unsigned char)i;
        strcpy(buf, "{\"schema_version\":1,\"exported\":\"0\"}\n"
                    "{\"scope\":\"gshell\",\"key\":\"desktop/color\","
                    "\"type\":0,\"v\":9}\n"
                    "{\"scope\":\"user\",\"key\":\"b\",\"type\":2,\"v\":\"");
        n = (int)strlen(buf);
        fmt_b64(buf + n, (int)sizeof(buf) - n, raw, CFG_BLOB_MAX);
        buf[strlen(buf) - 4] = '\0';              /* 末尾の 1 組を外す */
        strcat(buf, "AAAAAAAA\"}\n");            /* 4096B を越える */
        put_file("/big.json", buf);
    }
    CHECK(ran("import", "/big.json", "--scope", "gshell", NULL) == 0);
    CHECK(cap_has("imported 1 records (gshell), replaced\n"));
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("9\n"));
    CHECK(ran("import", "/big.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("bad line 3: value"));

    /* 構文そのものの違反なら対象外の scope でも止まる */
    put_file("/badsyn.json",
             "{\"schema_version\":1,\"exported\":\"0\"}\n"
             "{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":1}\n"
             "{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":01}\n");
    CHECK(ran("import", "/badsyn.json", "--scope", "gshell", NULL) == 1);
    CHECK(cap_has("bad line 3: value"));
    /* 63B の対象 scope に前半が一致する長い scope を取り違えない */
    {
        static char buf[8192];
        char target[80];
        int i;
        strcpy(target, "app:");
        for (i = 0; i < 59; i++) strcat(target, "n");
        strcpy(buf, "{\"schema_version\":1,\"exported\":\"0\"}\n{\"scope\":\"");
        strcat(buf, target);
        strcat(buf, "extra\",\"key\":\"a\",\"type\":0,\"v\":1}\n");
        put_file("/pfx.json", buf);
        CHECK(ran("import", "/pfx.json", "--scope", target, NULL) == 0);
        CHECK(cap_has(" records ("));
        CHECK(cap_has("imported 0 records ("));   /* 対象は 0 件 */
    }
}

/* --- (C4) 引数解釈 ---------------------------------------------------- */
static void c_s3_args(void)
{
    CfgArgs a;
    char *av[6];
    int n;

    av[0] = (char *)"cfg";
    av[1] = (char *)"import";
    av[2] = (char *)"/b.json";
    av[3] = (char *)"--scope";
    av[4] = (char *)"gshell";
    av[5] = (char *)"--merge";
    for (n = 3; n <= 6; n++) {
        if (n == 4) continue;                     /* --scope に値が無い形 */
        CHECK(cfg_cmd_parse(n, av, &a) == 0);
        CHECK(a.cmd == CFG_CMD_IMPORT);
        CHECK(!strcmp(a.path, "/b.json"));
        CHECK(a.merge == (n == 6));
        CHECK((a.scope != NULL) == (n >= 5));
    }
    CHECK(cfg_cmd_parse(4, av, &a) == -1);        /* --scope の値が無い */
    av[3] = (char *)"--merge";
    av[4] = (char *)"--merge";
    CHECK(cfg_cmd_parse(5, av, &a) == -1);        /* 二重 --merge */
    av[3] = (char *)"--bogus";
    CHECK(cfg_cmd_parse(4, av, &a) == -1);
    av[2] = (char *)"--scope";
    CHECK(cfg_cmd_parse(3, av, &a) == -1);        /* path が旗 */
    CHECK(cfg_cmd_parse(2, av, &a) == -1);        /* path 無し */
    /* usage に載っている */
    CHECK(ran("import", NULL, NULL, NULL, NULL) == 1);
    CHECK(cap_has("cfg import <file> [--scope <scope>] [--merge]"));
}

/* --- (C5) 状態と拒否 -------------------------------------------------- */
static void c_s3_reject(void)
{
    static const char HDR[] = "{\"schema_version\":1,\"exported\":\"0\"}\n";
    static char buf[CFG_JSON_LINE_MAX * 2];
    int i;

    /* MISSING — 検証は通るが書けない */
    put_file("/x.json", HDR);
    CHECK(ran("import", "/x.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("cannot import: MISSING"));
    CHECK(cap_has("settings.db missing: run 'cfg init'"));
    CHECK(fixture_find(CFG_DB_PATH, 0) == NULL);

    /* CORRUPT (0 バイト) */
    CHECK(fixture_find(CFG_DB_PATH, 1) != NULL);
    CHECK(ran("import", "/x.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("cannot import: CORRUPT"));

    /* 版が新しいバックアップ — DB を開く前に断る */
    reset_all();
    s3_seed();
    put_file("/v2.json", "{\"schema_version\":2,\"exported\":\"0\"}\n");
    snap_the_list();
    CHECK(ran("import", "/v2.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("newer backup: schema_version 2"));
    CHECK(list_matches());

    /* ヘッダが無い / 壊れた行 / 無いファイル */
    put_file("/bad1.json", "hello\n");
    CHECK(ran("import", "/bad1.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("not a settings backup"));
    put_file("/bad2.json",
             "{\"schema_version\":1,\"exported\":\"0\"}\n"
             "{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":1}\n"
             "{\"scope\":\"gshell\",\"key\":\"Bad\",\"type\":0,\"v\":1}\n");
    CHECK(ran("import", "/bad2.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("bad line 3: key"));
    CHECK(list_matches());                        /* 1 行も書いていない */
    CHECK(ran("import", "/nosuch.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("cannot open the import file"));

    /* 重複 (fixture の中の 2 行) */
    put_file("/dup.json",
             "{\"schema_version\":1,\"exported\":\"0\"}\n"
             "{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":1}\n"
             "{\"scope\":\"gshell\",\"key\":\"a\",\"type\":0,\"v\":2}\n");
    CHECK(ran("import", "/dup.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("duplicate record at line 3"));
    CHECK(list_matches());
    /* --scope で対象外なら重複も無視 (構文検証だけ) */
    CHECK(ran("import", "/dup.json", "--scope", "user", NULL) == 0);
    CHECK(cap_has("imported 0 records (user), replaced\n"));

    /* 行が長すぎる */
    reset_all();
    s3_seed();
    snap_the_list();
    strcpy(buf, "{\"schema_version\":1,\"exported\":\"0\"}\n{\"scope\":\"user\",");
    for (i = (int)strlen(buf); i < CFG_JSON_LINE_MAX + 100; i++) buf[i] = 'x';
    buf[i] = '\0';
    put_file("/long.json", buf);
    CHECK(ran("import", "/long.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("line 2 too long"));
    CHECK(list_matches());
}

/* --- (C6) 途中失敗 / 巡回の間の差し替え / close 失敗 ------------------ */
static void c_s3_fail(void)
{
    /* --- set が途中で失敗したら 1 行も残らない --- */
    s3_seed();
    CHECK(ran("export", "/b.json", NULL, NULL, NULL) == 0);
    snap_the_list();
    inj_prep_fail = "INSERT OR REPLACE";
    inj_prep_skip = 2;                            /* 3 件目の set で落ちる */
    CHECK(ran("import", "/b.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("import failed ("));
    inj_prep_fail = NULL;
    inj_prep_skip = 0;
    CHECK(list_matches());                        /* 削除も巻き戻っている */

    /* --- 2 巡目の read が落ちたら rollback ---
     * 512B 未満の入力なら 1 巡目の read は 2 回 (本体 + EOF) で終わるので、
     * 3 回目 = 2 巡目の最初の read。削除は済んでいる段で落ちる。 */
    put_file("/small.json",
             "{\"schema_version\":1,\"exported\":\"0\"}\n"
             "{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":1}\n"
             "{\"scope\":\"user\",\"key\":\"b\",\"type\":0,\"v\":2}\n");
    inj_fread_fail_at = 3;
    CHECK(ran("import", "/small.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("read failed"));
    inj_fread_fail_at = 0;
    inj_fread_n = 0;
    CHECK(list_matches());

    /* --- lseek が使えなければ重複検出ができない = 失敗 --- */
    put_file("/coll.json",
             "{\"schema_version\":1,\"exported\":\"0\"}\n"
             "{\"scope\":\"user\",\"key\":\"89\",\"type\":0,\"v\":1}\n"
             "{\"scope\":\"user\",\"key\":\"adp\",\"type\":0,\"v\":2}\n");
    inj_seek_fail = 1;
    CHECK(ran("import", "/coll.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("read failed"));
    inj_seek_fail = 0;
    CHECK(list_matches());

    /* --- hash が衝突する別物 2 件は両方とも受理される --- */
    CHECK(ran("import", "/coll.json", NULL, NULL, NULL) == 0);
    CHECK(cap_has("imported 2 records (all scopes), replaced\n"));
    CHECK(ran("get", "user", "89", NULL, NULL) == 0);
    CHECK(cap_has("1\n"));
    CHECK(ran("get", "user", "adp", NULL, NULL) == 0);
    CHECK(cap_has("2\n"));

    /* --- 往復 1 の B4: commit 前の失敗で rollback も落ちたら別欄で出す --- */
    reset_all();
    s3_seed();
    snap_the_list();
    put_file("/small.json",
             "{\"schema_version\":1,\"exported\":\"0\"}\n"
             "{\"scope\":\"user\",\"key\":\"a\",\"type\":0,\"v\":1}\n"
             "{\"scope\":\"user\",\"key\":\"b\",\"type\":0,\"v\":2}\n");
    inj_fread_fail_at = 3;                        /* 2 巡目の最初の read */
    inj_exec_match[0] = "ROLLBACK";               /* 後始末も落とす */
    inj_exec_sub[0] = "SELECT 1 FROM no_such_table_for_tdd";
    CHECK(ran("import", "/small.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("read failed (rollback/close failed "));
    inj_fread_fail_at = 0;
    inj_fread_n = 0;
    inj_exec_match[0] = NULL;
    inj_exec_sub[0] = NULL;
    /* rollback が成功する通常の経路では尾は付かない */
    inj_fread_fail_at = 3;
    CHECK(ran("import", "/small.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("read failed\n"));
    CHECK(!cap_has("rollback/close failed"));
    inj_fread_fail_at = 0;
    inj_fread_n = 0;
    CHECK(list_matches());                        /* SQLite の close が巻き戻す */

    /* status 拒否の経路でも同じ尾が付く。0 バイトの DB だと接続を掴む前に
     * 落ちて close する相手がいないので、**開けるが meta が 0 行**の DB
     * (= CORRUPT、接続は生きている) を使う。 */
    reset_all();
    {
        static const char *bad[] = {
            "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)",
            "CREATE TABLE settings (scope TEXT, key TEXT, type INTEGER,"
            " ival INTEGER, tval TEXT, bval BLOB, PRIMARY KEY (scope,key))"
            " WITHOUT ROWID",
            NULL
        };
        raw_db(CFG_DB_PATH, bad);
    }
    put_file("/x.json", "{\"schema_version\":1,\"exported\":\"0\"}\n");
    inj_close_fail = SQLITE_IOERR;
    CHECK(ran("import", "/x.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("cannot import: CORRUPT (rollback/close failed "));
    inj_close_fail = 0;

    /* --- commit の後の close 失敗は「更新済み」と出して終了 1 --- */
    reset_all();
    s3_seed();
    CHECK(ran("export", "/b.json", NULL, NULL, NULL) == 0);
    CHECK(ran("set", "gshell", "desktop/color", "int", "77") == 0);
    inj_close_after_commit = SQLITE_IOERR;
    CHECK(ran("import", "/b.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("imported 10 records (all scopes), replaced (close failed "));
    inj_close_fail = 0;
    inj_close_after_commit = 0;
    /* commit は通っているので値は戻っている */
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("1\n"));
}

/* --- (C7) 生成入力: 件数上限と巡回の間の差し替え --------------------- */
static void c_s3_gen(void)
{
    /* --- 8192 件は受理 (DB が MISSING なので書く前で止まる) --- */
    gen_path = "/gen.json";
    gen_rows = CFG_IMPORT_MAX_ROWS;
    CHECK(ran("import", "/gen.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("cannot import: MISSING"));     /* 検証は通った */

    /* --- 8193 件は検証で断る --- */
    reset_all();
    gen_path = "/gen.json";
    gen_rows = CFG_IMPORT_MAX_ROWS + 1;
    CHECK(ran("import", "/gen.json", NULL, NULL, NULL) == 1);
    CHECK(cap_has("too many records"));

    /* --- 素の通し (5 件) --- */
    reset_all();
    s3_seed();
    gen_path = "/gen.json";
    gen_rows = 5;
    CHECK(ran("import", "/gen.json", "--scope", "user", NULL) == 0);
    CHECK(cap_has("imported 5 records (user), replaced\n"));
    snap_the_list();

    /* --- 2 巡目で件数が変わる --- */
    gen_opens = 0;
    gen_rows2 = 6;
    CHECK(ran("import", "/gen.json", "--scope", "user", NULL) == 1);
    CHECK(cap_has("input changed during import"));
    CHECK(list_matches());

    /* --- 2 巡目に重複が現れる --- */
    gen_opens = 0;
    gen_rows2 = 0;
    gen_dup = 3;
    CHECK(ran("import", "/gen.json", "--scope", "user", NULL) == 1);
    CHECK(cap_has("duplicate record at line 5"));   /* 0 起点の 3 行目 = 4 件目 */
    CHECK(list_matches());

    /* --- 2 巡目のヘッダの版が上がる --- */
    gen_opens = 0;
    gen_dup = 0;
    gen_ver2 = 2;
    CHECK(ran("import", "/gen.json", "--scope", "user", NULL) == 1);
    CHECK(cap_has("newer backup: schema_version 2"));
    CHECK(list_matches());
}

/* --- (C8) 8192 件を実 SQLite に書き切る (往復 1 の non-blocker) -------- */
static void c_s3_bulk(void)
{
    put_file(CFG_TSV_PATH, TSV_OK_TEXT);
    CHECK(cfg_init(NULL) == 0);
    gen_path = "/gen.json";
    gen_rows = CFG_IMPORT_MAX_ROWS;
    /* 384KB の MEMSYS5 プール (実 os32_sqlite_vfs.c) の上で、8192 行を
     * **1 つの transaction**で書き切れるか。置換なので全削除 + 8192 INSERT。*/
    CHECK(ran("import", "/gen.json", "--scope", "user", NULL) == 0);
    CHECK(cap_has("imported 8192 records (user), replaced\n"));
    CHECK(cap_yields_open == 0);
    /* 中身が本当に入っている (先頭 / 末尾 / 途中) */
    CHECK(ran("get", "user", "k0000", NULL, NULL) == 0);
    CHECK(cap_has("1\n"));
    CHECK(ran("get", "user", "k8191", NULL, NULL) == 0);
    CHECK(cap_has("1\n"));
    CHECK(ran("get", "user", "k4096", NULL, NULL) == 0);
    CHECK(cap_has("1\n"));
    CHECK(ran("get", "user", "k8192", NULL, NULL) == 0);
    CHECK(cap_has("(not set)"));
    /* gshell (tsv の 3 件) は触られていない */
    CHECK(ran("get", "gshell", "desktop/color", NULL, NULL) == 0);
    CHECK(cap_has("1\n"));
}

/* ========================================================================= */

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(os32_sqlite_init() == SQLITE_OK);
    reset_all();
    cases_run++;

    if (!strcmp(argv[1], "missing")) c_missing();
    else if (!strcmp(argv[1], "corrupt")) c_corrupt();
    else if (!strcmp(argv[1], "version")) c_version();
    else if (!strcmp(argv[1], "types")) c_types();
    else if (!strcmp(argv[1], "roundtrip")) c_roundtrip();
    else if (!strcmp(argv[1], "txn")) c_txn();
    else if (!strcmp(argv[1], "limits")) c_limits();
    else if (!strcmp(argv[1], "enum")) c_enum();
    else if (!strcmp(argv[1], "meta")) c_meta();
    else if (!strcmp(argv[1], "shm_copy")) c_shm_copy();
    else if (!strcmp(argv[1], "init")) c_init();
    else if (!strcmp(argv[1], "rename")) c_rename();
    else if (!strcmp(argv[1], "tsv")) c_tsv();
    else if (!strcmp(argv[1], "args")) c_args();
    else if (!strcmp(argv[1], "cmd")) c_cmd();
    else if (!strcmp(argv[1], "list_big")) c_list_big();
    else if (!strcmp(argv[1], "stat_fail")) c_stat_fail();
    else if (!strcmp(argv[1], "export_guard")) c_export_guard();
    else if (!strcmp(argv[1], "txn_poison")) c_txn_poison();
    else if (!strcmp(argv[1], "fetch_fail")) c_fetch_fail();
    else if (!strcmp(argv[1], "nullval")) c_nullval();
    else if (!strcmp(argv[1], "corrupt_schema")) c_corrupt_schema();
    else if (!strcmp(argv[1], "commit_diag")) c_commit_diag();
    else if (!strcmp(argv[1], "open_close_fail")) c_open_close_fail();
    else if (!strcmp(argv[1], "bind_fail")) c_bind_fail();
    else if (!strcmp(argv[1], "get_many")) c_get_many();
    else if (!strcmp(argv[1], "enum_reenter_get")) c_enum_reenter_get();
    else if (!strcmp(argv[1], "out_fail")) c_out_fail();
    else if (!strcmp(argv[1], "long_scope")) c_long_scope();
    else if (!strcmp(argv[1], "intmin")) c_intmin();
    else if (!strcmp(argv[1], "tsv_many")) c_tsv_many();
    else if (!strcmp(argv[1], "r2_int_fail")) c_r2_int_fail();
    else if (!strcmp(argv[1], "r2_txn_error")) c_r2_txn_error();
    else if (!strcmp(argv[1], "r2_close_diag")) c_r2_close_diag();
    else if (!strcmp(argv[1], "r2_null_type")) c_r2_null_type();
    else if (!strcmp(argv[1], "r2_wide")) c_r2_wide();
    else if (!strcmp(argv[1], "r2_badval")) c_r2_badval();
    else if (!strcmp(argv[1], "r2_alias")) c_r2_alias();
    else if (!strcmp(argv[1], "r3_fat")) c_r3_fat();
    else if (!strcmp(argv[1], "r3_enum_write")) c_r3_enum_write();
    else if (!strcmp(argv[1], "r3_enum_type")) c_r3_enum_type();
    else if (!strcmp(argv[1], "r3_enum_err")) c_r3_enum_err();
    else if (!strcmp(argv[1], "r3_tsv_long")) c_r3_tsv_long();
    else if (!strcmp(argv[1], "s5_pure")) c_s5_pure();
    else if (!strcmp(argv[1], "s5_bench")) c_s5_bench();
    else if (!strcmp(argv[1], "s5_pool")) c_s5_pool();
    else if (!strcmp(argv[1], "s3_json")) c_s3_json();
    else if (!strcmp(argv[1], "s3_roundtrip")) c_s3_roundtrip();
    else if (!strcmp(argv[1], "s3_scope")) c_s3_scope();
    else if (!strcmp(argv[1], "s3_args")) c_s3_args();
    else if (!strcmp(argv[1], "s3_reject")) c_s3_reject();
    else if (!strcmp(argv[1], "s3_fail")) c_s3_fail();
    else if (!strcmp(argv[1], "s3_gen")) c_s3_gen();
    else if (!strcmp(argv[1], "s3_bulk")) c_s3_bulk();
    else CHECK(0);

    canary_check(argv[1]);
    CHECK(memsys5_check_canary() == 0);
    if (strcmp(argv[1], "tsv")) printf("PASS %s\n", argv[1]);
    return 0;
}
