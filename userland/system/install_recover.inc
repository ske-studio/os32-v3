/* ======================================================================== */
/*  INSTALL_RECOVER.INC — `install --recover-settings` / `--revert-settings` */
/*                        (票 docs/archive/settings/TASK_S3.md §1)          */
/*                                                                          */
/*  install.c から #include される本体。KAPI を直接呼ばず、すべての口を        */
/*  `RecoverOps` (関数ポインタ表) 経由にしてあるので、ホスト TDD              */
/*  (tools/tests/install_recover_host.c) が同じソースを取り込んで            */
/*  RAM バックエンドと障害注入の上で回せる。                                  */
/*                                                                          */
/*  設計原則 (票 §1): 元の対 (settings.db + -journal) は最後の切替まで        */
/*  1 バイトも動かさない。退避も復帰も rename ではなく **コピー + バイト比較**。 */
/*  rename は最終切替の 1 回だけで、その両名残存は消さずに停止する。           */
/*  消してよいのは今回自分が作ったファイルと、承認済み 1 世代の旧 .bak* /      */
/*  .failed* だけ。                                                          */
/*                                                                          */
/*  [C1] C89 (GNU89)。libc は使わない。                                      */
/* ======================================================================== */

/* ---- KAPI 境界 (ホスト TDD はここを贋物に差し替える) -------------------- */
typedef struct {
    int  (*stat)(const char *path, OS32_Stat *st);
    int  (*open)(const char *path, int mode);
    int  (*read)(int fd, void *buf, u32 size);
    int  (*write)(int fd, const void *buf, u32 size);
    void (*close)(int fd);
    int  (*unlink)(const char *path);
    int  (*rename)(const char *oldpath, const char *newpath);
    int  (*mount)(const char *prefix, const char *dev, const char *fs);
    int  (*is_mounted)(const char *prefix);
    int  (*sync)(void);
    int  (*db_open_existing)(const char *path, int writable);
    int  (*db_prepare_only)(int handle, const char *sql);
    int  (*db_step)(int handle);
    int  (*db_finalize)(int handle);
    int  (*db_close)(int handle);
    int  (*db_error_code)(int handle);
    unsigned char *(*shm)(void);
    void (*kprintf)(u8 attr, const char *fmt, ...);
    int  (*getkey)(void);
} RecoverOps;

/* ---- 対象の 9 名 + マスタ ---------------------------------------------- */
#define RC_TARGET_MOUNT   "/hd0"
#define RC_TARGET_ETC     "/hd0/etc"
#define RC_TARGET_DEV     "hd0"
#define RC_TARGET_FS      "ext2"
#define RC_MASTER         "/etc/settings.db"

#define RC_N_DB           0
#define RC_N_JOURNAL      1
#define RC_N_BAK          2
#define RC_N_BAKJ         3
#define RC_N_NEW          4
#define RC_N_NEWJ         5
#define RC_N_FAILED       6
#define RC_N_FAILEDJ      7
#define RC_N_STATE        8
#define RC_NAMES          9

static const char *const rc_path[RC_NAMES] = {
    "/hd0/etc/settings.db",
    "/hd0/etc/settings.db-journal",
    "/hd0/etc/settings.db.bak",
    "/hd0/etc/settings.db.bak-journal",
    "/hd0/etc/settings.db.new",
    "/hd0/etc/settings.db.new-journal",
    "/hd0/etc/settings.db.failed",
    "/hd0/etc/settings.db.failed-journal",
    "/hd0/etc/settings.db.recover-state"
};

static const char *const rc_name[RC_NAMES] = {
    "settings.db",
    "settings.db-journal",
    "settings.db.bak",
    "settings.db.bak-journal",
    "settings.db.new",
    "settings.db.new-journal",
    "settings.db.failed",
    "settings.db.failed-journal",
    "settings.db.recover-state"
};

/* 三値 */
#define RC_ABSENT   0
#define RC_PRESENT  1
#define RC_UNKNOWN  2

/* 印の phase */
#define RC_PH_NONE       0
#define RC_PH_BACKUP     1
#define RC_PH_SWITCHING  2
#define RC_PH_SWITCHED   3
#define RC_PH_FAILED     4
#define RC_PH_REVERTING  5
#define RC_PH_DONE       6
#define RC_PH_COUNT      7

static const char *const rc_phase_name[RC_PH_COUNT] = {
    "none", "backup", "switching", "switched", "failed", "reverting", "done"
};

/* VFS の st_dev は (dev_type << 8 | unit) + 1 (fs/vfs.c の vfs_mount_dev_of、
 * 種別は fs/vfs.h の VFS_DEV_*)。カーネルヘッダは外部プログラムから見えない
 * ので、ここが唯一の写し ([C4] の但し書き: 正典は fs/vfs.h)。0 = 不明。 */
#define RC_DEV_HD          0
#define RC_DEV_FD          1
#define RC_DEV_TYPE(x)     ((int)((((x) - 1u) >> 8) & 0xFFu))
#define RC_DEV_UNIT(x)     ((int)(((x) - 1u) & 0xFFu))

/* `.new` の検証 close が失敗したときの正式手順 (票 §1f 4、追加往復 non-blocker)。
 * 同じ起動のまま rm すると隔離接続が掴む inode を消すので、再起動が前提。 */
#define RC_NEW_CLOSE_STEPS \
    "REBOOT from the floppy, then: install --revert-settings hd0," \
    " rm /hd0/etc/settings.db.new, install --recover-settings hd0"

/* 16KB のコピー / 比較バッファ (票 §1e) */
#define RC_BUF 16384
static u8 rc_buf_a[RC_BUF];
static u8 rc_buf_b[RC_BUF];
/* 印は 1 行。書式は `phase=<p> orig=<present|missing> journal=<present|absent>
 * size=<n>\n` で、最長でも 80B に収まる。 */
