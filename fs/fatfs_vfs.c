/* ======================================================================== */
/*  FATFS_VFS.C — FatFs VFS統合ラッパー                                     */
/*                                                                          */
/*  FatFs (elm-chan.org) のAPIをOS32のVfsOpsインターフェースに接続する。      */
/*  ext2_vfs.c と同パターンのマルチインスタンス対応。                        */
/*                                                                          */
/*  mount() で FatFsCtx を kmalloc確保、umount() で kfree解放。             */
/* ======================================================================== */

#include "fatfs/ff.h"
#include "vfs.h"
#include "kmalloc.h"
#include "kstring.h"
#include "kprintf.h"
#include "ide.h"    /* ide_get_info, ide_drive_present — ジオメトリ情報取得のみ */
#include "dev.h"
#include "os_time.h"
#include "pc98pt.h"   /* PC98PartEntry (票 TASK_HDD_INSTALL 段 1-4) */
#include "bootinfo.h" /* bootinfo_part_geom (区画表の幾何、m4) */

#include "fatfs/diskio_os32.h"   /* diskio_set_* */

/* ======== FatFs VFSコンテキスト ======== */
typedef struct {
    FATFS  fatfs;     /* FatFsワークエリア */
    int    dev_id;    /* OS32デバイスID */
    BYTE   pdrv;      /* FatFs物理ドライブ番号 */
    char   vol[4];    /* ボリューム文字列 "0:" or "1:" */
} FatFsCtx;

/* 物理ドライブ占有フラグ (pdrv 0=FDD, 1=HDD)。
 * diskio.c のドライブ選択と FatFs[] ボリューム登録はグローバルなため、
 * 同一 pdrv への多重マウントは先行マウントを破壊する。ここで遮断する。 */
static int pdrv_busy[FF_VOLUMES];


/* ======== FRESULT → VFSエラー変換 ======== */
static int ff_to_vfs(FRESULT fr)
{
    switch (fr) {
    case FR_OK:             return VFS_OK;
    case FR_NO_FILE:        return VFS_ERR_NOTFOUND;
    case FR_NO_PATH:        return VFS_ERR_NOTFOUND;
    case FR_INVALID_NAME:   return VFS_ERR_INVAL;
    case FR_DENIED:         return VFS_ERR_IO;
    case FR_EXIST:          return VFS_ERR_EXIST;
    case FR_WRITE_PROTECTED: return VFS_ERR_IO;
    case FR_NOT_READY:      return VFS_ERR_IO;
    case FR_DISK_ERR:       return VFS_ERR_IO;
    case FR_INT_ERR:        return VFS_ERR_IO;
    case FR_NOT_ENABLED:    return VFS_ERR_IO;
    case FR_NO_FILESYSTEM:  return VFS_ERR_IO;
    default:                return VFS_ERR_IO;
    }
}


/* ======== FRESULT → VFSエラー変換 (存在確認の入口だけ) ========
 * stat / get_size のように「在るか」を問う入口では FR_INVALID_NAME も
 * NOTFOUND に写す。ffconf.h は FF_USE_LFN 0 なので、8.3 に収まらない名前
 * (例: "settings.db-journal") は f_stat が FR_INVALID_NAME を返すが、
 * そのボリュームに**存在しえない名前**は stat の意味では「存在しない」。
 * KAPI v50 の db_open_existing は hot journal 検査で "<db>-journal" を
 * vfs_stat し、NOTFOUND 以外を IOERR と断じるため、INVAL のままだと
 * FDD ブート (root = FAT) で SQLITE_IOERR になっていた (2026-09-13)。
 * open / read / write 系は INVAL のまま — 呼び手が不正な名前を渡した、
 * という診断をつぶさないため。 */
static int ff_stat_to_vfs(FRESULT fr)
{
    if (fr == FR_INVALID_NAME) return VFS_ERR_NOTFOUND;
    return ff_to_vfs(fr);
}


/* ======== 名前の入口検査 (実装レビュー ラリー 2 の blocker) ========
 * FatFs は '\' を区切りと読み、0x20 以下のバイトで名前を打ち切り、"DISK." を
 * "DISK" と同じ SFN にする。VFS の BUSY / pinned は 1 バイトの fold で名前を
 * 比べるので、この綴りを素通しすると使用中の実体を別の綴りで消せた
 * (vfs_rm("/fd0/disk.img ") など)。意味の変わる綴りはここで INVAL で断る。
 * 規則と Win32 (HostDrv) との差は fs/vfs_name_rules.inc。
 * FAT は**途中の空白も**断る — FatFs はそこで名前を打ち切るので、
 * "DISK.IMG ignored" も "DISK.IMG" になる (Win32 では途中の空白は正当)。 */
