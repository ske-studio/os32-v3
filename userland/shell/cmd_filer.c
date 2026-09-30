/* ======================================================================== */
/*  CMD_FILER.C — シェル内蔵TVRAMファイラ                                    */
/*                                                                          */
/*  シェル内蔵コマンドとして実装することで、exec_run で起動したプログラムが    */
/*  終了した後もファイラ画面に復帰できる。                                   */
/*  (シェルは Level 0 (0x300000) に常駐しており、Level 1 のプログラムが       */
/*  0x400000 を上書きしてもシェルのコードは無傷で残る)                       */
/*                                                                          */
/*  描画関数は libfiler/filer_draw.c に分離済み。                           */
/*  本ファイルはイベントループ、ディレクトリ走査、シェル統合を担当。         */
/*                                                                          */
/*  操作: ←→↑↓=移動, Enter=開く/実行, BS=親ディレクトリ, Q/ESC=終了     */
/* ======================================================================== */

#include "shell.h"
#include "filer_draw.h"

/* ======================================================================== */
/*  ファイルタイプ関連付け                                                   */
/* ======================================================================== */

#define FL_FILETYPES_PATH   "/etc/filetypes"
/* FD (FAT、LFN なし) では 8.3 の短い名前で置く (build/packages.yaml の fd.rename)。
 * 短い名前を読むのは、/etc を載せているマウントが LFN の無い FS (FAT) のときだけ
 * — HDD (ext2) で正規名が欠けたり壊れたりしたとき、残っている短い名前を黙って
 * 掴まないように (Codex 実装レビュー往復 1 / 2)。
 * KAPI にはパスから FS 種別を引く口が無い (vfs_devname はマウント点の完全一致で
 * デバイス名を返すだけ、sys_stat の st_dev はマウントの番号だけ)。そこで確実に
 * FAT と言える場合だけを拾う: 「/etc がルートと同じマウント (st_dev が同じ) で、
 * ルートが FD」。FD 起動のルートは kernel.c が必ず FAT でマウントする
 * (ルートのデバイス名と FS 名は tools/tests/test_packages.py が kernel.c と
 * fs/fatfs_vfs.c に突き合わせる)。/etc に別のマウント (HDD の ext2 など) が
 * 載っていれば st_dev が違うので読まない。FAT の HDD が /etc に載る場合も
 * 読まない (安全側。短い名前を探さないだけ) */
#define FL_ETC_DIR           "/etc"
#define FL_FILETYPES_PATH_83 "/etc/filetype"
#define FL_FD_DEV_PREFIX0    'f'   /* kernel.c の FD ルート "fd0" の先頭 2 文字 */
#define FL_FD_DEV_PREFIX1    'd'
#define FL_FILETYPES_MAXSZ  8192
#define FL_MAX_ASSOC        128

/* キーコード */
#define FL_KEY_ESC          0x1B
#define FL_KEY_ENTER        0x0D
#define FL_KEY_BS           0x08
#define FL_KEY_UP           0x1E
#define FL_KEY_DOWN         0x1F
#define FL_KEY_LEFT         0x1D
#define FL_KEY_RIGHT        0x1C

/* キーリピートウェイト (tick単位、1tick ≈ 10ms) */
#define FL_KEY_WAIT_TICKS   6

/* ファイルタイプ関連付けエントリ */
typedef struct {
    const char *ext;
    const char *cmd;
} FL_Assoc;

/* ======================================================================== */
/*  静的変数                                                                */
/* ======================================================================== */

static FL_State fl_state;
static int fl_running;

/* ファイルタイプ関連付け (動的確保) */
static char *ft_buf = NULL;
static FL_Assoc *ft_table = NULL;
static int ft_count = 0;

/* ======================================================================== */
/*  ファイルタイプ関連付け読み込み                                           */
/* ======================================================================== */

/* /etc を載せているマウントが FD のルート (= FAT) か。上の説明のとおり */
static int ft_etc_on_fd_root(void)
{
    OS32_Stat st_root, st_etc;
    const char *dev;

    if (g_api->sys_stat("/", &st_root) != 0) return 0;
    if (g_api->sys_stat(FL_ETC_DIR, &st_etc) != 0) return 0;
    if (st_root.st_dev != st_etc.st_dev) return 0;
    dev = g_api->vfs_devname("/");
    return dev && dev[0] == FL_FD_DEV_PREFIX0 && dev[1] == FL_FD_DEV_PREFIX1;
}

