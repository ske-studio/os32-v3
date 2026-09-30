/* ======================================================================== */
/*  ASSET_CACHE.C -- キャッシュ管理 (検索・参照カウント・解放・統計)          */
/* ======================================================================== */

#include <string.h>
#include "libos32asset.h"

/* ---- asset_core.c で定義された内部構造体 ---- */
typedef struct {
    char    path[ASSET_MAX_PATH];
    u8     *data;
    u32     size;
    u16     ref_count;
    u8      state;
    u8      type;
    int     _fd;
    u32     _loaded;
    u32     _total;
    asset_load_callback _cb;
    void   *_cb_ctx;
} AssetEntry;

/* asset_core.c の内部関数 */
extern AssetEntry *asset__entries(void);
extern KernelAPI  *asset__api(void);

/* ====================================================================== */
/*  キャッシュ検索                                                         */
/* ====================================================================== */

asset_handle_t asset_find(const char *path)
{
    AssetEntry *entries = asset__entries();
    int i;

    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        if (entries[i].state != ASSET_STATE_EMPTY &&
            strcmp(entries[i].path, path) == 0) {
            return (asset_handle_t)i;
        }
    }
    return ASSET_INVALID;
}

/* ====================================================================== */
/*  アセットアクセス                                                       */
/* ====================================================================== */

const void *asset_data(asset_handle_t h)
{
    AssetEntry *entries = asset__entries();
    if (h < 0 || h >= ASSET_MAX_ENTRIES) return NULL;
    if (entries[h].state != ASSET_STATE_READY) return NULL;
    return entries[h].data;
}

u32 asset_size(asset_handle_t h)
{
    AssetEntry *entries = asset__entries();
    if (h < 0 || h >= ASSET_MAX_ENTRIES) return 0;
    return entries[h].size;
}

int asset_state(asset_handle_t h)
{
    AssetEntry *entries = asset__entries();
    if (h < 0 || h >= ASSET_MAX_ENTRIES) return ASSET_STATE_EMPTY;
    return entries[h].state;
}

/* ====================================================================== */
/*  参照カウント管理                                                       */
/* ====================================================================== */

void asset_retain(asset_handle_t h)
{
    AssetEntry *entries = asset__entries();
    if (h < 0 || h >= ASSET_MAX_ENTRIES) return;
    if (entries[h].state == ASSET_STATE_EMPTY) return;
    entries[h].ref_count++;
}

void asset_release(asset_handle_t h)
{
    AssetEntry *entries = asset__entries();
    KernelAPI *api = asset__api();

    if (h < 0 || h >= ASSET_MAX_ENTRIES) return;
    if (entries[h].state == ASSET_STATE_EMPTY) return;
    if (entries[h].ref_count == 0) return;

    entries[h].ref_count--;

    if (entries[h].ref_count == 0) {
        /* ロード中なら FD をクローズ */
        if (entries[h].state == ASSET_STATE_LOADING && entries[h]._fd >= 0) {
            api->sys_close(entries[h]._fd);
        }
        /* メモリ解放 */
        if (entries[h].data) {
            api->mem_free(entries[h].data);
        }
        memset(&entries[h], 0, sizeof(AssetEntry));
    }
}

void asset_release_all(void)
{
    AssetEntry *entries = asset__entries();
    KernelAPI *api = asset__api();
    int i;

    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        if (entries[i].state == ASSET_STATE_EMPTY) continue;

        if (entries[i].state == ASSET_STATE_LOADING && entries[i]._fd >= 0) {
            api->sys_close(entries[i]._fd);
        }
        if (entries[i].data) {
            api->mem_free(entries[i].data);
        }
        memset(&entries[i], 0, sizeof(AssetEntry));
    }
}

/* ====================================================================== */
/*  キャッシュ統計                                                         */
/* ====================================================================== */

int asset_cached_count(void)
{
    AssetEntry *entries = asset__entries();
    int i, count = 0;

    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        if (entries[i].state != ASSET_STATE_EMPTY) count++;
    }
    return count;
}

u32 asset_mem_used(void)
{
    AssetEntry *entries = asset__entries();
    int i;
    u32 total = 0;

    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        if (entries[i].state != ASSET_STATE_EMPTY) {
            total += entries[i].size;
        }
    }
    return total;
}
