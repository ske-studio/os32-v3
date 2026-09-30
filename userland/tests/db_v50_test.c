/* ======================================================================== */
/*  DB_V50_TEST.C — KAPI v50 (設定レジストリの基盤) の CPL=3 受入 (票 S0-K K2)*/
/*                                                                          */
/*  ここで踏むのは「CPL=3 から呼んだときだけ分かること」だけ。SQL の意味や    */
/*  診断の保持規則はホスト試験 (tools/tests/test_kapi_db_v50.py) が見る。     */
/*                                                                          */
/*    1. RO で存在しない DB を開いても **ファイルが作られない**              */
/*       (`db_open_existing` は CREATE を付けない)。                         */
/*    2. `db_error_code(-1)` に直前の open 失敗コードが残る。                 */
/*    3. prepare_only + bind + step で 1 行取れる (prepare だけでは進まない)。*/
/*    4. 4096B の blob が往復する。                                          */
/*    5. **不正範囲**のポインタ / 長さが -1 で断られ、この**試験プログラムが  */
/*       落ちない**。落ちたら (fault_kill_count が増えたら) 不合格 —         */
/*       カーネルが写す前に弾けていないということ。                          */
/*                                                                          */
/*  使い方: `db_v50_test [work.db]` (既定 /tmp/db_v50.db)。                   */
/*  合格は最後の "db_v50_test: PASS n/n" と、fault_kill_count が不変なこと。  */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"

/* CPL=3 アプリの許可帯 (exec/exec.c の ring3_ptr_ok)。番地は
 * userland/tests/ring3_guard.c と同じ **アプリ固有 PDE 1 枚** の既定配置を
 * 前提にする — このプログラムは build/app.conf でヒープを要求しない (0) ので
 * `paging_app_band_pdes` は必ず 1 を返し、帯の上端は MEM_APP_BAND_TOP。 */
#define BAND_TOP        0x800000UL   /* MEM_APP_BAND_TOP (スタック帯の上端) */
#define VRAM_END        0x0C0000UL   /* 許可帯 [0xA0000, 0xC0000) の末尾 */
/* 「許可帯の中だが非 present」= sbrk 上限のすぐ上 (guard_a)。
 * **ここは試験しない** (票 §1a の改定、2026-09-13): 検証は帯と長さだけを見る
 * ので -1 では返らず、カーネルが写した瞬間に #PF → 呼び手が kill される
 * (kprintf の %s など他の KAPI と同じ既定の扱い)。この試験プログラムは
 * 「落ちないこと」を合格条件にしているので、同じプロセスでは踏めない。 */

static int passed;
static int failed;
static KernelAPI *g;

/* KAPI が SHM に書いたエラー文。`db_last_error()` は **slot の状態**を返す口で、
 * open が slot を掴む前に失敗したときは "invalid handle" にしかならない
 * (実機 K2 の 2 つ目の観測がこれ)。open 失敗の理由は SHM の
 * DB_ResultHeader.error_offset の先にある。 */
static const char *shm_error(void)
{
    const DB_ResultHeader *hdr;
    if (!g || g->shm_base == 0) return "(no shm)";
    hdr = (const DB_ResultHeader *)g->shm_base;
    if (hdr->status != DB_STATUS_ERROR || hdr->error_offset <= 0)
        return "(no error text)";
    return (const char *)(g->shm_base + (u32)hdr->error_offset);
}

/* 文字列比較 (newlib を引かずに済ませる。C89)。 */
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static void ok(int cond, const char *name)
{
    if (cond) {
        passed++;
    } else {
        failed++;
        g->kprintf(0x41, "  FAIL: %s\n", name);
        /* 失敗したその場で診断を出す。実機は 1 回走らせるのに配備が要るので、
         * 「-1 だった」だけ持ち帰っても次の一手が決まらない。owner 別の
         * 「直前 open 失敗」コードと、SHM に残っているエラー文を並べる。 */
        g->kprintf(0x06, "        open_fail=%d shm_error=%s\n",
                   g->db_error_code(-1), shm_error());
    }
}

/* 使い捨ての作業 DB を legacy db_open (= CREATE 付き) で用意する。
 * v50 の open は既存 DB しか開けないので、土台はこちらで作る。 */
