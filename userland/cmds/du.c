/* ======================================================================== */
/*  DU.C -- ディスク使用量表示                                                */
/*                                                                          */
/*  Usage: du [-s] [PATH]                                                    */
/*  ディレクトリ内のファイルサイズを再帰的に合計して表示する。                   */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>
#include <string.h>

static KernelAPI *api;
static int opt_summary = 0;

/* パス結合ヘルパー */
static void join_path(char *dst, int dst_size, const char *dir, const char *name)
{
    int i = 0, j = 0;
    while (dir[i] && i < dst_size - 2) { dst[i] = dir[i]; i++; }
    if (i > 0 && dst[i-1] != '/' && i < dst_size - 2) dst[i++] = '/';
    while (name[j] && i < dst_size - 1) { dst[i++] = name[j++]; }
    dst[i] = '\0';
}

struct du_ctx {
    char basepath[256];
    unsigned long total;
    int depth;
};

static void du_cb(const DirEntry_Ext *entry, void *ctx)
{
    struct du_ctx *dc = (struct du_ctx *)ctx;
    char fullpath[256];

    /* . と .. をスキップ */
    if (entry->name[0] == '.') {
        if (entry->name[1] == '\0') return;
        if (entry->name[1] == '.' && entry->name[2] == '\0') return;
    }

    join_path(fullpath, sizeof(fullpath), dc->basepath, entry->name);

    if (entry->type == OS32_FILE_TYPE_DIR) {
        /* ディレクトリ: 再帰 */
        if (dc->depth < 8) {
            struct du_ctx sub;
            int i;
            for (i = 0; fullpath[i] && i < 255; i++) sub.basepath[i] = fullpath[i];
            sub.basepath[i] = '\0';
            sub.total = 0;
            sub.depth = dc->depth + 1;
            api->sys_ls(fullpath, (void *)du_cb, &sub);
            dc->total += sub.total;
            if (!opt_summary) {
                printf("%8lu  %s\n", sub.total, fullpath);
            }
        }
    } else {
        dc->total += entry->size;
    }
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    struct du_ctx ctx;
    const char *target = ".";
    int i;

    api = kapi;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            opt_summary = 1;
        } else if (argv[i][0] != '-') {
            target = argv[i];
        }
    }

    {
        int j = 0;
        while (target[j] && j < 255) { ctx.basepath[j] = target[j]; j++; }
        ctx.basepath[j] = '\0';
    }
    ctx.total = 0;
    ctx.depth = 0;

    api->sys_ls(target, (void *)du_cb, &ctx);
    printf("%8lu  %s\n", ctx.total, target);

    return 0;
}
