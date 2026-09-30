/* ========================================================================= */
/*  DB_ERRSTR_HOST.C — 票 TASK_DB_ERRSTR §5 (E5 / E6 / E7) のホスト TDD      */
/*                                                                           */
/*  見るのは 1 つだけ: **CPL=3 のアプリに返す文字列が共有メモリの中にあるか**。*/
/*  ホストでは番地の区別が付かないので「読めた」ではなく `test_shm` の        */
/*  範囲の検査として書く (票 §5 の E6)。                                     */
/*                                                                           */
/*  実物だけを組む: 実 `kapi/kapi_db.c` + 実 `lib/sqlite3/sqlite3.c` +        */
/*  実 `lib/sqlite3/os32_sqlite_vfs.c` + 実 `fs/vfs_fd.c`。模型は exec 側の    */
/*  ポインタ検証と SHM の置き場だけ — どちらもカーネル番地に依存する。        */
/*  DB は `:memory:` で開くので、ホストのファイルシステムには触らない。       */
/*                                                                           */
/*  実行: python3 -B tools/tests/test_db_errstr.py                           */
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
int vfs_stat(const char *path, OS32_Stat *st)
{
    FixtureFile *f;
    probes++;
    f = fixture_find(host_resolve(path), 0);
    if (!f) return OS32_ERR_NOTFOUND;
    if (st) { memset(st, 0, sizeof(*st)); st->st_size = f->size; }
    return 0;
}

/* ---- exec 側の模型 (kapi_db.c が唯一使う口) ---------------------------- */
/* CPL=0 の直呼びと同じ扱い。NULL と長さの溢れだけは実物と同じく断る
 * (db_v50_selftest の (2) がここを踏む)。 */
int ring3_user_range_ok(u32 p, u32 len)
{
    if (p == 0) return 0;
    if (len == 0) return 1;
    /* 実機は 32bit。ホストの `u32` (= unsigned long) は 64bit なので、
     * 折り返しは 32bit の幅で見ないと db_v50_selftest の (2) が通らない。 */
    if ((p & 0xFFFFFFFFul) + (len & 0xFFFFFFFFul) > 0xFFFFFFFFul) return 0;
    return 1;
}

/* SHM の置き場。末尾に番兵を置いて「16KB を 1 バイトも越えない」を見る。 */
#define SHM_CANARY 256
static unsigned char test_shm[DB_SHM_BLOCK_SIZE + SHM_CANARY];
#include "../../kapi/kapi_db.c"

/* ========================================================================= */
/*  判定の道具                                                               */
/* ========================================================================= */

/* E6 の本体。返り値が **共有メモリの中** を指しているか。
 * カーネルの .rodata / .data / SQLite の帯は当然この外にある。 */
#define IN_SHM(p) ((const unsigned char *)(const void *)(p) >= test_shm && \
                   (const unsigned char *)(const void *)(p) \
                       < test_shm + DB_SHM_BLOCK_SIZE)

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

static void reset_all(void)
{
    int i;
    memset(test_shm, 0, sizeof(test_shm));
    memset(db_slots, 0, sizeof(db_slots));
    memset(db_open_fail, 0, sizeof(db_open_fail));
    memset(fixture_files, 0, sizeof(fixture_files));
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].in_use = 0;
    resolve_cwd = "";
    resolve_owner = current_owner = 2;
    fixture_init();
}

/* 切り詰めた診断文が UTF-8 として壊れていないか (gotcha §4-27)。 */
static int utf8_ok(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n;
        if (*p < 0x80u) n = 0;
        else if ((*p & 0xE0u) == 0xC0u) n = 1;
        else if ((*p & 0xF0u) == 0xE0u) n = 2;
        else if ((*p & 0xF8u) == 0xF0u) n = 3;
        else return 0;
        p++;
        while (n-- > 0) {
            if ((*p & 0xC0u) != 0x80u) return 0;
            p++;
        }
    }
    return 1;
}

