/* ========================================================================= */
/*  IME_DICT_HOST.C — 辞書の常駐接続の開き直し (票 TASK_VFS_FD_PATH v3 の 6)  */
/*                                                                           */
/*  実物だけを組む: 実 `kernel/ime_dict.c` + 実 `lib/sqlite3/sqlite3.c` +     */
/*  実 `lib/sqlite3/os32_sqlite_vfs.c` + 実 `fs/vfs_fd.c` + RAM バックエンド  */
/*  (tools/tests/sqlite_groups_backend.h)。ルーティングは                     */
/*  vfs_fd_sqlite_host.c の模型 (パスで動く FS)。                             */
/*                                                                           */
/*  失効は VFS (vfs_rm / 置き換え rename / umount) が FD に付ける印なので、   */
/*  ここでは辞書の実体を差し替えた後に**同じ印**を FD 表へ直に付ける。        */
/*                                                                           */
/*  実行: python3 -B tools/tests/test_vfs_fd_path.py ime                     */
/* ========================================================================= */

#define main f2a_main
#include "vfs_fd_sqlite_host.c"
#undef main

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
int vfs_sync(void) { return 0; }
int vfs_rm(const char *path) { return fixture_rm(path); }
int vfs_stat(const char *path, OS32_Stat *st)
{
    FixtureFile *f = fixture_find(path, 0);
    if (!f) return OS32_ERR_NOTFOUND;
    if (st) { memset(st, 0, sizeof(*st)); st->st_size = f->size; }
    return 0;
}

/* 画面 (kprintf) とシリアル (serial_puts) に出た行を数える */
static int n_kprintf, n_reopened, n_0hit;
static char last_kprintf[256];
/* 1 = 検索の失敗 (0hit の診断行) の時点で journal を置く。SQLite 自身の
 * hot journal 処理 (古い接続の読み始め) より**後**、開き直しの判断より**前**に
 * 残っている journal を作るため — 古い接続の失敗した書き込みが残した形 */
static int plant_journal;
static int fixture_create(void *ctx, const char *path, const void *buf, u32 size);
void kprintf(u8 color, const char *fmt, ...)
{
    (void)color; n_kprintf++; str_cpy(last_kprintf, fmt, sizeof(last_kprintf));
    if (strstr(fmt, "0hit")) n_0hit++;
    if (plant_journal && strstr(fmt, "0hit")) {
        plant_journal = 0;
        (void)fixture_create(NULL, "/db/fep.db-journal", "JJJJ", 4);
    }
}
void serial_puts(const char *s)
{ if (strstr(s, "reopened")) n_reopened++; }
int utf8_strlen(const u8 *s)
{ int n = 0; while (*s) { if ((*s & 0xC0) != 0x80) n++; s++; } return n; }

/* 読みの失敗の注入 (パスに部分一致する read_stream を n 回 IO にする) */
static int fail_reads;
static u32 fail_from;       /* この offset 以上の読みだけを失敗させる */
static int inj_read(void *ctx, const char *path, void *buf, u32 size, u32 offset)
{
    if (fail_reads > 0 && offset >= fail_from && strstr(path, "fep.db")) {
        fail_reads--;
        return VFS_ERR_IO;
    }
    return fixture_read(ctx, path, buf, size, offset);
}

#include "../../kernel/ime_dict.c"

/* ---- 足場 ---- */
static void make_dict(const char *path, const char *kanji)
{
    sqlite3 *db = 0;
    char sql[256];
    CHECK(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                          (const char *)0) == SQLITE_OK);
    snprintf(sql, sizeof(sql),
             "CREATE TABLE dict(yomi TEXT, kanji TEXT, pos_id INT, cost INT);"
             "INSERT INTO dict VALUES('か','%s',1,10);", kanji);
    CHECK(sqlite3_exec(db, sql, 0, 0, 0) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
}

/* 実体の差し替え (`rm` → 同じ名前に作り直し と同じ結果。hsync の置き換え
 * rename は辞書を開いている間は BUSY で断られるので、ここへは来ない):
 * 別名で作った DB の中身を本名へ移し、別名を消す */
