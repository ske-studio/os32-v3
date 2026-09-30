/* ======================================================================== */
/*  ASSET_CORE.C -- アセットマネージャ コア (初期化・終了・ポンプ・デバッグ)  */
/* ======================================================================== */

#include <string.h>
#include "libos32asset.h"

/* ---- 内部構造体 (asset_internal.h 相当) ---- */
typedef struct {
    char    path[ASSET_MAX_PATH]; /* ファイルパス (キャッシュキー) */
    u8     *data;                 /* ロード済みデータへのポインタ */
    u32     size;                 /* データサイズ (バイト) */
    u16     ref_count;            /* 参照カウント */
    u8      state;                /* ASSET_STATE_* */
    u8      type;                 /* ASSET_TYPE_* */
    /* 非同期ロード用の中間状態 */
    int     _fd;                  /* ロード中のFD (-1=未使用) */
    u32     _loaded;              /* 読み込み済みバイト数 */
    u32     _total;               /* ファイル全体サイズ */
    /* 非同期コールバック */
    asset_load_callback _cb;
    void   *_cb_ctx;
} AssetEntry;

/* ---- グローバル状態 ---- */
static AssetEntry g_entries[ASSET_MAX_ENTRIES];
static char       g_base_path[ASSET_MAX_PATH];
static KernelAPI *g_api;

/* ====================================================================== */
/*  内部ヘルパー (他ファイルからも参照)                                     */
/* ====================================================================== */

/* 内部: エントリ配列とAPI取得 (asset_cache.c, asset_loader.c から参照) */
AssetEntry *asset__entries(void)  { return g_entries; }
KernelAPI  *asset__api(void)      { return g_api; }
const char *asset__base_path(void){ return g_base_path; }

/* 内部: 空きスロット検索 (-1=満杯) */
int asset__find_empty(void)
{
    int i;
    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        if (g_entries[i].state == ASSET_STATE_EMPTY) return i;
    }
    return -1;
}

/* 内部: フルパス構築 (base_path + path) */
void asset__build_path(char *out, int out_size, const char *path)
{
    int blen = (int)strlen(g_base_path);
    int plen = (int)strlen(path);

    if (blen + plen >= out_size) {
        /* オーバーフロー防止: pathだけにフォールバック */
        strncpy(out, path, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    memcpy(out, g_base_path, blen);
    memcpy(out + blen, path, plen);
    out[blen + plen] = '\0';
}

/* ====================================================================== */
/*  6. API -- システム管理                                                 */
/* ====================================================================== */

int asset_init(KernelAPI *api)
{
    if (!api) return -1;
    g_api = api;
    memset(g_entries, 0, sizeof(g_entries));
    g_base_path[0] = '\0';
    return 0;
}

void asset_shutdown(void)
{
    int i;
    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        if (g_entries[i].state == ASSET_STATE_LOADING && g_entries[i]._fd >= 0) {
            g_api->sys_close(g_entries[i]._fd);
        }
        if (g_entries[i].data) {
            g_api->mem_free(g_entries[i].data);
        }
    }
    memset(g_entries, 0, sizeof(g_entries));
    g_api = NULL;
}

void asset_set_base_path(const char *prefix)
{
    if (!prefix) {
        g_base_path[0] = '\0';
        return;
    }
    strncpy(g_base_path, prefix, ASSET_MAX_PATH - 1);
    g_base_path[ASSET_MAX_PATH - 1] = '\0';
}

void asset_pump(void)
{
    int i;
    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        AssetEntry *e = &g_entries[i];
        u32 remain, chunk;
        int rd;

        if (e->state != ASSET_STATE_LOADING) continue;
        if (e->_fd < 0) continue;

        remain = e->_total - e->_loaded;
        chunk = (remain < ASSET_CHUNK_SIZE) ? remain : ASSET_CHUNK_SIZE;

        rd = g_api->sys_read(e->_fd, e->data + e->_loaded, chunk);
        if (rd <= 0) {
            /* 読み込みエラー */
            g_api->sys_close(e->_fd);
            e->_fd = -1;
            e->state = ASSET_STATE_ERROR;
            continue;
        }

        e->_loaded += (u32)rd;

        if (e->_loaded >= e->_total) {
            /* 読み込み完了 */
            g_api->sys_close(e->_fd);
            e->_fd = -1;
            e->state = ASSET_STATE_READY;

            /* コールバック呼出 */
            if (e->_cb) {
                e->_cb((asset_handle_t)i, e->data, e->size, e->_cb_ctx);
            }
        }
    }
}

/* ====================================================================== */
/*  12. デバッグ                                                           */
/* ====================================================================== */

void asset_debug_dump(void)
{
    static const char *state_names[] = {"EMPTY", "LOADING", "READY", "ERROR"};
    int i;
    int count = 0;
    u32 total_mem = 0;

    g_api->kprintf(ATTR_CYAN, "--- Asset Manager Dump ---\n");
    g_api->kprintf(ATTR_CYAN, "base_path: \"%s\"\n", g_base_path);

    for (i = 0; i < ASSET_MAX_ENTRIES; i++) {
        AssetEntry *e = &g_entries[i];
        if (e->state == ASSET_STATE_EMPTY) continue;

        g_api->kprintf(ATTR_WHITE,
            "[%2d] %s ref=%d size=%lu state=%s\n",
            i, e->path, (int)e->ref_count, e->size,
            state_names[e->state < 4 ? e->state : 0]);

        if (e->state == ASSET_STATE_LOADING) {
            g_api->kprintf(ATTR_YELLOW, "     progress: %lu/%lu\n",
                e->_loaded, e->_total);
        }

        count++;
        total_mem += e->size;
    }

    g_api->kprintf(ATTR_CYAN, "entries: %d/%d, mem: %lu bytes\n",
        count, ASSET_MAX_ENTRIES, total_mem);
}