/* 診断領域を「前の呼び出しの残り」で汚す。写す側が NUL を置き忘れたら
 * ここが読み出しに出てくる — 空の SHM では NUL 忘れが見えない。 */
static void diag_dirty(void)
{
    memset(test_shm + DB_SHM_DIAG_OFFSET, 0x5A, DB_SHM_DIAG_SIZE);
}

/* 診断領域の写しを取っておく (結果の書き込みが踏んでいないかを見るため)。 */
static char diag_saved[DB_SHM_DIAG_SIZE];

static void diag_save(void)
{
    memcpy(diag_saved, test_shm + DB_SHM_DIAG_OFFSET, sizeof(diag_saved));
}

static int diag_intact(void)
{
    return memcmp(diag_saved, test_shm + DB_SHM_DIAG_OFFSET,
                  sizeof(diag_saved)) == 0;
}

/* 診断領域を使わない使い捨て接続。実 FS を触らない `:memory:`。 */
static int open_mem(void)
{
    int h = kapi_db_open(":memory:");
    CHECK(h >= 0);
    return h;
}

/* ========================================================================= */
/*  1. E6 — 返り先が共有メモリの中か (db_last_error の全 4 経路)             */
/* ========================================================================= */
static void errstr_range(void)
{
    const char *m;
    int h;

    /* (a) handle が範囲外 (下と上の 2 通り) — 元は .rodata の "invalid handle" */
    diag_dirty();
    m = kapi_db_last_error(-1);
    CHECK(IN_SHM(m));
    CHECK(!strcmp(m, "invalid handle"));
    diag_dirty();
    m = kapi_db_last_error(DB_MAX_CONNECTIONS);
    CHECK(IN_SHM(m));
    CHECK(!strcmp(m, "invalid handle"));

    /* (b) 空きスロット (!in_use) — 2 本目の "invalid handle" */
    diag_dirty();
    m = kapi_db_last_error(0);
    CHECK(IN_SHM(m));
    CHECK(!strcmp(m, "invalid handle"));

    /* (c) cleanup_message 経路 — 元はカーネルの .bss */
    db_slots[1].cleanup_error = SQLITE_BUSY;
    kstrncpy(db_slots[1].cleanup_message, "close failed: database is locked",
             DB_ERROR_SIZE);
    diag_dirty();
    m = kapi_db_last_error(1);
    CHECK(IN_SHM(m));
    CHECK(!strcmp(m, "close failed: database is locked"));
    db_slots[1].cleanup_error = SQLITE_OK;
    db_slots[1].cleanup_message[0] = '\0';

    /* (d) sqlite3_errmsg 経路 — 元は SQLite の帯 (0x2xxxxx) */
    h = open_mem();
    CHECK(kapi_db_exec(h, "SELECT * FROM nosuchtable") == -1);
    diag_dirty();
    m = kapi_db_last_error(h);
    CHECK(IN_SHM(m));
    CHECK(strstr(m, "no such table") != NULL);

    /* 写した後は SQLite 側が何をしても安全 (票 §4 の 4)。別の SQL を
     * 流して errmsg を差し替えても、返してあった文字列は動かない。 */
    {
        char keep[DB_SHM_ERRSTR_MAX];
        str_cpy(keep, m, (int)sizeof(keep));
        CHECK(kapi_db_exec(h, "CREATE TABLE t(a,b)") == 0);
        CHECK(!strcmp(m, keep));
    }

    CHECK(kapi_db_close(h) == 0);
    canary_check("errstr_range");
}