static void replace_dict(const char *kanji)
{
    FixtureFile *src, *dst;
    make_dict("/db/fep.new", kanji);
    src = fixture_find("/db/fep.new", 0);
    dst = fixture_find("/db/fep.db", 0);
    CHECK(src && dst);
    memcpy(dst->data, src->data, src->size);
    dst->size = src->size;
    fixture_rm("/db/fep.new");
}

static int dict_fd(IME_Dict *d) { return d->db ? os32_sqlite_db_fd(d->db) : -1; }

static const char *first_kanji(IME_Dict *d, int *n)
{
    static IME_Result r[4];
    *n = ime_dict_search(d, "か", r, 4);
    return *n > 0 ? r[0].kanji : "";
}

/* ---- (7) ユーザー辞書の管理 API も同じ回復経路 (Codex 実装レビュー
 *      ラリー 1 の B5) ---- */
static int has_bytes(const unsigned char *d, u32 n, const char *pat)
{
    u32 i, m = (u32)strlen(pat);
    for (i = 0; i + m <= n; i++) if (!memcmp(d + i, pat, m)) return 1;
    return 0;
}

static void stale_now(IME_Dict *d, const char *kanji)
{
    replace_dict(kanji);
    open_files[dict_fd(d)].stale = 1;
}

static void admin_apis(IME_Dict *d)
{
    static IME_UserEntry ents[8];
    int reopened, n;
    FixtureFile *out;

    /* clear: 失効した接続で DELETE を流さず、開き直してから消す */
    stale_now(d, "蛾");
    reopened = n_reopened;
    CHECK(ime_user_clear(d) == 0);
    CHECK(n_reopened == reopened + 1);
    CHECK(d->db != 0 && !vfs_fd_is_stale(dict_fd(d)));
    CHECK(!strcmp(first_kanji(d, &n), "蛾"));
    printf("PASS admin_clear\n");

    /* list: 開き直した接続の中身を返す */
    ime_dict_learn(d, "か", "蛾");
    stale_now(d, "我");
    reopened = n_reopened;
    CHECK(ime_user_list(d, "", ents, 8) == 0);        /* 新しい実体は学習なし */
    CHECK(n_reopened == reopened + 1);
    ime_dict_learn(d, "か", "我");
    CHECK(ime_user_list(d, "", ents, 8) == 1 && !strcmp(ents[0].kanji, "我"));
    printf("PASS admin_list\n");

    /* delete: 開き直してから消す (新しい実体の行が消える) */
    stale_now(d, "牙");
    ime_dict_learn(d, "か", "牙");                    /* 学習も開き直す */
    CHECK(ime_user_list(d, "", ents, 8) == 1);
    open_files[dict_fd(d)].stale = 1;
    reopened = n_reopened;
    CHECK(ime_user_delete(d, "か", "牙") == 0);
    CHECK(n_reopened == reopened + 1);
    CHECK(ime_user_list(d, "", ents, 8) == 0);
    printf("PASS admin_delete\n");

    /* export: 開き直してから書き出す */
    ime_dict_learn(d, "か", "牙");
    open_files[dict_fd(d)].stale = 1;
    reopened = n_reopened;
    CHECK(ime_user_export(d, "/db/out.csv") == 0);
    CHECK(n_reopened == reopened + 1);
    out = fixture_find("/db/out.csv", 0);
    CHECK(out != NULL && out->size > 0 && has_bytes(out->data, out->size, "牙"));
    printf("PASS admin_export\n");

    /* export: 読みの途中の失敗を EOF と取り違えない (成功にしない)。
     * 失効 → 開き直し (新しい接続は頁キャッシュが空) → 表の頁が読めない →
     * I/O エラーとして 1 回開き直す → また読めない → 失敗 */
    open_files[dict_fd(d)].stale = 1;
    fail_from = 1024;
    fail_reads = 1000000;
    CHECK(ime_user_export(d, "/db/out2.csv") < 0);
    fail_reads = 0;
    fail_from = 0;
    printf("PASS admin_export_step_error\n");

    /* 失効を伴わない I/O エラー 1 回: 管理 API も 1 回だけ開き直してやり直す
     * (検索・学習と同じ)。頁キャッシュの空いた接続で表の頁を 1 回だけ落とす */
    {
        int k;
        for (k = 0; k < 4; k++) {
            CHECK(ime_dict_reopen(d, "/db/fep.db") == 0);
            ime_dict_learn(d, "か", "牙");
            CHECK(ime_dict_reopen(d, "/db/fep.db") == 0);
            fail_from = 1024;
            fail_reads = 1;
            reopened = n_reopened;
            if (k == 0) CHECK(ime_user_list(d, "", ents, 8) == 1);
            if (k == 1) CHECK(ime_user_export(d, "/db/out3.csv") == 0);
            if (k == 2) CHECK(ime_user_delete(d, "か", "牙") == 0);
            if (k == 3) CHECK(ime_user_clear(d) == 0);
            CHECK(fail_reads == 0);                   /* 1 回は落ちた */
            CHECK(n_reopened == reopened + 1);
            CHECK(d->io_retried == 0);                /* 成功で戻す */
            fail_from = 0;
        }
    }
    printf("PASS admin_io_once\n");

    /* 失効 + hot journal: 管理 API も旧接続で SQL を流さない (辞書無し、
     * journal は不変、「実行に失敗した」ではなく「journal が残っている」) */
    CHECK(ime_dict_reopen(d, "/db/fep.db") == 0);
    stale_now(d, "賀");
    CHECK(fixture_create(NULL, "/db/fep.db-journal", "JJJJ", 4) == VFS_OK);
    n_kprintf = 0;
    CHECK(ime_user_clear(d) < 0);
    CHECK(d->db == 0);
    CHECK(strstr(last_kprintf, "remains") != NULL);
    {
        FixtureFile *j = fixture_find("/db/fep.db-journal", 0);
        CHECK(j != NULL && j->size == 4 && !memcmp(j->data, "JJJJ", 4));
    }
    fixture_rm("/db/fep.db-journal");
    CHECK(ime_dict_open(d, "/db/fep.db") == 0);
    /* 学習も同じ (旧接続で UPSERT を流さない) */
    stale_now(d, "雅");
    CHECK(fixture_create(NULL, "/db/fep.db-journal", "JJJJ", 4) == VFS_OK);
    n_kprintf = 0;
    ime_dict_learn(d, "か", "雅");
    CHECK(d->db == 0);
    CHECK(strstr(last_kprintf, "remains") != NULL);   /* Learn failed ではない */
    {
        FixtureFile *j = fixture_find("/db/fep.db-journal", 0);
        CHECK(j != NULL && j->size == 4 && !memcmp(j->data, "JJJJ", 4));
    }
    fixture_rm("/db/fep.db-journal");
    CHECK(ime_dict_open(d, "/db/fep.db") == 0);
    printf("PASS admin_hot_journal\n");

    /* list: prepare の失敗は 0 件 (= 学習なし) ではなく負 (途中の失敗と同じ
     * -4)。ime コマンドは負を「一覧に失敗」と出す (実装レビュー ラリー 2 の
     * 非 blocker)。I/O エラーではないので開き直さない */
    CHECK(sqlite3_exec((sqlite3 *)d->db, "DROP TABLE dict_user", 0, 0, 0) == SQLITE_OK);
    reopened = n_reopened;
    n_kprintf = 0;
    CHECK(ime_user_list(d, "", ents, 8) == -4);
    CHECK(ime_user_list(d, "か", ents, 8) == -4);
    CHECK(strstr(last_kprintf, "prepare failed") != NULL);
    CHECK(n_reopened == reopened);
    CHECK(ime_dict_reopen(d, "/db/fep.db") == 0);   /* dict_user を作り直す */
    CHECK(ime_user_list(d, "", ents, 8) == 0);
    printf("PASS admin_list_prepare_error\n");
}

