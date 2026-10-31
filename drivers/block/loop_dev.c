/* ======================================================================== */
/*  LOOP_DEV.C — 汎用ループバックブロックデバイス                            */
/*                                                                          */
/*  ディスクイメージファイルを lo0..lo3 ブロックデバイスとして公開する。      */
/*  拡張子でフォーマットを自動判別:                                          */
/*    .d88/.d77/.88d → D88 (トラックテーブル + セクタヘッダ走査)            */
/*    .fdi           → FDI (4096B ヘッダ + RAW データ, FDD用)               */
/*    .hdi           → HDI (4096B ヘッダ + RAW データ, HDD用)               */
/*    .nhd           → NHD (T98-Next r0, 512B ヘッダ + RAW データ, HDD用)   */
/*    .img/.bin       → RAW (ヘッダなし, 既知容量テーブルで判定)            */
/*    その他         → 将来: ISO 等                                        */
/*                                                                          */
/*  D88 部分の実装根拠: docs/D88_FORMAT_SPEC.md                             */
/*  FDI 部分の実装根拠: Anex86 FDI header specification                     */
/* ======================================================================== */

#include "loop_dev.h"
#include "../base/dev.h"
#include "vfs.h"
#include "kprintf.h"
#include "le_internal.h"

#define LOOP_FDD_SPT_MAX 64
#define LOOP_HEADER_BYTES_MAX 65536
#define LOOP_SECTOR_BYTES_MAX 4096
#define LOOP_HDD_CYL_MAX U16_MAX
#define LOOP_BIOS_HD_BPS 1024
#define LOOP_BIOS_DD_BPS 512
#define LOOP_BIOS_144_SPT 18
#define D88_SH_R_OFF 2
#define D88_SH_N_OFF 3
#define D88_SH_SPT_OFF 4
#define D88_SH_DATA_LEN_OFF 14
#define D88_FIXED_HDR_BYTES 32
#define D88_OFF_MEDIA 0x1B
#define D88_OFF_SIZE 0x1C
#define D88_OFF_TRACKS 0x20
#define D88_TRACK_ENTRY_BYTES 4
#define D88_SECTOR_N_MAX 8
#define D88_SECTOR_BASE_BYTES 128u
#define FDI_OFF_HDRSIZE 8
#define FDI_OFF_DATASIZE 12
#define FDI_OFF_BPS 16
#define FDI_OFF_SPT 20
#define FDI_OFF_HEADS 24
#define FDI_OFF_CYLS 28

/* ======== 共通定数 ======== */
#define LOOP_MAX_SLOTS    4

/* フォーマット種別 (loop_dev.h で公開定義済み) */

/* ======== D88 定数 (D88_FORMAT_SPEC.md §2, §3) ======== */
#define D88_HDR_SIZE      0x2B0   /* 688 bytes */
#define D88_MAX_TRACKS    164
#define D88_SECT_HDR_SIZE 16

#define D88_MEDIA_2D   0x00
#define D88_MEDIA_2DD  0x10
#define D88_MEDIA_2HD  0x20
#define D88_MEDIA_1D   0x30
#define D88_MEDIA_1DD  0x40

/* ======== FDI 定数 ======== */
#define FDI_HDR_SIZE      4096
#define FDI_HDR_FIELDS    32    /* 先頭 32B にジオメトリ情報 */

/* ======== NHD 定数 (docs/NHD_FORMAT.md) ======== */
#define NHD_HDR_SIZE      512
#define NHD_SIG           "T98HDDIMAGE.R0"   /* sig[16] 先頭 14 文字 + NUL */
#define NHD_OFF_HDRSIZE   0x110   /* LE32 ヘッダサイズ (=512) */
#define NHD_OFF_CYLS      0x114   /* LE32 シリンダ数 */
#define NHD_OFF_HEADS     0x118   /* LE16 ヘッド数 */
#define NHD_OFF_SPT       0x11A   /* LE16 セクタ/トラック */
#define NHD_OFF_BPS       0x11C   /* LE16 セクタ長 */

/* ======== 内部構造体 ======== */
typedef struct {
    int      in_use;
    int      fd;
    int      owns_fd;          /* 1=detach時にclose, 0=呼び出し側が管理 */
    int      writable;         /* 1=O_RDWR で開けた (loop_dev_attach 経由のみ) */
    u8       fmt;              /* LOOP_FMT_D88 / LOOP_FMT_FDI / LOOP_FMT_HDI */
    u32      file_size;
    u32      data_offset;      /* RAW データ開始位置 (FDI:4096, D88:未使用) */

    /* D88 専用フィールド */
    u8       media;
    u32      track_offset[D88_MAX_TRACKS];

    /* 共通ジオメトリ */
    u16      cyls;
    u8       heads;
    u8       spt;
    u16      lba_sect_size;
    u32      total_lba;

    Device   dev;
} LoopSlot;

