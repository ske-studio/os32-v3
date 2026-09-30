#include "cmd_fs_shared.h"
#include <stdio.h>

#define IO_BUF_SIZE 65536
static u8 *io_buf = NULL;

static int ensure_io_buf(void)
{
    if (io_buf) return 0;
    io_buf = (u8 *)g_api->mem_alloc(IO_BUF_SIZE);
    return io_buf ? 0 : -1;
}

static void release_io_buf(void)
{
    if (io_buf) { g_api->mem_free(io_buf); io_buf = NULL; }
}

static int do_copy_file(const char *cmd_name, const char *src, const char *dst) {
    int fd_in, fd_out, sz, rc;

    if (fs_same_file(src, dst)) {
        g_api->kprintf(ATTR_RED, "%s: '%s' and '%s' are the same file\n",
                       cmd_name, src, dst);
        return -1;
    }
    if (ensure_io_buf() < 0) {
        g_api->kprintf(ATTR_RED, "%s: out of memory\n", cmd_name);
        return -1;
    }

    fd_in = g_api->sys_open(src, KAPI_O_RDONLY);
    if (fd_in < 0) {
        g_api->kprintf(ATTR_RED, "%s: cannot open '%s': %s\n",
                       cmd_name, src, fs_strerror(fd_in));
        return -1;
    }

    fd_out = g_api->sys_open(dst, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd_out < 0) {
        g_api->kprintf(ATTR_RED, "%s: cannot create '%s': %s\n",
                       cmd_name, dst, fs_strerror(fd_out));
        g_api->sys_close(fd_in);
        return -1;
    }

    /* R6: read / write の失敗をここで拾って**負**を返す。0 を返していたころ、
     * 別 FS への mv (do_move_file) が「コピーできた」と読んで sys_unlink(src)
     * まで走り、中途半端な複製だけを残して原本を消していた。 */
    rc = 0;
    while (1) {
        sz = g_api->sys_read(fd_in, io_buf, IO_BUF_SIZE);
        if (sz < 0) {
            g_api->kprintf(ATTR_RED, "%s: read failed %s\n", cmd_name, src);
            rc = -1;
            break;
        }
        if (sz == 0) break; /* EOF */

        if (g_api->sys_write(fd_out, io_buf, sz) != sz) {
            g_api->kprintf(ATTR_RED, "%s: write failed %s\n", cmd_name, dst);
            rc = -1;
            break;
        }
    }

    g_api->sys_close(fd_in);
    g_api->sys_close(fd_out);
    return rc;
}

/* 種別を問う。**分からなければ断る** (TASK_FS_TYPE §3、POLICY_DEBUG §4-35)。
 *
 * fs_is_dir() は stat と列挙の両方が失敗すると 0 を返すので、それを「ファイル」
 * と読むと cp -r が宛先直下へ展開して既存ファイルを上書きし、rm が読めない
 * ディレクトリを unlink へ渡していた。ここでは「読めなかった」を「ファイル」
 * にも「無い」にも読み替えない。
 *
 * 戻り値: FS_KIND_DIR / FS_KIND_FILE / OS32_ERR_NOTFOUND (無いと分かった)。
 *         それ以外の負値は判定できなかったことを表し、断りを表示済み。 */
static int file_kind_or_refuse(const char *cmd_name, const char *path)
{
    int kind = fs_path_kind(path);

    if (kind >= 0 || kind == OS32_ERR_NOTFOUND) return kind;
    g_api->kprintf(ATTR_RED, "%s: cannot determine the type of '%s': %s\n",
                   cmd_name, path, fs_strerror(kind));
    return kind;
}

#define FILE_KIND_UNKNOWN(k) ((k) < 0 && (k) != OS32_ERR_NOTFOUND)

/* 再帰コピー: エントリ収集方式 */
#define MAX_COPY_ENTRIES 64
#define MAX_COPY_DEPTH   8

/* I-4: 名前幅は DirEntry_Ext.name と同じ。31B で切っていたころは 32 文字
 * 以上のファイルがコピーされず、切り詰めが衝突すると別ファイルを繰り返し
 * コピーしていた。1 段ぶん 64 × 260B ≈ 16.6KB になるので、**スタックには
 * 置かず** mem_alloc から取る (常駐シェルのスタックは 40KB で、深さ 8 の
 * 再帰に積むと溢れる)。 */
struct copy_entry {
    char name[OS32_MAX_PATH];
    int  is_dir;   /* 1=ディレクトリ, 0=ファイル */
};