/* ---- (8) SQLite のファイルメソッドは失効した FD で成功しない (Codex
 *      実装レビュー ラリー 1 の B2)。close だけは通り、FD を解放できる ---- */
static sqlite3_file *main_file(sqlite3 *db)
{
    sqlite3_file *pf = 0;
    CHECK(sqlite3_file_control(db, "main", SQLITE_FCNTL_FILE_POINTER, &pf) ==
          SQLITE_OK && pf && pf->pMethods);
    return pf;
}

static void expect_methods_fail(sqlite3_file *pf)
{
    const sqlite3_io_methods *m = pf->pMethods;
    char buf[16];
    sqlite3_int64 sz = -1;
    int res = 0;
    CHECK(m->xRead(pf, buf, sizeof(buf), 0) == SQLITE_IOERR_READ);
    CHECK(m->xWrite(pf, buf, sizeof(buf), 0) == SQLITE_IOERR_WRITE);
    CHECK(m->xTruncate(pf, 0) == SQLITE_IOERR_TRUNCATE);
    CHECK(m->xSync(pf, 0) == SQLITE_IOERR_FSYNC);
    CHECK(m->xFileSize(pf, &sz) == SQLITE_IOERR_FSTAT);
    CHECK(m->xLock(pf, SQLITE_LOCK_SHARED) == SQLITE_IOERR_LOCK);
    CHECK(m->xUnlock(pf, SQLITE_LOCK_NONE) == SQLITE_IOERR_UNLOCK);
    CHECK(m->xCheckReservedLock(pf, &res) == SQLITE_IOERR_CHECKRESERVEDLOCK);
    CHECK(m->xFileControl(pf, SQLITE_FCNTL_LOCKSTATE, &res) == SQLITE_IOERR);
}

