/* ======================================================================== */
/*  FD_REDIRECT.C -- FD 0/1/2 リダイレクト管理                               */
/*                                                                          */
/*  標準入出力 (stdin/stdout/stderr) をファイルやメモリバッファに             */
/*  リダイレクトするためのカーネルモジュール。                                */
/*                                                                          */
/*  vfs_fd.c の vfs_read_fd()/vfs_write_fd() から呼び出され、                */
/*  リダイレクト先への透過的な入出力切り替えを提供する。                      */
/* ======================================================================== */

#include "fd_redirect.h"
#include "vfs.h"
#include "os32_kapi_shared.h"

/* FD 0/1/2 のリダイレクト状態テーブル */
static FdRedirect redir_table[3];

/* 現在のリソース所有者 (exec ネスト深度)。exec.c がレベル遷移時に更新する */
static int cur_res_owner = 0;

void res_owner_set(int owner) { cur_res_owner = owner; }
int  res_owner_get(void)      { return cur_res_owner; }

/* ======================================================================== */
/*  初期化                                                                  */
/* ======================================================================== */

void fd_redirect_init(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        redir_table[i].target_type = FD_TARGET_CONSOLE;
        redir_table[i].file_fd = -1;
        redir_table[i].buffer = (u8 *)0;
        redir_table[i].buf_capacity = 0;
        redir_table[i].buf_pos = 0;
        redir_table[i].buf_len = 0;
        redir_table[i].owner = 0;
        redir_table[i].access = (RedirAccess){0};
    }
}

/* ======================================================================== */
/*  リダイレクト設定                                                        */
/* ======================================================================== */

int fd_redirect_to_file(int fd, const char *path, int mode)
{
    int file_fd;
    int open_mode;

    if (fd < 0 || fd > 2) return -1;
    if (!path) return -1;

    /* 既存のリダイレクトを解除 */
    fd_redirect_reset(fd);

    /* オープンモードを決定 */
    if (mode == FD_REDIR_READ) {
        open_mode = O_RDONLY;
    } else if (mode == FD_REDIR_APPEND) {
        open_mode = O_WRONLY | O_CREAT;
    } else {
        /* FD_REDIR_WRITE: 上書き */
        open_mode = O_WRONLY | O_CREAT | O_TRUNC;
    }

    file_fd = vfs_open(path, open_mode);
    if (file_fd < 0) return file_fd;

    /* 追記モードの場合、末尾にシーク */
    if (mode == FD_REDIR_APPEND) {
        vfs_seek(file_fd, 0, SEEK_END);
    }

    redir_table[fd].target_type = FD_TARGET_FILE;
    redir_table[fd].file_fd = file_fd;
    redir_table[fd].buffer = (u8 *)0;
    redir_table[fd].buf_capacity = 0;
    redir_table[fd].buf_pos = 0;
    redir_table[fd].buf_len = 0;
    redir_table[fd].owner = cur_res_owner;
    redir_table[fd].access = (RedirAccess){0};

    return 0;
}

int fd_redirect_to_buffer(int fd, u8 *buf, u32 size, u32 len)
{
    RedirAccess access;

    if (fd < 0 || fd > 2) return -1;
    if (!buf || size == 0 || len > size || size - 1 > ~(u32)0 - (u32)(uptr)buf)
        return -1;
    if (!redir_access_capture(&access) ||
        !redir_access_check(&access, (u32)(uptr)buf, size, 1)) return -1;

    /* 既存のリダイレクトを解除 */
    fd_redirect_reset(fd);

    redir_table[fd].target_type = FD_TARGET_BUFFER;
    redir_table[fd].file_fd = -1;
    redir_table[fd].buffer = buf;
    redir_table[fd].buf_capacity = size;
    redir_table[fd].buf_pos = 0;
    redir_table[fd].buf_len = len;
    redir_table[fd].owner = cur_res_owner;
    redir_table[fd].access = access;

    return 0;
}

/* ======================================================================== */
/*  リダイレクト解除                                                        */
/* ======================================================================== */

void fd_redirect_reset(int fd)
{
    if (fd < 0 || fd > 2) return;

    /* ファイルリダイレクト中ならクローズ */
    if (redir_table[fd].target_type == FD_TARGET_FILE) {
        if (redir_table[fd].file_fd >= 0) {
            vfs_close(redir_table[fd].file_fd);
        }
    }

    redir_table[fd].target_type = FD_TARGET_CONSOLE;
    redir_table[fd].file_fd = -1;
    redir_table[fd].buffer = (u8 *)0;
    redir_table[fd].buf_capacity = 0;
    redir_table[fd].buf_pos = 0;
    redir_table[fd].buf_len = 0;
    redir_table[fd].owner = 0;
    redir_table[fd].access = (RedirAccess){0};
}

void fd_redirect_reset_owned(int owner)
{
    int fd;
    for (fd = 0; fd < 3; fd++) {
        if (redir_table[fd].target_type != FD_TARGET_CONSOLE &&
            redir_table[fd].owner == owner) {
            fd_redirect_reset(fd);
        }
    }
}

/* ======================================================================== */
/*  状態問い合わせ                                                          */
/* ======================================================================== */

/* fd 0/1/2 の種別を答える**唯一の場所** ([C4]、票 TASK_FSTAT_REDIR §3-1)。
 * 契約は fd_redirect.h のコメントにある。ここを変えれば isatty も fstat も
 * fd_is_redirected も一緒に動く — 片方だけ直せない形にするための 1 本。 */