#include "vfs_name_rules.inc"


/* ======== パスにボリューム接頭辞を付加 ========
 * 戻り値: VFS_OK / VFS_ERR_INVAL (FatFs が意味を変える名前) /
 *         VFS_ERR_NAMETOOLONG ("0:" 付きで out に収まらない)。
 * 名前を受け取る入口は**必ず**ここを通るので、検査もここに置く。
 *
 * **切り詰めない** (実装レビュー ラリー 3、Codex B1 / Opus): FD 起動では FAT が
 * "/" にマウントされるので、FS に届く相対パスは VFS の上限 (255 バイト) まで
 * 伸びる。以前は "0:" を前に付けた後 kstrncat が末尾を黙って落としたため、
 * 255 バイトの "…/disk1.imgXY" が 253 バイトの "…/disk1.img" になり、BUSY /
 * pinned の比較 (元の 255 バイトの名前) をすり抜けて使用中の実体を消せた。
 * 収まらなければ FatFs に何も渡さずに断る。 */
static int ff_make_path(const FatFsCtx *fc, const char *path, char *out, int max)
{
    int len;
    int rc;
    u32 need;

    out[0] = '\0';
    rc = vfs_name_rule_check(path, VFS_NAME_RULE_FAT);
    if (rc != VFS_OK) return rc;
    /* 要る大きさ: "0:" + (先頭に '/' が無ければ 1) + path + NUL。
     * 空の path は "0:/" (4 バイト) */
    if (path && path[0]) {
        need = 2u + (path[0] != '/' ? 1u : 0u) + kstrlen(path) + 1u;
    } else {
        need = 4u;
    }
    if (max < 0 || need > (u32)max) return VFS_ERR_NAMETOOLONG;
    /* "0:/path..." のような FatFs パスを構築 */
    out[0] = fc->vol[0];
    out[1] = ':';
    out[2] = '\0';
    len = 2;

    if (path && path[0]) {
        if (path[0] != '/') {
            out[len++] = '/';
            out[len] = '\0';
        }
        /* kstrncat の n は「残り容量」ではなくバッファ全体サイズ
         * (strlcat セマンティクス)。max - len - 1 を渡すと数文字ぶん
         * 早く切り詰められていた */
        kstrncat(out, path, (u32)max);
    } else {
        out[len++] = '/';
        out[len] = '\0';
    }
    return VFS_OK;
}


/* ======== ディレクトリ一覧 ======== */
static int fatfs_vfs_list(void *ctx, const char *path, vfs_dir_cb cb, void *user_ctx)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    VfsDirEntry ve;
    int rc;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;

    fr = f_opendir(&dir, fpath);
    if (fr != FR_OK) return ff_to_vfs(fr);

    /* 列挙の途中で f_readdir が失敗したら、そこで打ち切って**負**を返す。
     * それまでに callback へ渡した項目は取り消さない (部分列挙 + 負の戻り)。
     * 以前は打ち切った後も VFS_OK を返していたので、FDD (FAT) の I/O 失敗が
     * vfs_ls → sys_ls → install の copy_directory に届かず、欠けたファイルの
     * まま「成功」になっていた (S3I2-K、2026-09-14)。 */
    rc = VFS_OK;
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) {
            rc = ff_to_vfs(fr);
            break;
        }
        if (fno.fname[0] == '\0') break;

        kstrncpy(ve.name, fno.fname, VFS_MAX_PATH);
        ve.type = (fno.fattrib & AM_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        ve.size = fno.fsize;
        cb(&ve, user_ctx);
    }

    f_closedir(&dir);
    return rc;
}


/* ======== ファイル読み込み (一括) ======== */
static int fatfs_vfs_read(void *ctx, const char *path, void *buf, u32 max_size)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    FIL fil;
    FRESULT fr;
    UINT br;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;

    fr = f_open(&fil, fpath, FA_READ);
    if (fr != FR_OK) return ff_to_vfs(fr);

    fr = f_read(&fil, buf, max_size, &br);
    f_close(&fil);

    if (fr != FR_OK) return ff_to_vfs(fr);
    return (int)br;
}