static void expect_methods_ok(sqlite3_file *pf)
{
    const sqlite3_io_methods *m = pf->pMethods;
    sqlite3_int64 sz = -1;
    int res = 1;
    CHECK(m->xTruncate(pf, 1024) == SQLITE_OK);
    CHECK(m->xSync(pf, 0) == SQLITE_OK);
    CHECK(m->xFileSize(pf, &sz) == SQLITE_OK && sz > 0);
    CHECK(m->xCheckReservedLock(pf, &res) == SQLITE_OK && res == 0);
}

static void sqlite_methods_stale(void)
{
    sqlite3 *db = 0;
    sqlite3_file *pf;
    Os32SqliteGroup g;
    int fd;

    /* 旧来の経路 (既定の VFS、IME 辞書と同じ) */
    make_dict("/db/m.db", "木");
    CHECK(sqlite3_open_v2("/db/m.db", &db, SQLITE_OPEN_READWRITE, 0) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "SELECT count(*) FROM dict", 0, 0, 0) == SQLITE_OK);
    pf = main_file(db);
    fd = os32_sqlite_db_fd(db);
    CHECK(fd >= 3);
    expect_methods_ok(pf);
    open_files[fd].stale = 1;
    expect_methods_fail(pf);
    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(!open_files[fd].in_use);                    /* 失効後も解放できる */
    printf("PASS sqlite_stale_legacy\n");

    /* group の経路 (kapi_db の接続) */
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &g) == SQLITE_OK);
    db = 0;
    CHECK(os32_sqlite_group_open(&g, "/db/m.db", SQLITE_OPEN_READWRITE, &db) ==
          SQLITE_OK);
    CHECK(sqlite3_exec(db, "SELECT count(*) FROM dict", 0, 0, 0) == SQLITE_OK);
    pf = main_file(db);
    fd = os32_sqlite_db_fd(db);
    CHECK(fd >= 3);
    expect_methods_ok(pf);
    open_files[fd].stale = 1;
    expect_methods_fail(pf);
    /* 失効後も (unlink で失効しても) 接続が閉じるまでは rename が BUSY */
    CHECK(vfs_fd_rename_busy(open_files[fd].fs_ctx, "/db/m.db", "/y") == 1);
    CHECK(vfs_fd_rename_busy(open_files[fd].fs_ctx, "/x", "/db/m.db-journal") == 1);
    CHECK(os32_sqlite_group_begin_close(&g) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(&g, sqlite3_close(db)) == SQLITE_OK);
    CHECK(!open_files[fd].in_use);
    CHECK(vfs_fd_rename_busy(&mock_ops, "/db/m.db", "/y") == 0);
    CHECK(vfs_fd_rename_busy(&mock_ops, "/x", "/db/m.db-journal") == 0);
    printf("PASS sqlite_stale_group\n");
}

