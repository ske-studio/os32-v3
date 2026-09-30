/* ======================================================================== */
/*  BOOTLOG.C — 最後の起動のログ (純粋な部分)                               */
/*                                                                          */
/*  方針と API は include/bootlog.h。ここは I/O も VFS も触らない:           */
/*  溜める・あふれを数える・書き出し用の 1 本を組む・書き出しの手順 (ops    */
/*  越し)。ホスト試験 tools/tests/bootlog_host.c がこのファイルをそのまま    */
/*  #include する。VFS と版情報へ結ぶのは kernel/bootlog_save.c。           */
/*                                                                          */
/*  「リング」と呼んでいるが、先頭を残す方針なので実体は前から詰める 1 本の  */
/*  配列で、折り返さない。本文の前に BOOTLOG_HDR_MAX、後ろに              */
/*  BOOTLOG_TAIL_MAX を空けておき、ヘッダと末尾行をそこへ置いて連続した     */
/*  1 本として書く (写し用の 2 本目を持たない)。                            */
/* ======================================================================== */

#include "os32_kapi_shared.h"   /* OS32_ERR_EXIST / OS32_ERR_NOTFOUND (types.h より先) */
#include "bootlog.h"

/* 割込み禁止区間。ホスト試験は CPL=3 で走り cli/popfl を実行できないので、
 * 錠は試験側が用意する (呼んだ回数と順序、lock が返した札を unlock が
 * そのまま受け取ること = IF の復元を見る)。カーネルは kernel/con_sink.c と
 * 同じ irq_save / irq_restore。 */
#ifdef BOOTLOG_NO_IRQ_LOCK
unsigned int bootlog_lock(void);
void bootlog_unlock(unsigned int f);
#else
#include "io.h"
static unsigned int bootlog_lock(void)       { return irq_save(); }
static void bootlog_unlock(unsigned int f)   { irq_restore(f); }
#endif

#define BL_TEXT_OFF   ((u32)BOOTLOG_HDR_MAX)
#define BL_TEXT_MAX   ((u32)SYS_BOOTLOG_TEXT_MAX)

static char g_bl_buf[BOOTLOG_HDR_MAX + SYS_BOOTLOG_TEXT_MAX + BOOTLOG_TAIL_MAX];
static u32  g_bl_len;          /* 本文のバイト数 */
static u32  g_bl_dropped;      /* あふれで捨てたバイト数 */
static int  g_bl_full;         /* 一度あふれたら 1 (以後は全部捨てる) */
static int  g_bl_stopped;      /* bootlog_stop 後は 1。BSS なので最初から積む */

/* ------------------------------------------------------------------------ */
/*  積む                                                                     */
/* ------------------------------------------------------------------------ */

/* UTF-8 の先頭バイトが示す文字の長さ (継続バイト 10xxxxxx は 0、不正は 1)。 */
static u32 bl_utf8_len(u8 c)
{
    if ((c & 0xC0u) == 0x80u) return 0;
    if ((c & 0xE0u) == 0xC0u) return 2;
    if ((c & 0xF0u) == 0xE0u) return 3;
    if ((c & 0xF8u) == 0xF0u) return 4;
    return 1;
}

/* 本文の末尾が文字の途中で終わっていたら、その文字の頭まで戻す。戻した
 * バイト数を返す。あふれた瞬間にだけ呼ぶ — 続きが来ないと分かったときだけ
 * 途中の文字を捨てる (console.c は 1 バイトずつ積むので、あふれる前の本文が
 * 文字の途中で終わるのは普通)。push をまたいだ文字 (残り 1 バイトに E3 が
 * 収まり、次の 81 82 が捨てられる) もここで揃う。 */