static LoopSlot loop_slots[LOOP_MAX_SLOTS];
static const char *slot_names[LOOP_MAX_SLOTS] = {
    "lo0", "lo1", "lo2", "lo3"
};

/* ======== ゼロ埋めヘルパ ======== */
static void zero_fill(u8 *dst, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++)
        dst[i] = 0;
}

/* ======================================================================== */
/*  拡張子判定ヘルパ                                                        */
/* ======================================================================== */
static int str_ends_with(const char *s, const char *suffix)
{
    int slen, xlen, i;
    const char *p;
    slen = 0;
    for (p = s; *p; p++) slen++;
    xlen = 0;
    for (p = suffix; *p; p++) xlen++;
    if (xlen > slen) return 0;
    for (i = 0; i < xlen; i++) {
        char a = s[slen - xlen + i];
        char b = suffix[i];
        /* 大文字小文字無視 */
        if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
        if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
        if (a != b) return 0;
    }
    return 1;
}

static int detect_format(const char *path)
{
    if (str_ends_with(path, ".d88") ||
        str_ends_with(path, ".d77") ||
        str_ends_with(path, ".88d"))
        return LOOP_FMT_D88;
    if (str_ends_with(path, ".fdi"))
        return LOOP_FMT_FDI;
    if (str_ends_with(path, ".hdi"))
        return LOOP_FMT_HDI;
    if (str_ends_with(path, ".nhd"))
        return LOOP_FMT_NHD;
    if (str_ends_with(path, ".img") ||
        str_ends_with(path, ".bin"))
        return LOOP_FMT_RAW;
    return LOOP_FMT_NONE;
}

#include "loop_dev_block_io.inc"

#include "loop_dev_format.inc"

#include "loop_dev_chs_io.inc"

/* ======================================================================== */
/*  公開 API                                                                */
/* ======================================================================== */

void loop_dev_init(void)
{
    int i;
    for (i = 0; i < LOOP_MAX_SLOTS; i++) {
        LoopSlot *s = &loop_slots[i];
        s->in_use       = 0;
        s->fd           = -1;
        s->owns_fd      = 0;
        s->writable     = 0;
        s->fmt          = LOOP_FMT_NONE;
        s->dev.name     = slot_names[i];
        s->dev.type     = DEV_BLOCK;
        s->dev.bus_type = DEV_BUS_LOOP;
        s->dev.bus_id   = (u8)i;
        s->dev.sect_size   = 0;
        s->dev.total_sects = 0;
        s->dev.blk_read    = 0;
        s->dev.blk_write   = 0;
        s->dev.chr_read    = 0;
        s->dev.chr_write   = 0;
        s->dev.ioctl       = 0;
        s->dev.priv        = (void *)s;
        s->dev.blk_read_chs  = 0;
        s->dev.blk_write_chs = 0;
        s->dev.cyls  = 0;
        s->dev.heads = 0;
        s->dev.spt   = 0;
        dev_register(&s->dev);
    }
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_attach_fd — fd ベースのアタッチ (拡張子判定済み fmt を受け取る)     */
/*                                                                          */
/*  拡張子で判定した fmt に応じて attach_d88 / attach_fdi / attach_hdi を     */
/*  呼び出す。fd の所有権は呼び出し側に残る (detach 時に close しない)。    */
/* ------------------------------------------------------------------------ */
int loop_dev_attach_fd(int fd, int slot, int fmt)
{
    LoopSlot *s;
    u32 fsize;
    int ret;
    const char *fmt_name;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS || fd < 0)
        return -1;

    s = &loop_slots[slot];
    if (s->in_use)
        return LOOP_ERR_BUSY;

    fsize = vfs_get_size(fd);

    /* フォーマット別アタッチ処理 */
    switch (fmt) {
    case LOOP_FMT_D88:
        ret = attach_d88(s, fd, fsize);
        fmt_name = "D88";
        break;
    case LOOP_FMT_FDI:
        ret = attach_fdi(s, fd, fsize);
        fmt_name = "FDI";
        break;
    case LOOP_FMT_HDI:
        ret = attach_hdi(s, fd, fsize);
        fmt_name = "HDI";
        break;
    case LOOP_FMT_NHD:
        ret = attach_nhd(s, fd, fsize);
        fmt_name = "NHD";
        break;
    case LOOP_FMT_RAW:
        ret = attach_raw(s, fsize);
        fmt_name = "RAW";
        break;
    default:
        return LOOP_ERR_FORMAT;
    }

    if (ret != 0)
        return ret;

    /* デバイス活性化 */
    s->fd = fd;
    s->in_use = 1;
    /* 使用中のイメージの置き換え・削除を VFS に断らせる (票 TASK_VFS_FD_PATH
     * 方針 v3 の 6)。幾何や D88 索引を持つので、開き直しでは続けられない */
    (void)vfs_fd_set_pinned(fd, 1);
    s->owns_fd = 0;  /* fd は呼び出し側が管理 */
    s->dev.sect_size   = (int)s->lba_sect_size;
    s->dev.total_sects = s->total_lba;
    /* CHS フィールド */
    s->dev.blk_read_chs  = loop_blk_read_chs;
    s->dev.blk_write_chs = 0;
    s->dev.cyls  = (u16)s->cyls;
    s->dev.heads = (u16)s->heads;
    s->dev.spt   = (u16)s->spt;

    kprintf(KLOG_LEGACY_GREEN,
        "[LOOP] lo%d: %s C=%d H=%d SPT=%d BPS=%d LBA=%u\n",
        slot, fmt_name,
        (int)s->cyls, (int)s->heads, (int)s->spt,
        (int)s->lba_sect_size, s->total_lba);

    return 0;
}

