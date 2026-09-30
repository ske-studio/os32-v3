/* ======================================================================== */
/*  STAT.C -- ファイル状態表示                                                */
/*                                                                          */
/*  Usage: stat PATH...                                                      */
/*  sys_stat() の結果を 1 パス 1 行で出す。種別・サイズ・st_dev (生値と復号)・ */
/*  st_ino・mode・リンク数・所有・3 つの時刻。                                */
/*                                                                          */
/*  用途: S3 のリカバリは root の st_dev を復号して FDD ブートを判定し、      */
/*  st_ino で同一性 (同じ inode を 2 名が指していないか) を見る               */
/*  (docs/archive/settings/TASK_S3.md §1a / §1b)。ゲスト上でその 2 つを       */
/*  直接観測する手段が無かったので、根拠を取るための道具として足した。        */
/*  st_dev の規則は docs/06_filesystem.md §6-1。                             */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>

/* VFS の st_dev は (dev_type << 8 | unit) + 1 (fs/vfs.c の vfs_mount_dev_of)。
 * +1 は「不明」を表す 0 と衝突させないため (hd0 = 1、fd0 = 257)。種別の正典は
 * fs/vfs.h の VFS_DEV_* だが、カーネルヘッダは外部プログラムから見えないので
 * ここへ写す ([C4] の但し書き。同じ写しが userland/system/install_recover.inc
 * の RC_DEV_* にもある — 正典は fs/vfs.h の 1 か所)。 */
#define STAT_DEV_UNKNOWN    0u
#define STAT_DEV_HD         0
#define STAT_DEV_FD         1
#define STAT_DEV_SERIAL     2
#define STAT_DEV_CD         3
#define STAT_DEV_HOSTDRV    4
#define STAT_DEV_TYPE(x)    ((int)((((x) - 1u) >> 8) & 0xFFu))
#define STAT_DEV_UNIT(x)    ((int)(((x) - 1u) & 0xFFu))

/* stat_dev_text() の出力に必要なバイト数。最長は "type255 unit255" + NUL。 */
#define STAT_DEV_TEXT_MAX   32

static KernelAPI *api;

static const char *stat_type_name(u16 mode);
static const char *stat_dev_prefix(int type);
static void stat_dev_text(u32 dev, char *out);
static const char *stat_errstr(int rc);
static int stat_one(const char *path);

int main(int argc, char **argv, KernelAPI *kapi)
{
    int i;
    int errors = 0;

    api = kapi;

    if (argc < 2) {
        printf("Usage: stat PATH...\n");
        return 1;
    }

    /* 1 つ失敗しても残りは続ける (終了コードだけ 1 にする) */
    for (i = 1; i < argc; i++) {
        errors += stat_one(argv[i]);
    }

    return errors ? 1 : 0;
}

/* 種別は st_mode の S_IFMT ビット (os32_kapi_shared.h の OS_S_IF*) で決まる */
static const char *stat_type_name(u16 mode)
{
    switch (mode & OS_S_IFMT) {
    case OS_S_IFREG: return "FILE";
    case OS_S_IFDIR: return "DIR";
    case OS_S_IFCHR: return "DEV";
    case OS_S_IFIFO: return "FIFO";   /* パイプ。パス名では出てこない */
    default:         return "UNKNOWN";
    }
}

/* 種別 → デバイス名の接頭辞。未知の種別は 0 を返す (呼び側が数字で出す) */
static const char *stat_dev_prefix(int type)
{
    switch (type) {
    case STAT_DEV_HD:      return "hd";
    case STAT_DEV_FD:      return "fd";
    case STAT_DEV_SERIAL:  return "ser";
    case STAT_DEV_CD:      return "cd";
    case STAT_DEV_HOSTDRV: return "host";
    default:               return (const char *)0;
    }
}

/* st_dev の復号。out は STAT_DEV_TEXT_MAX バイト以上。 */
static void stat_dev_text(u32 dev, char *out)
{
    const char *prefix;
    int type, unit;

    if (dev == STAT_DEV_UNKNOWN) {
        /* FS が埋めず VFS もマウントを引けなかった = 判定不能 */
        sprintf(out, "unknown");
        return;
    }

    type = STAT_DEV_TYPE(dev);
    unit = STAT_DEV_UNIT(dev);
    prefix = stat_dev_prefix(type);

    if (prefix) {
        sprintf(out, "%s%d", prefix, unit);
    } else {
        sprintf(out, "type%d unit%d", type, unit);
    }
}

/* OS32_ERR_* → メッセージ (userland/shell/cmd_fs_shared.c の fs_strerror と同文) */
static const char *stat_errstr(int rc)
{
    switch (rc) {
    case OS32_ERR_IO:       return "I/O error";
    case OS32_ERR_NOTFOUND: return "No such file or directory";
    case OS32_ERR_NOMOUNT:  return "Not mounted";
    case OS32_ERR_NOSPC:    return "No space left on device";
    case OS32_ERR_EXIST:    return "File exists";
    case OS32_ERR_NOTDIR:   return "Not a directory";
    case OS32_ERR_NOTEMPTY: return "Directory not empty";
    case OS32_ERR_ISDIR:    return "Is a directory";
    case OS32_ERR_INVAL:    return "Invalid argument";
    case OS32_ERR_NOSYS:    return "Not supported";
    default:                return "Unknown error";
    }
}

/* 戻り: 0 = 出力した / 1 = エラー行を出した */
static int stat_one(const char *path)
{
    OS32_Stat st;
    char dev[STAT_DEV_TEXT_MAX];
    int rc;

    rc = api->sys_stat(path, &st);
    if (rc != 0) {
        printf("stat: %s: %s\n", path, stat_errstr(rc));
        return 1;
    }

    stat_dev_text(st.st_dev, dev);

    printf("%s: %s size=%lu dev=%lu(%s) ino=%lu mode=0%o nlink=%u"
           " uid=%u gid=%u atime=%lu mtime=%lu ctime=%lu\n",
           path, stat_type_name(st.st_mode),
           (unsigned long)st.st_size,
           (unsigned long)st.st_dev, dev,
           (unsigned long)st.st_ino,
           (unsigned int)st.st_mode,
           (unsigned int)st.st_nlink,
           (unsigned int)st.st_uid, (unsigned int)st.st_gid,
           (unsigned long)st.st_atime,
           (unsigned long)st.st_mtime,
           (unsigned long)st.st_ctime);
    return 0;
}