#define RC_MARK_MAX 128
static char rc_mark_buf[RC_MARK_MAX];
static char rc_mark_back[RC_MARK_MAX];

/* 走査の結果 */
static int rc_state[RC_NAMES];
static u32 rc_size[RC_NAMES];
static u32 rc_dev[RC_NAMES];
static u32 rc_ino[RC_NAMES];
/* 今回自分が作ったファイル (失敗時に消してよいのはこれだけ) */
static int rc_made[RC_NAMES];

typedef struct {
    int present;
    int phase;
    int orig_present;
    int journal_present;
    u32 size;
} RcMark;

typedef struct {
    int version;
    int rows;
    int sum;
    int err;         /* 0 = ok、それ以外は SQLite のコード (open 失敗は -1) */
    int close_err;   /* 0 = ok */
} RcDbInfo;

static RcMark rc_mark;

/* ======================================================================== */
/*  小物 (libc を使わない)                                                   */
/* ======================================================================== */

static int rc_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* dst に src を足す。dst_cap は NUL 込み。溢れたら 0 を返す。 */
static int rc_append(char *dst, int cap, int *len, const char *src)
{
    int i = 0;
    while (src[i]) {
        if (*len + 1 >= cap) return 0;
        dst[*len] = src[i];
        (*len)++;
        i++;
    }
    dst[*len] = '\0';
    return 1;
}

static int rc_append_u32(char *dst, int cap, int *len, u32 v)
{
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    while (n > 0) {
        n--;
        if (*len + 1 >= cap) return 0;
        dst[*len] = tmp[n];
        (*len)++;
    }
    dst[*len] = '\0';
    return 1;
}

/* ======================================================================== */
/*  ファイルの複写と照合 (rename を使わない)                                  */
/* ======================================================================== */

#define RC_CP_OK        0
#define RC_CP_SRC       (-1)   /* 読み側が開けない */
#define RC_CP_DST       (-2)   /* 書き側が開けない */
#define RC_CP_READ      (-3)   /* read が負を返した */
#define RC_CP_WRITE     (-4)   /* short write / write 失敗 */

/* 現行 install の copy_file は read の負を EOF 扱いにするので使えない
 * (票 §1e の 2)。回復専用に「read の負は失敗、short write は失敗」で書く。 */
static int rc_copy_file(const RecoverOps *ops, const char *src, const char *dst)
{
    int fs, fd, n, w;

    fs = ops->open(src, KAPI_O_RDONLY);
    if (fs < 0) return RC_CP_SRC;
    fd = ops->open(dst, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) { ops->close(fs); return RC_CP_DST; }

    for (;;) {
        n = ops->read(fs, rc_buf_a, (u32)RC_BUF);
        if (n < 0) { ops->close(fs); ops->close(fd); return RC_CP_READ; }
        if (n == 0) break;
        w = ops->write(fd, rc_buf_a, (u32)n);
        if (w != n) { ops->close(fs); ops->close(fd); return RC_CP_WRITE; }
    }
    ops->close(fs);
    ops->close(fd);
    return RC_CP_OK;
}

/* バイト比較。0 = 一致、負 = 不一致 / 読めない。 */
static int rc_cmp_file(const RecoverOps *ops, const char *a, const char *b)
{
    int fa, fb, na, nb, i;

    fa = ops->open(a, KAPI_O_RDONLY);
    if (fa < 0) return -1;
    fb = ops->open(b, KAPI_O_RDONLY);
    if (fb < 0) { ops->close(fa); return -1; }

    for (;;) {
        na = ops->read(fa, rc_buf_a, (u32)RC_BUF);
        nb = ops->read(fb, rc_buf_b, (u32)RC_BUF);
        if (na < 0 || nb < 0) { ops->close(fa); ops->close(fb); return -1; }
        if (na != nb) { ops->close(fa); ops->close(fb); return -1; }
        if (na == 0) break;
        for (i = 0; i < na; i++) {
            if (rc_buf_a[i] != rc_buf_b[i]) {
                ops->close(fa);
                ops->close(fb);
                return -1;
            }
        }
    }
    ops->close(fa);
    ops->close(fb);
    return 0;
}

/* コピー + 長さ + バイト比較をまとめて。0 = 成功。 */
static int rc_clone(const RecoverOps *ops, const char *src, const char *dst)
{
    OS32_Stat sa, sb;

    if (rc_copy_file(ops, src, dst) != RC_CP_OK) return -1;
    if (ops->stat(src, &sa) != 0) return -1;
    if (ops->stat(dst, &sb) != 0) return -1;
    if (sa.st_size != sb.st_size) return -1;
    return rc_cmp_file(ops, src, dst);
}

/* ======================================================================== */
/*  印 (settings.db.recover-state) — rename せず直接書いて読み戻す            */
/* ======================================================================== */

static int rc_mark_write(const RecoverOps *ops, int phase, int orig_present,
                         int journal_present, u32 size)
{
    int len = 0, fd, n, i;

    rc_mark_buf[0] = '\0';
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len, "phase=")) return -1;
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len, rc_phase_name[phase])) return -1;
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len, " orig=")) return -1;
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len,
                   orig_present ? "present" : "missing")) return -1;
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len, " journal=")) return -1;
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len,
                   journal_present ? "present" : "absent")) return -1;
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len, " size=")) return -1;
    if (!rc_append_u32(rc_mark_buf, RC_MARK_MAX, &len, size)) return -1;
    if (!rc_append(rc_mark_buf, RC_MARK_MAX, &len, "\n")) return -1;

    fd = ops->open(rc_path[RC_N_STATE],
                   KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) return -1;
    n = ops->write(fd, rc_mark_buf, (u32)len);
    ops->close(fd);
    if (n != len) return -1;

    /* 読み戻して一致を確認する (往復 3 の B1) */
    fd = ops->open(rc_path[RC_N_STATE], KAPI_O_RDONLY);
    if (fd < 0) return -1;
    n = ops->read(fd, rc_mark_back, (u32)RC_MARK_MAX);
    ops->close(fd);
    if (n != len) return -1;
    for (i = 0; i < len; i++) {
        if (rc_mark_back[i] != rc_mark_buf[i]) return -1;
    }

    rc_mark.present = 1;
    rc_mark.phase = phase;
    rc_mark.orig_present = orig_present;
    rc_mark.journal_present = journal_present;
    rc_mark.size = size;
    return 0;
}