/* パスベースのアタッチ (既存 API 互換) */
int loop_dev_attach(const char *vfs_path, int slot)
{
    LoopSlot *s;
    int fd, fmt, ret;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS || !vfs_path)
        return -1;

    s = &loop_slots[slot];
    if (s->in_use)
        return LOOP_ERR_BUSY;

    /* 拡張子でフォーマット判別 (高速パス) */
    fmt = detect_format(vfs_path);
    if (fmt == LOOP_FMT_NONE)
        return LOOP_ERR_FORMAT;

    /* ファイルオープン。ゲストの WRITE (INT 1Bh AH=05h) を通すため
     * まず O_RDWR を試み、開けないファイルシステムでは O_RDONLY に
     * フォールバックして読み取り専用フラグを立てる。
     * (vfs_fd.c は O_RDONLY fd への write を拒否する — 「成功と答えて
     *  書いていない」状態を作らないため、可否はここで確定させる) */
    fd = vfs_open(vfs_path, O_RDWR);
    if (fd >= 0) {
        s->writable = 1;
    } else {
        fd = vfs_open(vfs_path, 0);
        if (fd < 0)
            return -1;
        s->writable = 0;
    }

    /* fd ベースのアタッチに委譲 */
    ret = loop_dev_attach_fd(fd, slot, fmt);
    if (ret != 0) {
        vfs_close(fd);
        s->writable = 0;
        return ret;
    }

    /* パス経由の場合は detach 時に close */
    s->owns_fd = 1;
    return 0;
}

void loop_dev_detach(int slot)
{
    LoopSlot *s;
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return;

    s = &loop_slots[slot];
    if (!s->in_use) return;

    /* 使用中の印を外す (attach_fd 経由の FD は呼び出し側に残る) */
    if (s->fd >= 0) (void)vfs_fd_set_pinned(s->fd, 0);
    /* owns_fd の場合のみ close (attach_fd 経由では呼び出し側が管理) */
    if (s->fd >= 0 && s->owns_fd)
        vfs_close(s->fd);

    s->fd      = -1;
    s->in_use  = 0;
    s->owns_fd = 0;
    s->writable = 0;
    s->fmt     = LOOP_FMT_NONE;
    s->dev.blk_read    = 0;
    s->dev.blk_write   = 0;
    s->dev.sect_size   = 0;
    s->dev.total_sects = 0;
    s->dev.blk_read_chs  = 0;
    s->dev.blk_write_chs = 0;
    s->dev.cyls  = 0;
    s->dev.heads = 0;
    s->dev.spt   = 0;
}

/* ------------------------------------------------------------------------ */
/*  LBA API — FDI/HDI/NHD/RAW 用 (D88 は非対応)                             */
/*                                                                          */
/*  SASI/IDE BIOS の LBA 線形アクセス用。範囲チェックは呼び出し側           */
/*  (sasi_pos) にもあるが、ここでも二重に持つ                               */
/*  (08_dos5.md §2「範囲外要求に無関係なセクタを返して成功」の再発防止)。   */
/* ------------------------------------------------------------------------ */
static LoopSlot *lba_slot_ok(int slot, u32 lba, u32 count)
{
    LoopSlot *s;
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;
    s = &loop_slots[slot];
    if (!s->in_use) return 0;
    if (count == 0) return 0;
    if (lba >= s->total_lba) return 0;
    if (count > s->total_lba - lba) return 0;
    return s;
}

int loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf)
{
    LoopSlot *s;
    u32 offset, total_bytes;
    int rd;

    s = lba_slot_ok(slot, lba, count);
    if (!s) return -1;
    if (s->fmt == LOOP_FMT_D88) return LOOP_ERR_UNSUPPORTED;

    offset = s->data_offset + lba * (u32)s->lba_sect_size;
    total_bytes = count * (u32)s->lba_sect_size;

    if (vfs_seek(s->fd, (int)offset, 0) < 0) return -1;
    rd = vfs_read_fd(s->fd, buf, total_bytes);
    if (rd < (int)total_bytes) return -1;
    return 0;
}

int loop_dev_write_lba(int slot, u32 lba, u32 count, const void *buf)
{
    LoopSlot *s;
    u32 offset, total_bytes;
    int wr;

    s = lba_slot_ok(slot, lba, count);
    if (!s) return -1;
    if (s->fmt == LOOP_FMT_D88) return LOOP_ERR_UNSUPPORTED;
    if (!s->writable) return LOOP_ERR_WRITE_PROTECTED;

    offset = s->data_offset + lba * (u32)s->lba_sect_size;
    total_bytes = count * (u32)s->lba_sect_size;

    if (vfs_seek(s->fd, (int)offset, 0) < 0) return -1;
    wr = vfs_write_fd(s->fd, buf, total_bytes);
    if (wr < (int)total_bytes) return -1;
    return 0;
}

int loop_dev_is_writable(int slot)
{
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;
    if (!loop_slots[slot].in_use) return 0;
    return loop_slots[slot].writable;
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_get_fd — スロットの VFS fd を取得                              */
/* ------------------------------------------------------------------------ */
int loop_dev_get_fd(int slot)
{
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    if (!loop_slots[slot].in_use) return -1;
    return loop_slots[slot].fd;
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_get_geometry — ジオメトリ取得                                  */
/* ------------------------------------------------------------------------ */
int loop_dev_get_geometry(int slot, u16 *cyls, u8 *heads, u8 *spt,
                          u16 *bps, u32 *total_lba)
{
    LoopSlot *s;
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    s = &loop_slots[slot];
    if (!s->in_use) return -1;
    if (cyls)      *cyls = s->cyls;
    if (heads)     *heads = s->heads;
    if (spt)       *spt = s->spt;
    if (bps)       *bps = s->lba_sect_size;
    if (total_lba) *total_lba = s->total_lba;
    return 0;
}

int loop_dev_status(int slot, u32 *out_total, int *out_bps)
{
    LoopSlot *s;
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;

    s = &loop_slots[slot];
    if (!s->in_use) return 0;

    if (out_total) *out_total = s->total_lba;
    if (out_bps)   *out_bps   = (int)s->lba_sect_size;
    return 1;
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_get_format — フォーマット種別取得                                */
/* ------------------------------------------------------------------------ */
int loop_dev_get_format(int slot)
{
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return LOOP_FMT_NONE;
    if (!loop_slots[slot].in_use) return LOOP_FMT_NONE;
    return (int)loop_slots[slot].fmt;
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_is_hdd — HDD イメージ判定                                        */
/* ------------------------------------------------------------------------ */
int loop_dev_is_hdd(int slot)
{
    u8 fmt;
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;
    fmt = loop_slots[slot].fmt;
    return (fmt == LOOP_FMT_HDI || fmt == LOOP_FMT_NHD) ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_get_media — メディア種別取得 (DA/UA 上位ニブル)                 */
/* ------------------------------------------------------------------------ */
int loop_dev_get_media(int slot)
{
    LoopSlot *s;
    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;
    s = &loop_slots[slot];
    if (!s->in_use) return 0;

    if (s->fmt == LOOP_FMT_HDI || s->fmt == LOOP_FMT_NHD)
        return LOOP_MEDIA_HDD;

    /* FDD: セクタサイズで判定 */
    if (s->lba_sect_size == LOOP_BIOS_HD_BPS)
        return LOOP_MEDIA_2HD_1232;
    if (s->lba_sect_size == LOOP_BIOS_DD_BPS) {
        if (s->spt >= LOOP_BIOS_144_SPT)
            return LOOP_MEDIA_2HD_144;
        return LOOP_MEDIA_2DD;
    }
    return LOOP_MEDIA_2HD_1232;  /* デフォルト */
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_get_sec_n — セクタ長コード N (log2(bps/128))                    */
/* ------------------------------------------------------------------------ */
u8 loop_dev_get_sec_n(int slot)
{
    LoopSlot *s;
    u16 tmp;
    u8 n;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;
    s = &loop_slots[slot];
    if (!s->in_use) return 0;

    tmp = s->lba_sect_size;
    n = 0;
    while (tmp > D88_SECTOR_BASE_BYTES && n < D88_SECTOR_N_MAX) { tmp >>= 1; n++; }
    return n;
}