static u32 bl_trim_partial_tail(void)
{
    u32 cont = 0, need;
    const char *t = &g_bl_buf[BL_TEXT_OFF];

    while (cont < g_bl_len && cont < 3 &&
           ((u8)t[g_bl_len - 1 - cont] & 0xC0u) == 0x80u) cont++;
    if (cont >= g_bl_len) return 0;                 /* 頭が無い (不正) — 触らない */
    need = bl_utf8_len((u8)t[g_bl_len - 1 - cont]);
    if (need == 0 || cont + 1 >= need) return 0;    /* 完結している (か不正) */
    g_bl_len -= cont + 1;
    return cont + 1;
}

void bootlog_push(const char *buf, u32 len)
{
    unsigned int f;
    u32 room, n, i;

    if (g_bl_stopped || !buf || len == 0) return;   /* 止めた後の費用は 1 回の読み */
    f = bootlog_lock();
    if (g_bl_stopped) { bootlog_unlock(f); return; }
    if (g_bl_full) {
        g_bl_dropped += len;
        bootlog_unlock(f);
        return;
    }
    room = BL_TEXT_MAX - g_bl_len;
    n = len;
    if (n > room) {
        n = room;
        g_bl_full = 1;
        g_bl_dropped += len - n;
    }
    for (i = 0; i < n; i++) g_bl_buf[BL_TEXT_OFF + g_bl_len + i] = buf[i];
    g_bl_len += n;
    /* 入る分だけ写した後、末尾を文字の境界まで戻す (戻した分も「捨てた」) */
    if (g_bl_full) g_bl_dropped += bl_trim_partial_tail();
    bootlog_unlock(f);
}

void bootlog_stop(void)
{
    unsigned int f = bootlog_lock();
    g_bl_stopped = 1;
    bootlog_unlock(f);
}

int bootlog_is_active(void) { return !g_bl_stopped; }
u32 bootlog_len(void)       { return g_bl_len; }
u32 bootlog_dropped(void)   { return g_bl_dropped; }

/* ------------------------------------------------------------------------ */
/*  組む (kstring / kprintf を引かない — ホストでそのまま走らせるため)       */
/* ------------------------------------------------------------------------ */

/* dst[*pos] 以降へ s を足す。cap - 1 までで止める (NUL の分を残す)。 */
static void bl_put_str(char *dst, u32 cap, u32 *pos, const char *s)
{
    while (s && *s && *pos + 1 < cap) dst[(*pos)++] = *s++;
}

static void bl_put_dec(char *dst, u32 cap, u32 *pos, u32 v)
{
    char t[11];
    int n = 0;
    do { t[n++] = (char)('0' + (int)(v % 10u)); v /= 10u; } while (v && n < 10);
    while (n > 0 && *pos + 1 < cap) dst[(*pos)++] = t[--n];
}

static void bl_put_hex8(char *dst, u32 cap, u32 *pos, u32 v)
{
    static const char hx[] = "0123456789ABCDEF";
    int sh;
    for (sh = 28; sh >= 0 && *pos + 1 < cap; sh -= 4)
        dst[(*pos)++] = hx[(v >> sh) & 0xFu];
}

u32 bootlog_format_header(char *buf, u32 cap, const BootlogHeaderInfo *hi)
{
    u32 pos = 0;

    if (!buf || cap < 2) return 0;
    bl_put_str(buf, cap, &pos, "# OS32 boot log  Build ");
    bl_put_str(buf, cap, &pos, (hi && hi->build) ? hi->build : "?");
    bl_put_str(buf, cap, &pos, "  Commit ");
    bl_put_str(buf, cap, &pos, (hi && hi->commit) ? hi->commit : "?");
    bl_put_str(buf, cap, &pos, "  Image CRC ");
    if (hi && hi->crc_valid) {
        bl_put_hex8(buf, cap, &pos, hi->image_crc);
        bl_put_str(buf, cap, &pos, " (");
        bl_put_dec(buf, cap, &pos, hi->image_size);
        bl_put_str(buf, cap, &pos, " bytes)");
    } else {
        bl_put_str(buf, cap, &pos, "none");
    }
    bl_put_str(buf, cap, &pos, "  uptime-ticks ");
    bl_put_dec(buf, cap, &pos, hi ? hi->ticks : 0);
    /* 切り詰めても 1 行で終わらせる */
    if (pos + 1 >= cap) pos = cap - 2;
    buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}