/* 先頭が tok なら p を進めて 1 を返す */
static int rc_eat(const char **p, const char *tok)
{
    const char *q = *p;
    while (*tok) {
        if (*q != *tok) return 0;
        q++; tok++;
    }
    *p = q;
    return 1;
}

/* 0 = 読めた、-1 = 壊れている / 読めない */
static int rc_mark_read(const RecoverOps *ops, RcMark *out)
{
    int fd, n, i;
    const char *p;
    u32 v;

    out->present = 0;
    out->phase = RC_PH_NONE;
    out->orig_present = 0;
    out->journal_present = 0;
    out->size = 0;

    fd = ops->open(rc_path[RC_N_STATE], KAPI_O_RDONLY);
    if (fd < 0) return -1;
    n = ops->read(fd, rc_mark_back, (u32)RC_MARK_MAX);
    ops->close(fd);
    /* 印は「規定の 1 行 + LF」ちょうどでなければ壊れている (往復 1 の B5)。
     * 最初の LF で打ち切ると `phase=done\ngarbage` や埋込み NUL の後ろに
     * 別の内容を持つファイルを `done` として受理してしまう。
     * n == RC_MARK_MAX は「これ以上あるかもしれない」= 1 行と示せない。 */
    if (n <= 0 || n >= RC_MARK_MAX) return -1;
    for (i = 0; i < n; i++) {
        if (rc_mark_back[i] == '\0') return -1;          /* 埋込み NUL */
        if (rc_mark_back[i] == '\n' && i != n - 1) return -1;  /* 複数行 */
    }
    if (rc_mark_back[n - 1] != '\n') return -1;          /* 末尾 LF が要る */
    rc_mark_back[n - 1] = '\0';

    p = rc_mark_back;
    if (!rc_eat(&p, "phase=")) return -1;
    out->phase = -1;
    for (i = 1; i < RC_PH_COUNT; i++) {
        const char *q = p;
        if (rc_eat(&q, rc_phase_name[i]) && *q == ' ') {
            out->phase = i;
            p = q;
            break;
        }
    }
    if (out->phase < 0) return -1;
    if (!rc_eat(&p, " orig=")) return -1;
    if (rc_eat(&p, "present")) out->orig_present = 1;
    else if (rc_eat(&p, "missing")) out->orig_present = 0;
    else return -1;
    if (!rc_eat(&p, " journal=")) return -1;
    if (rc_eat(&p, "present")) out->journal_present = 1;
    else if (rc_eat(&p, "absent")) out->journal_present = 0;
    else return -1;
    if (!rc_eat(&p, " size=")) return -1;
    if (*p < '0' || *p > '9') return -1;
    v = 0;
    while (*p >= '0' && *p <= '9') {
        /* 最終桁も見る: 429496729 * 10 + 6 は u32 を回り込む */
        if (v > 429496729u ||
            (v == 429496729u && (u32)(*p - '0') > 5u)) return -1;
        v = v * 10u + (u32)(*p - '0');
        p++;
    }
    if (*p != '\0') return -1;
    out->size = v;
    out->present = 1;
    return 0;
}

/* ======================================================================== */
/*  DB の検査 (票 §1c) — meta + 全行走査。integrity_check は OMIT。           */
/* ======================================================================== */

static const char RC_SQL_META[] =
    "SELECT COUNT(*), MIN(typeof(schema_version)), MAX(typeof(schema_version)),"
    " MIN(schema_version), MAX(schema_version),"
    " MIN(schema_version BETWEEN 1 AND 2147483647) FROM meta";
static const char RC_SQL_AGG[] =
    "SELECT COUNT(*), SUM(length(scope)+length(key)+coalesce(length(tval),0)"
    "+coalesce(length(bval),0)) FROM settings";
static const char RC_SQL_ROWS[] =
    "SELECT scope,key,type,ival,tval,bval FROM settings ORDER BY scope,key";