/* 収集用バッファ (スタック節約のため static) */
static struct copy_entry g_copy_entries[MAX_COPY_ENTRIES];
static int g_copy_count;
static int g_copy_over;    /* I-4: 上限を超えた (中止する) */

/* sys_ls コールバック: エントリを収集するだけ */
static void collect_entries_cb(const DirEntry_Ext *entry, void *ctx)
{
    int nlen;
    (void)ctx;

    /* . と .. をスキップ */
    if (entry->name[0] == '.' &&
        (entry->name[1] == '\0' || (entry->name[1] == '.' && entry->name[2] == '\0')))
        return;

    if (g_copy_count >= MAX_COPY_ENTRIES) { g_copy_over = 1; return; }

    nlen = strlen(entry->name);
    if (nlen >= OS32_MAX_PATH - 1) nlen = OS32_MAX_PATH - 1;
    memcpy(g_copy_entries[g_copy_count].name, entry->name, nlen);
    g_copy_entries[g_copy_count].name[nlen] = '\0';
    g_copy_entries[g_copy_count].is_dir = (entry->type == OS32_FILE_TYPE_DIR) ? 1 : 0;
    g_copy_count++;
}

/* ディレクトリの再帰コピー (collect-then-copy)。
 * 戻り値: 0 = 全部通った / 非 0 = 1 件でも失敗した (票 §2-3「赤字 = 非 0」)。*/
static int do_copy_recursive_impl(const char *src, const char *dst, int depth)
{
    /* 収集表の写し。再帰で g_copy_entries が上書きされるので 1 段ごとに
     * 自分のぶんを持つ。I-4 で 1 段 16.6KB になったのでヒープから取る。 */
    struct copy_entry *local_entries;
    int local_count, i, rc;
    int status = 0;

    if (depth >= MAX_COPY_DEPTH) {
        g_api->kprintf(ATTR_RED, "cp: max depth exceeded: %s\n", src);
        return SH_STATUS_ERROR;
    }

    local_entries = (struct copy_entry *)
        g_api->mem_alloc(sizeof(struct copy_entry) * MAX_COPY_ENTRIES);
    if (!local_entries) {
        g_api->kprintf(ATTR_RED, "%s", "cp: out of memory\n");
        return SH_STATUS_ERROR;
    }

    /* **収集が先、mkdir は後**。上限超過や列挙の失敗で引き返す経路が
     * mkdir の後ろにあったので、宛先に**空のディレクトリだけ**が残っていた。 */
    g_copy_count = 0;
    g_copy_over = 0;
    rc = g_api->sys_ls(src, collect_entries_cb, (void *)0);
    if (rc < 0) {
        g_api->kprintf(ATTR_RED, "cp -r: cannot read directory '%s': %s\n",
                       src, fs_strerror(rc));
        g_api->mem_free(local_entries);
        return SH_STATUS_ERROR;
    }
    if (g_copy_over) {
        g_api->kprintf(ATTR_RED, "cp -r: too many entries in '%s' (max %d)\n",
                       src, MAX_COPY_ENTRIES);
        g_api->mem_free(local_entries);
        return SH_STATUS_ERROR;
    }

    local_count = g_copy_count;
    for (i = 0; i < local_count; i++) {
        local_entries[i] = g_copy_entries[i];
    }

    /* 宛先ディレクトリを作成。**戻り値を見る** — 作れていないのに中へ進むと
     * 親の下に中身が散る / 既存のファイルを潰す。
     * 既に在る**ディレクトリ**への上書きコピー (`cp -r a b` を 2 回打つ
     * 使い方) は今までどおり通す。呼び手 (cmd_cp) の file_kind_or_refuse は
     * 最上段の宛先しか見ていないので、EXIST のときはここで型を確かめる
     * (同名のファイルが在る / 型が分からない、なら中へ進まない)。 */
    rc = g_api->sys_mkdir(dst);
    if (rc != 0 && !(rc == OS32_ERR_EXIST && fs_path_kind(dst) == FS_KIND_DIR)) {
        g_api->kprintf(ATTR_RED, "cp -r: cannot create directory '%s': %s\n",
                       dst, fs_strerror(rc));
        g_api->mem_free(local_entries);
        return SH_STATUS_ERROR;
    }

    /* 収集後にコピーを実行 */
    for (i = 0; i < local_count; i++) {
        char src_path[PATH_MAX_LEN];
        char dst_path[PATH_MAX_LEN];

        /* I-3: 収まらない綴りは黙って切り詰めず飛ばす (別の宛先を潰さない) */
        if (fs_join_path(src_path, src, local_entries[i].name) < 0 ||
            fs_join_path(dst_path, dst, local_entries[i].name) < 0) {
            g_api->kprintf(ATTR_RED, "cp: path too long: %s\n",
                           local_entries[i].name);
            status = SH_STATUS_ERROR;
            continue;
        }

        if (local_entries[i].is_dir) {
            if (do_copy_recursive_impl(src_path, dst_path, depth + 1) != 0)
                status = SH_STATUS_ERROR;
        } else {
            if (do_copy_file("cp", src_path, dst_path) != 0)
                status = SH_STATUS_ERROR;
        }
    }
    g_api->mem_free(local_entries);
    return status;
}