/* ======== ファイル書き込み (一括) ======== */
static int fatfs_vfs_write(void *ctx, const char *path, const void *data, u32 size)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    FIL fil;
    FRESULT fr, fr_close;
    UINT bw;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;

    fr = f_open(&fil, fpath, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) return ff_to_vfs(fr);

    fr = f_write(&fil, data, size, &bw);
    /* f_close は最後のセクタとディレクトリエントリ (サイズ・クラスタ鎖) を
     * 書き戻す。ここが落ちると f_write が OK でも媒体には残らないので、
     * その結果も返す (f_write の失敗が先)。以前は捨てていたので、起動ログの
     * 最後のフラッシュが落ちても「保存成功」になっていた (2026-09-25)。
     * fatfs_vfs_sync は無条件に OK を返すので、閉じる時の失敗をここで
     * 拾わないと呼び手には見えない。 */
    fr_close = f_close(&fil);

    if (fr != FR_OK) return ff_to_vfs(fr);
    if (fr_close != FR_OK) return ff_to_vfs(fr_close);
    return (int)bw;
}


/* ======== ファイル削除 ======== */
static int fatfs_vfs_unlink(void *ctx, const char *path)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;
    return ff_to_vfs(f_unlink(fpath));
}


/* ======== ディレクトリ作成 ======== */
static int fatfs_vfs_mkdir(void *ctx, const char *path)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;
    return ff_to_vfs(f_mkdir(fpath));
}


/* ======== ディレクトリ削除 ======== */
static int fatfs_vfs_rmdir(void *ctx, const char *path)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    FILINFO fno;
    FRESULT fr;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;
    /* FatFs の f_unlink はファイルも (空の) ディレクトリも消す。rmdir が
     * ファイルを消すと、VFS の pinned / BUSY を通らない経路で使用中の
     * 実体を失う (実装レビュー ラリー 3、Opus)。先に種別を見て、
     * ディレクトリ以外は NOTDIR で断る。 */
    fr = f_stat(fpath, &fno);
    if (fr != FR_OK) return ff_stat_to_vfs(fr);
    if (!(fno.fattrib & AM_DIR)) return VFS_ERR_NOTDIR;
    return ff_to_vfs(f_unlink(fpath));
}


/* ======== リネーム ======== */
static int fatfs_vfs_rename(void *ctx, const char *old_path, const char *new_path)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath_old[VFS_MAX_PATH];
    char fpath_new[VFS_MAX_PATH];

    rc_path = ff_make_path(fc, old_path, fpath_old, sizeof(fpath_old));
    if (rc_path != VFS_OK) return rc_path;
    rc_path = ff_make_path(fc, new_path, fpath_new, sizeof(fpath_new));
    if (rc_path != VFS_OK) return rc_path;
    return ff_to_vfs(f_rename(fpath_old, fpath_new));
}


/* ======== ファイルサイズ取得 ======== */
static int fatfs_vfs_get_size(void *ctx, const char *path, u32 *size)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    FILINFO fno;
    FRESULT fr;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;
    fr = f_stat(fpath, &fno);
    if (fr != FR_OK) return ff_stat_to_vfs(fr);
    *size = fno.fsize;
    return VFS_OK;
}


/* ======== ストリーム読み込み (オフセット指定) ======== */
static int fatfs_vfs_read_stream(void *ctx, const char *path, void *buf, u32 size, u32 offset)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    FIL fil;
    FRESULT fr;
    UINT br;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;

    fr = f_open(&fil, fpath, FA_READ);
    if (fr != FR_OK) return ff_to_vfs(fr);

    fr = f_lseek(&fil, (FSIZE_t)offset);
    if (fr != FR_OK) { f_close(&fil); return ff_to_vfs(fr); }

    fr = f_read(&fil, buf, size, &br);
    f_close(&fil);

    if (fr != FR_OK) return ff_to_vfs(fr);
    return (int)br;
}


/* ======== ストリーム書き込み (オフセット指定) ======== */
static int fatfs_vfs_write_stream(void *ctx, const char *path, const void *data, u32 size, u32 offset)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    FIL fil;
    FRESULT fr;
    UINT bw;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;

    fr = f_open(&fil, fpath, FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) return ff_to_vfs(fr);

    fr = f_lseek(&fil, (FSIZE_t)offset);
    if (fr != FR_OK) { f_close(&fil); return ff_to_vfs(fr); }

    fr = f_write(&fil, data, size, &bw);
    f_close(&fil);

    if (fr != FR_OK) return ff_to_vfs(fr);
    return (int)bw;
}