const char *bootlog_compose(const char *header, u32 *out_len)
{
    unsigned int f;
    u32 hl = 0, start, end, i;

    while (header && header[hl] && hl < (u32)BOOTLOG_HDR_MAX) hl++;
    f = bootlog_lock();
    start = BL_TEXT_OFF - hl;
    for (i = 0; i < hl; i++) g_bl_buf[start + i] = header[i];

    /* 末尾行は本文の直後 (空きの BOOTLOG_TAIL_MAX に収まる)。本文が行の
     * 途中で切れていたら改行を足してから書く。 */
    end = BL_TEXT_OFF + g_bl_len;
    if (g_bl_len > 0 && g_bl_buf[end - 1] != '\n') g_bl_buf[end++] = '\n';
    {
        u32 pos = 0;
        char *t = &g_bl_buf[end];
        u32 cap = (u32)sizeof(g_bl_buf) - end;
        bl_put_str(t, cap, &pos, "# end  kept ");
        bl_put_dec(t, cap, &pos, g_bl_len);
        bl_put_str(t, cap, &pos, " bytes  dropped ");
        bl_put_dec(t, cap, &pos, g_bl_dropped);
        bl_put_str(t, cap, &pos, " bytes\n");
        end += pos;
    }
    bootlog_unlock(f);
    if (out_len) *out_len = end - start;
    return &g_bl_buf[start];
}

/* ------------------------------------------------------------------------ */
/*  書き出しの手順                                                           */
/* ------------------------------------------------------------------------ */

static int bl_streq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int bootlog_plan(const char *fstype)
{
    if (bl_streq(fstype, SYS_BOOTLOG_FS_EXT2)) return BOOTLOG_FS_EXT2;
    if (bl_streq(fstype, SYS_BOOTLOG_FS_FAT))  return BOOTLOG_FS_FAT;
    return BOOTLOG_FS_SKIP;    /* hostdrv / iso9660 / serialfs / 未マウント */
}

const char *bootlog_old_path(int kind)
{
    if (kind == BOOTLOG_FS_EXT2) return SYS_BOOTLOG_OLD;
    if (kind == BOOTLOG_FS_FAT)  return SYS_BOOTLOG_OLD_83;
    return 0;
}

const char *bootlog_stage_name(int stage)
{
    switch (stage) {
    case BOOTLOG_ST_OK:        return "ok";
    case BOOTLOG_ST_MKDIR_VAR: return "mkdir " SYS_BOOTLOG_VAR_DIR;
    case BOOTLOG_ST_MKDIR_LOG: return "mkdir " SYS_BOOTLOG_DIR;
    case BOOTLOG_ST_RM_NEW:    return "rm " SYS_BOOTLOG_NEW;
    case BOOTLOG_ST_WRITE:     return "write " SYS_BOOTLOG_NEW;
    case BOOTLOG_ST_RM_OLD:    return "rm old";
    case BOOTLOG_ST_ROTATE:    return "rotate";
    case BOOTLOG_ST_PUBLISH:   return "publish";
    case BOOTLOG_ST_SYNC:      return "sync";
    default:                   return "?";
    }
}

/* ops->write の生の戻り値が「全部書けて閉じられた」か。vfs_write は FS ごとに
 * 約束が違う: ext2 (fs/ext2_vfs.c) は成功で VFS_OK (0)、FAT (fs/fatfs_vfs.c)
 * は書いたバイト数 (f_write の bw、f_close の失敗は負)。負はどちらも失敗。
 * 種別を知っているのはここなので、ここで揃える。 */
int bootlog_write_ok(int kind, int rc, u32 len)
{
    /* 負はどちらの式でも成功にならない (len は 0xFFFFFFFF に届かない) */
    if (kind == BOOTLOG_FS_EXT2) return rc == 0;
    if (kind == BOOTLOG_FS_FAT)  return rc >= 0 && (u32)rc == len;
    return 0;
}

