/* ======================================================================== */
/*  SAVE_TEST.C — libos32save テストプログラム                              */
/* ======================================================================== */

#include "os32api.h"
#include "libos32save.h"
#include "rt/testresult.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

extern KernelAPI *kapi;
#define api kapi

/* 保存先は HostDrv (票 docs/archive/test/TASK_TEST_RESULT.md §3)。**帯域は 1 か所**
 * — 以前は "/host/..." を 9 か所に直接書いていたので、HostDrv の無い構成では
 * 全項目が落ちて「不合格」に見えていた。今は SAVE_TEST_DIR が載っていなければ
 * 走らせずに SKIP (終了コード 2) を返す。 */
#define SAVE_TEST_DIR    "/host"
#define SAVE_TEST_FILE   SAVE_TEST_DIR "/save_test.dat"
#define SAVE_PEEK_FILE   SAVE_TEST_DIR "/save_peek.dat"
#define SAVE_MIGR_FILE   SAVE_TEST_DIR "/save_migrate.dat"

static int g_total;
static int g_passed;

static void check(const char *label, int cond)
{
    g_total++;
    if (cond) {
        g_passed++;
        api->kprintf(ATTR_GREEN, "  [OK] %s\n", label);
    } else {
        api->kprintf(ATTR_RED, "  [NG] %s\n", label);
    }
}

static void check_eq(const char *label, int got, int expect)
{
    g_total++;
    if (got == expect) {
        g_passed++;
        api->kprintf(ATTR_GREEN, "  [OK] %s = %d\n", label, got);
    } else {
        api->kprintf(ATTR_RED, "  [NG] %s: got %d, expect %d\n",
                     label, got, expect);
    }
}

static void header(const char *title)
{
    api->kprintf(ATTR_CYAN, "\n=== %s ===\n", title);
}

/* テスト用データ構造体 */
typedef struct {
    int  val1;
    char text[16];
    u32  val2;
} TestSaveData;