/* ---- (9) `<名前>-journal` が 255 バイトを超える名前は開く時点で断る
 *      (Opus 実装レビュー ラリー 1 の nb2) ---- */
static void journal_name_len(void)
{
    static char name[VFS_MAX_PATH + 8];
    sqlite3 *db = 0;
    u32 room = (u32)VFS_MAX_PATH - 1u - 8u;          /* 247 */
    memset(name, 'n', sizeof(name));
    memcpy(name, "/db/", 4);
    name[room + 1] = '\0';                             /* 248: 1 バイト超過 */
    CHECK(sqlite3_open_v2(name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                          0) == SQLITE_CANTOPEN);
    if (db) sqlite3_close(db);
    db = 0;
    CHECK(fixture_find(name, 0) == NULL);              /* 作ってもいない */
    name[room] = '\0';                                 /* 247: 収まる */
    CHECK(sqlite3_open_v2(name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                          0) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "CREATE TABLE t(x); INSERT INTO t VALUES(1);",
                       0, 0, 0) == SQLITE_OK);        /* ジャーナルも開ける */
    CHECK(sqlite3_close(db) == SQLITE_OK);
    printf("PASS journal_name_len\n");
}

int main(void)
{
    static IME_Dict d;
    int n, fd;

    CHECK(os32_sqlite_init() == SQLITE_OK);
    fixture_init();
    mock_ops.read_stream = inj_read;
    make_dict("/db/fep.db", "蚊");
    CHECK(ime_dict_open(&d, "/db/fep.db") == 0);
    CHECK(!strcmp(first_kanji(&d, &n), "蚊") && n == 1);
    fd = dict_fd(&d);
    CHECK(fd >= 3);
    CHECK(open_files[fd].sqlite_db == 1);     /* rename は BUSY で断られる印 */
    CHECK(open_files[fd].protect == 1);       /* exec_exit の回収から外す印 */
    printf("PASS open\n");

    /* (1) 実体の差し替え → 失効 → 接続ごと開き直して新しい中身で答える */
    replace_dict("課");
    open_files[fd].stale = 1;                 /* vfs_rm / rename が付ける印 */
    CHECK(!strcmp(first_kanji(&d, &n), "課") && n == 1);
    CHECK(n_reopened == 1);
    CHECK(!open_files[fd].in_use || !open_files[fd].stale ||
          dict_fd(&d) != fd);                 /* 古い FD は閉じた */
    CHECK(open_files[dict_fd(&d)].protect == 1);
    CHECK(open_files[dict_fd(&d)].sqlite_db == 1);
    printf("PASS stale_reopen\n");

    /* (2) 失効 1 回につき 1 回。次の失効ではまた開き直す */
    replace_dict("可");
    open_files[dict_fd(&d)].stale = 1;
    CHECK(!strcmp(first_kanji(&d, &n), "可"));
    CHECK(n_reopened == 2);
    printf("PASS stale_each_event\n");

    /* (3) 失効を伴わない I/O エラーは 1 回だけ開き直す (成功で戻す) */
    fail_reads = 1;
    CHECK(!strcmp(first_kanji(&d, &n), "可"));
    CHECK(n_reopened == 3);
    CHECK(d.io_retried == 0);                 /* やり直しが成功したので戻した */
    fail_reads = 1000000;                     /* 読めないまま */
    CHECK(first_kanji(&d, &n)[0] == '\0' && n == 0);
    CHECK(n_reopened == 3);                   /* 開き直しても読めない = 開けない */
    CHECK(d.db == 0);                         /* 辞書無しで動く */
    fail_reads = 0;
    CHECK(first_kanji(&d, &n)[0] == '\0' && n == 0);
    /* 開き直せるが中身の頁だけ読めない: 1 回開き直したら、以後の検索では
     * 開き直さない (読めないまま開き直しを繰り返さない) */
    CHECK(ime_dict_open(&d, "/db/fep.db") == 0);
    fail_from = 1024;                          /* 1 頁目 (schema) は読める */
    fail_reads = 1000000;
    CHECK(first_kanji(&d, &n)[0] == '\0');
    CHECK(n_reopened == 4 && d.db != 0 && d.io_retried == 1);
    CHECK(first_kanji(&d, &n)[0] == '\0');
    CHECK(first_kanji(&d, &n)[0] == '\0');
    CHECK(n_reopened == 4);
    fail_reads = 0;
    fail_from = 0;
    CHECK(!strcmp(first_kanji(&d, &n), "可"));
    CHECK(d.io_retried == 0);                  /* 成功で戻す */
    printf("PASS io_once\n");

    /* (4a) 失効 + hot journal が**旧接続が SQL を流す前から**残っている
     * (Codex 実装レビュー ラリー 1 の B1)。旧接続で sqlite3_step をすると
     * SQLite は本体を読む前に hot journal を判定し、ジャーナルを再生・削除
     * する。開き直しの判断より先に journal が消えると、開き直してしまう。
     * → 旧接続では何も実行せずに閉じ、journal を見て辞書無し、journal は不変 */
    CHECK(ime_dict_reopen(&d, "/db/fep.db") == 0);
    CHECK(!strcmp(first_kanji(&d, &n), "可"));
    replace_dict("化");
    open_files[dict_fd(&d)].stale = 1;
    CHECK(fixture_create(NULL, "/db/fep.db-journal", "JJJJ", 4) == VFS_OK);
    n_kprintf = 0; n_0hit = 0;
    CHECK(first_kanji(&d, &n)[0] == '\0' && n == 0);
    CHECK(d.db == 0);
    CHECK(n_kprintf >= 1 && strstr(last_kprintf, "remains"));
    CHECK(n_0hit == 0);                        /* 旧接続で検索していない */
    {
        FixtureFile *j = fixture_find("/db/fep.db-journal", 0);
        CHECK(j != NULL && j->size == 4 && !memcmp(j->data, "JJJJ", 4));
    }
    printf("PASS hot_journal_before_sql\n");

    /* (4b) 失効を伴わない I/O エラーの後に journal が残っている (古い接続の
     * 失敗した書き込みが残した形): 開き直しの判断で見つけて開かない */
    fixture_rm("/db/fep.db-journal");
    CHECK(ime_dict_open(&d, "/db/fep.db") == 0);   /* 頁キャッシュは空 */
    fail_reads = 1000000;
    fail_from = 1024;
    n_kprintf = 0;
    plant_journal = 1;
    CHECK(first_kanji(&d, &n)[0] == '\0' && n == 0);
    CHECK(plant_journal == 0);
    CHECK(d.db == 0);
    CHECK(n_kprintf >= 1 && strstr(last_kprintf, "remains"));
    CHECK(fixture_find("/db/fep.db-journal", 0) != NULL);   /* 触らない */
    fail_reads = 0;
    fail_from = 0;
    printf("PASS hot_journal\n");

    /* (5) 長さ 0 の journal は hot ではない — 開き直す */
    fixture_rm("/db/fep.db-journal");
    CHECK(fixture_create(NULL, "/db/fep.db-journal", "", 0) == VFS_OK);
    CHECK(ime_dict_open(&d, "/db/fep.db") == 0);
    replace_dict("火");
    open_files[dict_fd(&d)].stale = 1;
    CHECK(!strcmp(first_kanji(&d, &n), "火"));
    printf("PASS empty_journal\n");

    /* (6) 学習 (UPSERT) も失効で開き直して 1 回やり直す */
    replace_dict("科");
    open_files[dict_fd(&d)].stale = 1;
    n_kprintf = 0;
    ime_dict_learn(&d, "か", "科");
    CHECK(n_kprintf == 0);                    /* Learn failed を出していない */
    CHECK(!strcmp(first_kanji(&d, &n), "科"));
    printf("PASS learn_reopen\n");

    admin_apis(&d);
    sqlite_methods_stale();
    journal_name_len();

    printf("SUMMARY ime_dict PASS\n");
    return 0;
}