int bootlog_save_with(const BootlogFsOps *ops, int kind,
                      const char *data, u32 len, int *fail_rc)
{
    const char *old = bootlog_old_path(kind);
    int rc;

    if (fail_rc) *fail_rc = 0;
    if (!ops || !old) return BOOTLOG_ST_OK;     /* 書かない種別 */

    /* 1. ディレクトリ。無ければ書けないので止める */
    rc = ops->mkdir(SYS_BOOTLOG_VAR_DIR);
    if (rc != 0 && rc != OS32_ERR_EXIST) {
        if (fail_rc) *fail_rc = rc;
        return BOOTLOG_ST_MKDIR_VAR;
    }
    rc = ops->mkdir(SYS_BOOTLOG_DIR);
    if (rc != 0 && rc != OS32_ERR_EXIST) {
        if (fail_rc) *fail_rc = rc;
        return BOOTLOG_ST_MKDIR_LOG;
    }

    /* 2. 残っている boot.new を**消す** (無ければ成功)。書き込みで既存の
     *    inode を再利用しない — ext2 の rename は新名を載せてから旧名を消す
     *    (fs/ext2_dir.c) ので、前回の公開が旧名の削除で落ちていると boot.new
     *    と boot.log が同じ inode を指している。そこへ write すると boot.log
     *    まで切り詰める。消せなければ (NOTFOUND 以外) ここで止める */
    rc = ops->rm(SYS_BOOTLOG_NEW);
    if (rc != 0 && rc != OS32_ERR_NOTFOUND) {
        if (fail_rc) *fail_rc = rc;
        return BOOTLOG_ST_RM_NEW;
    }

    /* 3. 今回のログを**まず一時ファイル**へ。既存の boot.log / 前回分には
     *    まだ触らない — ここで落ちても失うものは無い。書けなかった (途中で
     *    切れた) 一時ファイルは消しておく (成否は問わない。消せなければ
     *    不完全な boot.new が残るので、残存だけでは完了を保証しない) */
    rc = ops->write(SYS_BOOTLOG_NEW, data, len);
    if (!bootlog_write_ok(kind, rc, len)) {
        if (fail_rc) *fail_rc = rc;
        (void)ops->rm(SYS_BOOTLOG_NEW);
        return BOOTLOG_ST_WRITE;
    }

    /* 4. 今の boot.log を前回分へ。rename が boot.log の有無を教える:
     *      NOTFOUND = boot.log が無い (前回の保存が途中で止まった)。**.1 を
     *                 消さない** — 唯一の旧世代を残す
     *      EXIST    = FatFs (f_rename は宛先があると断る)。.1 を消してもう一度
     *      0        = 付け替えた (ext2 の rename は宛先を置き換える)
     *    落ちたら boot.log を残して止める */
    rc = ops->rename(SYS_BOOTLOG_FILE, old);
    if (rc == OS32_ERR_EXIST) {
        rc = ops->rm(old);
        if (rc != 0 && rc != OS32_ERR_NOTFOUND) {
            if (fail_rc) *fail_rc = rc;
            return BOOTLOG_ST_RM_OLD;
        }
        rc = ops->rename(SYS_BOOTLOG_FILE, old);
    }
    if (rc != 0 && rc != OS32_ERR_NOTFOUND) {
        if (fail_rc) *fail_rc = rc;
        return BOOTLOG_ST_ROTATE;
    }

    /* 5. 今回分を公開。落ちても前回分は .1 に、今回分は boot.new に残る */
    rc = ops->rename(SYS_BOOTLOG_NEW, SYS_BOOTLOG_FILE);
    if (rc != 0) {
        if (fail_rc) *fail_rc = rc;
        return BOOTLOG_ST_PUBLISH;
    }

    /* 6. 電源断に備えて書き戻す */
    rc = ops->sync();
    if (rc != 0) {
        if (fail_rc) *fail_rc = rc;
        return BOOTLOG_ST_SYNC;
    }
    return BOOTLOG_ST_OK;
}