/* ファイル破損エミュレーション */
static int corrupt_file(const char *path, int offset)
{
    int fd = open(path, O_RDWR);
    u8 b;

    if (fd < 0) return -1;

    lseek(fd, offset, SEEK_SET);
    if (read(fd, &b, 1) != 1) {
        close(fd);
        return -1;
    }

    b ^= 0xFF; /* ビット反転 */

    lseek(fd, offset, SEEK_SET);
    if (write(fd, &b, 1) != 1) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/* ====================================================================== */
/*  テスト1: 基本的な往復ロード/セーブ (正常形)                              */
/* ====================================================================== */
static void test_roundtrip(void)
{
    SaveContext sc;
    TestSaveData s_data, l_data;
    int rc;

    header("Test 1: Normal Save and Read Roundtrip");

    /* テストデータの初期化 */
    s_data.val1 = 12345;
    strcpy(s_data.text, "HelloOS32");
    s_data.val2 = 0xDEADBEEF;

    memset(&l_data, 0, sizeof(l_data));

    /* セーブ側コンテキスト設定 */
    save_begin(&sc, "TEST", 1);
    rc = save_add_region(&sc, &s_data, sizeof(s_data), 101);
    check_eq("save_add_region", rc, 0);

    /* 書き込み */
    rc = save_write(&sc, SAVE_TEST_FILE, 42); /* user_meta = 42 */
    check_eq("save_write", rc, 0);

    /* ロード側コンテキスト設定 */
    save_begin(&sc, "TEST", 1);
    save_add_region(&sc, &l_data, sizeof(l_data), 101);

    /* 読み込みと検証 */
    rc = save_read(&sc, SAVE_TEST_FILE);
    check_eq("save_read", rc, 0);

    /* データの往復一致確認 */
    check_eq("loaded val1 = 12345", l_data.val1, 12345);
    check("loaded text = HelloOS32", strcmp(l_data.text, "HelloOS32") == 0);
    check("loaded val2 = 0xDEADBEEF", l_data.val2 == 0xDEADBEEF);
}

/* ====================================================================== */
/*  テスト2: マジックコード不一致とCRCエラー検出                            */
/* ====================================================================== */
static void test_errors(void)
{
    SaveContext sc;
    TestSaveData l_data;
    int rc;

    header("Test 2: Verification and Error Handling");

    /* 1. マジックコード不一致 */
    save_begin(&sc, "BADM", 1); /* 期待値を "BADM" に設定 */
    save_add_region(&sc, &l_data, sizeof(l_data), 101);
    rc = save_read(&sc, SAVE_TEST_FILE);
    check_eq("magic mismatch returns -2", rc, -2);

    /* 2. CRC破損検出 (ファイルを1バイト壊す) */
    corrupt_file(SAVE_TEST_FILE, 10); /* ヘッダ内のどこかを破壊 */
    save_begin(&sc, "TEST", 1);
    save_add_region(&sc, &l_data, sizeof(l_data), 101);
    rc = save_read(&sc, SAVE_TEST_FILE);
    check_eq("corrupted data returns -3", rc, -3);
}

/* ====================================================================== */
/*  テスト3: メタ情報の覗き見 (save_peek)                                   */
/* ====================================================================== */
static void test_peek(void)
{
    SaveMeta sm;
    int rc;

    TestSaveData s_data;
    SaveContext sc;

    header("Test 3: Metadata Peek");

    /* 再度正常なファイルを作成 */
    s_data.val1 = 999;
    strcpy(s_data.text, "PeekTest");
    s_data.val2 = 0x777;

    save_begin(&sc, "PEEK", 2);
    save_add_region(&sc, &s_data, sizeof(s_data), 102);
    save_write(&sc, SAVE_PEEK_FILE, 9999);

    /* ロードせずにヘッダを覗き見る */
    rc = save_peek(SAVE_PEEK_FILE, &sm);
    check_eq("save_peek rc=0", rc, 0);
    check("magic is PEEK", memcmp(sm.magic, "PEEK", 4) == 0);
    check_eq("version = 2", (int)sm.version, 2);
    check_eq("user_meta = 9999", (int)sm.user_meta, 9999);
    check("total_size is valid", sm.total_size > 0);
    api->kprintf(ATTR_WHITE, "    peek metadata: size=%d, mtime=%d\n",
                 sm.total_size, sm.mtime);
}

/* ====================================================================== */
/*  テスト4: バージョン移行 (migration)                                     */
/* ====================================================================== */
typedef struct {
    u16 old_field;
} OldStruct;

typedef struct {
    u32 new_field;
} NewStruct;

static int test_migrate_cb(u32 old_ver, u32 new_ver,
                           const void *region_data, u16 region_id, u32 old_size,
                           void *dest_ptr, u32 dest_size)
{
    if (old_ver == 1 && new_ver == 2 && region_id == 202) {
        const OldStruct *old_s = (const OldStruct *)region_data;
        NewStruct *new_s = (NewStruct *)dest_ptr;

        (void)old_size; (void)dest_size;

        /* 古い型の u16 を新しい型の u32 に変換してコピー */
        new_s->new_field = (u32)old_s->old_field * 10;
        return 0;
    }
    return -1;
}

static void test_migration(void)
{
    SaveContext sc;
    OldStruct s_data;
    NewStruct l_data;
    int rc;

    header("Test 4: Version Migration");

    s_data.old_field = 45;

    /* 1. バージョン 1 で書き込み */
    save_begin(&sc, "MIGR", 1);
    save_add_region(&sc, &s_data, sizeof(s_data), 202);
    save_write(&sc, SAVE_MIGR_FILE, 1);

    /* 2. 移行コールバックの設定 */
    save_set_migrate_cb(test_migrate_cb);

    /* 3. バージョン 2 としてロード */
    memset(&l_data, 0, sizeof(l_data));
    save_begin(&sc, "MIGR", 2);
    save_add_region(&sc, &l_data, sizeof(l_data), 202);

    rc = save_read(&sc, SAVE_MIGR_FILE);
    check_eq("save_read with migration success", rc, 0);
    check_eq("migrated field (45 * 10) = 450", (int)l_data.new_field, 450);

    /* 4. コールバッククリア */
    save_set_migrate_cb((save_migrate_fn)0);
}

/* ====================================================================== */
/*  メイン                                                                 */
/* ====================================================================== */
int main(int argc, char **argv, KernelAPI *k)
{
    (void)argc; (void)argv; (void)k;

    g_total = 0;
    g_passed = 0;

    api->kprintf(ATTR_CYAN, "save_test: libos32save test suite\n");

    if (!api->sys_is_mounted(SAVE_TEST_DIR)) {
        return os32_test_summary_skip(api, "save_test",
                                      SAVE_TEST_DIR " is not mounted");
    }

    test_roundtrip();
    test_errors();
    test_peek();
    test_migration();

    return os32_test_summary(api, "save_test", g_passed, g_total);
}
