/* ======================================================================== */
/*  MAN.C — マニュアル表示コマンド                                           */
/*                                                                          */
/*  Usage: man [COMMAND]                                                     */
/*         man -l          全マニュアル一覧                                  */
/*         man -k KEYWORD  キーワード検索 (NAME行)                          */
/* ======================================================================== */
#include "os32api.h"
#include "os32/help.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *api;

/* /usr/man/ の.1ファイルを列挙するコールバック */
static void list_cb(const DirEntry_Ext *entry, void *ctx)
{
    int len;
    char name_buf[64];
    int i;
    char pad[16];
    int pl;

    /* ディレクトリはスキップ */
    if (entry->type == OS32_FILE_TYPE_DIR) return;

    /* .1 拡張子チェック */
    len = strlen(entry->name);
    if (len < 3) return;
    if (entry->name[len-2] != '.' || entry->name[len-1] != '1') return;

    /* 拡張子を除去してコマンド名を取得 */
    for (i = 0; i < len - 2 && i < 62; i++) {
        name_buf[i] = entry->name[i];
    }
    name_buf[i] = '\0';

    /* パディング */
    pl = 0;
    while (i < 12 && pl < 15) { pad[pl++] = ' '; i++; }
    pad[pl] = '\0';

    /* NAME行を読み取って簡易説明を取得 */
    {
        char man_path[OS32_MAX_PATH];
        char buf[4096];
        int fd, sz;
        const char *s;

        /* パス構築 */
        i = 0;
        s = OS32_MAN_DIR;
        while (*s && i < OS32_MAX_PATH - 10) man_path[i++] = *s++;
        man_path[i++] = '/';
        s = entry->name;
        while (*s && i < OS32_MAX_PATH - 1) man_path[i++] = *s++;
        man_path[i] = '\0';

        fd = api->sys_open(man_path, KAPI_O_RDONLY);
        if (fd >= 0) {
            sz = api->sys_read(fd, buf, sizeof(buf) - 1);
            api->sys_close(fd);
            if (sz > 0) {
                char desc[128];
                int di = 0;
                buf[sz] = '\0';

                /* "# <name>" の次の非空行を説明として取得 */
                s = buf;
                /* 最初の # 行をスキップ */
                while (*s && *s != '\n') s++;
                if (*s == '\n') s++;
                /* 空行スキップ */
                while (*s == '\n' || *s == '\r') s++;
                /* 説明行を取得 */
                while (*s && *s != '\n' && di < 126) {
                    desc[di++] = *s++;
                }
                desc[di] = '\0';

                if (api->sys_isatty(1)) {
                    api->kprintf(ATTR_CYAN, "  %s", name_buf);
                    api->kprintf(ATTR_WHITE, "%s- %s\n", pad, desc);
                } else {
                    printf("  %s%s- %s\n", name_buf, pad, desc);
                }
                return;
            }
        }
    }

    /* 説明なしの場合 */
    if (api->sys_isatty(1)) {
        api->kprintf(ATTR_WHITE, "  %s\n", name_buf);
    } else {
        printf("  %s\n", name_buf);
    }
}

/* キーワード検索コールバック */
typedef struct {
    const char *keyword;
    int found;
} SearchCtx;

static int str_contains(const char *haystack, const char *needle)
{
    int hlen, nlen, i, j;
    if (!needle[0]) return 1;
    hlen = strlen(haystack);
    nlen = strlen(needle);
    if (nlen > hlen) return 0;
    for (i = 0; i <= hlen - nlen; i++) {
        int ok = 1;
        for (j = 0; j < nlen; j++) {
            char a = haystack[i+j];
            char b = needle[j];
            /* 大文字小文字無視 */
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static void search_cb(const DirEntry_Ext *entry, void *ctx)
{
    SearchCtx *sctx = (SearchCtx *)ctx;
    int len, i;
    char name_buf[64];
    char man_path[OS32_MAX_PATH];
    char buf[4096];
    int fd, sz;
    const char *s;

    if (entry->type == OS32_FILE_TYPE_DIR) return;

    len = strlen(entry->name);
    if (len < 3) return;
    if (entry->name[len-2] != '.' || entry->name[len-1] != '1') return;

    /* コマンド名取得 */
    for (i = 0; i < len - 2 && i < 62; i++) name_buf[i] = entry->name[i];
    name_buf[i] = '\0';

    /* ファイル読み込み */
    i = 0;
    s = OS32_MAN_DIR;
    while (*s && i < OS32_MAX_PATH - 10) man_path[i++] = *s++;
    man_path[i++] = '/';
    s = entry->name;
    while (*s && i < OS32_MAX_PATH - 1) man_path[i++] = *s++;
    man_path[i] = '\0';

    fd = api->sys_open(man_path, KAPI_O_RDONLY);
    if (fd < 0) return;
    sz = api->sys_read(fd, buf, sizeof(buf) - 1);
    api->sys_close(fd);
    if (sz <= 0) return;
    buf[sz] = '\0';

    /* コマンド名とNAME行でキーワード検索 */
    if (str_contains(name_buf, sctx->keyword) ||
        str_contains(buf, sctx->keyword)) {
        /* 説明を取得 (最初の#行の次の非空行) */
        char desc[128];
        int di = 0;
        s = buf;
        while (*s && *s != '\n') s++;
        if (*s == '\n') s++;
        while (*s == '\n' || *s == '\r') s++;
        while (*s && *s != '\n' && di < 126) desc[di++] = *s++;
        desc[di] = '\0';

        if (api->sys_isatty(1)) {
            api->kprintf(ATTR_CYAN, "  %s", name_buf);
            api->kprintf(ATTR_WHITE, " - %s\n", desc);
        } else {
            printf("  %s - %s\n", name_buf, desc);
        }
        sctx->found++;
    }
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    api = kapi;

    if (argc < 2) {
        printf("Usage: man [COMMAND]\n");
        printf("       man -l          List all manpages\n");
        printf("       man -k KEYWORD  Search manpages\n");
        return 1;
    }

    /* -l: 一覧表示 */
    if (strcmp(argv[1], "-l") == 0) {
        if (api->sys_isatty(1)) {
            api->kprintf(ATTR_YELLOW, "%s", "\nAvailable manual pages:\n\n");
        } else {
            printf("\nAvailable manual pages:\n\n");
        }
        api->sys_ls(OS32_MAN_DIR, list_cb, (void *)0);
        printf("\n");
        return 0;
    }

    /* -k: キーワード検索 */
    if (strcmp(argv[1], "-k") == 0) {
        SearchCtx ctx;
        if (argc < 3) {
            printf("Usage: man -k KEYWORD\n");
            return 1;
        }
        ctx.keyword = argv[2];
        ctx.found = 0;
        if (api->sys_isatty(1)) {
            api->kprintf(ATTR_YELLOW, "\nSearching for '%s':\n\n", argv[2]);
        } else {
            printf("\nSearching for '%s':\n\n", argv[2]);
        }
        api->sys_ls(OS32_MAN_DIR, search_cb, &ctx);
        if (ctx.found == 0) {
            printf("  No matches found.\n");
        }
        printf("\n");
        return 0;
    }

    /* 通常: マニュアル表示 */
    if (os32_help_show(argv[1]) != 0) {
        if (api->sys_isatty(1)) {
            api->kprintf(ATTR_RED, "No manual entry for %s\n", argv[1]);
        } else {
            printf("No manual entry for %s\n", argv[1]);
        }
        return 1;
    }

    return 0;
}
