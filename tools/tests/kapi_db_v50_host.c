/* ========================================================================= */
/*  KAPI_DB_V50_HOST.C — 票 S0-K §1d のホスト TDD                            */
/*                                                                           */
/*  実物だけを組む: 実 `kapi/kapi_db.c` + 実 `lib/sqlite3/sqlite3.c` +        */
/*  実 `lib/sqlite3/os32_sqlite_vfs.c` + 実 `fs/vfs_fd.c` + RAM バックエンド。 */
/*  模型にするのは exec 側のポインタ検証 (`ring3_user_range_ok`) と           */
/*  SHM の置き場だけ — どちらもカーネル番地に依存してホストでは動かせない。    */
/*  ホストのファイルシステムには 1 バイトも触らない。                          */
/*                                                                           */
/*  実行: python3 -B tools/tests/test_kapi_db_v50.py                         */
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
/* fs/vfs.c の vfs_stat / vfs_rm は中で vfs_resolve_path を呼ぶ。相対名と
 * cwd の連結・**切り詰め**はそこで起きるので、模型も同じ順で呼ぶ。 */
static const char *host_resolve(const char *path)
{
    static char resolved[VFS_MAX_PATH];
    vfs_resolve_path(path, resolved, (int)sizeof(resolved));
    return resolved;
}
int vfs_rm(const char *path) { probes++; return fixture_rm(host_resolve(path)); }
/* stat の障害注入 (blocker 2)。`stat_fail_on` に部分一致する path の stat が
 * `stat_fail_rc` を返す。実 FS では EACCES / EIO / ELOOP がこの形で来る。 */
static const char *stat_fail_on;
static int stat_fail_rc = OS32_ERR_IO;
static int stat_calls;
int vfs_stat(const char *path, OS32_Stat *st)
{
    FixtureFile *f;
    probes++;
    stat_calls++;
    if (stat_fail_on && strstr(path, stat_fail_on)) return stat_fail_rc;
    f = fixture_find(host_resolve(path), 0);
    if (!f) return OS32_ERR_NOTFOUND;
    if (st) { memset(st, 0, sizeof(*st)); st->st_size = f->size; }
    return 0;
}

/* ---- exec 側の模型 (kapi_db.c が唯一使う口) ---------------------------- */
/* 0 = CPL=0 の直呼び (素通し) / 1 = CPL=3 由来 (帯と PTE を見る)。
 * 帯は [BAND_LO, BAND_HI) の 1 本。**PTE は見ない** (2026-09-13、実機 K2):
 * 許可帯の中の非 present なページ (guard / 未マップ) はカーネルが写した
 * ところで #PF になり、既存のフォールトガードが呼び手を kill する — 他の
 * KAPI と同じ扱い。GUARD_PAGE は「帯の中でも検証は通る」ことの目印として
 * 残す (実機ではそこを渡すと -1 ではなく kill)。 */
#define HOST_PAGE      4096u
#define BAND_LO        0x00400000u
#define BAND_HI        0x00800000u
#define GUARD_PAGE     0x007BF000u
static int host_cpl3;
static u32 host_range_calls;
/* CPL=3 の模型で「読んでよい」実ポインタ範囲 (登録制)。数値の帯
 * [BAND_LO, BAND_HI) はホストの本物のポインタを覆えないので、
 * open_existing / prepare_only を CPL=3 の規則で通すにはこちらが要る。 */
static const char *host_user_lo;
static const char *host_user_hi;
static u32 host_range_refused;

int ring3_user_range_ok(u32 p, u32 len)
{
    u32 page, last;
    host_range_calls++;
    if (!host_cpl3) return 1;
    if (p == 0) { host_range_refused++; return 0; }
    if (len == 0) return 1;
    if (p + len < p) { host_range_refused++; return 0; }
    if (host_user_lo) {
        /* 登録した実ポインタ範囲。ホストの 64bit ポインタを u32 に落とさず
         * そのまま比べる (kapi_db.c は (u32) に落として渡すので、下位 32bit
         * が一致する範囲を探す = 試験側で登録した領域だけを許す)。 */
        const char *q = host_user_lo;
        while (q < host_user_hi) {
            if ((u32)(unsigned long)q == p) {
                if ((unsigned long)(host_user_hi - q) < (unsigned long)len) {
                    host_range_refused++;
                    return 0;
                }
                return 1;
            }
            q++;
        }
        host_range_refused++;
        return 0;
    }
    last = (p + len - 1u) & ~(HOST_PAGE - 1u);
    for (page = p & ~(HOST_PAGE - 1u); ; page += HOST_PAGE) {
        if (page < BAND_LO || page >= BAND_HI) return 0;
        if (page >= last) break;
    }
    return 1;
}

/* SHM の置き場。末尾に番兵を置いて「16KB を 1 バイトも越えない」を見る。 */
#define SHM_CANARY 256
static unsigned char test_shm[DB_SHM_BLOCK_SIZE + SHM_CANARY];

/* 列値の実体化の境界 (往復 2 の B3)。SQLite は確保に失敗すると accessor で
 * NULL を返す。実機の MEMSYS5 (384KB) ではこれが起きうるが、ホストでは
 * `sqlite3_step` の方が先に落ちて同じ形を作れない。**その 1 本だけ**を
 * 差し替えて、長さはあるのにポインタが無い状態を決定的に作る。 */
static int fail_column_ptr;
static int fake_errcode;          /* 0 = 素通し */
static const void *host_column_blob(sqlite3_stmt *st, int i)
{ return fail_column_ptr ? (const void *)0 : sqlite3_column_blob(st, i); }
static const unsigned char *host_column_text(sqlite3_stmt *st, int i)
{ return fail_column_ptr ? (const unsigned char *)0 : sqlite3_column_text(st, i); }
static int host_errcode(sqlite3 *db)
{ return fake_errcode ? fake_errcode : sqlite3_errcode(db); }
static int host_ext_errcode(sqlite3 *db)
{ return fake_errcode ? fake_errcode : sqlite3_extended_errcode(db); }
#define sqlite3_column_blob host_column_blob
#define sqlite3_column_text host_column_text
#define sqlite3_errcode host_errcode
#define sqlite3_extended_errcode host_ext_errcode
#include "../../kapi/kapi_db.c"
#undef sqlite3_column_blob
#undef sqlite3_column_text
#undef sqlite3_errcode
#undef sqlite3_extended_errcode

static int cases_run;

static void reset_all(void)
{
    int i;
    memset(test_shm, 0, sizeof(test_shm));
    memset(db_slots, 0, sizeof(db_slots));
    memset(db_open_fail, 0, sizeof(db_open_fail));
    memset(fixture_files, 0, sizeof(fixture_files));
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].in_use = 0;
    host_cpl3 = 0;
    host_user_lo = NULL;
    host_user_hi = NULL;
    host_range_refused = 0;
    stat_fail_on = NULL;
    stat_fail_rc = OS32_ERR_IO;
    stat_calls = 0;
    resolve_cwd = "";
    fail_column_ptr = 0;
    fake_errcode = 0;
    sqlite3_hard_heap_limit64(0);
    resolve_owner = current_owner = 2;
    fixture_init();
}

static void canary_check(const char *where)
{
    int i;
    for (i = 0; i < SHM_CANARY; i++) {
        if (test_shm[DB_SHM_BLOCK_SIZE + i] != 0) {
            fprintf(stderr, "FAIL %s: wrote %d bytes past the 16KB block\n",
                    where, i + 1);
            exit(1);
        }
    }
}