/* ======== stat ======== */
static int fatfs_vfs_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    int rc_path;
    char fpath[VFS_MAX_PATH];
    FILINFO fno;
    FRESULT fr;

    if (!buf) return VFS_ERR_INVAL;

    rc_path = ff_make_path(fc, path, fpath, sizeof(fpath));
    if (rc_path != VFS_OK) return rc_path;
    fr = f_stat(fpath, &fno);
    if (fr != FR_OK) return ff_stat_to_vfs(fr);

    kmemset(buf, 0, sizeof(OS32_Stat));
    buf->st_size = fno.fsize;
    /* FAT属性 → POSIXモードへの簡易変換 */
    if (fno.fattrib & AM_DIR) {
        buf->st_mode = 0040755; /* drwxr-xr-x */
    } else if (fno.fattrib & AM_RDO) {
        buf->st_mode = 0100444; /* -r--r--r-- */
    } else {
        buf->st_mode = 0100644; /* -rw-r--r-- */
    }
    buf->st_mtime = dos_time_to_epoch(fno.fdate, fno.ftime);
    buf->st_atime = buf->st_mtime;
    buf->st_ctime = buf->st_mtime;

    return VFS_OK;
}


/* ======== sync ======== */
static int fatfs_vfs_sync(void *ctx)
{
    (void)ctx;
    /* FatFsはf_close/f_syncでフラッシュ済み */
    return VFS_OK;
}


/* ======== ブロック情報 ======== */
static u32 fatfs_vfs_total_blocks(void *ctx)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    return fc->fatfs.n_fatent - 2; /* 総クラスタ数 */
}

static u32 fatfs_vfs_free_blocks(void *ctx)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    DWORD nclst;
    FATFS *fs;
    char vol[4];

    vol[0] = fc->vol[0];
    vol[1] = ':';
    vol[2] = '\0';

    if (f_getfree(vol, &nclst, &fs) != FR_OK) return 0;
    return (u32)nclst;
}

static u32 fatfs_vfs_block_size(void *ctx)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    /* FF_MAX_SS != FF_MIN_SS の場合、ssize にランタイムセクタサイズが格納される */
#if FF_MAX_SS == FF_MIN_SS
    return (u32)fc->fatfs.csize * (u32)FF_MAX_SS;
#else
    return (u32)fc->fatfs.csize * (u32)fc->fatfs.ssize;
#endif
}


/* ======== PC-98パーティション検出 ======== */

/* PC-98パーティションテーブルエントリ (32バイト) は drivers/pc98pt.h の
 * PC98PartEntry (標準配置、ext2 / ローダ / hdprep / nhd_deploy.py と共有)。
 * バイト列からは pc98pt_get で取り出す (構造体を重ねない)。 */

/* FAT系パーティションかどうかの判定 (sys_id) */
static int pc98_is_fat_sysid(u8 id)
{
    /* PC-98 DOS パーティションタイプ:
     * 0x01/0x11/0x21: FAT12
     * 0x04/0x14/0x24: FAT16 (<32MB)
     * 0x06/0x16/0x26: FAT16 (>32MB)
     * 0x0B/0x1B/0x2B: FAT32
     * 0x81/0xA1: アクティブ+FAT12
     * 上位ニブルのフラグ部分を無視してチェック */
    u8 base = id & 0x7F;
    if (base == 0x01 || base == 0x11 || base == 0x21) return 1; /* FAT12 */
    if (base == 0x04 || base == 0x14 || base == 0x24) return 1; /* FAT16 */
    if (base == 0x06 || base == 0x16 || base == 0x26) return 1; /* FAT16B */
    if (base == 0x0B || base == 0x1B || base == 0x2B) return 1; /* FAT32 */
    return 0;
}

/* CHS→LBA変換 (ジオメトリ情報使用) */
static u32 chs_to_lba(u16 cyl, u8 head, u8 sector, u16 nheads, u16 nspt)
{
    if (nheads == 0 || nspt == 0) return 0;
    return ((u32)cyl * nheads + head) * nspt + sector;
}