static DB_ColumnInfo *rc_col(const RecoverOps *ops, int col)
{
    unsigned char *shm = ops->shm();
    DB_ResultHeader *hdr;
    if (!shm) return (DB_ColumnInfo *)0;
    hdr = (DB_ResultHeader *)shm;
    if (col < 0 || col >= (int)hdr->column_count) return (DB_ColumnInfo *)0;
    return (DB_ColumnInfo *)(shm + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
}

static int rc_col_type(const RecoverOps *ops, int col)
{
    DB_ColumnInfo *info = rc_col(ops, col);
    return info ? (int)info->type : DB_TYPE_NULL;
}

static int rc_col_int(const RecoverOps *ops, int col)
{
    DB_ColumnInfo *info = rc_col(ops, col);
    if (!info || info->type != DB_TYPE_INT || info->data_offset == 0) return 0;
    return (int)(*(i32 *)(ops->shm() + info->data_offset));
}

static int rc_col_is_integer_word(const RecoverOps *ops, int col)
{
    DB_ColumnInfo *info = rc_col(ops, col);
    const char *s;
    if (!info || info->type != DB_TYPE_TEXT || info->data_offset == 0) return 0;
    s = (const char *)(ops->shm() + info->data_offset);
    return rc_streq(s, "integer");
}

/* 0 = 全部成功。out->err / out->close_err に診断を置く。 */
static int rc_db_check(const RecoverOps *ops, const char *path, RcDbInfo *out)
{
    int h, rc, count, lo, hi, in_range, rows, sum, walked;

    out->version = 0;
    out->rows = 0;
    out->sum = 0;
    out->err = 0;
    out->close_err = 0;

    h = ops->db_open_existing(path, 0);
    if (h < 0) {
        rc = ops->db_error_code(-1);
        out->err = rc ? rc : -1;
        return -1;
    }

    /* (a) meta */
    if (ops->db_prepare_only(h, RC_SQL_META) != 0) {
        rc = ops->db_error_code(h);
        out->err = rc ? rc : -1;
        goto close_out;
    }
    if (ops->db_step(h) != DB_STATUS_ROW) {
        rc = ops->db_error_code(h);
        out->err = rc ? rc : -1;
        ops->db_finalize(h);
        goto close_out;
    }
    count = rc_col_int(ops, 0);
    lo = rc_col_int(ops, 3);
    hi = rc_col_int(ops, 4);
    in_range = (rc_col_type(ops, 5) == DB_TYPE_INT) ? rc_col_int(ops, 5) : 0;
    if (count != 1 || !rc_col_is_integer_word(ops, 1) ||
        !rc_col_is_integer_word(ops, 2) || lo != hi || in_range != 1 ||
        rc_col_type(ops, 3) != DB_TYPE_INT || rc_col_type(ops, 4) != DB_TYPE_INT) {
        out->err = -1;
        ops->db_finalize(h);
        goto close_out;
    }
    ops->db_finalize(h);
    out->version = lo;

    /* (b) 件数と集計 */
    if (ops->db_prepare_only(h, RC_SQL_AGG) != 0) {
        rc = ops->db_error_code(h);
        out->err = rc ? rc : -1;
        goto close_out;
    }
    if (ops->db_step(h) != DB_STATUS_ROW) {
        rc = ops->db_error_code(h);
        out->err = rc ? rc : -1;
        ops->db_finalize(h);
        goto close_out;
    }
    if (rc_col_type(ops, 0) != DB_TYPE_INT) {
        out->err = -1;
        ops->db_finalize(h);
        goto close_out;
    }
    rows = rc_col_int(ops, 0);
    sum = (rc_col_type(ops, 1) == DB_TYPE_INT) ? rc_col_int(ops, 1) : 0;
    ops->db_finalize(h);
    out->rows = rows;
    out->sum = sum;

    /* (c) 全行走査 — 読み出せることを確かめる */
    if (ops->db_prepare_only(h, RC_SQL_ROWS) != 0) {
        rc = ops->db_error_code(h);
        out->err = rc ? rc : -1;
        goto close_out;
    }
    walked = 0;
    for (;;) {
        rc = ops->db_step(h);
        if (rc == DB_STATUS_ROW) { walked++; continue; }
        if (rc == DB_STATUS_DONE) break;
        out->err = ops->db_error_code(h);
        if (!out->err) out->err = -1;
        ops->db_finalize(h);
        goto close_out;
    }
    if (walked != rows) out->err = -1;

close_out:
    if (ops->db_close(h) != 0) {
        rc = ops->db_error_code(h);
        out->close_err = rc ? rc : -1;
    }
    return (out->err == 0 && out->close_err == 0) ? 0 : -1;
}

/* ======================================================================== */
/*  1a — 起動媒体と対象デバイスの検査                                         */
/* ======================================================================== */

static int rc_check_media(const RecoverOps *ops, const char *drive)
{
    OS32_Stat st;

    if (drive && !rc_streq(drive, RC_TARGET_DEV)) {
        ops->kprintf(ATTR_RED, "%s\n", "unsupported drive");
        return -1;
    }
    if (ops->stat("/", &st) != 0) {
        ops->kprintf(ATTR_RED, "stat failed (%s)\n", "/");
        return -1;
    }
    if (st.st_dev == 0 || RC_DEV_TYPE(st.st_dev) != RC_DEV_FD) {
        ops->kprintf(ATTR_RED, "%s\n",
                     "recover-settings must run from the install floppy");
        return -1;
    }

    if (!ops->is_mounted(RC_TARGET_MOUNT)) {
        if (ops->mount(RC_TARGET_MOUNT, RC_TARGET_DEV, RC_TARGET_FS) != 0) {
            ops->kprintf(ATTR_RED, "cannot mount %s\n", RC_TARGET_MOUNT);
            return -1;
        }
    }
    if (ops->stat(RC_TARGET_MOUNT, &st) != 0) {
        ops->kprintf(ATTR_RED, "stat failed (%s)\n", RC_TARGET_MOUNT);
        return -1;
    }
    if (st.st_dev == 0 || RC_DEV_TYPE(st.st_dev) != RC_DEV_HD ||
        RC_DEV_UNIT(st.st_dev) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "target /hd0 is not hd0");
        return -1;
    }
    if (ops->stat(RC_TARGET_ETC, &st) != 0) {
        ops->kprintf(ATTR_RED, "stat failed (%s)\n", RC_TARGET_ETC);
        return -1;
    }
    if (st.st_dev == 0 || RC_DEV_TYPE(st.st_dev) != RC_DEV_HD ||
        RC_DEV_UNIT(st.st_dev) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "target /hd0 is not hd0");
        return -1;
    }
    return 0;
}

/* ======================================================================== */
/*  1b — 9 名の走査と事前検査                                                */
/* ======================================================================== */