static int do_copy_recursive(const char *src, const char *dst)
{
    return do_copy_recursive_impl(src, dst, 0);
}

static int cmd_cp(int argc, char **argv)
{
    int i, is_dest_dir, kind;
    int status = 0;
    int opt_recursive = 0;
    int file_start = 1;
    const char *dst;

    if (argc < 3) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }

    /* オプション解析 */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            int j;
            for (j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'r' || argv[i][j] == 'R')
                    opt_recursive = 1;
            }
            file_start = i + 1;
        } else {
            break;
        }
    }

    if (argc - file_start < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }

    dst = argv[argc - 1];
    kind = file_kind_or_refuse("cp", dst);
    if (FILE_KIND_UNKNOWN(kind)) return SH_STATUS_ERROR;
    is_dest_dir = (kind == FS_KIND_DIR);

    if (argc - file_start > 2 && !is_dest_dir) {
        g_api->kprintf(ATTR_RED, "%s", "cp: multiple files must be copied into a directory\n");
        return SH_STATUS_USAGE;
    }

    for (i = file_start; i < argc - 1; i++) {
        const char *src = argv[i];
        if (argv[i][0] == '-') continue; /* オプションをスキップ */

        /* 分からない入力はその 1 件だけ断る (不存在は do_copy_file が報告) */
        kind = file_kind_or_refuse("cp", src);
        if (FILE_KIND_UNKNOWN(kind)) { status = SH_STATUS_ERROR; continue; }

        if (kind == FS_KIND_DIR) {
            if (!opt_recursive) {
                g_api->kprintf(ATTR_RED, "cp: -r not specified; omitting directory '%s'\n", src);
                status = SH_STATUS_ERROR;
                continue;
            }
            /* 再帰コピー */
            if (is_dest_dir) {
                char dpath[PATH_MAX_LEN];
                if (fs_join_path(dpath, dst, get_basename(src)) < 0) {
                    g_api->kprintf(ATTR_RED, "cp: path too long: %s\n", src);
                    status = SH_STATUS_ERROR;
                    continue;
                }
                if (do_copy_recursive(src, dpath) != 0) status = SH_STATUS_ERROR;
            } else {
                if (do_copy_recursive(src, dst) != 0) status = SH_STATUS_ERROR;
            }
        } else {
            if (is_dest_dir) {
                char dpath[PATH_MAX_LEN];
                if (fs_join_path(dpath, dst, get_basename(src)) < 0) {
                    g_api->kprintf(ATTR_RED, "cp: path too long: %s\n", src);
                    status = SH_STATUS_ERROR;
                    continue;
                }
                if (do_copy_file("cp", src, dpath) != 0) status = SH_STATUS_ERROR;
            } else {
                if (do_copy_file("cp", src, dst) != 0) status = SH_STATUS_ERROR;
            }
        }
    }
    release_io_buf();
    return status;
}

/* 1 件の移動: 同一 FS なら rename、FS をまたぐときだけコピー+削除。
 * 戻り値: 0 = 通った / 非 0 = 失敗 (赤字を 1 行出している)。 */
static int do_move_one(const char *src, const char *dpath)
{
    int rc, kind;

    if (fs_same_file(src, dpath)) {
        g_api->kprintf(ATTR_RED, "mv: '%s' and '%s' are the same file\n", src, dpath);
        return SH_STATUS_ERROR;
    }

    rc = g_api->sys_rename(src, dpath);
    if (rc == 0) return 0;

    if (rc != OS32_ERR_INVAL) {
        g_api->kprintf(ATTR_RED, "mv: cannot move '%s' to '%s': %s\n",
                       src, dpath, fs_strerror(rc));
        return SH_STATUS_ERROR;
    }

    /* OS32_ERR_INVAL = FS をまたぐ (または rename 非対応 FS)。
     * ディレクトリはコピーできないので拒否する。以前は無条件にコピー+削除で、
     * ディレクトリを渡すとディレクトリの生ブロックを新ファイルへ書き、
     * 元は消えないという壊れ方をしていた。種別が分からないときも断る
     * (ファイルと読んでコピー + unlink へ進まない) */
    kind = file_kind_or_refuse("mv", src);
    if (kind < 0) {
        if (kind == OS32_ERR_NOTFOUND)
            g_api->kprintf(ATTR_RED, "mv: cannot move '%s': %s\n",
                           src, fs_strerror(kind));
        return SH_STATUS_ERROR;
    }
    if (kind == FS_KIND_DIR) {
        g_api->kprintf(ATTR_RED,
            "mv: cannot move directory '%s' to '%s' (across filesystems or into itself)\n",
            src, dpath);
        return SH_STATUS_ERROR;
    }
    if (do_copy_file("mv", src, dpath) != 0) return SH_STATUS_ERROR;
    rc = g_api->sys_unlink(src);
    if (rc != 0) {
        g_api->kprintf(ATTR_RED, "mv: copied but cannot remove '%s': %s\n",
                       src, fs_strerror(rc));
        return SH_STATUS_ERROR;
    }
    return 0;
}

