/* ======================================================================== */
/*  ASSET_LOADER.C -- ファイルI/O + 同期/非同期ロード                       */
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
extern AssetEntry  *asset__entries(void);
extern KernelAPI   *asset__api(void);
extern const char  *asset__base_path(void);
extern int          asset__find_empty(void);
extern void         asset__build_path(char *out, int out_size, const char *path);

/* ====================================================================== */
/*  同期ロード                                                             */
/* ====================================================================== */

asset_handle_t asset_load(const char *path, int type)
{
    AssetEntry *entries = asset__entries();
    KernelAPI *api = asset__api();
    char fullpath[ASSET_MAX_PATH];
    asset_handle_t existing;
    int slot, fd, rd;
    OS32_Stat st;
    AssetEntry *e;

    if (!api || !path) return ASSET_INVALID;

    /* キャッシュ検索 */
    existing = asset_find(path);
    if (existing != ASSET_INVALID) {
        asset_retain(existing);
        return existing;
    }

    /* 空きスロット確保 */
    slot = asset__find_empty();
    if (slot < 0) return ASSET_INVALID;

    /* フルパス構築 */
    asset__build_path(fullpath, sizeof(fullpath), path);

    /* ファイルサイズ取得 */
    if (api->sys_stat(fullpath, &st) != 0) return ASSET_INVALID;
    if (st.st_size == 0) return ASSET_INVALID;

    /* メモリ確保 */
    e = &entries[slot];
    e->data = (u8 *)api->mem_alloc(st.st_size);
    if (!e->data) return ASSET_INVALID;

    /* ファイル読み込み */
    fd = api->sys_open(fullpath, KAPI_O_RDONLY);
    if (fd < 0) {
        api->mem_free(e->data);
        e->data = NULL;
        return ASSET_INVALID;
    }

    rd = api->sys_read(fd, e->data, st.st_size);
    api->sys_close(fd);

    if (rd != (int)st.st_size) {
        api->mem_free(e->data);
        e->data = NULL;
        return ASSET_INVALID;
    }

    /* エントリ設定 */
    strncpy(e->path, path, ASSET_MAX_PATH - 1);
    e->path[ASSET_MAX_PATH - 1] = '\0';
    e->size = st.st_size;
    e->ref_count = 1;
    e->state = ASSET_STATE_READY;
    e->type = (u8)type;
    e->_fd = -1;
    e->_loaded = 0;
    e->_total = 0;
    e->_cb = NULL;
    e->_cb_ctx = NULL;

    return (asset_handle_t)slot;
}

/* ====================================================================== */
/*  非同期ロード                                                           */
/* ====================================================================== */

asset_handle_t asset_load_async(const char *path, int type,
                                 asset_load_callback cb, void *user_ctx)
{
    AssetEntry *entries = asset__entries();
    KernelAPI *api = asset__api();
    char fullpath[ASSET_MAX_PATH];
    asset_handle_t existing;
    int slot, fd;
    OS32_Stat st;
    AssetEntry *e;

    if (!api || !path) return ASSET_INVALID;

    /* キャッシュ検索 */
    existing = asset_find(path);
    if (existing != ASSET_INVALID) {
        asset_retain(existing);
        /* 既にREADYなら即座にコールバック */
        if (cb && entries[existing].state == ASSET_STATE_READY) {
            cb(existing, entries[existing].data,
               entries[existing].size, user_ctx);
        }
        return existing;
    }

    /* 空きスロット確保 */
    slot = asset__find_empty();
    if (slot < 0) return ASSET_INVALID;

    /* フルパス構築 */
    asset__build_path(fullpath, sizeof(fullpath), path);

    /* ファイルサイズ取得 */
    if (api->sys_stat(fullpath, &st) != 0) return ASSET_INVALID;
    if (st.st_size == 0) return ASSET_INVALID;

    /* メモリ事前確保 */
    e = &entries[slot];
    e->data = (u8 *)api->mem_alloc(st.st_size);
    if (!e->data) return ASSET_INVALID;

    /* FDオープン (クローズは asset_pump で行う) */
    fd = api->sys_open(fullpath, KAPI_O_RDONLY);
    if (fd < 0) {
        api->mem_free(e->data);
        e->data = NULL;
        return ASSET_INVALID;
    }

    /* エントリ設定 (LOADING状態で返す) */
    strncpy(e->path, path, ASSET_MAX_PATH - 1);
    e->path[ASSET_MAX_PATH - 1] = '\0';
    e->size = st.st_size;
    e->ref_count = 1;
    e->state = ASSET_STATE_LOADING;
    e->type = (u8)type;
    e->_fd = fd;
    e->_loaded = 0;
    e->_total = st.st_size;
    e->_cb = cb;
    e->_cb_ctx = user_ctx;

    return (asset_handle_t)slot;
}

/* ====================================================================== */
/*  進捗取得                                                               */
/* ====================================================================== */

int asset_progress(asset_handle_t h)
{
    AssetEntry *entries = asset__entries();

    if (h < 0 || h >= ASSET_MAX_ENTRIES) return 0;

    if (entries[h].state == ASSET_STATE_READY) return 100;
    if (entries[h].state != ASSET_STATE_LOADING) return 0;
    if (entries[h]._total == 0) return 0;

    return (int)((entries[h]._loaded * 100) / entries[h]._total);
}
