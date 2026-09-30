#include "cmd_fs_shared.h"
#include <stdio.h>

struct ls_opts {
    int format_long;
    int show_all;
};

static void vfs_ls_cb(const DirEntry_Ext *entry, void *ctx)
{
    struct ls_opts *opts = (struct ls_opts *)ctx;
    int is_tty = g_api->sys_isatty(1);

    /* -a がなければ . と .. をスキップ */
    if (!opts->show_all) {
        if (entry->name[0] == '.' &&
            (entry->name[1]=='\0' || (entry->name[1]=='.' && entry->name[2]=='\0')))
            return;
    }

    if (opts->format_long) {
        char size_buf[16];
        if (entry->type == OS32_FILE_TYPE_DIR) {
            if (is_tty) g_api->kprintf(ATTR_CYAN, "  <DIR>    DIR   %s\n", entry->name);
            else printf("  <DIR>    DIR   %s\n", entry->name);
        } else {
            format_size(entry->size, size_buf, 10);
            if (is_tty) g_api->kprintf(ATTR_WHITE, "  %s B  FILE  %s\n", size_buf, entry->name);
            else printf("  %s B  FILE  %s\n", size_buf, entry->name);
        }
    } else {
        if (entry->type == OS32_FILE_TYPE_DIR) {
            if (is_tty) g_api->kprintf(ATTR_CYAN, "%s/  ", entry->name);
            else printf("%s/\n", entry->name);
        } else {
            if (is_tty) g_api->kprintf(ATTR_WHITE, "%s  ", entry->name);
            else printf("%s\n", entry->name);
        }
    }
}

#ifdef SHELL_AS_APP
/* B2: CPL=3 では sys_ls のコールバックから KAPI を呼べない (int 0x80 の
 * 再入で落ちる)。vfs_ls_cb は sys_isatty / kprintf / printf を呼ぶので、
 * 写し取り (sh_ls_collect_cb) を挟んで sys_ls が戻ってから流す。
 * 上限を超えた分は数だけ知らせる。 */
static void ls_run(const char *path, struct ls_opts *opts)
{
    int i, n;
    DirEntry_Ext e;

    sh_ls_reset();
    g_api->sys_ls(path, sh_ls_collect_cb, (void *)0);
    n = sh_ls_count_get();
    for (i = 0; i < n; i++) {
        sh_ls_fill(i, &e);
        vfs_ls_cb(&e, opts);
    }
    if (sh_ls_dropped() > 0)
        g_api->kprintf(ATTR_CYAN, "\n  (... %d more)\n", sh_ls_dropped());
}
#else
/* 常駐 (CPL=0) は従来どおりコールバックで直接出す (展開後のトークンは同一) */
#define ls_run(path, opts)  (g_api->sys_ls((path), vfs_ls_cb, (opts)))
#endif

static int cmd_ls(int argc, char **argv)
{
    struct ls_opts opts;
    int i;
    int path_idx_start = 1;
    int is_tty = g_api->sys_isatty(1);
    int status = 0;

    opts.format_long = 0;
    opts.show_all = 0;

    /* オプション解析: -l, -a, -la, -al 等に対応 */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            int j;
            for (j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'l') opts.format_long = 1;
                else if (argv[i][j] == 'a') opts.show_all = 1;
            }
            path_idx_start = i + 1;
        } else {
            break;
        }
    }

    if (path_idx_start >= argc) {
        if (opts.format_long) {
            if (is_tty) g_api->kprintf(ATTR_WHITE, "%s", "  SIZE     TYPE  NAME\n");
            else printf("%s", "  SIZE     TYPE  NAME\n");
        }
        ls_run(".", &opts);
        if (!opts.format_long) printf("\n");
    } else {
        for (i = path_idx_start; i < argc; i++) {
            int kind = fs_path_kind(argv[i]);
            if (kind == FS_KIND_DIR) {
                if (argc - path_idx_start > 1) {
                    if (is_tty) g_api->kprintf(ATTR_CYAN, "\n%s:\n", argv[i]);
                    else printf("\n%s:\n", argv[i]);
                }
                if (opts.format_long) {
                    if (is_tty) g_api->kprintf(ATTR_WHITE, "%s", "  SIZE     TYPE  NAME\n");
                    else printf("%s", "  SIZE     TYPE  NAME\n");
                }
                ls_run(argv[i], &opts);
                if (!opts.format_long) printf("\n");
            } else if (kind == FS_KIND_FILE) {
                if (opts.format_long) {
                    OS32_Stat st;
                    char size_buf[16];
                    u32 sz = (g_api->sys_stat(argv[i], &st) == 0) ? st.st_size : 0;
                    format_size(sz, size_buf, 10);
                    printf("  %s B  FILE  %s\n", size_buf, argv[i]);
                } else {
                    printf("%s  ", argv[i]);
                    if (i == argc - 1) printf("\n");
                }
            } else {
                /* 以前は存在しないパスもファイル名として印字していた */
                g_api->kprintf(ATTR_RED, "ls: cannot access '%s': %s\n",
                               argv[i], fs_strerror(kind));
                status = SH_STATUS_ERROR;
            }
        }
    }
    return status;
}