/* HDD のパーティションテーブルをスキャンしてFATパーティションのLBAを返す
 * 見つからなければ 0 を返す
 * phys_sec_size: IDE物理セクタサイズ (256=SASI, 512=IDE) */
static u32 pc98_find_fat_partition(int drv, u16 phys_sec_size)
{
    u8 buf[512];
    IdeInfo info;
    u16 heads, spt;
    int i;
    int pt_offset;  /* パーティションテーブルのbuf内オフセット */
    int pt_max;     /* 最大エントリ数 */
    char devname[8];
    Device *dev;

    /* ジオメトリ取得。区画表の CHS → LBA は ext2 と同じ規則 (BIOS 幾何、無ければ
     * IDENTIFY の既定) で読む — 票 TASK_HDD_INSTALL 段 1 (m4)。 */
    if (ide_get_info(drv, &info) != 0) return 0;
    if (bootinfo_part_geom(drv, &heads, &spt) < 0) return 0;

    /* Device API ポインタ取得 */
    devname[0] = 'h'; devname[1] = 'd';
    devname[2] = '0' + (char)drv; devname[3] = '\0';
    dev = dev_find(devname);
    if (!dev) return 0;

    kprintf(0x07, "[pc98pt] drv=%d geom: C=%d H=%d S=%d total=%lu phys=%d\n",
            drv, info.cylinders, heads, spt,
            (unsigned long)info.total_sectors, phys_sec_size);

    /* PC-98パーティションテーブルの読み取り。
     * SASI 256Bセクタの場合: パーティションテーブルはHDIセクタ1(256B)にあるが、
     * IDEは512B転送なのLBA 0を読むとHDI sec 0+1が結合される。
     * よってbufの後半256Bにパーティションテーブルがある。
     * IDE 512Bセクタの場合: 従来通りセクタ1を読む。 */
    if (phys_sec_size == 256) {
        /* SASI: LBA 0を読んで後半256Bからパース */
        if (dev_blk_read_lba(dev, 0, 1, buf) != 0) return 0;
        pt_offset = 256;
        pt_max = 8;  /* (512-256)/32 = 8エントリ */
    } else {
        /* IDE: セクタ1を読む */
        if (dev_blk_read_lba(dev, 1, 1, buf) != 0) return 0;
        pt_offset = 0;
        pt_max = 16;  /* 512/32 = 16エントリ */
    }

    /* パーティションエントリをスキャン */
    for (i = 0; i < pt_max; i++) {
        PC98PartEntry ent;
        PC98PartEntry *pe = &ent;
        u32 lba;

        (void)pc98pt_get(buf + pt_offset, i, &ent);

        /* 空エントリの検出: sys_idとbootableが共に0なら終了 */
        if (pe->sys_id == 0 && pe->bootable == 0) break;

        kprintf(0x07, "[pc98pt] entry %d: sys=%02x boot=%02x "
                "chs=%d/%d/%d\n",
                i, pe->sys_id, pe->bootable,
                pe->start_cyl, pe->start_head, pe->start_sector);

        /* FAT系パーティションを探す */
        if (pc98_is_fat_sysid(pe->sys_id)) {
            lba = chs_to_lba(pe->start_cyl, pe->start_head,
                             pe->start_sector, heads, spt);
            kprintf(0x07, "[pc98pt] FAT partition at LBA %lu "
                    "(C=%d H=%d S=%d)\n",
                    (unsigned long)lba, pe->start_cyl,
                    pe->start_head, pe->start_sector);
            return lba;
        }
    }

    /* PC-98形式で見つからなかった場合、IBM PC MBRも試す */
    if (dev_blk_read_lba(dev, 0, 1, buf) != 0) return 0;
    if (buf[510] == 0x55 && buf[511] == 0xAA) {
        /* 標準MBRパーティションテーブル (offset 446) */
        for (i = 0; i < 4; i++) {
            u8 *pe = buf + 446 + i * 16;
            u8 type = pe[4];
            u32 lba;
            if (type == 0) continue;
            /* FAT系: 01,04,06,0B,0C,0E,0F */
            if (type == 0x01 || type == 0x04 || type == 0x06 ||
                type == 0x0B || type == 0x0C || type == 0x0E) {
                lba = (u32)pe[8] | ((u32)pe[9] << 8) |
                      ((u32)pe[10] << 16) | ((u32)pe[11] << 24);
                kprintf(0x07, "[mbr] FAT partition at LBA %lu "
                        "(type=%02x)\n", (unsigned long)lba, type);
                return lba;
            }
        }
    }

    return 0;
}