static void ft_load(void)
{
    int fd, sz, i, line_start, got_eq;
    char *p;

    ft_count = 0;

    fd = g_api->sys_open(FL_FILETYPES_PATH, O_RDONLY);
    if (fd < 0 && ft_etc_on_fd_root()) fd = g_api->sys_open(FL_FILETYPES_PATH_83, O_RDONLY);
    if (fd < 0) return;

    ft_buf = (char *)g_api->mem_alloc(FL_FILETYPES_MAXSZ);
    if (!ft_buf) { g_api->sys_close(fd); return; }

    sz = g_api->sys_read(fd, ft_buf, FL_FILETYPES_MAXSZ - 1);
    /* T16: 読み切れなければ**関連付け表を使わない**。末尾の行が途中で切れると
     * `.txt=ed` が `.txt=e` になり、別のコマンドに関連付いて起動する。 */
    if (sz == FL_FILETYPES_MAXSZ - 1) {
        char probe;
        if (g_api->sys_read(fd, &probe, 1) > 0) {
            g_api->sys_close(fd);
            g_api->mem_free(ft_buf);
            ft_buf = NULL;
            return;
        }
    }
    g_api->sys_close(fd);
    if (sz <= 0) { g_api->mem_free(ft_buf); ft_buf = NULL; return; }
    ft_buf[sz] = '\0';

    ft_table = (FL_Assoc *)g_api->mem_alloc(sizeof(FL_Assoc) * FL_MAX_ASSOC);
    if (!ft_table) { g_api->mem_free(ft_buf); ft_buf = NULL; return; }

    line_start = 0;
    for (i = 0; i <= sz; i++) {
        if (ft_buf[i] == '\n' || ft_buf[i] == '\0') {
            ft_buf[i] = '\0';
            p = &ft_buf[line_start];

            if (*p != '\0' && *p != '#' && ft_count < FL_MAX_ASSOC) {
                got_eq = 0;
                {
                    char *q = p;
                    while (*q) {
                        if (*q == '=') {
                            *q = '\0';
                            ft_table[ft_count].ext = p;
                            ft_table[ft_count].cmd = q + 1;
                            if (*(q + 1) != '\0') {
                                ft_count++;
                            }
                            got_eq = 1;
                            break;
                        }
                        q++;
                    }
                }
                (void)got_eq;
            }
            line_start = i + 1;
        }
    }
}

static void ft_free(void)
{
    if (ft_table) { g_api->mem_free(ft_table); ft_table = NULL; }
    if (ft_buf) { g_api->mem_free(ft_buf); ft_buf = NULL; }
    ft_count = 0;
}

static const char *ft_find(const char *filename)
{
    int i, len, elen;
    const char *dot = NULL;
    const char *p;

    p = filename;
    while (*p) { if (*p == '.') dot = p; p++; }
    if (!dot) return NULL;

    len = (int)(p - dot);

    for (i = 0; i < ft_count; i++) {
        elen = strlen(ft_table[i].ext);
        if (elen == len && strncmp(dot, ft_table[i].ext, len) == 0) {
            return ft_table[i].cmd;
        }
    }
    return NULL;
}

/* ======================================================================== */
/*  パスユーティリティ                                                      */
/* ======================================================================== */

/* 戻り値: 連結後の長さ / out に収まらなければ -1 (票 T16)。
 * 以前は戻り値が無く、溢れても切った綴りで**別のファイルを起動**し、
 * **別のディレクトリへ移動**していた。 */
static int fl_path_join(char *out, int out_sz,
                        const char *dir, const char *name)
{
    int i = 0, j = 0;
    while (dir[j]) {
        if (i >= out_sz - 2) return -1;
        out[i++] = dir[j++];
    }
    if (i > 0 && out[i - 1] != '/') out[i++] = '/';
    j = 0;
    while (name[j]) {
        if (i >= out_sz - 1) return -1;
        out[i++] = name[j++];
    }
    out[i] = '\0';
    return i;
}

static void fl_path_parent(char *path)
{
    int len = 0;
    int last_slash = 0;
    int k;

    while (path[len]) len++;
    if (len > 1 && path[len - 1] == '/') len--;

    for (k = 0; k < len; k++) {
        if (path[k] == '/') last_slash = k;
    }

    if (last_slash == 0) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        path[last_slash] = '\0';
    }
}

/* ======================================================================== */
/*  ファイルスキャン                                                        */
/* ======================================================================== */

static int fl_is_bin_file(const char *name)
{
    int len = 0;
    const char *p = name;
    while (*p) { len++; p++; }
    if (len < 4) return 0;
    p = name + len - 4;
    if (p[0] != '.') return 0;
    if (p[1] == 'b' && p[2] == 'i' && p[3] == 'n') return 1;
    if (p[1] == 'B' && p[2] == 'I' && p[3] == 'N') return 1;
    return 0;
}