/* 使い捨ての DB を legacy db_open (CREATE 付き) で作る。 */
static void make_db(const char *path, const char *ddl)
{
    int h = kapi_db_open(path);
    CHECK(h >= 0);
    CHECK(kapi_db_exec(h, ddl) == 0);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 1. RO / RW の no-create と open 前の検査 -------------------------- */
static void open_existing(void)
{
    int h;

    /* 欠損 DB は RO / RW とも失敗し、**作られない** */
    CHECK(kapi_db_open_existing("/nosuch.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    CHECK(fixture_find("/nosuch.db", 0) == NULL);
    CHECK(kapi_db_open_existing("/nosuch.db", 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    CHECK(fixture_find("/nosuch.db", 0) == NULL);
    CHECK(fixture_find("/nosuch.db-journal", 0) == NULL);

    /* 0 バイトのファイルは NOTADB。SQLite を呼ばないので中身は変わらない。 */
    CHECK(fixture_find("/empty.db", 1) != NULL);
    CHECK(kapi_db_open_existing("/empty.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_NOTADB);
    CHECK(fixture_find("/empty.db", 0)->size == 0);

    /* hot journal があれば RO / RW とも BUSY_RECOVERY。journal は消えない。 */
    make_db("/real.db", "CREATE TABLE t(x)");
    CHECK(fixture_create(NULL, "/real.db-journal", "hot", 3) == VFS_OK);
    CHECK(kapi_db_open_existing("/real.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_BUSY_RECOVERY);
    CHECK(kapi_db_open_existing("/real.db", 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_BUSY_RECOVERY);
    CHECK(fixture_find("/real.db-journal", 0) != NULL);
    CHECK(fixture_find("/real.db-journal", 0)->size == 3);
    CHECK(fixture_rm("/real.db-journal") == VFS_OK);

    /* 引数の拒否 */
    CHECK(kapi_db_open_existing("/real.db", 2) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_MISUSE);
    CHECK(kapi_db_open_existing("", 0) == -1);
    CHECK(kapi_db_open_existing(":memory:", 0) == -1);
    CHECK(kapi_db_open_existing(":MEMORY:", 0) == -1);
    CHECK(kapi_db_open_existing("file:/real.db", 0) == -1);
    CHECK(kapi_db_open_existing(NULL, 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_MISUSE);

    /* 正常な RO / RW open。成功で「直前 open 失敗」は 0 に戻る。 */
    h = kapi_db_open_existing("/real.db", 0);
    CHECK(h >= 0);
    CHECK(kapi_db_error_code(-1) == SQLITE_OK);
    CHECK(kapi_db_error_code(h) == SQLITE_OK);
    /* RO 接続では書けない */
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(1)") == -1);
    CHECK(kapi_db_error_code(h) != SQLITE_OK);
    CHECK(kapi_db_close(h) == 0);

    h = kapi_db_open_existing("/real.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(1)") == 0);
    CHECK(kapi_db_error_code(h) == SQLITE_OK);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 2. prepare_only は step しない ------------------------------------ */
static void prepare_only(void)
{
    static char big[DB_SQL_MAX_BYTES + 64];
    int h;

    make_db("/p.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/p.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(7)") == 0);

    /* SELECT: prepare_only では先頭行が進まない (legacy prepare とは違う) */
    CHECK(kapi_db_prepare_only(h, "SELECT x FROM t") == 0);
    CHECK(((DB_ResultHeader *)test_shm)->status != DB_STATUS_ROW);
    CHECK(kapi_db_column_int(h, 0) == 0);      /* まだ行は無い */
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(kapi_db_column_int(h, 0) == 7);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);

    /* DML: prepare_only では実行されない */
    CHECK(kapi_db_prepare_only(h, "INSERT INTO t VALUES(8)") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT count(*) FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(kapi_db_column_int(h, 0) == 1);      /* 8 は入っていない */
    CHECK(kapi_db_finalize(h) == 0);

    /* 複数 statement は拒否、末尾の空白 / コメント / ; は可 */
    CHECK(kapi_db_prepare_only(h, "SELECT 1; SELECT 2") == -1);
    CHECK(kapi_db_error_code(h) == SQLITE_MISUSE);
    CHECK(kapi_db_prepare_only(h, "SELECT 1;  ") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT 1 -- tail") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT 1 /* tail */") == 0);
    CHECK(kapi_db_prepare_only(h, "   ") == -1);
    CHECK(kapi_db_prepare_only(h, "") == -1);

    /* NUL 込み 1024B ちょうどは通り、1 バイト超は**切り捨てず**拒否 */
    memset(big, ' ', sizeof(big));
    memcpy(big, "SELECT 1", 8);
    big[DB_SQL_MAX_BYTES - 1] = '\0';
    CHECK(kapi_db_prepare_only(h, big) == 0);
    big[DB_SQL_MAX_BYTES - 1] = ' ';
    big[DB_SQL_MAX_BYTES] = '\0';
    CHECK(kapi_db_prepare_only(h, big) == -1);
    CHECK(kapi_db_error_code(h) == SQLITE_MISUSE);

    CHECK(kapi_db_close(h) == 0);
}

/* ---- 3. bind の上限と往復 --------------------------------------------- */
static void binds(void)
{
    static unsigned char blob[DB_BIND_BLOB_MAX + 1];
    static char text[DB_BIND_TEXT_MAX + 2];
    const unsigned char *got;
    int h, i;

    for (i = 0; i < (int)sizeof(blob); i++) blob[i] = (unsigned char)(i & 0xFF);
    memset(text, 'x', sizeof(text));

    make_db("/b.db", "CREATE TABLE t(a,b,c)");
    h = kapi_db_open_existing("/b.db", 1);
    CHECK(h >= 0);

    /* prepare の前 / step の後は bind できない */
    CHECK(kapi_db_bind_int(h, 1, 1) == -1);
    CHECK(kapi_db_error_code(h) == SQLITE_MISUSE);

    CHECK(kapi_db_prepare_only(h, "INSERT INTO t VALUES(?,?,?)") == 0);
    CHECK(kapi_db_bind_int(h, 0, 1) == -1);             /* 1-based */
    CHECK(kapi_db_error_code(h) == SQLITE_RANGE);
    CHECK(kapi_db_bind_int(h, 4, 1) == -1);
    CHECK(kapi_db_error_code(h) == SQLITE_RANGE);
    CHECK(kapi_db_bind_text(h, 1, NULL, 0) == -1);      /* NULL は bind_null */
    CHECK(kapi_db_bind_text(h, 1, text, -1) == -1);     /* 負の長さ */
    CHECK(kapi_db_bind_text(h, 1, text, DB_BIND_TEXT_MAX + 1) == -1);
    CHECK(kapi_db_bind_blob(h, 3, blob, DB_BIND_BLOB_MAX + 1) == -1);
    CHECK(kapi_db_bind_blob(h, 3, NULL, 4) == -1);

    CHECK(kapi_db_bind_text(h, 1, text, DB_BIND_TEXT_MAX) == 0);
    CHECK(kapi_db_bind_int(h, 2, -12345) == 0);
    CHECK(kapi_db_bind_blob(h, 3, blob, DB_BIND_BLOB_MAX) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);
    CHECK(kapi_db_bind_int(h, 2, 1) == -1);             /* step の後は不可 */

    /* 0B の text / blob は NULL ではない空値 */
    CHECK(kapi_db_prepare_only(h, "INSERT INTO t VALUES(?,?,?)") == 0);
    CHECK(kapi_db_bind_text(h, 1, text, 0) == 0);
    CHECK(kapi_db_bind_null(h, 2) == 0);
    CHECK(kapi_db_bind_blob(h, 3, blob, 0) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);

    CHECK(kapi_db_prepare_only(h,
        "SELECT typeof(a), typeof(b), typeof(c) FROM t WHERE length(a)=0") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(!strcmp(kapi_db_column_text(h, 0), "text"));
    CHECK(!strcmp(kapi_db_column_text(h, 1), "null"));
    CHECK(!strcmp(kapi_db_column_text(h, 2), "blob"));
    CHECK(kapi_db_finalize(h) == 0);

    /* 4096B blob の往復 (SQLITE_TRANSIENT なのでスクラッチを上書きしてよい) */
    memset(blob_copy_buf, 0, sizeof(blob_copy_buf));
    CHECK(kapi_db_prepare_only(h,
        "SELECT c FROM t WHERE length(c)=?") == 0);
    CHECK(kapi_db_bind_int(h, 1, DB_BIND_BLOB_MAX) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    got = (const unsigned char *)kapi_db_column_text(h, 0);
    for (i = 0; i < DB_BIND_BLOB_MAX; i++) CHECK(got[i] == (unsigned char)(i & 0xFF));
    CHECK(kapi_db_finalize(h) == 0);
    canary_check("binds");

    CHECK(kapi_db_close(h) == 0);
}

/* ---- 4. db_error_code の保持規則 --------------------------------------- */
static void error_code(void)
{
    int h, code;

    CHECK(kapi_db_error_code(-2) == SQLITE_MISUSE);
    CHECK(kapi_db_error_code(DB_MAX_CONNECTIONS) == SQLITE_MISUSE);
    CHECK(kapi_db_error_code(0) == SQLITE_MISUSE);   /* 一度も開いていない */

    make_db("/e.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/e.db", 1);
    CHECK(h >= 0 && kapi_db_error_code(h) == SQLITE_OK);

    /* データ操作の失敗はコードを残し、成功で 0 に戻る */
    CHECK(kapi_db_prepare_only(h, "SELECT * FROM missing") == -1);
    code = kapi_db_error_code(h);
    CHECK(code != SQLITE_OK);
    CHECK(kapi_db_error_code(h) == code);            /* 取得しても消えない */
    CHECK(kapi_db_prepare_only(h, "SELECT 1") == 0);
    CHECK(kapi_db_error_code(h) == SQLITE_OK);

    /* finalize / close は成功しても上書きしない */
    CHECK(kapi_db_prepare_only(h, "SELECT * FROM missing") == -1);
    code = kapi_db_error_code(h);
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_error_code(h) == code);
    CHECK(kapi_db_close(h) == 0);
    CHECK(kapi_db_error_code(h) == code);            /* close 後も再利用まで */

    /* slot を再利用すると新しい接続の状態になる */
    CHECK(kapi_db_open_existing("/e.db", 0) == h);
    CHECK(kapi_db_error_code(h) == SQLITE_OK);
    CHECK(kapi_db_close(h) == 0);

    /* owner 別の欄。別の owner の失敗は混ざらない。 */
    resolve_owner = current_owner = 2;
    CHECK(kapi_db_open_existing("/nosuch.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    resolve_owner = current_owner = 3;
    CHECK(kapi_db_error_code(-1) == SQLITE_OK);
    CHECK(kapi_db_open_existing("/e.db", 2) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_MISUSE);
    resolve_owner = current_owner = 2;
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    current_owner = DB_OWNER_SLOTS;                  /* 池の外 */
    CHECK(kapi_db_error_code(-1) == SQLITE_MISUSE);
    resolve_owner = current_owner = 2;
}

/* ---- 4b. ブート自己診断 (N2 (c): make check で slot 番号の整合を踏む) ----
 * db_v50_selftest() は KAPI_SLOT_* の位置と SHM 境界などを検査する骨。
 * F1 で slot 件数を数値直書きしていたため v51 の末尾追記で bit0 が立ち、
 * 実機の kselftest が 86/1 に落ちた。ここで 0 を確かめれば make check で
 * 同じ崩れを踏む。*/
static void v50_selftest(void)
{
    /* db_v50_selftest の (2) は 0xFFFFFF00 が帯外であることに依るので、
     * ホストでは CPL=3 の帯検査を有効にする (32bit の overflow はホストの
     * 64bit 幅では起きないため。user_range ケースと同じ帯 [BAND_LO,BAND_HI))。*/
    host_cpl3 = 1;
    CHECK(db_v50_selftest() == 0);
    host_cpl3 = 0;
}

/* ---- 4c. **KAPI をもう 1 本足しても落ちないこと** (票 H3 の是正、2026-09-15)
 *
 * 以前 db_v50_selftest() の (0) は
 *     KAPI_SLOT_COUNT != KAPI_SLOT_HOST_CLOSE + 1
 * と書いてあり、「host_close の後ろに 1 本も足されていないこと」を要求して
 * いた。[ABI2] は末尾追記を正当な操作と定めているので、これは **KAPI を
 * 1 本足すたびに必ず落ちる**。実際 v52 の `sys_set_mtime` (slot 213) で
 * make check が落ちた。
 *
 * この項の意図は「追記した 7 本が 201..207 に居て既存の db_* が動いて
 * いない」であって、表がそこで終わっていることではない。判定を
 * db_slot_layout_ok(slot_count) に切り出してあるので、**未来の追記を
 * 引数で模して**ここで踏める。次の KAPI 追加で同じ罠を踏まないための項。 */
static void slot_layout_append(void)
{
    /* いまの表 */
    CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT));
    /* 末尾に 1 本 / 10 本 / 100 本足した「未来の KAPI」 */
    CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT + 1));
    CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT + 10));
    CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT + 100));
    /* 既知の末尾 slot を含む長さは最低限必要 (下限は見る) */
    CHECK(db_slot_layout_ok(KAPI_SLOT_HOST_CLOSE + 1));
    CHECK(!db_slot_layout_ok(KAPI_SLOT_HOST_CLOSE));
    CHECK(!db_slot_layout_ok(0));
    CHECK(!db_slot_layout_ok(-1));
    /* 表そのものが実際に host_close より長いこと (生成物との突き合わせ) */
    CHECK(KAPI_SLOT_COUNT > KAPI_SLOT_HOST_CLOSE);
}

/* ---- 5. SHM の境界 (票 §1b) -------------------------------------------- */
static void shm_bound(void)
{
    u32 hdr = (u32)sizeof(DB_ResultHeader);
    u32 desc = (u32)sizeof(DB_ColumnInfo);
    int h;

    /* 純関数の側。上限は**結果側の上限** — ブロック 0 の末尾は
     * db_last_error() の診断領域 (票 TASK_DB_ERRSTR)。 */
    CHECK(shm_row_fits_n(1, DB_SHM_RESULT_LIMIT - hdr - desc));
    CHECK(!shm_row_fits_n(1, DB_SHM_RESULT_LIMIT - hdr - desc + 1));
    CHECK(!shm_row_fits_n((int)((DB_SHM_RESULT_LIMIT - hdr) / desc) + 1, 0));
    CHECK(!shm_row_fits_n(-1, 0));

    /* 実接続: 16KB に収まらない 1 行は -1、SHM の外へ 1 バイトも書かない */
    make_db("/s.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/s.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(zeroblob(20000))") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT x FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    CHECK(kapi_db_error_code(h) == SQLITE_TOOBIG);
    CHECK(((DB_ResultHeader *)test_shm)->status == DB_STATUS_ERROR);
    CHECK(((DB_ResultHeader *)test_shm)->column_count == 0);
    canary_check("shm_bound");
    CHECK(kapi_db_finalize(h) == 0);

    /* 列数だけで descriptor 領域を溢れさせても同じ (部分 ROW を返さない) */
    CHECK(kapi_db_prepare_only(h,
        "SELECT 1,2,3,4,5,6,7,8,9,10 FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);     /* 10 列は収まる */
    canary_check("shm_bound cols");
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 6. CPL=3 のポインタ検証 ------------------------------------------- */
static void user_range(void)
{
    int h;
    char *inband = (char *)malloc(64);

    CHECK(inband != NULL);
    memset(inband, 'q', 64);

    /* CPL=0 の呼び手は帯を見ない (NULL と長さだけ) */
    host_cpl3 = 0;
    CHECK(db_user_range_ok(inband, 8));
    CHECK(!db_user_range_ok(NULL, 1));
    /* 加算 overflow。include/types.h の u32 はホストでは 64bit (unsigned long)
     * なので、ホスト幅の端で同じ経路を踏む。 */
    CHECK(!db_user_range_ok((const void *)~(u32)0xFF, 0x200u));

    /* CPL=3: 帯の外、帯の末尾をまたぐ、ガードをまたぐ、overflow */
    host_cpl3 = 1;
    CHECK(db_user_range_ok((const void *)BAND_LO, 16));
    CHECK(!db_user_range_ok((const void *)(BAND_LO - 1), 2));
    CHECK(!db_user_range_ok((const void *)(BAND_HI - 1), 2));
    CHECK(db_user_range_ok((const void *)(BAND_HI - 2), 2));
    /* 帯の中は PTE を見ないので通る。実機ではここを写すと #PF → 呼び手を
     * kill (票 §1a の改定、2026-09-13)。「-1 が返る」とは主張しない。 */
    CHECK(db_user_range_ok((const void *)(GUARD_PAGE - 1), 2));
    CHECK(db_user_range_ok((const void *)(GUARD_PAGE + 8), 2));
    CHECK(!db_user_range_ok((const void *)~(u32)0x0F, 0x20u));
    CHECK(!db_user_range_ok(NULL, 0));
    host_cpl3 = 0;

    /* NUL の無い path は上限まで探して拒否 (切り捨てて開かない) */
    {
        static char no_nul[OS32_MAX_PATH + 64];
        memset(no_nul, 'a', sizeof(no_nul));
        CHECK(kapi_db_open_existing(no_nul, 0) == -1);
        CHECK(kapi_db_error_code(-1) == SQLITE_MISUSE);
    }

    /* 検証を通った text はカーネルのスクラッチへ写る (SQLite は呼び手の
     * ポインタを持たない) — 写した後にユーザ側を書き換えても値は変わらない。*/
    make_db("/u.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/u.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_prepare_only(h, "INSERT INTO t VALUES(?)") == 0);
    memcpy(inband, "keep", 5);
    CHECK(kapi_db_bind_text(h, 1, inband, 4) == 0);
    memcpy(inband, "GONE", 4);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);
    CHECK(kapi_db_prepare_only(h, "SELECT x FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(!strcmp(kapi_db_column_text(h, 0), "keep"));
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_close(h) == 0);
    free(inband);
}

/* ---- 7. 回収順序 (票 §1c) ---------------------------------------------- */
/*  DB 接続の main / journal FD は汎用 FD (kapi_db.c は既定 VFS を使う) なので、
 *  vfs_close_owned が先に走ると SQLite は死んだ FD 越しに rollback / close を
 *  投げることになる。新しい並び (DB → FD) と古い並び (FD → DB) を両方走らせ、
 *  観測できる差を assert する。                                             */
static void reclaim_order(const char *mode)
{
    static unsigned char before[FIXTURE_BYTES];
    u32 before_size;
    int h, live, i, db_probes;

    make_db("/r.db", "CREATE TABLE t(x)");
    resolve_owner = current_owner = 4;
    h = kapi_db_open_existing("/r.db", 1);
    CHECK(h >= 0);
    /* ページキャッシュを小さくして **本体ファイルへの spill** を起こす。
     * spill すると rollback は journal を読み戻さなければならない = 後始末に
     * 生きた FD が要る。キャッシュに収まる小さな transaction では I/O が
     * 起きず、順序の違いが観測できない。 */
    CHECK(kapi_db_exec(h, "PRAGMA cache_size = 2") == 0);
    before_size = fixture_find("/r.db", 0)->size;
    memcpy(before, fixture_find("/r.db", 0)->data, before_size);
    CHECK(kapi_db_exec(h, "BEGIN") == 0);
    for (i = 0; i < 60; i++)
        CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(zeroblob(512))") == 0);
    /* 未 commit なので hot journal が出来ている = rollback に FD が要る */
    CHECK(fixture_find("/r.db-journal", 0) != NULL);
    live = 0;
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) if (open_files[i].in_use) live++;
    CHECK(live >= 2);

    if (!strcmp(mode, "new")) {
        db_probes = probes;
        db_cleanup_owned(4);            /* 票 §1c の並び: DB が先 */
        db_probes = probes - db_probes;
        vfs_close_owned(4);
    } else {
        vfs_close_owned(4);             /* 旧: FD が先 */
        live = 0;
        for (i = 3; i < VFS_MAX_OPEN_FILES; i++) if (open_files[i].in_use) live++;
        CHECK(live == 0);               /* 後始末の前に FD が消えている */
        db_probes = probes;
        db_cleanup_owned(4);
        db_probes = probes - db_probes;
    }
    printf("ORDER %s: backend calls during db teardown = %d\n", mode, db_probes);

    printf("ORDER %s: slot in_use=%d isolated=%d last_error=%d journal=%s\n",
           mode, db_slots[h].in_use, db_slots[h].isolated,
           db_slots[h].last_error,
           fixture_find("/r.db-journal", 0) ? "left" : "gone");
    /* 参考値: 本体サイズは **どちらの並びでも縮まない**。os32 SQLite VFS の
     * xTruncate がまだ no-op 成功だから (票 F3a)。ここでは判定に使わない。 */
    printf("ORDER %s: db size before=%u after=%u head_same=%d\n", mode,
           (unsigned)before_size, (unsigned)fixture_find("/r.db", 0)->size,
           memcmp(before, fixture_find("/r.db", 0)->data, before_size) == 0);
    live = 0;
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) if (open_files[i].in_use) live++;
    printf("ORDER %s: fds left=%d\n", mode, live);

    if (!strcmp(mode, "new")) {
        /* DB が先なら rollback も close も **生きた FD の上**で終わる。
         * 観測点は「後始末がバックエンドに届いた回数」— journal の読み戻しは
         * FD 越しにしか起きない。 */
        CHECK(!db_slots[h].in_use && !db_slots[h].isolated);
        CHECK(fixture_find("/r.db-journal", 0) == NULL);
        CHECK(live == 0);
        CHECK(db_probes >= 10);
    } else {
        /* 旧い並びでは FD が先に消えるので、rollback の I/O は
         * バックエンドに 1 度も届かない (path で引く journal の削除だけ)。
         * **戻り値は成功のまま**なのがこの並びの怖さ — os32 SQLite VFS は
         * まだ I/O 失敗を握り潰す (票 F3a は未実施)。だから「成功したか」では
         * なく「後始末が届いたか」で見る。 */
        CHECK(db_probes <= 3);
    }
    resolve_owner = current_owner = 2;
}

/* ---- 8. FEP のような protect 付き FD / 他 owner を巻き込まない ---------- */
static void owner_isolation(void)
{
    int parent, child;

    make_db("/p1.db", "CREATE TABLE t(x)");
    make_db("/c1.db", "CREATE TABLE t(x)");
    resolve_owner = current_owner = 1;
    parent = kapi_db_open_existing("/p1.db", 1);
    CHECK(parent >= 0);
    CHECK(kapi_db_prepare_only(parent, "SELECT 1") == 0);
    resolve_owner = current_owner = 3;
    child = kapi_db_open_existing("/c1.db", 1);
    CHECK(child >= 0 && child != parent);

    db_cleanup_owned(3);
    CHECK(!db_slots[child].in_use);
    CHECK(db_slots[parent].in_use);
    CHECK(kapi_db_step(parent) == DB_STATUS_ROW);   /* 親は無事 */
    resolve_owner = current_owner = 1;
    CHECK(kapi_db_close(parent) == 0);
    resolve_owner = current_owner = 2;
}

/* ---- 9. SHM に**ちょうど**収まる TEXT / BLOB (実装レビュー K1) ---------- */
/*  事前検査 (shm_row_fits) と writer (shm_write_row) の境界条件がずれると、
 *  事前検査を通った行が payload 無し (data_offset = 0) の ROW になり、
 *  しかも診断は成功のまま = 欠落に気付けない。1 列の行で両端を踏む。      */
static void shm_exact(void)
{
    int room = (int)DB_SHM_RESULT_LIMIT - (int)sizeof(DB_ResultHeader)
               - (int)sizeof(DB_ColumnInfo);
    DB_ColumnInfo *info = (DB_ColumnInfo *)(test_shm + sizeof(DB_ResultHeader));
    char sql[128];
    int h, i;

    make_db("/x.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/x.db", 1);
    CHECK(h >= 0);

    /* TEXT: 書ける最大は room - 1 (終端 NUL の分)。ちょうどは **書ける**。 */
    sprintf(sql, "SELECT substr(hex(zeroblob(%d)), 1, %d)", room, room - 1);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(info->type == DB_TYPE_TEXT);
    CHECK((int)info->length == room - 1);
    CHECK(info->data_offset != 0);                  /* 欠落 ROW ではない */
    CHECK((int)strlen(kapi_db_column_text(h, 0)) == room - 1);
    CHECK(kapi_db_error_code(h) == SQLITE_OK);
    canary_check("shm_exact text fit");
    CHECK(kapi_db_finalize(h) == 0);

    /* TEXT: +1 バイトは事前検査で落ちる (部分 ROW を返さない)。 */
    sprintf(sql, "SELECT substr(hex(zeroblob(%d)), 1, %d)", room + 2, room);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    CHECK(kapi_db_error_code(h) == SQLITE_TOOBIG);
    canary_check("shm_exact text over");
    CHECK(kapi_db_finalize(h) == 0);

    /* BLOB: 書ける最大は room ちょうど (終端不要)。 */
    sprintf(sql, "SELECT zeroblob(%d)", room);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(info->type == DB_TYPE_BLOB);
    CHECK((int)info->length == room);
    CHECK(info->data_offset != 0);
    for (i = 0; i < room; i++)
        CHECK(test_shm[info->data_offset + i] == 0);
    canary_check("shm_exact blob fit");
    CHECK(kapi_db_finalize(h) == 0);

    /* BLOB: +1 は拒否。 */
    sprintf(sql, "SELECT zeroblob(%d)", room + 1);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    CHECK(kapi_db_error_code(h) == SQLITE_TOOBIG);
    canary_check("shm_exact blob over");
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 10. stat の I/O 障害を不存在扱いしない (実装レビュー K2) ----------- */
static void stat_faults(void)
{
    int before;

    make_db("/f.db", "CREATE TABLE t(x)");

    /* journal の stat が I/O で落ちたら「journal 無し」と言い切れない。
     * SQLite を呼ばずに IOERR で断る (バックエンドへの I/O が増えない)。 */
    stat_fail_on = "-journal";
    before = stat_calls;
    CHECK(kapi_db_open_existing("/f.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_IOERR);
    CHECK(stat_calls == before + 2);       /* 本体 + journal の stat だけ */
    CHECK(kapi_db_open_existing("/f.db", 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_IOERR);

    /* 本体の stat も同じ: NOTFOUND だけが「不存在」。 */
    stat_fail_on = "/f.db";
    before = stat_calls;
    CHECK(kapi_db_open_existing("/f.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_IOERR);
    CHECK(stat_calls == before + 1);
    stat_fail_rc = OS32_ERR_NOTFOUND;
    CHECK(kapi_db_open_existing("/f.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);   /* 確定した不存在 */

    /* 障害を外せば普通に開く */
    stat_fail_on = NULL;
    CHECK(kapi_db_open_existing("/f.db", 0) >= 0);
}

/* ---- 11. RW open の失敗を一律 CANTOPEN にしない (実装レビュー K3) ------- */
static void journal_mode(void)
{
    static char junk[2048];
    int h;

    /* 非空だが DB ではないファイル。sqlite3_open_v2 は成功し、schema を読む
     * `PRAGMA journal_mode` が SQLITE_NOTADB で落ちる。 */
    memset(junk, 'Z', sizeof(junk));
    CHECK(fixture_create(NULL, "/junk.db", junk, sizeof(junk)) == VFS_OK);
    CHECK(kapi_db_open_existing("/junk.db", 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_NOTADB);     /* CANTOPEN ではない */
    CHECK(kapi_db_error_code(0) == SQLITE_NOTADB);      /* slot にも残る */

    /* 読めるが journal_mode の照会が I/O で落ちる場合も NOTADB / IOERR 系で、
     * 「照会は通ったが DELETE でない」だけが CANTOPEN。 */
    make_db("/g.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/g.db", 1);
    CHECK(h >= 0);
    CHECK(db_journal_mode_check(db_slots[h].db) == SQLITE_OK);
    /* 照会は通るが DELETE ではない → CANTOPEN (照会の失敗コードと別物)。
     * 接続の journal_mode を明示的に変えて、その枝だけを踏む。 */
    CHECK(kapi_db_exec(h, "PRAGMA journal_mode=MEMORY") == 0);
    CHECK(db_journal_mode_check(db_slots[h].db) == SQLITE_CANTOPEN);
    CHECK(kapi_db_exec(h, "PRAGMA journal_mode=DELETE") == 0);
    CHECK(db_journal_mode_check(db_slots[h].db) == SQLITE_OK);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 12. stmt 無しの step が診断を 0 に戻す (実装レビュー K4) ----------- */
static void step_no_stmt(void)
{
    int h;

    make_db("/n.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/n.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_prepare_only(h, "SELECT * FROM missing") == -1);
    CHECK(kapi_db_error_code(h) != SQLITE_OK);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);          /* stmt が無い */
    CHECK(kapi_db_error_code(h) == SQLITE_OK);         /* 成功したので 0 */
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 13. 下位層の path 容量 (実装レビュー K5) --------------------------- */
/*  vfs_resolve_path は VFS_MAX_PATH (256B) の作業バッファで正規化するので、
 *  `<path>-journal` がそこに収まらない path では journal の stat が **本体**
 *  に当たり、健全な DB が BUSY_RECOVERY に見える。open の前に断ること。     */
static void path_len(void)
{
    static char longp[VFS_MAX_PATH + 16];
    u32 room = (u32)VFS_MAX_PATH - 1u - 8u;    /* NUL と "-journal" の分 */
    int h;

    /* ちょうど収まる長さ (247B) は開ける */
    memset(longp, 'a', sizeof(longp));
    longp[0] = '/';
    longp[room] = '\0';
    CHECK(strlen(longp) == room);
    make_db(longp, "CREATE TABLE t(x)");
    h = kapi_db_open_existing(longp, 0);
    CHECK(h >= 0);
    CHECK(kapi_db_close(h) == 0);

    /* 1 バイト長いと journal 名が下位層に収まらない → CANTOPEN。
     * このとき **本体は存在する** ので、旧実装は BUSY_RECOVERY を返していた。 */
    longp[room] = 'a';
    longp[room + 1] = '\0';
    /* 本体は fixture に直接置く。legacy db_open で作ると、SQLite が開く
     * journal 名 (256B) を VFS が NAMETOOLONG で断るので書けない
     * (票 TASK_VFS_FD_PATH 以降。以前は切り詰めて本体と衝突していた)。 */
    CHECK(fixture_create(NULL, longp, "db", 2) == VFS_OK);
    CHECK(kapi_db_open_existing(longp, 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    CHECK(kapi_db_open_existing(longp, 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);

    /* kapi_db_open (作成もする経路) も**開く時点で**断る (Opus 実装レビュー
     * ラリー 1 の nb2)。以前は本体だけ開けて、最初の書き込みでジャーナルを
     * 開けない「読めるのに書けない」接続を返していた */
    CHECK(kapi_db_open(longp) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    /* 下の SQLite VFS (xOpen) も同じ長さで断る (IME 辞書などの直の接続) */
    {
        sqlite3 *db = 0;
        CHECK(sqlite3_open_v2(longp, &db, SQLITE_OPEN_READWRITE, 0) ==
              SQLITE_CANTOPEN);
        if (db) sqlite3_close(db);
    }
    /* ちょうど収まる長さは kapi_db_open でも開けて書ける */
    longp[room] = '\0';
    h = kapi_db_open(longp);
    CHECK(h >= 0);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 15. SQLITE_TRANSIENT: スクラッチを上書きしても値が残る ------------- */
static void transient(void)
{
    int h;

    make_db("/tr.db", "CREATE TABLE t(a,b)");
    h = kapi_db_open_existing("/tr.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_prepare_only(h, "INSERT INTO t VALUES(?,?)") == 0);
    /* 2 本の bind は **同じ** text_copy_buf / blob_copy_buf を使う。
     * SQLITE_TRANSIENT なので 1 本目の値は SQLite 側に写っていること。 */
    CHECK(kapi_db_bind_text(h, 1, "first", 5) == 0);
    CHECK(kapi_db_bind_text(h, 2, "second", 6) == 0);
    CHECK(!memcmp(text_copy_buf, "second", 6));   /* スクラッチは上書き済み */
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);

    CHECK(kapi_db_prepare_only(h, "SELECT a, b FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(!strcmp(kapi_db_column_text(h, 0), "first"));
    CHECK(!strcmp(kapi_db_column_text(h, 1), "second"));
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 16. 末尾判定は SQLite と 1 対 1 か (往復 2 の B1) ------------------ */
/*  自前の空白 / コメント表は必ずずれる。`\v` (トークナイザの続き走査は
 *  sqlite3Isspace = 6 文字)、UTF-8 BOM (TK_SPACE)、閉じていないブロック
 *  コメント (SQLite は空白にしない) の 3 つが反例。SQLite 自身に
 *  末尾をもう一度 prepare させれば、判定は定義上 1 対 1 になる。           */
static void sql_tail_sqlite(void)
{
    int h;

    make_db("/ts.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/ts.db", 1);
    CHECK(h >= 0);

    /* SQLite が受理する末尾は wrap も受理する */
    CHECK(kapi_db_prepare_only(h, "SELECT 1; \v") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT 1;\357\273\277") == 0);   /* UTF-8 BOM */
    CHECK(kapi_db_prepare_only(h, "SELECT 1; -- comment") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT 1; /* c */") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT 1;;") == 0);            /* 空 statement */
    CHECK(kapi_db_prepare_only(h, "SELECT 1;\f\r\n\t ") == 0);

    /* SQLite が読めない末尾は拒否する (閉じていないブロックコメント) */
    CHECK(kapi_db_prepare_only(h, "SELECT 1;/*") == -1);
    CHECK(kapi_db_error_code(h) != SQLITE_OK);
    /* 2 本目の statement は拒否 */
    CHECK(kapi_db_prepare_only(h, "SELECT 1; SELECT 2") == -1);
    CHECK(kapi_db_error_code(h) == SQLITE_MISUSE);
    /* 末尾が \v で**始まる**と SQLite のトークナイザは TK_ILLEGAL にする
     * (CC_SPACE の run に入ってからの続きだけが sqlite3Isspace = \v を含む)。
     * 自前の表ではこの非対称を写せない。拒否理由のコードは SQLite のもの。 */
    CHECK(kapi_db_prepare_only(h, "SELECT 1;\v SELECT 2") == -1);
    CHECK(kapi_db_error_code(h) != SQLITE_OK);
    CHECK(kapi_db_error_code(h) != SQLITE_MISUSE);
    CHECK(kapi_db_prepare_only(h, "SELECT 1; \v SELECT 2") == -1);
    CHECK(kapi_db_error_code(h) == SQLITE_MISUSE);
    CHECK(kapi_db_prepare_only(h, "SELECT 1;\v") == -1);   /* 先頭 \v は不可 */
    CHECK(kapi_db_prepare_only(h, "SELECT 1; \v") == 0);   /* 空白の続きなら可 */

    /* 判定は本体の prepare と同じ接続で行う = SQLite の規則そのもの。
     * 拒否したあとに stmt が残っていないこと (B2 と同じ規則)。 */
    CHECK(kapi_db_prepare_only(h, "SELECT 1; SELECT 2") == -1);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 17. 拒否した prepare が旧 stmt を残さない (往復 2 の B2) ----------- */
static void prepare_replaces(void)
{
    static char big[DB_SQL_MAX_BYTES + 64];
    int h, mode;

    make_db("/pr.db", "CREATE TABLE t(x)");
    memset(big, ' ', sizeof(big));
    memcpy(big, "SELECT 1", 8);
    big[DB_SQL_MAX_BYTES] = '\0';        /* NUL 込み 1025B = 上限超過 */

    for (mode = 0; mode < 3; mode++) {
        h = kapi_db_open_existing("/pr.db", 1);
        CHECK(h >= 0);
        CHECK(kapi_db_exec(h, "DELETE FROM t") == 0);

        /* 積んで bind まで済ませた INSERT がある状態で… */
        CHECK(kapi_db_prepare_only(h, "INSERT INTO t VALUES(?)") == 0);
        CHECK(kapi_db_bind_int(h, 1, 42) == 0);

        /* …引数検証で弾かれる prepare を出す (空 / 上限超過 / NULL)。 */
        if (mode == 0) CHECK(kapi_db_prepare_only(h, "") == -1);
        else if (mode == 1) CHECK(kapi_db_prepare_only(h, big) == -1);
        else CHECK(kapi_db_prepare_only(h, (const char *)0) == -1);
        CHECK(kapi_db_error_code(h) == SQLITE_MISUSE);

        /* 旧 stmt は捨てられているので step は何も実行しない。 */
        CHECK(kapi_db_step(h) == DB_STATUS_DONE);
        CHECK(kapi_db_bind_int(h, 1, 1) == -1);      /* bind もできない */

        CHECK(kapi_db_prepare_only(h, "SELECT count(*) FROM t") == 0);
        CHECK(kapi_db_step(h) == DB_STATUS_ROW);
        CHECK(kapi_db_column_int(h, 0) == 0);        /* INSERT は走っていない */
        CHECK(kapi_db_finalize(h) == 0);
        CHECK(kapi_db_close(h) == 0);
    }

    /* 単一 statement の拒否 (tail あり) でも同じ */
    h = kapi_db_open_existing("/pr.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_prepare_only(h, "INSERT INTO t VALUES(?)") == 0);
    CHECK(kapi_db_bind_int(h, 1, 7) == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT 1; SELECT 2") == -1);
    CHECK(kapi_db_step(h) == DB_STATUS_DONE);
    CHECK(kapi_db_prepare_only(h, "SELECT count(*) FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(kapi_db_column_int(h, 0) == 0);
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 18. 実体化の失敗を ROW にしない (往復 2 の B3) --------------------- */
/*  16KB に収まる長さでも、SQLite が値を組み立てるメモリを取れなければ
 *  accessor は NULL を返す。長さだけ見て書くと data_offset = 0 の
 *  「成功した欠落 ROW」になる。hard heap limit で確保を失敗させて踏む。   */
static void materialize_fail(void)
{
    int room = (int)DB_SHM_RESULT_LIMIT - (int)sizeof(DB_ResultHeader)
               - (int)sizeof(DB_ColumnInfo);
    DB_ColumnInfo *info = (DB_ColumnInfo *)(test_shm + sizeof(DB_ResultHeader));
    char sql[128];
    sqlite3_int64 used;
    int h, code;

    make_db("/m.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("/m.db", 1);
    CHECK(h >= 0);

    /* (a) 対照: 限界なしなら「収まる BLOB」はちゃんと取れる。 */
    sprintf(sql, "SELECT zeroblob(%d)", room);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(info->data_offset != 0);
    CHECK(kapi_db_finalize(h) == 0);

    /* (b) **本命**: step は通ったのに accessor が値を返せない形。
     * 長さ (sqlite3_column_bytes) は生きているので、長さだけを見る実装は
     * 「収まる行」と判断し、payload を書かないまま ROW を成功として返す。 */
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(zeroblob(100))") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT x FROM t") == 0);
    memset(test_shm, 0, sizeof(test_shm));
    fail_column_ptr = 1;
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    fail_column_ptr = 0;
    code = kapi_db_error_code(h);
    printf("MATERIALIZE ptr code=%d status=%d cols=%d stmt=%d\n", code,
           (int)((DB_ResultHeader *)test_shm)->status,
           (int)((DB_ResultHeader *)test_shm)->column_count,
           db_slots[h].active_stmt != 0);
    CHECK(code == SQLITE_NOMEM);   /* 「値が取れなかった」の説明になる */
    /* 部分 ROW を公開していない。stmt は生きたまま (呼び手が finalize する)。 */
    CHECK(((DB_ResultHeader *)test_shm)->status == DB_STATUS_ERROR);
    CHECK(((DB_ResultHeader *)test_shm)->column_count == 0);
    CHECK(db_slots[h].active_stmt != 0);
    canary_check("materialize_fail ptr");
    CHECK(kapi_db_finalize(h) == 0);

    /* (b2) TEXT 側も同じ規則 (accessor が値を返せない)。 */
    CHECK(kapi_db_exec(h, "DELETE FROM t") == 0);
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES('abcdefghij')") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT x FROM t") == 0);
    memset(test_shm, 0, sizeof(test_shm));
    fail_column_ptr = 1;
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    fail_column_ptr = 0;
    CHECK(kapi_db_error_code(h) == SQLITE_NOMEM);
    CHECK(((DB_ResultHeader *)test_shm)->status == DB_STATUS_ERROR);
    CHECK(((DB_ResultHeader *)test_shm)->column_count == 0);
    CHECK(kapi_db_finalize(h) == 0);

    /* (b3) 長さが 0 に潰れている枝。`sqlite3_column_bytes` は確保に失敗すると
     * 0 を返すので、ポインタの有無では見分けられない。errcode だけが根拠。
     * 0 バイトの値そのものは**正当**なので、errcode が NOMEM でなければ通す。 */
    CHECK(kapi_db_exec(h, "DELETE FROM t") == 0);
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(x'')") == 0);   /* 0B blob */
    CHECK(kapi_db_prepare_only(h, "SELECT x FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);            /* 0B は ROW でよい */
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT x FROM t") == 0);
    memset(test_shm, 0, sizeof(test_shm));
    fake_errcode = SQLITE_NOMEM;
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    fake_errcode = 0;
    CHECK(kapi_db_error_code(h) == SQLITE_NOMEM);
    CHECK(((DB_ResultHeader *)test_shm)->column_count == 0);
    CHECK(kapi_db_finalize(h) == 0);

    /* (c) 実機に近い側: MEMSYS5 を締めると **step の方が先に**落ちる。
     * どこで落ちても「部分 ROW を返さない」は同じでなければならない。 */
    CHECK(kapi_db_prepare_only(h, "SELECT zeroblob(200000)") == 0);
    used = sqlite3_memory_used();
    sqlite3_hard_heap_limit64(used + 32768);    /* 戻り値は直前の上限 */
    CHECK(sqlite3_hard_heap_limit64(-1) == used + 32768);
    memset(test_shm, 0, sizeof(test_shm));
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    code = kapi_db_error_code(h);
    printf("MATERIALIZE limit code=%d\n", code);
    CHECK(code == SQLITE_NOMEM || (code & 0xFF) == SQLITE_NOMEM);
    CHECK(((DB_ResultHeader *)test_shm)->status == DB_STATUS_ERROR);
    CHECK(((DB_ResultHeader *)test_shm)->column_count == 0);
    canary_check("materialize_fail limit");
    sqlite3_hard_heap_limit64(0);
    CHECK(kapi_db_close(h) == 0);
}

/* ---- 19. journal 名の容量は **解決後** で見る (往復 2 の B4) ------------ */
/*  入力が 4B でも、cwd を連結した解決名は上限まで伸びる。fs/vfs.c は
 *  VFS_MAX_PATH の作業バッファで連結して **切り詰める** ので、cwd が 251B
 *  (末尾 '/' 込み) だと `f.db-journal` の解決名が 255B に切られて
 *  `<cwd>f.db` = 本体とまったく同じ名前になる。相対名の長さだけを見る実装は
 *  健全な DB を BUSY_RECOVERY として断る。                                  */
static void build_cwd(char *cwd, u32 len)
{
    u32 k;
    for (k = 0; k < len; k++) cwd[k] = 'c';
    cwd[0] = '/';
    cwd[len - 1u] = '/';
    cwd[len] = '\0';
}

static void resolve_len(void)
{
    static char cwd[VFS_MAX_PATH + 16];
    static char abs_name[VFS_MAX_PATH + 16];
    u32 cap = (u32)VFS_MAX_PATH - 1u;           /* 解決名の文字数上限 (255) */
    u32 fit = cap - 8u;                         /* journal を足せる上限 (247) */
    int h;

    /* (a) 本体の解決名が上限ちょうど = journal 名が切り詰められて衝突する形 */
    build_cwd(cwd, cap - 4u);                   /* 251B、末尾 '/' */
    strcpy(abs_name, cwd);
    strcat(abs_name, "f.db");
    CHECK(strlen(abs_name) == cap);
    /* 中身は問わない (GREEN は SQLite を呼ぶ前に断る)。legacy kapi_db_open は
     * path を 254B で切るので、ここは fixture に直接置く。 */
    CHECK(fixture_create(NULL, abs_name, "db", 2) == VFS_OK);
    resolve_cwd = cwd;
    /* 旧実装はここで journal 名を切り詰めて本体と衝突させた (BUSY_RECOVERY
     * の元)。票 TASK_VFS_FD_PATH 以降の resolver は切り詰めずに断る */
    CHECK(host_resolve("f.db-journal")[0] == '\0');
    CHECK(kapi_db_open_existing("f.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);   /* BUSY_RECOVERY でない */
    CHECK(kapi_db_open_existing("f.db", 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    resolve_cwd = "";

    /* (b) 解決名が 248B = journal が入らない → CANTOPEN */
    build_cwd(cwd, fit + 1u - 4u);
    strcpy(abs_name, cwd);
    strcat(abs_name, "f.db");
    CHECK(strlen(abs_name) == fit + 1u);
    CHECK(fixture_create(NULL, abs_name, "db", 2) == VFS_OK);
    resolve_cwd = cwd;
    CHECK(kapi_db_open_existing("f.db", 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    resolve_cwd = "";

    /* (c) 解決名が 247B = ちょうど入る → 開ける */
    build_cwd(cwd, fit - 4u);
    strcpy(abs_name, cwd);
    strcat(abs_name, "f.db");
    CHECK(strlen(abs_name) == fit);
    make_db(abs_name, "CREATE TABLE t(x)");
    resolve_cwd = cwd;
    h = kapi_db_open_existing("f.db", 0);
    CHECK(h >= 0);
    CHECK(kapi_db_close(h) == 0);
    resolve_cwd = "";

    /* (d) 相対名は SQLite に渡らない — 解決した絶対名で open している。 */
    make_db("/sub/rel.db", "CREATE TABLE t(x)");
    resolve_cwd = "/sub/";
    h = kapi_db_open_existing("rel.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES(1)") == 0);
    CHECK(kapi_db_close(h) == 0);
    resolve_cwd = "";
    CHECK(fixture_find("/sub/rel.db", 0) != NULL);
    CHECK(fixture_find("rel.db", 0) == NULL);
}

/* ---- 20. 切り詰め済みの絶対名を信じない (往復 3) ----------------------- */
/*  fs/vfs.c の vfs_resolve_path は VFS_MAX_PATH の作業領域に cwd と入力を
 *  連結し、**切り詰めてから** `.` / `..` を畳む。溢れた入力は「短い別の
 *  絶対名」として返るので、解決結果だけを見ても切り詰めは分からない。
 *  cwd `/tmp` + `"./" × 122 + "a/../b.db"` は `/tmp/b` に化ける。         */
static void resolve_truncate(void)
{
    static char evil[VFS_MAX_PATH];
    static char toolong[VFS_MAX_PATH + 8];
    const char *resolved;
    u32 i, n = 0;
    int h;

    for (i = 0; i < 122u; i++) { evil[n++] = '.'; evil[n++] = '/'; }
    memcpy(evil + n, "a/../b.db", 9);
    n += 9u;
    evil[n] = '\0';
    CHECK(strlen(evil) == 253);          /* NUL 込み 254B = 入口の上限内 */

    make_db("/tmp/b", "CREATE TABLE decoy(x)");        /* 化けた先の DB */
    resolve_cwd = "/tmp";

    /* 旧実装は連結 → 切り詰め → 正規化の順で `/tmp/b` に化けた。票
     * TASK_VFS_FD_PATH 以降の resolver は大きい作業領域で正規化してから
     * 長さを見るので、正しい名前 `/tmp/b.db` になる (化けない)。kapi_db は
     * 入口の長さ検査 (db_resolve_fits) で従来どおり安全側に断る。 */
    resolved = host_resolve(evil);
    printf("TRUNCATE resolved=%s\n", resolved);
    CHECK(!strcmp(resolved, "/tmp/b.db"));

    /* 要求は `.../b.db`。解決名を信じると **別の DB** の RW handle を返す。 */
    CHECK(kapi_db_open_existing(evil, 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    CHECK(kapi_db_open_existing(evil, 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);

    /* 絶対名でも同じ規則 (NUL 込み VFS_MAX_PATH を超えたら断る)。
     * 終端は **配列の内側**に置く (上限より大きい器で試す)。 */
    memset(toolong, 'z', sizeof(toolong));
    toolong[0] = '/';
    toolong[VFS_MAX_PATH - 1] = '\0';    /* 255 文字 = ぎりぎり通る長さ */
    CHECK(strlen(toolong) == (u32)VFS_MAX_PATH - 1u);
    CHECK(db_resolve_fits(toolong));
    toolong[VFS_MAX_PATH - 1] = 'z';
    toolong[VFS_MAX_PATH] = '\0';        /* 256 文字 = 1 バイト超過 */
    CHECK(strlen(toolong) == (u32)VFS_MAX_PATH);
    CHECK(!db_resolve_fits(toolong));

    /* 短い相対名は従来どおり通る */
    resolve_cwd = "/tmp";
    make_db("/tmp/ok.db", "CREATE TABLE t(x)");
    h = kapi_db_open_existing("ok.db", 1);
    CHECK(h >= 0);
    CHECK(kapi_db_close(h) == 0);
    resolve_cwd = "";
}

/* ---- 21. 深さ超過で別の DB に化ける (最終往復) -------------------------- */
/*  fs/vfs.c の成分表は VFS_MAX_PATH_DEPTH 本しかなく、溢れた成分は**黙って
 *  捨てられる**。その後ろの `..` は捨てられた成分ではなく **保持済みの成分**を
 *  1 つ消すので、`("/a"×32) + "/x/.." + ("/.."×31) + "/b.db"` は正しくは
 *  `/a/b.db` なのに `/b.db` に化ける。長さ (167B) では捕まらない。          */
static void resolve_depth(void)
{
    static char evil[VFS_MAX_PATH];
    static char deep[VFS_MAX_PATH];
    const char *resolved;
    u32 i, n = 0;
    int h;

    for (i = 0; i < (u32)VFS_MAX_PATH_DEPTH; i++) {
        evil[n++] = '/'; evil[n++] = 'a';
    }
    memcpy(evil + n, "/x/..", 5); n += 5u;
    for (i = 0; i < (u32)VFS_MAX_PATH_DEPTH - 1u; i++) {
        memcpy(evil + n, "/..", 3); n += 3u;
    }
    memcpy(evil + n, "/b.db", 5); n += 5u;
    evil[n] = '\0';
    CHECK(strlen(evil) == 167);                 /* 長さでは捕まらない */

    make_db("/b.db", "CREATE TABLE decoy(x)");  /* 化けた先の DB */

    /* 旧実装は溢れた成分を捨てて `/b.db` に化けた。票 TASK_VFS_FD_PATH
     * 以降の resolver は 33 個目を積む時点で断る (NAMETOOLONG、結果は空)。 */
    resolved = host_resolve(evil);
    printf("DEPTH resolved=%s\n", resolved);
    CHECK(resolved[0] == '\0');

    /* 要求は `/a/.../b.db`。解決名を信じると **別の DB** の handle が返る。 */
    CHECK(kapi_db_open_existing(evil, 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);
    CHECK(kapi_db_open_existing(evil, 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);

    /* 数え方: 空成分と `.` は数えず、`..` は 1 成分として数える。 */
    CHECK(db_path_depth("/") == 0);
    CHECK(db_path_depth("///a//b/") == 2);
    CHECK(db_path_depth("/./a/./b") == 2);
    CHECK(db_path_depth("/a/../b") == 3);
    CHECK(db_path_depth("a/b") == 2);

    /* 深さちょうど 32 は通る。33 は断る。 */
    n = 0;
    for (i = 0; i < (u32)VFS_MAX_PATH_DEPTH - 1u; i++) {
        deep[n++] = '/'; deep[n++] = 'd';
    }
    memcpy(deep + n, "/t.db", 5); n += 5u;
    deep[n] = '\0';
    CHECK(db_path_depth(deep) == (u32)VFS_MAX_PATH_DEPTH);
    CHECK(db_resolve_fits(deep));
    make_db(deep, "CREATE TABLE t(x)");
    h = kapi_db_open_existing(deep, 0);
    CHECK(h >= 0);
    CHECK(kapi_db_close(h) == 0);

    memcpy(deep + n, "/u", 2);
    deep[n + 2u] = '\0';
    CHECK(db_path_depth(deep) == (u32)VFS_MAX_PATH_DEPTH + 1u);
    CHECK(!db_resolve_fits(deep));
    CHECK(kapi_db_open_existing(deep, 0) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_CANTOPEN);

    /* 相対名では cwd の成分も数える。 */
    resolve_cwd = "/one/two";
    CHECK(db_resolve_fits("three"));
    CHECK(!db_resolve_fits(deep + 1));          /* cwd 2 + 深い相対名 */
    resolve_cwd = "";
}

/* ---- 22. CPL=3 の規則を通した open / prepare (実機 K2 の回帰) ----------- */
/*  これまでのケースは `host_cpl3 = 0` (= CPL=0 の直呼び) で走っていたので、
 *  `db_user_str_copy` → `ring3_user_range_ok` の経路が **1 度も踏まれて
 *  いなかった**。実機 K2 で `db_open_existing` が SQLITE_MISUSE を返した
 *  (= 検証が path を拒否した) のはこの穴。ここで両側を固定する:
 *    - 読める範囲に居る path / sql はそのまま通る
 *    - 1 バイトでも範囲から出る path は **MISUSE** で断られ、診断は
 *      「引数そのものの拒否」と別の文言になる                               */
static void cpl3_paths(void)
{
    static char upath[64];
    static char usql[64];
    int h;

    make_db("/c3.db", "CREATE TABLE t(x)");

    /* (a) 範囲に収まる path は CPL=3 でも開ける */
    strcpy(upath, "/c3.db");
    host_user_lo = upath;
    host_user_hi = upath + sizeof(upath);
    host_cpl3 = 1;
    h = kapi_db_open_existing(upath, 1);
    CHECK(h >= 0);
    CHECK(kapi_db_error_code(-1) == SQLITE_OK);
    CHECK(host_range_refused == 0);

    /* (b) sql も同じ経路を通る */
    strcpy(usql, "SELECT 1");
    host_user_lo = usql;
    host_user_hi = usql + sizeof(usql);
    CHECK(kapi_db_prepare_only(h, usql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(kapi_db_finalize(h) == 0);
    CHECK(kapi_db_close(h) == 0);

    /* (c) NUL まで届かない範囲 = 検証が拒否 → MISUSE。診断の文言で
     * 「引数の拒否」と見分けられる (実機で db_last_error が切り分けの手)。 */
    host_user_lo = upath;
    host_user_hi = upath + 3;            /* "/c3" までしか読めない */
    CHECK(kapi_db_open_existing(upath, 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_MISUSE);
    CHECK(host_range_refused > 0);
    CHECK(strstr((const char *)(test_shm + sizeof(DB_ResultHeader)),
                 "range check") != NULL);

    /* 引数そのものの拒否は別の文言 */
    host_user_lo = upath;
    host_user_hi = upath + sizeof(upath);
    strcpy(upath, ":memory:");
    CHECK(kapi_db_open_existing(upath, 1) == -1);
    CHECK(kapi_db_error_code(-1) == SQLITE_MISUSE);
    CHECK(strstr((const char *)(test_shm + sizeof(DB_ResultHeader)),
                 "range check") == NULL);
    host_cpl3 = 0;
    host_user_lo = NULL;
    host_user_hi = NULL;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(os32_sqlite_init() == SQLITE_OK);
    reset_all();
    cases_run++;

    if (!strcmp(argv[1], "open_existing")) open_existing();
    else if (!strcmp(argv[1], "prepare_only")) prepare_only();
    else if (!strcmp(argv[1], "binds")) binds();
    else if (!strcmp(argv[1], "error_code")) error_code();
    else if (!strcmp(argv[1], "v50_selftest")) v50_selftest();
    else if (!strcmp(argv[1], "slot_layout_append")) slot_layout_append();
    else if (!strcmp(argv[1], "shm_bound")) shm_bound();
    else if (!strcmp(argv[1], "user_range")) user_range();
    else if (!strcmp(argv[1], "owner_isolation")) owner_isolation();
    else if (!strcmp(argv[1], "shm_exact")) shm_exact();
    else if (!strcmp(argv[1], "stat_faults")) stat_faults();
    else if (!strcmp(argv[1], "journal_mode")) journal_mode();
    else if (!strcmp(argv[1], "step_no_stmt")) step_no_stmt();
    else if (!strcmp(argv[1], "path_len")) path_len();
    else if (!strcmp(argv[1], "transient")) transient();
    else if (!strcmp(argv[1], "sql_tail_sqlite")) sql_tail_sqlite();
    else if (!strcmp(argv[1], "prepare_replaces")) prepare_replaces();
    else if (!strcmp(argv[1], "materialize_fail")) materialize_fail();
    else if (!strcmp(argv[1], "resolve_len")) resolve_len();
    else if (!strcmp(argv[1], "resolve_truncate")) resolve_truncate();
    else if (!strcmp(argv[1], "resolve_depth")) resolve_depth();
    else if (!strcmp(argv[1], "cpl3_paths")) cpl3_paths();
    else if (!strncmp(argv[1], "order_", 6)) reclaim_order(argv[1] + 6);
    else CHECK(0);

    canary_check(argv[1]);
    CHECK(memsys5_check_canary() == 0);
    printf("PASS %s\n", argv[1]);
    return 0;
}
