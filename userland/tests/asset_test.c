/* ======================================================================== */
/*  ASSET_TEST.C -- libos32asset テストプログラム                            */
/*                                                                          */
/*  テスト内容:                                                              */
/*    1. 同期ロード (既存ファイル読み込み)                                    */
/*    2. キャッシュヒット (同じパスの二重ロード → ref_count確認)             */
/*    3. 参照カウント (release → メモリ解放確認)                             */
/*    4. 非同期ロード (asset_load_async + asset_pump)                       */
/*    5. エラーケース (存在しないパス)                                       */
/* ======================================================================== */

#include <stdio.h>
#include <string.h>
#include "os32api.h"
#include "libos32asset.h"
#include "rt/testresult.h"

static KernelAPI *api;
static int test_pass = 0;
static int test_fail = 0;

/* テスト結果表示 */
static void check(const char *name, int cond)
{
    if (cond) {
        api->kprintf(ATTR_GREEN, "  PASS: %s\n", name);
        test_pass++;
    } else {
        api->kprintf(ATTR_RED, "  FAIL: %s\n", name);
        test_fail++;
    }
}

/* 非同期コールバック用 */
static int async_called = 0;
static u32 async_size = 0;

static void on_async_done(asset_handle_t h, const void *data,
                           u32 size, void *ctx)
{
    (void)h;
    (void)data;
    (void)ctx;
    async_called = 1;
    async_size = size;
}

int main(int argc, char **argv, KernelAPI *sys_api)
{
    asset_handle_t h1, h2, h3, h4;
    int i;

    (void)argc;
    (void)argv;
    api = sys_api;

    api->kprintf(ATTR_CYAN, "=== libos32asset Test ===\n\n");

    /* ---- 初期化 ---- */
    api->kprintf(ATTR_WHITE, "[1] Init\n");
    check("asset_init", asset_init(api) == 0);
    check("cached_count == 0", asset_cached_count() == 0);
    check("mem_used == 0", asset_mem_used() == 0);

    /* ---- 同期ロード (/etc/filetypes を使用) ---- */
    api->kprintf(ATTR_WHITE, "\n[2] Sync Load\n");
    h1 = asset_load("/etc/filetypes", ASSET_TYPE_RAW);
    check("load /etc/filetypes != INVALID", h1 != ASSET_INVALID);

    if (h1 != ASSET_INVALID) {
        check("state == READY", asset_state(h1) == ASSET_STATE_READY);
        check("data != NULL", asset_data(h1) != NULL);
        check("size > 0", asset_size(h1) > 0);
        check("cached_count == 1", asset_cached_count() == 1);
        check("mem_used > 0", asset_mem_used() > 0);

        api->kprintf(ATTR_YELLOW, "  file size: %lu bytes\n", asset_size(h1));
    }

    /* ---- キャッシュヒット ---- */
    api->kprintf(ATTR_WHITE, "\n[3] Cache Hit\n");
    h2 = asset_load("/etc/filetypes", ASSET_TYPE_RAW);
    check("second load == same handle", h2 == h1);
    check("cached_count still 1", asset_cached_count() == 1);

    /* ---- 参照カウント ---- */
    api->kprintf(ATTR_WHITE, "\n[4] Ref Count\n");
    asset_release(h2);
    check("after 1st release: still cached", asset_cached_count() == 1);
    asset_release(h1);
    check("after 2nd release: freed", asset_cached_count() == 0);
    check("mem_used == 0", asset_mem_used() == 0);

    /* ---- 非同期ロード ---- */
    api->kprintf(ATTR_WHITE, "\n[5] Async Load\n");
    async_called = 0;
    async_size = 0;
    h3 = asset_load_async("/etc/filetypes", ASSET_TYPE_RAW,
                           on_async_done, NULL);
    check("async load != INVALID", h3 != ASSET_INVALID);

    if (h3 != ASSET_INVALID) {
        /* pumpを回してロード完了まで待つ */
        for (i = 0; i < 1000; i++) {
            asset_pump();
            if (asset_state(h3) == ASSET_STATE_READY) break;
        }

        check("async state == READY", asset_state(h3) == ASSET_STATE_READY);
        check("async callback called", async_called == 1);
        check("async size > 0", async_size > 0);
        check("progress == 100", asset_progress(h3) == 100);

        asset_release(h3);
    }

    /* ---- エラーケース ---- */
    api->kprintf(ATTR_WHITE, "\n[6] Error Cases\n");
    h4 = asset_load("/nonexistent/file.dat", ASSET_TYPE_RAW);
    check("load nonexistent == INVALID", h4 == ASSET_INVALID);

    check("find nonexistent == INVALID",
          asset_find("/nonexistent/file.dat") == ASSET_INVALID);

    /* ---- base_path テスト ---- */
    api->kprintf(ATTR_WHITE, "\n[7] Base Path\n");
    asset_set_base_path("/etc/");
    h1 = asset_load("filetypes", ASSET_TYPE_RAW);
    check("load with base_path != INVALID", h1 != ASSET_INVALID);
    if (h1 != ASSET_INVALID) {
        check("data != NULL", asset_data(h1) != NULL);
        asset_release(h1);
    }
    asset_set_base_path("");

    /* ---- デバッグダンプ ---- */
    api->kprintf(ATTR_WHITE, "\n[8] Debug Dump\n");
    h1 = asset_load("/etc/filetypes", ASSET_TYPE_RAW);
    asset_debug_dump();
    if (h1 != ASSET_INVALID) asset_release(h1);

    /* ---- 終了 ---- */
    asset_shutdown();

    return os32_test_summary(api, "asset_test",
                             test_pass, test_pass + test_fail);
}