/* ========================================================================= */
/*  2. E6 — db_column_text の全経路 (正常系 + エラー 3 経路)                 */
/* ========================================================================= */
static void coltext_range(void)
{
    const char *m;
    const char *ok;
    int h;

    h = open_mem();
    CHECK(kapi_db_exec(h, "CREATE TABLE t(a,b)") == 0);
    CHECK(kapi_db_exec(h, "INSERT INTO t VALUES('hello', NULL)") == 0);
    CHECK(kapi_db_prepare_only(h, "SELECT a,b FROM t") == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);

    /* 正常系 (元から共有メモリ) */
    ok = kapi_db_column_text(h, 0);
    CHECK(IN_SHM(ok));
    CHECK(!strcmp(ok, "hello"));

    /* エラー経路 1: col が範囲外 (上と下) */
    diag_dirty();
    m = kapi_db_column_text(h, 99);
    CHECK(IN_SHM(m));
    CHECK(m[0] == '\0');
    diag_dirty();
    m = kapi_db_column_text(h, -1);
    CHECK(IN_SHM(m));
    CHECK(m[0] == '\0');

    /* エラー経路 2: data_offset == 0 (NULL 列) */
    diag_dirty();
    m = kapi_db_column_text(h, 1);
    CHECK(IN_SHM(m));
    CHECK(m[0] == '\0');

    /* 空文字列を返しても、結果データは踏まれない */
    CHECK(!strcmp(ok, "hello"));

    /* エラー経路 3: slot / active_stmt が無い */
    CHECK(kapi_db_finalize(h) == 0);
    diag_dirty();
    m = kapi_db_column_text(h, 0);
    CHECK(IN_SHM(m));
    CHECK(m[0] == '\0');
    diag_dirty();
    m = kapi_db_column_text(-1, 0);
    CHECK(IN_SHM(m));
    CHECK(m[0] == '\0');
    diag_dirty();
    m = kapi_db_column_text(DB_MAX_CONNECTIONS, 0);
    CHECK(IN_SHM(m));
    CHECK(m[0] == '\0');

    CHECK(kapi_db_close(h) == 0);
    canary_check("coltext_range");
}

/* ========================================================================= */
/*  3. E5 — 上限で切り、必ず NUL で終える                                    */
/* ========================================================================= */
static void errstr_truncate(void)
{
    static char sql[DB_SQL_MAX_BYTES];
    static char name[600];
    const char *full;
    const char *m;
    int h, i;

    h = open_mem();

    /* --- (a) ASCII: 上限ちょうどまで写して切る --- */
    for (i = 0; i < (int)sizeof(name) - 1; i++) name[i] = 'z';
    name[sizeof(name) - 1] = '\0';
    sprintf(sql, "SELECT * FROM %s", name);
    CHECK(kapi_db_exec(h, sql) == -1);

    full = sqlite3_errmsg(db_slots[h].db);
    CHECK(strlen(full) > (size_t)DB_SHM_ERRSTR_MAX - 1);   /* 本当に長い */

    /* 診断領域の外には 1 バイトも書かない: 手前 (結果) と奥 (空文字列) に
     * 番兵を置いて確かめる。 */
    diag_dirty();
    test_shm[DB_SHM_DIAG_OFFSET - 1] = 0x5A;

    m = kapi_db_last_error(h);
    CHECK(IN_SHM(m));
    CHECK(m == (const char *)(test_shm + DB_SHM_DIAG_OFFSET));
    CHECK(strlen(m) == (size_t)DB_SHM_ERRSTR_MAX - 1);     /* 上限いっぱい */
    CHECK(m[strlen(m)] == '\0');                           /* 必ず NUL */
    CHECK(test_shm[DB_SHM_DIAG_OFFSET - 1] == 0x5A);       /* 結果側は無傷 */
    CHECK(test_shm[DB_SHM_EMPTY_OFFSET] == 0x5A);          /* 奥も無傷 */
    /* 写せなかったぶんを「写せた」ことにしない — 切れた印が付く */
    CHECK(!strcmp(m + strlen(m) - 3, "..."));
    CHECK(!strncmp(m, full, strlen(m) - 3));
    canary_check("errstr_truncate ascii");

    /* --- (b) UTF-8: 切り口が文字の途中に落ちない (gotcha §4-27) --- */
    name[0] = '\0';
    for (i = 0; i < 190; i++) strcat(name, "\xE3\x81\x82");   /* 「あ」 */
    sprintf(sql, "SELECT * FROM \"%s\"", name);
    CHECK(kapi_db_exec(h, sql) == -1);
    full = sqlite3_errmsg(db_slots[h].db);
    CHECK(strlen(full) > (size_t)DB_SHM_ERRSTR_MAX - 1);

    diag_dirty();
    m = kapi_db_last_error(h);
    CHECK(IN_SHM(m));
    CHECK(strlen(m) <= (size_t)DB_SHM_ERRSTR_MAX - 1);
    CHECK(strlen(m) < (size_t)DB_SHM_ERRSTR_MAX - 1);       /* 境界へ戻った */
    CHECK(utf8_ok(m));
    CHECK(!strcmp(m + strlen(m) - 3, "..."));
    CHECK(!strncmp(m, full, strlen(m) - 3));
    canary_check("errstr_truncate utf8");

    /* --- (c) 短い文は切らない。NULL も落ちない --- */
    db_slots[h].cleanup_error = SQLITE_BUSY;
    kstrncpy(db_slots[h].cleanup_message, "short", DB_ERROR_SIZE);
    diag_dirty();
    m = kapi_db_last_error(h);
    CHECK(IN_SHM(m));
    CHECK(!strcmp(m, "short"));
    db_slots[h].cleanup_error = SQLITE_OK;
    db_slots[h].cleanup_message[0] = '\0';

    CHECK(kapi_db_close(h) == 0);
    canary_check("errstr_truncate");
}