static int make_fixture(const char *path)
{
    int h = g->db_open(path);
    if (h < 0) return -1;
    if (g->db_exec(h, "DROP TABLE IF EXISTS v50") != 0) { g->db_close(h); return -1; }
    if (g->db_exec(h, "CREATE TABLE v50(k TEXT, n INT, b BLOB)") != 0) {
        g->db_close(h);
        return -1;
    }
    g->db_close(h);
    return 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    static char blob_out[4096];
    static char blob_in[4096];
    static char no_nul[OS32_MAX_PATH + 64];
    const char *work = "/tmp/db_v50.db";
    const char *missing = "/tmp/db_v50_nosuch.db";
    int h, rc, i, code;

    g = api;
    passed = 0;
    failed = 0;
    if (argc >= 2) work = argv[1];

    api->kprintf(0xE1, "db_v50_test: KAPI v%d\n", (int)api->version);
    if (api->version < 50) {
        return os32_test_summary_skip(api, "db_v50_test",
                                      "kernel is older than KAPI v50");
    }

    /* ---- (1) RO で欠損 DB を開いても作られない ------------------------- */
    ok(api->db_open_existing(missing, 0) < 0, "RO open of a missing db fails");
    ok(api->db_open_existing(missing, 1) < 0, "RW open of a missing db fails");
    /* legacy db_open は CREATE 付きなので、作られていたらここで開けてしまう。
     * 「開けない」= ファイルが無い、という確かめ方はできないので stat を使う。*/
    {
        OS32_Stat st;
        ok(api->sys_stat(missing, &st) != 0, "the missing db was not created");
    }

    /* ---- (2) db_error_code(-1) に直前の open 失敗が残る ----------------- */
    code = api->db_error_code(-1);
    ok(code != 0, "db_error_code(-1) keeps the open failure");
    api->kprintf(0x07, "  open failure code = %d\n", code);
    ok(api->db_error_code(999) != 0, "an out-of-range handle is MISUSE");

    /* ---- 作業 DB を用意して RW で開く ---------------------------------- */
    if (make_fixture(work) != 0) {
        /* 作業 DB が作れない = 前提が無い (既定の /tmp は新規インストール直後に
         * 無いことがある)。不合格ではなく SKIP。別の場所は argv[1] で渡す。 */
        api->kprintf(0x41, "db_v50_test: cannot create %s\n", work);
        return os32_test_summary_skip(
            api, "db_v50_test",
            "cannot create the work db (pass a path as argv[1])");
    }
    /* 土台が本当に「中身のある既存 DB」か。0 バイトだと db_open_existing は
     * 契約どおり NOTADB で断るので、それを RW open の失敗と読み違えない。 */
    {
        OS32_Stat fst;
        int frc = api->sys_stat(work, &fst);
        api->kprintf(0x07, "  fixture %s stat=%d size=%d\n",
                     work, frc, (int)fst.st_size);
        ok(frc == 0 && fst.st_size > 0, "the fixture db exists and is non-empty");
    }
    /* RO でも開けるか (RW 固有の段 = journal_mode の照会を切り分ける)。 */
    h = api->db_open_existing(work, 0);
    ok(h >= 0, "RO open of an existing db");
    if (h >= 0) api->db_close(h);

    h = api->db_open_existing(work, 1);
    ok(h >= 0, "RW open of an existing db");
    if (h < 0) {
        api->kprintf(0x41, "db_v50_test: aborted\n");
        return os32_test_summary(api, "db_v50_test", passed, passed + failed);
    }
    ok(api->db_error_code(h) == 0, "a fresh handle has no failure");

    /* ---- (5) 不正範囲: 先頭は許可帯の中、範囲が外へ出る ---------------- */
    ok(api->db_prepare_only(h, "INSERT INTO v50(k,n,b) VALUES(?,?,?)") == 0,
       "prepare_only of a single statement");
    ok(api->db_bind_text(h, 1, (const char *)(VRAM_END - 1), 2) < 0,
       "text range crossing the end of a permitted band is refused");
    ok(api->db_bind_text(h, 1, (const char *)(BAND_TOP - 1), 2) < 0,
       "text range crossing the top of the app band is refused");
    /* 帯の**外**へ出る範囲だけを見る。帯の中の未マップページ (sbrk 上限〜
     * guard) は -1 ではなく kill なので、ここでは踏まない (上の注記)。 */
    ok(api->db_bind_text(h, 1, "x", -1) < 0, "a negative length is refused");
    ok(api->db_bind_text(h, 1, (const char *)0, 0) < 0,
       "a NULL text pointer is refused (db_bind_null is the way)");
    ok(api->db_bind_blob(h, 3, blob_in, 4097) < 0, "4097B blob is refused");

    /* NUL の無い path (上限まで探して見つからない)。切り捨てて開かないこと。*/
    for (i = 0; i < (int)sizeof(no_nul); i++) no_nul[i] = 'a';
    ok(api->db_open_existing(no_nul, 0) < 0, "a path without a NUL is refused");

    /* ---- (3)(4) prepare_only + bind + step で 1 行 ---------------------- */
    for (i = 0; i < 4096; i++) blob_in[i] = (char)(i & 0xFF);
    ok(api->db_bind_text(h, 1, "color", 5) == 0, "bind_text");
    ok(api->db_bind_int(h, 2, 12345) == 0, "bind_int");
    ok(api->db_bind_blob(h, 3, blob_in, 4096) == 0, "bind_blob 4096B");
    ok(api->db_bind_int(h, 0, 1) < 0, "index 0 is refused (1-based)");
    ok(api->db_bind_int(h, 9, 1) < 0, "an out-of-range index is refused");
    ok(api->db_step(h) == DB_STATUS_DONE, "step runs the bound INSERT once");

    /* prepare_only は step しない: SELECT の先頭行が進まないこと。 */
    ok(api->db_prepare_only(h, "SELECT n FROM v50 WHERE k = ?") == 0,
       "prepare_only of a SELECT");
    ok(api->db_bind_text(h, 1, "color", 5) == 0, "bind after prepare_only");
    rc = api->db_step(h);
    ok(rc == DB_STATUS_ROW, "the first row arrives only after step");
    ok(api->db_column_int(h, 0) == 12345, "the bound int came back");
    ok(api->db_bind_int(h, 1, 1) < 0, "bind after the first step is refused");
    api->db_finalize(h);

    /* blob の往復 (SHM の 16KB ブロックに 4096B + descriptor が収まる)。 */
    ok(api->db_prepare_only(h, "SELECT b FROM v50 WHERE k = ?") == 0,
       "prepare_only for the blob");
    ok(api->db_bind_text(h, 1, "color", 5) == 0, "bind for the blob");
    if (api->db_step(h) == DB_STATUS_ROW) {
        const char *p = api->db_column_text(h, 0);
        int same = 1;
        for (i = 0; i < 4096; i++) blob_out[i] = p ? p[i] : 0;
        for (i = 0; i < 4096; i++) {
            if (blob_out[i] != blob_in[i]) { same = 0; break; }
        }
        ok(p != 0 && same, "4096B blob roundtrip");
    } else {
        ok(0, "4096B blob roundtrip (no row)");
    }
    api->db_finalize(h);

    /* ---- SQLITE_TRANSIENT: スクラッチを上書きしても先の値が残る -------- */
    /* 2 本の bind_text は**同じ**カーネルスクラッチを使う。SQLite が
     * 自分で写していなければ 1 本目が 2 本目の値に化ける。 */
    ok(api->db_prepare_only(h, "SELECT ?, ?") == 0, "prepare_only of SELECT ?,?");
    ok(api->db_bind_text(h, 1, "first", 5) == 0, "bind_text #1");
    ok(api->db_bind_text(h, 2, "second", 6) == 0, "bind_text #2 (same scratch)");
    if (api->db_step(h) == DB_STATUS_ROW) {
        ok(same(api->db_column_text(h, 0), "first"), "TRANSIENT kept the 1st bind");
        ok(same(api->db_column_text(h, 1), "second"), "the 2nd bind is intact");
    } else {
        ok(0, "TRANSIENT kept the 1st bind (no row)");
    }
    api->db_finalize(h);

    /* ---- 複数 statement / 長すぎる SQL は拒否 -------------------------- */
    ok(api->db_prepare_only(h, "SELECT 1; SELECT 2") < 0,
       "a second statement is refused");
    ok(api->db_prepare_only(h, "SELECT 1 -- trailing comment") == 0,
       "a trailing comment is allowed");
    api->db_finalize(h);

    /* ---- 後片付けは診断を消さない -------------------------------------- */
    ok(api->db_prepare_only(h, "SELECT * FROM no_such_table") < 0,
       "prepare_only of a bad table fails");
    code = api->db_error_code(h);
    ok(code != 0, "the failure is kept");
    api->db_finalize(h);
    ok(api->db_error_code(h) == code, "finalize does not clear the diagnosis");
    api->db_close(h);
    ok(api->db_error_code(h) == code, "close does not clear the diagnosis");

    return os32_test_summary(api, "db_v50_test", passed, passed + failed);
}