static void fl_ls_callback(const void *entry_raw, void *ctx)
{
    const char *name = (const char *)entry_raw;
    const u8 *base = (const u8 *)entry_raw;
    u32 size;
    u8 type;
    FL_Entry *e;
    int i;

    (void)ctx;

    size = *(const u32 *)(base + 256);
    type = base[260];

    if (name[0] == '.') {
        if (name[1] == '\0') return;
        if (name[1] == '.' && name[2] == '\0') return;
    }

    /* T16: 63 バイトで切った名前を表に載せると、Enter で**別のファイル**を
     * 起動し、**別のディレクトリ**へ入る。載せずに件数だけ数える
     * (sh_ls.inc の「溢れた分は数だけ」と同じ作法)。 */
    for (i = 0; name[i]; i++) {}
    if (i > FL_MAX_NAME_LEN - 1) { fl_state.dropped++; return; }

    if (fl_state.count >= FL_MAX_ENTRIES) { fl_state.dropped++; return; }

    e = &fl_state.entries[fl_state.count];
    for (i = 0; name[i]; i++) e->name[i] = name[i];
    e->name[i] = '\0';
    e->size = size;
    e->is_dir = (type == OS32_FILE_TYPE_DIR) ? 1 : 0;
    e->is_exe = 0;
    if (type == OS32_FILE_TYPE_FILE && fl_is_bin_file(name))
        e->is_exe = 1;
    fl_state.count++;
}

static void fl_sort_entries(void)
{
    int start;
    int i, j;
    FL_Entry tmp;

    start = 0;
    if (fl_state.count > 0 &&
        fl_state.entries[0].name[0] == '.' &&
        fl_state.entries[0].name[1] == '.' &&
        fl_state.entries[0].name[2] == '\0') {
        start = 1;
    }

    for (i = start + 1; i < fl_state.count; i++) {
        memcpy(&tmp, &fl_state.entries[i], sizeof(FL_Entry));
        j = i - 1;
        while (j >= start) {
            int swap = 0;
            FL_Entry *a = &fl_state.entries[j];

            if (tmp.is_dir && !a->is_dir) {
                swap = 1;
            } else if (tmp.is_dir == a->is_dir) {
                if (strcmp(tmp.name, a->name) < 0) swap = 1;
            }

            if (!swap) break;
            memcpy(&fl_state.entries[j + 1], a, sizeof(FL_Entry));
            j--;
        }
        memcpy(&fl_state.entries[j + 1], &tmp, sizeof(FL_Entry));
    }
}

static void fl_scan_dir(void)
{
    fl_state.count = 0;
    fl_state.cursor = 0;
    fl_state.page_top = 0;
    fl_state.dropped = 0;

    if (!(fl_state.cwd[0] == '/' && fl_state.cwd[1] == '\0')) {
        FL_Entry *e = &fl_state.entries[0];
        e->name[0] = '.'; e->name[1] = '.'; e->name[2] = '\0';
        e->size = 0;
        e->is_dir = 1;
        e->is_exe = 0;
        fl_state.count = 1;
    }

    g_api->sys_ls(fl_state.cwd, (void *)fl_ls_callback, NULL);
    fl_sort_entries();
}

/* ======================================================================== */
/*  カーソル操作とアクション                                                */
/* ======================================================================== */

static int fl_move_cursor(int delta)
{
    int old_page_top = fl_state.page_top;
    int new_cursor = fl_state.cursor + delta;

    if (new_cursor < 0) new_cursor = 0;
    if (new_cursor >= fl_state.count) new_cursor = fl_state.count - 1;
    if (new_cursor < 0) new_cursor = 0;

    fl_state.cursor = new_cursor;

    if (fl_state.cursor < fl_state.page_top) {
        fl_state.page_top = (fl_state.cursor / FL_PAGE_ITEMS) * FL_PAGE_ITEMS;
    }
    if (fl_state.cursor >= fl_state.page_top + FL_PAGE_ITEMS) {
        fl_state.page_top = (fl_state.cursor / FL_PAGE_ITEMS) * FL_PAGE_ITEMS;
    }

    return (fl_state.page_top != old_page_top) ? 1 : 0;
}

static void fl_action_parent(void)
{
    fl_path_parent(fl_state.cwd);
    fl_scan_dir();
}

static void fl_exec_program(const char *cmdline)
{
    g_api->tvram_clear();
    sh_launch(cmdline);
    sh_gfx_restore();
    fl_scan_dir();
}