/* ========================================================================= */
/*  4. E7 — 結果データを上限いっぱいまで書いても診断文が壊れない             */
/* ========================================================================= */
static void result_bound(void)
{
    int room = (int)DB_SHM_RESULT_LIMIT - (int)sizeof(DB_ResultHeader)
               - (int)sizeof(DB_ColumnInfo);
    DB_ColumnInfo *info = (DB_ColumnInfo *)(test_shm + sizeof(DB_ResultHeader));
    static char sql[DB_SQL_MAX_BYTES];
    const char *m;
    int h, i;

    h = open_mem();

    /* 先に診断文を置く (これが踏まれないことを見る) */
    CHECK(kapi_db_exec(h, "SELECT * FROM nosuchtable") == -1);
    m = kapi_db_last_error(h);
    CHECK(IN_SHM(m));
    diag_save();

    /* (a) 結果側が使える上限ちょうどの BLOB は書ける */
    sprintf(sql, "SELECT zeroblob(%d)", room);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(info->type == DB_TYPE_BLOB);
    CHECK((int)info->length == room);
    CHECK(info->data_offset != 0);                     /* 欠落 ROW ではない */
    CHECK(info->data_offset + room == (i32)DB_SHM_RESULT_LIMIT);
    for (i = 0; i < room; i++) CHECK(test_shm[info->data_offset + i] == 0);
    CHECK(diag_intact());                              /* 診断文は無傷 */
    CHECK(!strcmp(m, (const char *)(test_shm + DB_SHM_DIAG_OFFSET)));
    canary_check("result_bound fit");
    CHECK(kapi_db_finalize(h) == 0);

    /* (b) 1 バイト超過は拒否される — 上限が診断領域を勘定に入れている証拠。
     * ここが DB_SHM_BLOCK_SIZE のままだと、この行が ROW として通る。 */
    sprintf(sql, "SELECT zeroblob(%d)", room + 1);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    CHECK(kapi_db_error_code(h) == SQLITE_TOOBIG);
    CHECK(diag_intact());
    canary_check("result_bound over");
    CHECK(kapi_db_finalize(h) == 0);

    /* (c) TEXT 側の境界 (終端 NUL の分だけ 1 バイト小さい) */
    sprintf(sql, "SELECT substr(hex(zeroblob(%d)), 1, %d)", room, room - 1);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ROW);
    CHECK(info->type == DB_TYPE_TEXT);
    CHECK((int)info->length == room - 1);
    CHECK(info->data_offset != 0);
    CHECK((int)strlen(kapi_db_column_text(h, 0)) == room - 1);
    CHECK(diag_intact());
    canary_check("result_bound text fit");
    CHECK(kapi_db_finalize(h) == 0);

    sprintf(sql, "SELECT substr(hex(zeroblob(%d)), 1, %d)", room + 2, room);
    CHECK(kapi_db_prepare_only(h, sql) == 0);
    CHECK(kapi_db_step(h) == DB_STATUS_ERROR);
    CHECK(kapi_db_error_code(h) == SQLITE_TOOBIG);
    CHECK(diag_intact());
    canary_check("result_bound text over");
    CHECK(kapi_db_finalize(h) == 0);

    /* (d) 1 回の db_exec のエラー欄 (error_offset) も同じ上限で切る。
     * 16KB を超える診断文は実接続では作れないので、ここだけ直に呼ぶ —
     * `shm_write_error_text` が DB_SHM_BLOCK_SIZE から引いていると、
     * この 1 本だけで診断領域が消える。 */
    {
        static char big[DB_SHM_BLOCK_SIZE + 512];
        i32 start = (i32)sizeof(DB_ResultHeader);
        memset(big, 'E', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        diag_save();
        shm_write_error_text(big);
        CHECK(((DB_ResultHeader *)test_shm)->status == DB_STATUS_ERROR);
        CHECK(((DB_ResultHeader *)test_shm)->error_offset == start);
        CHECK(strlen((const char *)(test_shm + start))
              <= (size_t)DB_SHM_RESULT_LIMIT - (size_t)start - 1u);
        CHECK(diag_intact());
        canary_check("result_bound errtext");
    }

    CHECK(kapi_db_close(h) == 0);
}

