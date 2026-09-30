/* ======================================================================== */
/*  FIND.C -- ファイル名検索 (再帰ディレクトリ走査)                            */
/*                                                                          */
/*  Usage: find [PATH] [-name PATTERN]                                       */
/*  指定ディレクトリ以下を再帰的に走査し、パターンに一致するファイルを表示。     */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>
#include <string.h>

static KernelAPI *api;
static const char *match_pattern = NULL;
static int max_depth = 8;

/* 簡易部分文字列マッチ (ワイルドカードなし: 部分一致) */
static int name_matches(const char *name, const char *pattern)
{
    int nlen, plen, i;
    if (!pattern) return 1; /* パターンなし: 全マッチ */
    nlen = strlen(name);
    plen = strlen(pattern);
    if (plen > nlen) return 0;
    for (i = 0; i <= nlen - plen; i++) {
        if (memcmp(name + i, pattern, plen) == 0) return 1;
    }
    return 0;
}

/* パス結合ヘルパー */
static void join_path(char *dst, int dst_size, const char *dir, const char *name)
{
    int i = 0, j = 0;
    while (dir[i] && i < dst_size - 2) { dst[i] = dir[i]; i++; }
    if (i > 0 && dst[i-1] != '/' && i < dst_size - 2) dst[i++] = '/';
    while (name[j] && i < dst_size - 1) { dst[i++] = name[j++]; }
    dst[i] = '\0';
}

struct find_ctx {
    char basepath[256];
    int depth;
};

static void find_cb(const DirEntry_Ext *entry, void *ctx)
{
    struct find_ctx *fc = (struct find_ctx *)ctx;
    char fullpath[256];

    /* . と .. をスキップ */
    if (entry->name[0] == '.') {
        if (entry->name[1] == '\0') return;
        if (entry->name[1] == '.' && entry->name[2] == '\0') return;
    }

    join_path(fullpath, sizeof(fullpath), fc->basepath, entry->name);

    if (name_matches(entry->name, match_pattern)) {
        printf("%s\n", fullpath);
    }

    /* ディレクトリなら再帰 */
    if (entry->type == OS32_FILE_TYPE_DIR && fc->depth < max_depth) {
        struct find_ctx sub;
        int i;
        for (i = 0; fullpath[i] && i < 255; i++) sub.basepath[i] = fullpath[i];
        sub.basepath[i] = '\0';
        sub.depth = fc->depth + 1;
        api->sys_ls(fullpath, (void *)find_cb, &sub);
    }
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    struct find_ctx ctx;
    const char *search_path = ".";
    int i;

    api = kapi;

    /* 引数解析 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            match_pattern = argv[++i];
        } else if (argv[i][0] != '-') {
            search_path = argv[i];
        }
    }

    /* 初期コンテキスト */
    {
        int j = 0;
        while (search_path[j] && j < 255) { ctx.basepath[j] = search_path[j]; j++; }
        ctx.basepath[j] = '\0';
    }
    ctx.depth = 0;

    api->sys_ls(search_path, (void *)find_cb, &ctx);

    return 0;
}