static int cmd_cd(int argc, char **argv)
{
    int rc;
    const char *target;
    const char *cwd;
    char old_dir[PATH_MAX_LEN];
    int print_after = 0;

    if (argc < 2) {
        /* 引数なし: ホームディレクトリに移動 */
        target = env_get("HOME");
        if (!target) target = "/";
    } else if (str_eq(argv[1], "-")) {
        /* 直前のディレクトリへ (以前は "-" という名前へ cd していた) */
        target = env_get("OLDPWD");
        if (!target) {
            g_api->kprintf(ATTR_RED, "%s", "cd: OLDPWD not set\n");
            return SH_STATUS_ERROR;
        }
        print_after = 1;
    } else {
        target = argv[1];
    }

    cwd = g_api->sys_getcwd();
    strncpy(old_dir, cwd ? cwd : "/", PATH_MAX_LEN - 1);
    old_dir[PATH_MAX_LEN - 1] = '\0';

    rc = g_api->sys_chdir(target);
    if (rc != 0) {
        g_api->kprintf(ATTR_RED, "cd: %s: %s\n", target, fs_strerror(rc));
        return SH_STATUS_ERROR;
    }
    env_set("OLDPWD", old_dir);
    cwd = g_api->sys_getcwd();
    env_set("PWD", cwd ? cwd : "/");
    if (print_after) printf("%s\n", cwd ? cwd : "/");
    return 0;
}

static int cmd_pwd(int argc, char **argv)
{
    const char *cwd;
    (void)argc; (void)argv;
    cwd = g_api->sys_getcwd();
    printf("%s\n", cwd ? cwd : "/");
    return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    int i, rc;
    int status = 0;
    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    for (i = 1; i < argc; i++) {
        rc = g_api->sys_mkdir(argv[i]);
        if (rc != 0) {
            g_api->kprintf(ATTR_RED, "mkdir: cannot create directory '%s': %s\n",
                           argv[i], fs_strerror(rc));
            status = SH_STATUS_ERROR;
        }
    }
    return status;
}

static int cmd_rmdir(int argc, char **argv)
{
    int i, rc;
    int status = 0;
    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    for (i = 1; i < argc; i++) {
        rc = g_api->sys_rmdir(argv[i]);
        if (rc != 0) {
            g_api->kprintf(ATTR_RED, "rmdir: failed to remove '%s': %s\n",
                           argv[i], fs_strerror(rc));
            status = SH_STATUS_ERROR;
        }
    }
    return status;
}



static const ShellCmd dir_cmds[] = {
    { "ls",    cmd_ls,    "[-la] [path...]", "List directory contents" },
    { "cd",    cmd_cd,    "path",           "Change working directory" },
    { "pwd",   cmd_pwd,   "",               "Print working directory" },
    { "mkdir", cmd_mkdir, "dir...",         "Create directories" },
    { "rmdir", cmd_rmdir, "dir...",         "Remove directories" },
    { (const char *)0, 0, 0, 0 }
};
void shell_cmd_dir_init(void) { shell_register_cmds(dir_cmds); }