/* ======== マウント ======== */
static void *fatfs_vfs_mount(int dev_id)
{
    FatFsCtx *fc;
    FRESULT fr;
    int dev_type;
    int drv_num;

    /* VFS からのエンコード (fs/vfs.h)。
     * VFS_DEV_HD → HDD → pdrv=1 / VFS_DEV_FD → FDD → pdrv=0 */
    dev_type = VFS_MOUNT_DEV_TYPE(dev_id);
    drv_num  = VFS_MOUNT_DEV_ID(dev_id);

    /* FDD/HDD 以外 (CD, hostdrv等) は対象外 */
    if (dev_type != VFS_DEV_HD && dev_type != VFS_DEV_FD) return (void *)0;

    /* pdrv 占有チェック: diskio のドライブ選択を書き換える前に判定する */
    if (pdrv_busy[dev_type == 1 ? 0 : 1]) return (void *)0;

    fc = (FatFsCtx *)kzalloc(sizeof(FatFsCtx));
    if (!fc) return (void *)0;

    fc->dev_id = dev_id;

    if (dev_type == 1) {
        /* FDD */
        fc->pdrv = 0;
        diskio_set_fdd_drive(drv_num);
    } else {
        /* HDD */
        fc->pdrv = 1;
        diskio_set_hdd_drive(drv_num);
    }

    fc->vol[0] = '0' + fc->pdrv;
    fc->vol[1] = ':';
    fc->vol[2] = '\0';

    /* FatFsにマウント要求 */
    fr = f_mount(&fc->fatfs, fc->vol, 1); /* 1 = 即座にマウント */

    /* マウント失敗かつHDDの場合、パーティションテーブルを解析してリトライ */
    if (fr == FR_NO_FILESYSTEM && dev_type == 0) {
        IdeInfo ide_info;
        u16 phys_sec_size = 512;
        u32 part_lba;
        Device *blkdev = (Device *)0;

        /* IDE物理セクタサイズを取得 (SASI 256B判定) */
        if (ide_get_info(drv_num, &ide_info) == 0) {
            phys_sec_size = ide_info.phys_sector_size;
        }
        diskio_set_hdd_ide_phys_size(phys_sec_size);

        /* Device API ポインタ取得 */
        {
            char dname[8];
            dname[0] = 'h'; dname[1] = 'd';
            dname[2] = '0' + (char)drv_num; dname[3] = '\0';
            blkdev = dev_find(dname);
        }

        part_lba = pc98_find_fat_partition(drv_num, phys_sec_size);
        if (part_lba > 0) {
            u8 dbg[512];
            u16 bps = 512;
            kprintf(0x07, "[fatfs] retrying with partition offset LBA=%lu "
                    "(phys=%d)\n",
                    (unsigned long)part_lba, phys_sec_size);
            /* パーティション先頭セクタを直接読んでBPS取得。
             * part_lbaはphys_sec_size単位のLBA。
             * dev_blk_read_lbaは512B返すのでBPBの先頭は含まれる。 */
            if (blkdev && dev_blk_read_lba(blkdev, (u32)part_lba, 1, dbg) == 0) {
                bps = (u16)dbg[11] | ((u16)dbg[12] << 8);
                kprintf(0x07, "[fatfs] sector %lu: jmp=%02x BPS=%d\n",
                        (unsigned long)part_lba, dbg[0], bps);
            }
            diskio_set_hdd_partition(part_lba);
            /* BPBの論理セクタサイズをdiskioに反映 (PC-98 DOSでは1024) */
            if (bps == 1024 || bps == 512) {
                diskio_set_hdd_sector_size(bps);
            }
            /* unmount してからリトライ */
            f_mount(0, fc->vol, 0);
            fr = f_mount(&fc->fatfs, fc->vol, 1);
        }
    }

    if (fr != FR_OK) {
        kprintf(0x07, "[fatfs] mount failed: type=%d drv=%d pdrv=%d err=%d\n",
                dev_type, drv_num, fc->pdrv, (int)fr);
        /* FatFs[] に登録済みの fc->fatfs を解除してから解放する
         * (解除しないと解放済み領域への参照が残る) */
        f_mount((FATFS *)0, fc->vol, 0);
        kfree(fc);
        return (void *)0;
    }

    pdrv_busy[fc->pdrv] = 1;

    kprintf(0x07, "[fatfs] mounted: type=%d drv=%d pdrv=%d FAT%s\n",
            dev_type, drv_num, fc->pdrv,
            fc->fatfs.fs_type == 1 ? "12" :
            fc->fatfs.fs_type == 2 ? "16" :
            fc->fatfs.fs_type == 3 ? "32" : "??");

    return (void *)fc;
}