static int cmd_mv(int argc, char **argv)
{
    int i, is_dest_dir, kind;
    int status = 0;
    const char *dst;

    if (argc < 3) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }

    dst = argv[argc - 1];
    kind = file_kind_or_refuse("mv", dst);
    if (FILE_KIND_UNKNOWN(kind)) return SH_STATUS_ERROR;
    is_dest_dir = (kind == FS_KIND_DIR);

    if (argc > 3 && !is_dest_dir) {
        g_api->kprintf(ATTR_RED, "%s", "mv: multiple files must be moved into a directory\n");
        return SH_STATUS_USAGE;
    }

    for (i = 1; i < argc - 1; i++) {
        const char *src = argv[i];

        if (is_dest_dir) {
            char dpath[PATH_MAX_LEN];
            if (fs_join_path(dpath, dst, get_basename(src)) < 0) {
                g_api->kprintf(ATTR_RED, "mv: path too long: %s\n", src);
                status = SH_STATUS_ERROR;
                continue;
            }
            if (do_move_one(src, dpath) != 0) status = SH_STATUS_ERROR;
        } else {
            if (do_move_one(src, dst) != 0) status = SH_STATUS_ERROR;
        }
    }
    release_io_buf();
    return status;
}

static int cmd_rm(int argc, char **argv)
{
    int i;
    int status = 0;
    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    for (i = 1; i < argc; i++) {
        int kind = file_kind_or_refuse("rm", argv[i]);
        if (FILE_KIND_UNKNOWN(kind)) { status = SH_STATUS_ERROR; continue; }
        if (kind == FS_KIND_DIR) {
            g_api->kprintf(ATTR_RED, "rm: cannot remove '%s': Is a directory (use rmdir)\n", argv[i]);
            status = SH_STATUS_ERROR;
        } else {
            int ret = g_api->sys_unlink(argv[i]);
            if (ret != 0) {
                g_api->kprintf(ATTR_RED, "rm: cannot remove '%s': %s\n",
                               argv[i], fs_strerror(ret));
                status = SH_STATUS_ERROR;
            }
        }
    }
    return status;
}
/* バッファを行番号付きで出力
 *
 * 行番号は**行の先頭で**出す。at_bol は「次に出す 1 バイトが行の先頭か」で、
 * cmd_cat が読み取りをまたいで持ち回る。これが 2 つの欠陥の直し:
 *   - 改行で終わるバッファの後ろで行番号を出さない (行がまだ始まっていない)
 *   - sys_read の切れ目が行の途中でも、そこで 1 行終わったことにしない
 * 改行で終わらないまま入力が尽きたときの行末の改行は cmd_cat が足す。 */
static void cat_with_linenum(const u8 *data, int len, int *line_num, int *at_bol)
{
    int i, start;
    char num_buf[12];
    int nlen, j;

    i = 0;
    while (i < len) {
        if (*at_bol) {
            /* 行番号を出力 */
            nlen = 0;
            {
                int n = *line_num;
                char tmp[12];
                int ti = 0;
                if (n == 0) tmp[ti++] = '0';
                while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
                /* 6桁右寄せ */
                for (j = 0; j < 6 - ti; j++) num_buf[nlen++] = ' ';
                while (ti > 0) num_buf[nlen++] = tmp[--ti];
            }
            num_buf[nlen++] = ' ';
            num_buf[nlen++] = ' ';
            g_api->sys_write(1, num_buf, nlen);
            *at_bol = 0;
        }

        /* 行の内容を出力 (改行があればそこまで、無ければバッファの終わりまで) */
        start = i;
        while (i < len && data[i] != '\n') i++;
        if (i < len) {
            i++;                    /* 改行も行の一部として出す */
            *at_bol = 1;
            (*line_num)++;
        }
        if (i > start) {
            g_api->sys_write(1, &data[start], i - start);
        }
    }
}