static int rc_scan(const RecoverOps *ops)
{
    OS32_Stat st;
    int i, j, rc;

    for (i = 0; i < RC_NAMES; i++) {
        rc_made[i] = 0;
        rc_size[i] = 0;
        rc_dev[i] = 0;
        rc_ino[i] = 0;
        rc = ops->stat(rc_path[i], &st);
        if (rc == 0) {
            rc_state[i] = RC_PRESENT;
            rc_size[i] = st.st_size;
            rc_dev[i] = st.st_dev;
            rc_ino[i] = st.st_ino;
        } else if (rc == OS32_ERR_NOTFOUND) {
            rc_state[i] = RC_ABSENT;
        } else {
            rc_state[i] = RC_UNKNOWN;
        }
    }

    for (i = 0; i < RC_NAMES; i++) {
        if (rc_state[i] == RC_UNKNOWN) {
            ops->kprintf(ATTR_RED, "stat failed (%s)\n", rc_name[i]);
            return -1;
        }
    }
    for (i = 0; i < RC_NAMES; i++) {
        if (rc_state[i] != RC_PRESENT) continue;
        if (rc_ino[i] == 0) {
            ops->kprintf(ATTR_RED,
                         "ambiguous: %s has no inode - needs manual recovery\n",
                         rc_name[i]);
            return -1;
        }
        for (j = 0; j < i; j++) {
            if (rc_state[j] != RC_PRESENT) continue;
            if (rc_dev[j] == rc_dev[i] && rc_ino[j] == rc_ino[i]) {
                ops->kprintf(ATTR_RED,
                             "ambiguous: %s and %s share an inode"
                             " - needs manual recovery\n",
                             rc_name[j], rc_name[i]);
                return -1;
            }
        }
    }
    return 0;
}

/* 印を読む。0 = 続行可、-1 = 停止。 */
static int rc_read_mark_or_stop(const RecoverOps *ops)
{
    if (rc_state[RC_N_STATE] != RC_PRESENT) {
        rc_mark.present = 0;
        rc_mark.phase = RC_PH_NONE;
        rc_mark.orig_present = 0;
        rc_mark.journal_present = 0;
        rc_mark.size = 0;
        return 0;
    }
    if (rc_mark_read(ops, &rc_mark) != 0) {
        ops->kprintf(ATTR_RED, "%s\n",
                     "recover-state unreadable - needs manual recovery");
        return -1;
    }
    return 0;
}

/* 承認前の表示 */
static void rc_show(const RecoverOps *ops, const RcDbInfo *master, int revert)
{
    if (!revert) {
        ops->kprintf(ATTR_WHITE, "master: schema_version %d, %d keys\n",
                     master->version, master->rows);
    }
    if (rc_state[RC_N_DB] == RC_PRESENT) {
        ops->kprintf(ATTR_WHITE, "settings.db: present %u B\n",
                     rc_size[RC_N_DB]);
    } else {
        ops->kprintf(ATTR_WHITE, "%s\n", "settings.db: missing");
    }
    if (rc_state[RC_N_JOURNAL] == RC_PRESENT && !revert) {
        ops->kprintf(ATTR_YELLOW, "%s\n",
                     "hot journal present - will be backed up as"
                     " .bak-journal and removed before switch");
    }
    /* 置換を予告するのは**この経路が実際に触る世代だけ**: recover は `.bak*`、
     * revert は `.failed*` (実装レビュー往復 1 の non-blocker)。 */
    if (!revert) {
        if (rc_state[RC_N_BAK] == RC_PRESENT)
            ops->kprintf(ATTR_YELLOW, "%s: will replace\n", rc_name[RC_N_BAK]);
        if (rc_state[RC_N_BAKJ] == RC_PRESENT)
            ops->kprintf(ATTR_YELLOW, "%s: will replace\n", rc_name[RC_N_BAKJ]);
    } else {
        if (rc_state[RC_N_FAILED] == RC_PRESENT)
            ops->kprintf(ATTR_YELLOW, "%s: will replace\n", rc_name[RC_N_FAILED]);
        if (rc_state[RC_N_FAILEDJ] == RC_PRESENT)
            ops->kprintf(ATTR_YELLOW, "%s: will replace\n",
                         rc_name[RC_N_FAILEDJ]);
    }
    if (rc_mark.present) {
        ops->kprintf(ATTR_WHITE, "recover-state: phase=%s orig=%s journal=%s\n",
                     rc_phase_name[rc_mark.phase],
                     rc_mark.orig_present ? "present" : "missing",
                     rc_mark.journal_present ? "present" : "absent");
    }
}

/* 1d — 承認。1 = Y、0 = それ以外。 */
static int rc_confirm(const RecoverOps *ops)
{
    int key;
    for (;;) {
        key = ops->getkey();
        if (key > 0) break;
    }
    if (key == 'Y' || key == 'y') {
        ops->kprintf(ATTR_YELLOW, "%s\n", "Y");
        return 1;
    }
    ops->kprintf(ATTR_YELLOW, "%s\n", "N");
    return 0;
}

/* 今回作ったファイルだけを消す */
static void rc_drop_made(const RecoverOps *ops)
{
    int i;
    for (i = 0; i < RC_NAMES; i++) {
        if (rc_made[i]) {
            ops->unlink(rc_path[i]);
            rc_made[i] = 0;
        }
    }
}

/* ======================================================================== */
/*  recover 本体                                                             */
/* ======================================================================== */