/* ======== アンマウント ======== */
static void fatfs_vfs_umount(void *ctx)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;

    if (fc) {
        f_mount((FATFS *)0, fc->vol, 0); /* アンマウント */
        pdrv_busy[fc->pdrv] = 0;
        kfree(fc);
    }
}


/* ======== マウント状態確認 ======== */
static int fatfs_vfs_is_mounted(void *ctx)
{
    FatFsCtx *fc = (FatFsCtx *)ctx;
    return (fc != (void *)0) ? 1 : 0;
}


/* ======== 名前の比較規則 (Codex 実装レビュー ラリー 1 の B4) ========
 * FatFs (FF_USE_LFN = 0、SBCS の FF_CODE_PAGE) は短い名前を探すときに英小文字を
 * 大文字にし、0x80 以上は符号ページの大文字化表 (ff.c の TBL_CT437) で畳む。
 * VFS の BUSY / pinned の名前比較を**同じ規則**にするための 1 バイト版。
 * 表は ff.c の TBL_CT437 の写し (一致は tools/tests/test_vfs_fd_path.py の
 * fatfold 段が ff.c の本文と突き合わせる)。符号ページを変えたらここも替える。 */
#if FF_CODE_PAGE != 437 || FF_USE_LFN != 0
#error "fat_name_fold: FatFs の名前規則 (CP437・LFN なし) と表を合わせ直すこと"
#endif
static const u8 fat_upper_ext[128] = {
    0x80, 0x9A, 0x45, 0x41, 0x8E, 0x41, 0x8F, 0x80,
    0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x8E, 0x8F,
    0x90, 0x92, 0x92, 0x4F, 0x99, 0x4F, 0x55, 0x55,
    0x59, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0x41, 0x49, 0x4F, 0x55, 0xA5, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

static u8 fat_name_fold(u8 c)
{
    if (c >= 'a' && c <= 'z') return (u8)(c - 'a' + 'A');
    if (c >= 0x80) return fat_upper_ext[c - 0x80];
    return c;
}

/* ======== VfsOps テーブル ======== */
static VfsOps fatfs_ops = {
    "fat",
    fatfs_vfs_mount,       fatfs_vfs_umount,      fatfs_vfs_is_mounted,
    fatfs_vfs_list,        fatfs_vfs_mkdir,        fatfs_vfs_rmdir,
    fatfs_vfs_read,        fatfs_vfs_write,        fatfs_vfs_unlink,
    fatfs_vfs_rename,
    fatfs_vfs_get_size,    fatfs_vfs_read_stream,  fatfs_vfs_write_stream,
    fatfs_vfs_sync,
    fatfs_vfs_total_blocks, fatfs_vfs_free_blocks, fatfs_vfs_block_size,
    fatfs_vfs_stat,
    /* set_mtime は持たない (票 H3)。vfs_set_mtime が OS32_ERR_NOSYS を
     * 返す = 失敗ではなく「この FS には無い」。**明示的に 0 を置く** —
     * -Wmissing-field-initializers が「書き忘れ」と区別できないため。 */
    0,
    /* create_excl (O_EXCL) も持たない (票 H2 §2-1)。**ホスト側が同時に書ける
     * FS では排他性が成り立たない**ので、持てないものは持たないと言う。
     * vfs_open が O_EXCL に OS32_ERR_NOSYS を返し、呼び手 (hsync) は
     * 直接上書きへ落ちずに replace_unsupported で断る。 */
    0,
    /* inode で動く口も持たない (票 TASK_VFS_FD_PATH)。FD はパスで動く */
    0,
    /* 大文字小文字を区別しない (FatFs の短い名前の規則) */
    fat_name_fold
};


/* ======== 初期化・登録 ======== */
void fatfs_init(void)
{
    vfs_register_fs(&fatfs_ops);
}