u16 fd_redirect_ifmt(int fd, int *out_file_fd)
{
    if (out_file_fd) *out_file_fd = -1;
    if (fd < 0 || fd > 2) return OS_S_IFCHR;

    switch (redir_table[fd].target_type) {
    case FD_TARGET_FILE:
        if (out_file_fd) *out_file_fd = redir_table[fd].file_fd;
        return OS_S_IFREG;
    case FD_TARGET_BUFFER:
        /* パイプは名前を持たない FIFO。**キャラクタデバイスではない** —
         * S_IFCHR と答えると isatty() が 0 を返すのと食い違う。 */
        return OS_S_IFIFO;
    default:
        return OS_S_IFCHR;
    }
}

int fd_is_redirected(int fd)
{
    if (fd < 0 || fd > 2) return 0;
    return (fd_redirect_ifmt(fd, (int *)0) != OS_S_IFCHR);
}

/* ======================================================================== */
/*  リダイレクト先への読み書き                                              */
/* ======================================================================== */

int fd_redirect_read(int fd, void *buf, u32 size)
{
    FdRedirect *r;

    if (fd < 0 || fd > 2) return -1;
    r = &redir_table[fd];

    if (r->target_type == FD_TARGET_FILE) {
        /* ファイルからの読み込み */
        return vfs_read_fd(r->file_fd, buf, size);
    }

    if (r->target_type == FD_TARGET_BUFFER) {
        u32 avail, count;
        if (r->buf_pos > r->buf_len || r->buf_len > r->buf_capacity) return -1;
        avail = r->buf_len - r->buf_pos;
        count = (size < avail) ? size : avail;
        return redir_access_copy(&r->access, r->buffer, &r->buf_pos,
                                 buf, count, 0);
    }

    return -1; /* コンソールモードでは呼ばれないはず */
}

int fd_redirect_buf_write_ok(const FdRedirect *r, u32 to_write)
{
    if (!r || r->buf_len > r->buf_capacity ||
        to_write > r->buf_capacity - r->buf_len ||
        r->buf_len > ~(u32)0 - (u32)(uptr)r->buffer) return 0;
    return redir_access_check(&r->access, (u32)(uptr)r->buffer + r->buf_len,
                              to_write, 1);
}

int fd_redirect_write(int fd, const void *buf, u32 size)
{
    FdRedirect *r;

    if (fd < 0 || fd > 2) return -1;
    r = &redir_table[fd];

    if (r->target_type == FD_TARGET_FILE) {
        /* ファイルへの書き込み */
        return vfs_write_fd(r->file_fd, buf, size);
    }

    if (r->target_type == FD_TARGET_BUFFER) {
        u32 space, count;
        if (r->buf_pos > r->buf_len || r->buf_len > r->buf_capacity) return -1;
        space = r->buf_capacity - r->buf_len;
        count = (size < space) ? size : space;
        return redir_access_copy(&r->access, r->buffer, &r->buf_len,
                                 (void *)buf, count, 1);
    }

    return -1;
}

/* ======================================================================== */
/*  パイプバッファ情報取得                                                  */
/* ======================================================================== */

u32 fd_redirect_get_buf_len(int fd)
{
    if (fd < 0 || fd > 2) return 0;
    if (redir_table[fd].target_type != FD_TARGET_BUFFER) return 0;
    return redir_table[fd].buf_len;
}

/* ======================================================================== */
/*  アプリ ID ごとの退避枠 (票 T9 §12 T1)                                    */
/*                                                                          */
/*  持ち替えは **移動** で行う (コピーではない)。同じ file_fd を「いまの表」  */
/*  と「枠」の両方が持つと、回収のときに二重 close になり、その間に別の       */
/*  open が同じ FD 番号を拾っていれば他人のファイルを閉じる。だから           */
/*  save は「写して、いまの表をコンソールへ」、restore の後は呼び手が         */
/*  clear_state で枠を空にする、という組で使う。                             */
/* ======================================================================== */

static void redir_entry_clear(FdRedirect *r)
{
    r->target_type = FD_TARGET_CONSOLE;
    r->file_fd = -1;
    r->buffer = (u8 *)0;
    r->buf_capacity = 0;
    r->buf_pos = 0;
    r->buf_len = 0;
    r->owner = 0;
    r->access = (RedirAccess){0};
}

void fd_redirect_save(FdRedirectState *out)
{
    int fd;
    if (!out) return;
    for (fd = 0; fd < FD_REDIRECT_SLOTS; fd++) {
        out->fd[fd] = redir_table[fd];
        redir_entry_clear(&redir_table[fd]);   /* 閉じない (所有は out へ) */
    }
}

void fd_redirect_restore(const FdRedirectState *in)
{
    int fd;
    if (!in) return;
    for (fd = 0; fd < FD_REDIRECT_SLOTS; fd++) {
        redir_table[fd] = in->fd[fd];
    }
}

void fd_redirect_clear_state(FdRedirectState *st)
{
    int fd;
    if (!st) return;
    for (fd = 0; fd < FD_REDIRECT_SLOTS; fd++) redir_entry_clear(&st->fd[fd]);
}

int fd_redirect_state_active(const FdRedirectState *st, int fd)
{
    if (!st || fd < 0 || fd >= FD_REDIRECT_SLOTS) return 0;
    return (st->fd[fd].target_type != FD_TARGET_CONSOLE);
}