static int rc_recover(const RecoverOps *ops, const char *drive)
{
    RcDbInfo master, probe;
    OS32_Stat sa, sb;
    int sync_rc, sa_rc, sb_rc;

    if (rc_check_media(ops, drive) != 0) return 1;
    if (rc_scan(ops) != 0) return 1;
    if (rc_read_mark_or_stop(ops) != 0) return 1;

    /* 印の phase による門 (白リスト: done か印無し) */
    if (rc_mark.present && rc_mark.phase != RC_PH_DONE) {
        ops->kprintf(ATTR_RED,
                     "previous recovery incomplete (phase=%s)"
                     " - run install --revert-settings hd0 first\n",
                     rc_phase_name[rc_mark.phase]);
        return 1;
    }
    if (rc_state[RC_N_NEW] == RC_PRESENT) {
        ops->kprintf(ATTR_RED, "stale %s present - inspect and remove manually\n",
                     rc_name[RC_N_NEW]);
        return 1;
    }
    if (rc_state[RC_N_NEWJ] == RC_PRESENT) {
        ops->kprintf(ATTR_RED, "stale %s present - inspect and remove manually\n",
                     rc_name[RC_N_NEWJ]);
        return 1;
    }

    /* 1c — マスタの検査 */
    if (rc_db_check(ops, RC_MASTER, &master) != 0) {
        if (master.err != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "master unreadable");
            if (master.close_err != 0)
                ops->kprintf(ATTR_RED, "master close failed (%d)\n",
                             master.close_err);
            return 1;
        }
        ops->kprintf(ATTR_RED, "master close failed (%d)\n", master.close_err);
        return 1;
    }
    ops->kprintf(ATTR_WHITE, "%s\n",
                 "note: integrity_check is not available (OMIT) - verified"
                 " meta, every row and a byte-for-byte copy only");

    rc_show(ops, &master, 0);
    ops->kprintf(ATTR_YELLOW,
                 "Replace /hd0/etc/settings.db with master (schema %d, %d keys)?"
                 " Current db(+journal) will be copied to"
                 " settings.db.bak(+.bak-journal) [Y/N]\n",
                 master.version, master.rows);
    if (!rc_confirm(ops)) return 0;

    /* ---- 1e. 退避 (コピー、元は不変) ---------------------------------- */
    if (rc_state[RC_N_BAK] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_BAK]) != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "cannot remove old backup");
            return 1;
        }
        rc_state[RC_N_BAK] = RC_ABSENT;
    }
    if (rc_state[RC_N_BAKJ] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_BAKJ]) != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "cannot remove old backup");
            return 1;
        }
        rc_state[RC_N_BAKJ] = RC_ABSENT;
    }

    if (rc_state[RC_N_DB] == RC_PRESENT) {
        rc_made[RC_N_BAK] = 1;
        if (rc_clone(ops, rc_path[RC_N_DB], rc_path[RC_N_BAK]) != 0) {
            ops->kprintf(ATTR_RED, "backup failed (%s)\n", rc_name[RC_N_BAK]);
            rc_drop_made(ops);
            return 1;
        }
    }
    if (rc_state[RC_N_JOURNAL] == RC_PRESENT) {
        rc_made[RC_N_BAKJ] = 1;
        if (rc_clone(ops, rc_path[RC_N_JOURNAL], rc_path[RC_N_BAKJ]) != 0) {
            ops->kprintf(ATTR_RED, "backup failed (%s)\n", rc_name[RC_N_BAKJ]);
            rc_drop_made(ops);
            return 1;
        }
    }

    if (rc_state[RC_N_STATE] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_STATE]) != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "cannot remove old recover-state");
            rc_drop_made(ops);
            return 1;
        }
        rc_state[RC_N_STATE] = RC_ABSENT;
    }
    rc_made[RC_N_STATE] = 1;
    if (rc_mark_write(ops, RC_PH_BACKUP,
                      rc_state[RC_N_DB] == RC_PRESENT,
                      rc_state[RC_N_JOURNAL] == RC_PRESENT,
                      rc_size[RC_N_DB]) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "recover-state write failed");
        rc_drop_made(ops);
        return 1;
    }
    /* ここから先、.bak* と印は「今回作ったから消してよい」ものではない */
    rc_made[RC_N_BAK] = 0;
    rc_made[RC_N_BAKJ] = 0;
    rc_made[RC_N_STATE] = 0;

    /* ---- 1f. コピー・検証・切替 ---------------------------------------- */
    if (rc_copy_file(ops, RC_MASTER, rc_path[RC_N_NEW]) != RC_CP_OK) {
        ops->kprintf(ATTR_RED, "copy failed (%s)\n", rc_name[RC_N_NEW]);
        ops->unlink(rc_path[RC_N_NEW]);
        return 1;
    }
    if (ops->stat(RC_MASTER, &sa) != 0 ||
        ops->stat(rc_path[RC_N_NEW], &sb) != 0 || sa.st_size != sb.st_size ||
        rc_cmp_file(ops, RC_MASTER, rc_path[RC_N_NEW]) != 0) {
        ops->kprintf(ATTR_RED, "verify failed (%s)\n", rc_name[RC_N_NEW]);
        ops->unlink(rc_path[RC_N_NEW]);
        return 1;
    }
    rc_db_check(ops, rc_path[RC_N_NEW], &probe);
    /* **close の失敗が最優先** (実装レビュー往復 1 の B1)。検証も失敗していると
     * 「壊れた .new を消す」枝に落ちるが、close に失敗した接続は隔離されていて
     * (kapi_db.c の F1) その inode を掴んだままなので、消してはいけない。
     * 再起動を明示の前提にする (設計レビュー往復 3 の B4)。 */
    if (probe.close_err != 0) {
        ops->kprintf(ATTR_RED,
                     "verify close failed (%d) - settings.db.new kept;"
                     " original untouched.\n", probe.close_err);
        if (probe.err != 0 || probe.version != master.version ||
            probe.rows != master.rows || probe.sum != master.sum)
            ops->kprintf(ATTR_RED, "verify failed (%s) as well\n",
                         rc_name[RC_N_NEW]);
        ops->kprintf(ATTR_RED, "%s\n", RC_NEW_CLOSE_STEPS);
        return 1;
    }
    if (probe.err != 0 || probe.version != master.version ||
        probe.rows != master.rows || probe.sum != master.sum) {
        ops->kprintf(ATTR_RED, "verify failed (%s)\n", rc_name[RC_N_NEW]);
        ops->unlink(rc_path[RC_N_NEW]);
        return 1;
    }

    /* 破壊段の開始 = journal の除去 */
    if (rc_state[RC_N_JOURNAL] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_JOURNAL]) != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "cannot remove hot journal");
            ops->unlink(rc_path[RC_N_NEW]);
            return 1;
        }
    }

    if (rc_mark_write(ops, RC_PH_SWITCHING, rc_mark.orig_present,
                      rc_mark.journal_present, rc_mark.size) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "recover-state write failed");
        return 1;
    }
    ops->rename(rc_path[RC_N_NEW], rc_path[RC_N_DB]);

    sa_rc = ops->stat(rc_path[RC_N_DB], &sa);
    sb_rc = ops->stat(rc_path[RC_N_NEW], &sb);
    if ((sa_rc != 0 && sa_rc != OS32_ERR_NOTFOUND) ||
        (sb_rc != 0 && sb_rc != OS32_ERR_NOTFOUND)) {
        ops->kprintf(ATTR_RED, "%s\n",
                     "needs manual recovery: cannot stat after rename");
        return 1;
    }
    if (sa_rc == 0 && sb_rc == 0) {
        ops->kprintf(ATTR_RED, "%s\n",
                     "needs manual recovery: settings.db and"
                     " settings.db.new both present");
        return 1;
    }
    if (sa_rc != 0) {
        /* 置換先の unlink の後で新名追加に失敗した形 (往復 3 の B2)。
         * 旧本体が残っているとは仮定せず、写しから戻す。 */
        int failed = 0;
        if (rc_mark.orig_present &&
            rc_clone(ops, rc_path[RC_N_BAK], rc_path[RC_N_DB]) != 0) failed = 1;
        if (!failed && rc_mark.journal_present &&
            rc_clone(ops, rc_path[RC_N_BAKJ], rc_path[RC_N_JOURNAL]) != 0)
            failed = 1;
        if (!failed && sb_rc == 0) ops->unlink(rc_path[RC_N_NEW]);
        if (failed) {
            rc_mark_write(ops, RC_PH_FAILED, rc_mark.orig_present,
                          rc_mark.journal_present, rc_mark.size);
            ops->kprintf(ATTR_RED, "%s\n",
                         "restore failed - run install --revert-settings hd0"
                         " after reboot");
            return 1;
        }
        ops->kprintf(ATTR_RED, "%s\n",
                     "rename left settings.db.new only - restored from"
                     " settings.db.bak; nothing was replaced");
        rc_mark_write(ops, RC_PH_DONE, rc_mark.orig_present,
                      rc_mark.journal_present, rc_mark.size);
        return 1;
    }

    /* ---- 1g(7). 記録 ---------------------------------------------------- */
    if (rc_mark_write(ops, RC_PH_SWITCHED, rc_mark.orig_present,
                      rc_mark.journal_present, rc_mark.size) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "recover-state write failed");
        return 1;
    }
    sync_rc = ops->sync();
    rc_db_check(ops, rc_path[RC_N_DB], &probe);
    /* reopen と close は**どちらもコード**で出す。同時に失敗したときに
     * close を `failed` に省略しない (実装レビュー往復 1 の non-blocker)。 */
    {
        u8 attr = (probe.err == 0 && probe.close_err == 0) ? ATTR_GREEN
                                                           : ATTR_RED;
        ops->kprintf(attr, "recovered: schema_version %d, %d keys, sync=%d,"
                     " reopen=", probe.version, probe.rows, sync_rc);
        if (probe.err != 0) ops->kprintf(attr, "%d", probe.err);
        else                ops->kprintf(attr, "%s", "ok");
        ops->kprintf(attr, "%s", ", close=");
        if (probe.close_err != 0) ops->kprintf(attr, "%d", probe.close_err);
        else                      ops->kprintf(attr, "%s", "ok");
        ops->kprintf(attr, "%s", "\n");
    }
    if (sync_rc != 0 || probe.err != 0 || probe.close_err != 0) {
        ops->kprintf(ATTR_RED, "%s\n",
                     "REBOOT from the floppy, then:"
                     " install --revert-settings hd0");
        return 1;
    }
    if (rc_mark_write(ops, RC_PH_DONE, rc_mark.orig_present,
                      rc_mark.journal_present, rc_mark.size) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "recover-state write failed");
        ops->kprintf(ATTR_RED, "%s\n",
                     "REBOOT from the floppy, then:"
                     " install --revert-settings hd0");
        return 1;
    }
    return 0;
}