/* 開いた FD を 1 本ぶん流す。**FD は閉じない** — 持ち主 (呼び手) が閉じる。
 * io_buf は呼び手が ensure_io_buf() で用意しておくこと。
 * 行頭かどうかは**読み取りをまたいで**持ち回る (IO_BUF_SIZE の切れ目で
 * 行が終わったことにしないため)。終わりは sys_read が 0 以下を返したところ。 */
static void cat_stream(int fd, int show_linenum)
{
    int line_num = 1;
    int at_bol = 1;
    int r;

    while (1) {
        r = g_api->sys_read(fd, io_buf, IO_BUF_SIZE);
        if (r <= 0) break;

        if (show_linenum) {
            cat_with_linenum(io_buf, r, &line_num, &at_bol);
        } else {
            g_api->sys_write(1, io_buf, r);
        }
    }
    /* 最後が改行で終わらない入力の行末 (従来どおり改行を足す) */
    if (show_linenum && !at_bol) g_api->sys_write(1, "\n", 1);
}

static int cmd_cat(int argc, char **argv)
{
    int i;
    int status = 0;
    int show_linenum = 0;
    int file_start = 1;

    /* オプション解析 */
    if (argc > 1 && argv[1][0] == '-') {
        int j;
        for (j = 1; argv[1][j]; j++) {
            if (argv[1][j] == 'n') show_linenum = 1;
        }
        file_start = 2;
    }

    /* ファイル名が 1 つも無ければ**標準入力 (FD 0)** を読む。
     * `echo a | cat` も `cat < f` も、シェルの fd_redirect が FD 0 を
     * 差し替えたところをそのまま読むだけ — リダイレクト表には触らない。
     *   - FD 0 はシェルの持ち物なので **sys_close しない**。
     *   - 端末のままだと vfs_read_fd の TTY 経路に EOF が無く戻れないので、
     *     grep / hexdump と同じく sys_isatty(0) で使い方を出して止める。 */
    if (file_start >= argc) {
        if (g_api->sys_isatty(0)) {
            shell_print_help(argv[0]);
            return SH_STATUS_USAGE;
        }
        if (ensure_io_buf() < 0) {
            g_api->kprintf(ATTR_RED, "%s", "cat: out of memory\n");
            return SH_STATUS_ERROR;
        }
        cat_stream(0, show_linenum);
        release_io_buf();
        return 0;
    }

    for (i = file_start; i < argc; i++) {
        int fd;

        fd = g_api->sys_open(argv[i], KAPI_O_RDONLY);
        if (fd < 0) {
            /* ディレクトリは open が OS32_ERR_ISDIR を返す (以前は生の
             * ディレクトリブロックを吐いていた) */
            g_api->kprintf(ATTR_RED, "cat: %s: %s\n", argv[i], fs_strerror(fd));
            status = SH_STATUS_ERROR;
            continue;
        }
        if (ensure_io_buf() < 0) {
            g_api->kprintf(ATTR_RED, "%s", "cat: out of memory\n");
            g_api->sys_close(fd);
            status = SH_STATUS_ERROR;
            continue;
        }

        cat_stream(fd, show_linenum);

        g_api->sys_close(fd);
        release_io_buf();
    }
    return status;
}

static int cmd_cat2(int argc, char **argv)
{
    return cmd_cat(argc, argv);
}

static int cmd_echo(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        int l = strlen(argv[i]);
        if (l > 0) {
            g_api->sys_write(1, argv[i], l);
        }
        if (i < argc - 1) g_api->sys_write(1, " ", 1);
    }
    g_api->sys_write(1, "\n", 1);
    return 0;
}

static const ShellCmd file_cmds[] = {
    { "cp",   cmd_cp,   "[-r] SRC DST / SRC... DIR", "Copy files" },
    { "mv",   cmd_mv,   "SRC DST / SRC... DIR", "Move files" },
    { "rm",   cmd_rm,   "FILE...",              "Remove files" },
    { "cat",  cmd_cat,  "[-n] [FILE...]",       "Print file contents (stdin if no FILE)" },
    { "cat2", cmd_cat2, "[-n] [FILE...]",       "Alias for cat" },
    { "echo", cmd_echo, "[args...] [> FILE]",   "Print or redirect text" },
    { (const char *)0, 0, 0, 0 }
};
void shell_cmd_file_init(void) { shell_register_cmds(file_cmds); }
