/* ======================================================================== */
/*  ASSET_DEMO.C -- libos32asset 非同期ロード デモプログラム                 */
/*                                                                          */
/*  複数ファイルを非同期ロードし、テキストベースのプログレスバーで            */
/*  読み込み進捗をリアルタイム表示する。                                      */
/*  ロード完了後、各アセットの情報をダンプして終了。                          */
/* ======================================================================== */

#include <stdio.h>
#include <string.h>
#include "os32api.h"
#include "libos32asset.h"

static KernelAPI *api;

/* ---- アセット定義 ---- */
#define NUM_ASSETS  4

static const char *asset_paths[NUM_ASSETS] = {
    "/etc/filetypes",
    "/sys/shell.bin",
    "/sys/unicode.bin",
    "/bin/cal.bin"
};

static const char *asset_labels[NUM_ASSETS] = {
    "filetypes ",
    "shell.bin ",
    "unicode   ",
    "cal.bin   "
};

static asset_handle_t handles[NUM_ASSETS];

/* ---- プログレスバー描画 (TVRAM直接) ---- */
/* 幅20文字のバーを描画 */
static void draw_bar(int row, const char *label, int pct)
{
    int filled, i;
    char buf[48];
    int col;

    filled = pct / 5;  /* 0-20 */
    if (filled > 20) filled = 20;

    /* ラベル (10文字) */
    col = 2;
    for (i = 0; label[i] && i < 10; i++) {
        api->tvram_putchar_at(col + i, row, label[i], ATTR_CYAN);
    }

    /* バー枠 */
    col = 13;
    api->tvram_putchar_at(col, row, '[', ATTR_WHITE);
    for (i = 0; i < 20; i++) {
        u8 attr;
        char ch;
        if (i < filled) {
            ch = '#';
            attr = ATTR_GREEN;
        } else {
            ch = '.';
            attr = 0xE8;  /* 暗いグレー */
        }
        api->tvram_putchar_at(col + 1 + i, row, ch, attr);
    }
    api->tvram_putchar_at(col + 21, row, ']', ATTR_WHITE);

    /* パーセント表示 */
    sprintf(buf, "%3d%%", pct);
    for (i = 0; buf[i]; i++) {
        api->tvram_putchar_at(col + 23 + i, row, buf[i], ATTR_YELLOW);
    }
}

/* ---- メイン ---- */
void main(int argc, char **argv, KernelAPI *sys_api)
{
    int i, all_done;
    int frame;

    (void)argc;
    (void)argv;
    api = sys_api;

    api->tvram_clear();

    /* タイトル */
    {
        const char *title = "=== Asset Manager Loading Demo ===";
        int len = (int)strlen(title);
        for (i = 0; i < len; i++) {
            api->tvram_putchar_at(3 + i, 1, title[i], ATTR_CYAN);
        }
    }

    /* 初期化 */
    asset_init(api);

    /* 非同期ロード開始 */
    for (i = 0; i < NUM_ASSETS; i++) {
        handles[i] = asset_load_async(asset_paths[i], ASSET_TYPE_RAW,
                                       NULL, NULL);
        draw_bar(4 + i, asset_labels[i], 0);
    }

    /* ローディングループ */
    frame = 0;
    all_done = 0;
    while (!all_done) {
        int ch;

        /* 非同期ポンプ */
        asset_pump();

        /* 進捗更新 */
        all_done = 1;
        for (i = 0; i < NUM_ASSETS; i++) {
            int pct;
            int st;

            if (handles[i] == ASSET_INVALID) {
                draw_bar(4 + i, asset_labels[i], 0);
                continue;
            }

            st = asset_state(handles[i]);
            pct = asset_progress(handles[i]);
            draw_bar(4 + i, asset_labels[i], pct);

            if (st == ASSET_STATE_LOADING) {
                all_done = 0;
            }
        }

        /* ローディングアニメーション */
        {
            static const char spinner[] = "|/-\\";
            api->tvram_putchar_at(2, 10, spinner[frame % 4], ATTR_YELLOW);
        }
        {
            const char *msg = "Loading...";
            int len = (int)strlen(msg);
            for (i = 0; i < len; i++) {
                api->tvram_putchar_at(4 + i, 10, msg[i], ATTR_WHITE);
            }
        }

        frame++;

        /* ESCで中断 */
        ch = api->kbd_trygetchar();
        if (ch == 0x1B) break;

        /* フレーム待ち */
        api->sys_halt();
    }

    /* 完了メッセージ */
    {
        const char *done_msg = "All assets loaded!";
        int len = (int)strlen(done_msg);
        for (i = 0; i < len; i++) {
            api->tvram_putchar_at(4 + i, 10, done_msg[i], ATTR_GREEN);
        }
        api->tvram_putchar_at(2, 10, '*', ATTR_GREEN);
    }

    /* アセット情報ダンプ */
    {
        const char *hdr = "--- Loaded Assets ---";
        int len = (int)strlen(hdr);
        for (i = 0; i < len; i++) {
            api->tvram_putchar_at(2 + i, 12, hdr[i], ATTR_CYAN);
        }
    }

    for (i = 0; i < NUM_ASSETS; i++) {
        char line[60];
        int j;

        if (handles[i] == ASSET_INVALID) {
            sprintf(line, "  %s  FAILED", asset_labels[i]);
        } else {
            sprintf(line, "  %s  %lu bytes", asset_labels[i],
                    asset_size(handles[i]));
        }

        for (j = 0; line[j] && j < 58; j++) {
            u8 attr;
            attr = (handles[i] == ASSET_INVALID) ? ATTR_RED : ATTR_WHITE;
            api->tvram_putchar_at(2 + j, 14 + i, line[j], attr);
        }
    }

    /* キャッシュ統計 */
    {
        char stats[60];
        int j;
        sprintf(stats, "Cache: %d entries, %lu bytes used",
                asset_cached_count(), asset_mem_used());
        for (j = 0; stats[j] && j < 58; j++) {
            api->tvram_putchar_at(2 + j, 19, stats[j], ATTR_YELLOW);
        }
    }

    /* キー待ち */
    {
        const char *wait = "Press any key to exit...";
        int len = (int)strlen(wait);
        for (i = 0; i < len; i++) {
            api->tvram_putchar_at(2 + i, 21, wait[i], 0xE8);
        }
    }
    while (api->kbd_trygetchar() < 0) {
        api->sys_halt();
    }

    /* 解放 */
    asset_release_all();
    asset_shutdown();
    api->tvram_clear();
}