/* ======================================================================== */
/*  revert 本体 (票 §1g)                                                     */
/* ======================================================================== */

static int rc_revert_fail(const RecoverOps *ops, const char *stage)
{
    ops->kprintf(ATTR_RED, "revert failed at %s (phase stays reverting)\n",
                 stage);
    return 1;
}

static int rc_revert(const RecoverOps *ops, const char *drive)
{
    int sync_rc;

    if (rc_check_media(ops, drive) != 0) return 1;
    if (rc_scan(ops) != 0) return 1;
    /* `.new*` の残骸では止まらない (往復 2 の R2) */
    if (rc_state[RC_N_STATE] != RC_PRESENT) {
        ops->kprintf(ATTR_RED, "%s\n", "nothing to revert (no recover-state)");
        return 1;
    }
    if (rc_read_mark_or_stop(ops) != 0) return 1;

    ops->kprintf(ATTR_WHITE,
                 "current db(+journal) -> settings.db.failed(+.failed-journal),"
                 " then restore: orig=%s journal=%s\n",
                 rc_mark.orig_present ? "present" : "missing",
                 rc_mark.journal_present ? "present" : "absent");
    rc_show(ops, (const RcDbInfo *)0, 1);
    ops->kprintf(ATTR_YELLOW, "%s\n", "Revert /hd0/etc/settings.db? [Y/N]");
    if (!rc_confirm(ops)) return 0;

    /* 0. 前提検査 (往復 3 の B3) */
    if (rc_mark.orig_present && rc_state[RC_N_BAK] != RC_PRESENT) {
        ops->kprintf(ATTR_RED, "backup missing (%s) - needs manual recovery\n",
                     rc_name[RC_N_BAK]);
        return 1;
    }
    if (rc_mark.journal_present && rc_state[RC_N_BAKJ] != RC_PRESENT) {
        ops->kprintf(ATTR_RED, "backup missing (%s) - needs manual recovery\n",
                     rc_name[RC_N_BAKJ]);
        return 1;
    }

    /* 1. 旧 .failed* を消す (承認済み 1 世代) */
    if (rc_state[RC_N_FAILED] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_FAILED]) != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "cannot remove old failed copy");
            return 1;
        }
        rc_state[RC_N_FAILED] = RC_ABSENT;
    }
    if (rc_state[RC_N_FAILEDJ] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_FAILEDJ]) != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "cannot remove old failed copy");
            return 1;
        }
        rc_state[RC_N_FAILEDJ] = RC_ABSENT;
    }

    /* 1b. 破壊段より前に phase=reverting (追加往復の 1) */
    if (rc_mark_write(ops, RC_PH_REVERTING, rc_mark.orig_present,
                      rc_mark.journal_present, rc_mark.size) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "recover-state write failed");
        return 1;
    }

    /* 2. 現在の対を .failed* へ (コピー) */
    if (rc_state[RC_N_DB] == RC_PRESENT) {
        rc_made[RC_N_FAILED] = 1;
        if (rc_clone(ops, rc_path[RC_N_DB], rc_path[RC_N_FAILED]) != 0) {
            ops->kprintf(ATTR_RED, "save failed (%s)\n", rc_name[RC_N_FAILED]);
            rc_drop_made(ops);
            return rc_revert_fail(ops, rc_name[RC_N_FAILED]);
        }
    }
    if (rc_state[RC_N_JOURNAL] == RC_PRESENT) {
        rc_made[RC_N_FAILEDJ] = 1;
        if (rc_clone(ops, rc_path[RC_N_JOURNAL], rc_path[RC_N_FAILEDJ]) != 0) {
            ops->kprintf(ATTR_RED, "save failed (%s)\n", rc_name[RC_N_FAILEDJ]);
            rc_drop_made(ops);
            return rc_revert_fail(ops, rc_name[RC_N_FAILEDJ]);
        }
    }
    rc_made[RC_N_FAILED] = 0;
    rc_made[RC_N_FAILEDJ] = 0;

    /* 3. 現在の journal を消す (2 で写しがある、旧 DB に新世代を付けない) */
    if (rc_state[RC_N_JOURNAL] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_JOURNAL]) != 0) {
            ops->kprintf(ATTR_RED, "%s\n", "cannot remove current journal");
            return rc_revert_fail(ops, rc_name[RC_N_JOURNAL]);
        }
        rc_state[RC_N_JOURNAL] = RC_ABSENT;
    }

    /* 4 / 5. 復元 */
    if (rc_mark.orig_present) {
        if (rc_clone(ops, rc_path[RC_N_BAK], rc_path[RC_N_DB]) != 0) {
            ops->kprintf(ATTR_RED,
                         "restore failed at %s - current copy is in"
                         " settings.db.failed\n", rc_name[RC_N_DB]);
            return rc_revert_fail(ops, rc_name[RC_N_DB]);
        }
    } else if (rc_state[RC_N_DB] == RC_PRESENT) {
        if (ops->unlink(rc_path[RC_N_DB]) != 0) {
            ops->kprintf(ATTR_RED,
                         "restore failed at %s - current copy is in"
                         " settings.db.failed\n", rc_name[RC_N_DB]);
            return rc_revert_fail(ops, rc_name[RC_N_DB]);
        }
        rc_state[RC_N_DB] = RC_ABSENT;
    }
    if (rc_mark.journal_present) {
        if (rc_clone(ops, rc_path[RC_N_BAKJ], rc_path[RC_N_JOURNAL]) != 0) {
            /* **復元先を消さない** (実装レビュー往復 1 の B2)。ここで
             * `settings.db-journal` を unlink すると「旧 DB だけ・journal 無し」
             * という、票 §1g-4 が禁じている対の分離そのものになる。両世代の
             * 写し (`.bak-journal` と `.failed-journal`) を残して手動へ。 */
            ops->kprintf(ATTR_RED,
                         "restore failed at %s - current copy is in"
                         " settings.db.failed\n", rc_name[RC_N_JOURNAL]);
            ops->kprintf(ATTR_RED,
                         "%s is still there - restore the pair by hand\n",
                         rc_name[RC_N_BAKJ]);
            return rc_revert_fail(ops, rc_name[RC_N_JOURNAL]);
        }
    }

    /* 6. sync → 全部成功したときだけ phase=done */
    sync_rc = ops->sync();
    if (sync_rc != 0) {
        ops->kprintf(ATTR_RED, "reverted: orig=%s, sync=%d\n",
                     rc_mark.orig_present ? "present" : "missing", sync_rc);
        return rc_revert_fail(ops, "sync");
    }
    if (rc_mark_write(ops, RC_PH_DONE, rc_mark.orig_present,
                      rc_mark.journal_present, rc_mark.size) != 0) {
        ops->kprintf(ATTR_RED, "%s\n", "recover-state write failed");
        return rc_revert_fail(ops, rc_name[RC_N_STATE]);
    }
    ops->kprintf(ATTR_GREEN, "reverted: orig=%s, sync=%d\n",
                 rc_mark.orig_present ? "present" : "missing", sync_rc);
    return 0;
}

/* 入口。revert = 0 なら --recover-settings、1 なら --revert-settings。 */
static int recover_settings_main(const RecoverOps *ops, const char *drive,
                                 int revert)
{
    return revert ? rc_revert(ops, drive) : rc_recover(ops, drive);
}