static void fl_action_enter(void)
{
    FL_Entry *e;

    if (fl_state.count == 0) return;
    e = &fl_state.entries[fl_state.cursor];

    if (e->is_dir) {
        if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == '\0') {
            fl_action_parent();
        } else {
            char new_cwd[FL_MAX_PATH_LEN];
            if (fl_path_join(new_cwd, FL_MAX_PATH_LEN,
                             fl_state.cwd, e->name) < 0) {
                fldraw_popup_message("Path too long", 0x41);
                return;
            }
            strncpy(fl_state.cwd, new_cwd, FL_MAX_PATH_LEN - 1);
            fl_state.cwd[FL_MAX_PATH_LEN - 1] = '\0';
            fl_scan_dir();
        }
    } else if (e->is_exe) {
        char fullpath[FL_MAX_PATH_LEN];
        if (fl_path_join(fullpath, FL_MAX_PATH_LEN,
                         fl_state.cwd, e->name) < 0) {
            fldraw_popup_message("Path too long", 0x41);
            return;
        }
        fl_exec_program(fullpath);
    } else {
        const char *prog = ft_find(e->name);
        if (prog) {
            char cmdline[FL_MAX_PATH_LEN * 2];
            char fullpath[FL_MAX_PATH_LEN];
            int ci = 0;
            const char *s;

            if (fl_path_join(fullpath, FL_MAX_PATH_LEN,
                             fl_state.cwd, e->name) < 0) {
                fldraw_popup_message("Path too long", 0x41);
                return;
            }

            /* 関連付けコマンド + ' ' + パス。切れた行で起動しない (T16)。 */
            s = prog;
            while (*s) {
                if (ci >= (int)sizeof(cmdline) - 2) {
                    fldraw_popup_message("Command line too long", 0x41);
                    return;
                }
                cmdline[ci++] = *s++;
            }
            cmdline[ci++] = ' ';
            s = fullpath;
            while (*s) {
                if (ci >= (int)sizeof(cmdline) - 1) {
                    fldraw_popup_message("Command line too long", 0x41);
                    return;
                }
                cmdline[ci++] = *s++;
            }
            cmdline[ci] = '\0';

            fl_exec_program(cmdline);
        } else {
            fldraw_popup_message("No program associated with this file type", 0x41);
        }
    }
}

/* ======================================================================== */
/*  ファイラメインループ                                                    */
/* ======================================================================== */

static void fl_init(const char *start_dir)
{
    memset(&fl_state, 0, sizeof(fl_state));
    strncpy(fl_state.cwd, start_dir, FL_MAX_PATH_LEN - 1);
    fl_state.cwd[FL_MAX_PATH_LEN - 1] = '\0';
    fldraw_init(g_api);
    ft_load();
    fl_scan_dir();
}

static void fl_loop(void)
{
    int need_full_redraw = 1;
    fl_running = 1;
    g_api->tvram_clear();

    while (fl_running) {
        int ch;
        int old_cursor;
        int page_changed;

        if (need_full_redraw) {
            fldraw_all(&fl_state);
            need_full_redraw = 0;
        }

        ch = g_api->kbd_getchar();
        old_cursor = fl_state.cursor;

        switch (ch) {
        case FL_KEY_UP:
            page_changed = fl_move_cursor(-FL_NUM_COLUMNS);
            if (page_changed) need_full_redraw = 1;
            else fldraw_cursor_update(&fl_state, old_cursor);
            break;
        case FL_KEY_DOWN:
            page_changed = fl_move_cursor(FL_NUM_COLUMNS);
            if (page_changed) need_full_redraw = 1;
            else fldraw_cursor_update(&fl_state, old_cursor);
            break;
        case FL_KEY_LEFT:
            page_changed = fl_move_cursor(-1);
            if (page_changed) need_full_redraw = 1;
            else fldraw_cursor_update(&fl_state, old_cursor);
            break;
        case FL_KEY_RIGHT:
            page_changed = fl_move_cursor(1);
            if (page_changed) need_full_redraw = 1;
            else fldraw_cursor_update(&fl_state, old_cursor);
            break;
        case FL_KEY_ENTER:
            fl_action_enter();
            need_full_redraw = 1;
            break;
        case FL_KEY_BS:
            fl_action_parent();
            need_full_redraw = 1;
            break;
        case FL_KEY_ESC:
        case 'q':
        case 'Q':
            fl_running = 0;
            break;
        default:
            break;
        }

        /* キーリピート制御 */
        {
            u32 wait_start = g_api->get_tick();
            while ((g_api->get_tick() - wait_start) < FL_KEY_WAIT_TICKS) {
                g_api->sys_halt();
            }
            while (g_api->kbd_trygetkey() >= 0)
                ;
        }
    }
}

/* ======================================================================== */
/*  シェルコマンドハンドラ                                                  */
/* ======================================================================== */

static int cmd_filer(int argc, char **argv)
{
    const char *start_dir;

    if (argc > 1) {
        start_dir = argv[1];
    } else {
        start_dir = g_api->sys_getcwd();
    }

    fl_init(start_dir);
    fl_loop();
    ft_free();
    g_api->tvram_clear();
    return 0;
}

/* 登録用テーブル */
static const ShellCmd filer_cmds[] = {
    { "filer", cmd_filer, "[dir]", "File manager" },
    { (const char *)0, 0, 0, 0 }
};

void shell_cmd_filer_init(void)
{
    shell_register_cmds(filer_cmds);
}