/* ========================================================================= */
/*  5. 領域の取り決めそのもの (ブート自己診断と同じ判定)                     */
/* ========================================================================= */
static void diag_layout(void)
{
    /* 結果と診断はブロック 0 を過不足なく分け合う */
    CHECK(DB_SHM_RESULT_LIMIT + DB_SHM_DIAG_SIZE == DB_SHM_BLOCK_SIZE);
    CHECK(DB_SHM_DIAG_OFFSET == DB_SHM_RESULT_LIMIT);
    /* 診断文のバッファと「常に NUL の 1 バイト」は重ならない */
    CHECK(DB_SHM_DIAG_OFFSET + DB_SHM_ERRSTR_MAX <= DB_SHM_EMPTY_OFFSET);
    CHECK(DB_SHM_EMPTY_OFFSET < DB_SHM_BLOCK_SIZE);
    /* 結果側に DB_ResultHeader + 1 列 + 実用的な payload が残っている */
    CHECK(DB_SHM_RESULT_LIMIT > (i32)(sizeof(DB_ResultHeader)
                                      + sizeof(DB_ColumnInfo)) + DB_BIND_BLOB_MAX);
    /* ブート自己診断がこの取り決めを見ている (bit 5)。他の bit も 0 のまま。 */
    {
        u32 bad = db_v50_selftest();
        if (bad) fprintf(stderr, "db_v50_selftest bad=0x%lX\n", (unsigned long)bad);
        CHECK((bad & (1u << 5)) == 0);
        CHECK(bad == 0);
    }
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(os32_sqlite_init() == SQLITE_OK);
    reset_all();

    if (!strcmp(argv[1], "errstr_range")) errstr_range();
    else if (!strcmp(argv[1], "coltext_range")) coltext_range();
    else if (!strcmp(argv[1], "errstr_truncate")) errstr_truncate();
    else if (!strcmp(argv[1], "result_bound")) result_bound();
    else if (!strcmp(argv[1], "diag_layout")) diag_layout();
    else CHECK(0);

    canary_check(argv[1]);
    CHECK(memsys5_check_canary() == 0);
    printf("PASS %s\n", argv[1]);
    return 0;
}
