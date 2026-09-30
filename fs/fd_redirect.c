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

/* exec/exec.c の出力保護 (fs/ はカーネルヘッダを見ない作法なので extern) */
extern int ring3_ptr_ok(u32 p);
extern int ring3_user_ranges_writable(u32 pa, u32 la, u32 pb, u32 lb);
extern int ring3_user_ranges_writable_always(u32 pa, u32 la, u32 pb, u32 lb);
extern int ring3_call_from_user(void);
extern void ring3_fault_kill(void);

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
        redir_table[i].user_origin = 0;
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
    redir_table[fd].user_origin = 0;

    return 0;
}

int fd_redirect_to_buffer(int fd, u8 *buf, u32 size, u32 len)
{
    int user;

    if (fd < 0 || fd > 2) return -1;
    if (!buf || size == 0) return -1;

    /* 由来はいま決める (あとで書くときの文脈 — WM の中かどうか — は由来と
     * 関係が無い)。アプリが張るなら、ここでも表を歩いて RW + USER を確かめる
     * (KAPI ラッパの出力検査と二重の守り。断るときは表を変えない)。 */
    user = ring3_call_from_user();
    if (user && !ring3_user_ranges_writable_always((u32)buf, size, 0, 0))
        return -1;

    /* 既存のリダイレクトを解除 */
    fd_redirect_reset(fd);

    redir_table[fd].target_type = FD_TARGET_BUFFER;
    redir_table[fd].file_fd = -1;
    redir_table[fd].buffer = buf;
    redir_table[fd].buf_capacity = size;
    redir_table[fd].buf_pos = 0;
    redir_table[fd].buf_len = len;
    redir_table[fd].owner = cur_res_owner;
    redir_table[fd].user_origin = user;

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
    redir_table[fd].user_origin = 0;
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
        /* バッファからの読み込み */
        u32 avail = r->buf_len - r->buf_pos;
        u32 to_read;
        u32 i;
        u8 *dst = (u8 *)buf;

        if (avail == 0) return 0; /* EOF */
        to_read = (size < avail) ? size : avail;
        for (i = 0; i < to_read; i++) {
            dst[i] = r->buffer[r->buf_pos + i];
        }
        r->buf_pos += to_read;
        return (int)to_read;
    }

    return -1; /* コンソールモードでは呼ばれないはず */
}

int fd_redirect_buf_write_ok(const FdRedirect *r, u32 to_write)
{
    u32 dst;

    if (!r || to_write == 0) return 1;
    dst = (u32)(r->buffer + r->buf_len);
    /* アプリが登録したバッファ: 書く瞬間の文脈 (WM の中でも) に関係なく歩く。
     * 登録時の検査の後でページ属性が変わり得る (sys_shm_lock で RO になる
     * など。CR0.WP=0 なのでカーネルの書きは止まらない)。 */
    if (r->user_origin)
        return ring3_user_ranges_writable_always(dst, to_write, 0, 0);
    /* 常駐側が登録したバッファ: ユーザ帯の番地だけ文脈つきの門で見る
     * (カーネル帯・シェル帯のバッファは対象外 — 従来どおり)。 */
    if (ring3_ptr_ok(dst))
        return ring3_user_ranges_writable(dst, to_write, 0, 0);
    return 1;
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
        /* バッファへの書き込み */
        u32 space = r->buf_capacity - r->buf_len;
        u32 to_write;
        u32 i;
        const u8 *src = (const u8 *)buf;

        to_write = (size < space) ? size : space;
        /* 書く前に毎回確かめる (票 TASK_KAPI_OUTPUT_GUARD、実装レビュー 4 の
         * 「最後の砦」)。判定は fd_redirect_buf_write_ok — アプリが登録した
         * バッファは WM の文脈でも素通しにしない (2026-09-26、代行レビュー P2)。 */
        if (!fd_redirect_buf_write_ok(r, to_write)) {
            ring3_fault_kill();   /* 戻らない */
        }
        for (i = 0; i < to_write; i++) {
            r->buffer[r->buf_len + i] = src[i];
        }
        r->buf_len += to_write;
        return (int)to_write;
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
    r->user_origin = 0;
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
