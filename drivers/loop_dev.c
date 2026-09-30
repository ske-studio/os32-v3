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
#include "dev.h"
#include "vfs.h"
#include "kprintf.h"

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

/* ======== LE ヘルパ ======== */
static u16 le16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8)
         | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

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
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
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

/* ======================================================================== */
/*  loop_blk_read_chs — Device 経由の CHS 読み出し                         */
/* ======================================================================== */
static int loop_blk_read_chs(Device *self, u16 cyl, u8 head, u8 sect,
                              void *buf)
{
    LoopSlot *s = (LoopSlot *)self->priv;
    int slot = (int)(s - loop_slots);
    return loop_dev_read_chs(slot, cyl, head, sect, buf);
}

/* ======================================================================== */
/*  raw_blk_read — RAW データ用 blk_read (FDI/NHD/HDI/ISO 等共通)          */
/*                                                                          */
/*  offset = data_offset + lba * lba_sect_size                              */
/* ======================================================================== */
static int raw_blk_read(Device *self, int lba, int count, void *buf)
{
    LoopSlot *s;
    u32 offset, total_bytes;
    int rd;

    s = (LoopSlot *)self->priv;
    if (!s || !s->in_use) return -1;
    if (lba < 0 || (u32)lba >= s->total_lba) return -1;
    if (count <= 0) return -1;
    if ((u32)(lba + count) > s->total_lba) return -1;

    offset = s->data_offset + (u32)lba * (u32)s->lba_sect_size;
    total_bytes = (u32)count * (u32)s->lba_sect_size;

    vfs_seek(s->fd, (int)offset, 0);
    rd = vfs_read_fd(s->fd, buf, total_bytes);
    if (rd < (int)total_bytes) {
        /* 読み切れなかった分はゼロ埋め */
        if (rd > 0)
            zero_fill((u8 *)buf + rd, total_bytes - (u32)rd);
        else
            zero_fill((u8 *)buf, total_bytes);
    }

    return 0;
}

/* ======================================================================== */
/*  d88_blk_read — D88仕様書準拠セクタ読み出し                              */
/*                                                                          */
/*  LBA → CHS 変換 → トラックオフセット参照 → セクタヘッダ走査            */
/*  → C/H/R 厳密一致 → データ読み出し                                      */
/* ======================================================================== */
static int d88_blk_read(Device *self, int lba, int count, void *buf)
{
    LoopSlot *s;
    u8 *dst;
    int sec_i;

    s = (LoopSlot *)self->priv;
    if (!s || !s->in_use) return -1;
    if (lba < 0 || (u32)lba >= s->total_lba) return -1;
    if (count <= 0) return -1;
    if ((u32)(lba + count) > s->total_lba) return -1;

    dst = (u8 *)buf;

    for (sec_i = 0; sec_i < count; sec_i++) {
        u32 cur_lba, track, cyl, head, r_1based;
        u32 track_idx, trk_off, pos;
        u8  shdr[D88_SECT_HDR_SIZE];
        u16 track_spt, data_len;
        int found, si, rd;
        u32 to_read, pad;

        cur_lba = (u32)lba + (u32)sec_i;

        /* LBA → CHS 変換 */
        track    = cur_lba / (u32)s->spt;
        cyl      = track / (u32)s->heads;
        head     = track % (u32)s->heads;
        r_1based = (cur_lba % (u32)s->spt) + 1;

        track_idx = cyl * (u32)s->heads + head;
        if (track_idx >= D88_MAX_TRACKS) return -1;

        trk_off = s->track_offset[track_idx];

        /* 不在トラック → エラー (実FDC準拠) */
        if (trk_off == 0 || trk_off >= s->file_size) {
            return -1;
        }

        /* トラック先頭セクタヘッダから SPT 取得 */
        vfs_seek(s->fd, (int)trk_off, 0);
        rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
        if (rd < D88_SECT_HDR_SIZE) return -1;
        track_spt = le16(shdr + 4);

        if (track_spt == 0 || track_spt > 64) return -1;

        /* セクタヘッダ走査: C/H/R 厳密一致 */
        found = 0;
        pos = trk_off;
        for (si = 0; si < (int)track_spt; si++) {
            vfs_seek(s->fd, (int)pos, 0);
            rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
            if (rd < D88_SECT_HDR_SIZE) return -1;

            data_len = le16(shdr + 14);

            if (data_len == 0) {
                pos += D88_SECT_HDR_SIZE;
                continue;
            }

            if (shdr[0] == (u8)cyl &&
                shdr[1] == (u8)head &&
                shdr[2] == (u8)r_1based) {
                to_read = (u32)data_len;
                if (to_read > (u32)s->lba_sect_size)
                    to_read = (u32)s->lba_sect_size;

                rd = vfs_read_fd(s->fd, dst, to_read);
                if (rd < (int)to_read) return -1;

                pad = (u32)s->lba_sect_size - to_read;
                if (pad > 0)
                    zero_fill(dst + to_read, pad);

                found = 1;
                break;
            }

            pos += D88_SECT_HDR_SIZE + (u32)data_len;
        }

        /* 未発見セクタ → エラー (実FDC準拠: No Data) */
        if (!found)
            return -1;

        dst += s->lba_sect_size;
    }

    return 0;
}

/* ======================================================================== */
/*  attach_d88 — D88 イメージのアタッチ処理                                 */
/* ======================================================================== */
static int attach_d88(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr_small[32];
    u8  tbl_buf[D88_MAX_TRACKS * 4];
    u8  sec0[D88_SECT_HDR_SIZE];
    int i, rd;

    if (fsize < D88_HDR_SIZE) return -2;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr_small, 32);
    if (rd < 32) return -4;

    s->media     = hdr_small[0x1B];
    s->file_size = le32(hdr_small + 0x1C);
    if (s->file_size > fsize)
        s->file_size = fsize;

    /* トラックオフセットテーブル */
    vfs_seek(fd, 0x20, 0);
    rd = vfs_read_fd(fd, tbl_buf, D88_MAX_TRACKS * 4);
    if (rd < (int)(D88_MAX_TRACKS * 4)) return -4;
    for (i = 0; i < D88_MAX_TRACKS; i++)
        s->track_offset[i] = le32(tbl_buf + i * 4);

    /* media → cyls/heads */
    switch (s->media) {
    case D88_MEDIA_2D:   s->cyls = 40; s->heads = 2; break;
    case D88_MEDIA_2DD:  s->cyls = 80; s->heads = 2; break;
    case D88_MEDIA_2HD:  s->cyls = 77; s->heads = 2; break;
    case D88_MEDIA_1D:   s->cyls = 40; s->heads = 1; break;
    case D88_MEDIA_1DD:  s->cyls = 80; s->heads = 1; break;
    default: return -2;
    }

    /* トラック0のセクタヘッダから N/SPT を取得 */
    if (s->track_offset[0] == 0 || s->track_offset[0] >= s->file_size)
        return -2;
    vfs_seek(fd, (int)s->track_offset[0], 0);
    rd = vfs_read_fd(fd, sec0, D88_SECT_HDR_SIZE);
    if (rd < D88_SECT_HDR_SIZE) return -4;

    {
        u8  n_val   = sec0[3];
        u16 hdr_spt = le16(sec0 + 4);
        if (hdr_spt == 0 || hdr_spt > 64 || n_val > 8)
            return -2;
        s->spt          = (u8)hdr_spt;
        s->lba_sect_size = (u16)(128u << n_val);
    }

    s->total_lba = (u32)s->cyls * (u32)s->heads * (u32)s->spt;
    s->fmt = LOOP_FMT_D88;
    s->dev.blk_read = d88_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_fdi — FDI イメージのアタッチ処理                                 */
/* ======================================================================== */
static int attach_fdi(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr[FDI_HDR_FIELDS];
    u32 hdr_size, fdd_size, sect_size, spt, surfaces, cyls;
    int rd;

    if (fsize < FDI_HDR_SIZE) return -2;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr, FDI_HDR_FIELDS);
    if (rd < FDI_HDR_FIELDS) return -4;

    /* FDI ヘッダ解析 (全フィールド LE32) */
    hdr_size  = le32(hdr + 8);
    fdd_size  = le32(hdr + 12);
    sect_size = le32(hdr + 16);
    spt       = le32(hdr + 20);
    surfaces  = le32(hdr + 24);
    cyls      = le32(hdr + 28);

    /* バリデーション */
    if (hdr_size < 32 || hdr_size > 65536) return -2;
    if (sect_size == 0 || sect_size > 4096) return -2;
    if (spt == 0 || spt > 64) return -2;
    if (surfaces == 0 || surfaces > 2) return -2;
    if (cyls == 0 || cyls > 1024) return -2;
    if (hdr_size + fdd_size > fsize) return -2;

    s->data_offset   = hdr_size;
    s->file_size     = fsize;
    s->cyls          = (u16)cyls;
    s->heads         = (u8)surfaces;
    s->spt           = (u8)spt;
    s->lba_sect_size = (u16)sect_size;
    s->total_lba     = cyls * surfaces * spt;
    s->fmt           = LOOP_FMT_FDI;
    s->dev.blk_read  = raw_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_hdi — HDI (Anex86 HDD) イメージのアタッチ処理                    */
/*                                                                          */
/*  FDI と同一ヘッダ構造 (4096B ヘッダ + RAW データ)。                       */
/*  拡張子 .hdi で区別する (マジックナンバーなし)。                          */
/* ======================================================================== */
static int attach_hdi(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr[FDI_HDR_FIELDS];
    u32 hdr_size, data_size, sect_size, spt, surfaces, cyls;
    int rd;

    if (fsize < FDI_HDR_SIZE) return -2;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr, FDI_HDR_FIELDS);
    if (rd < FDI_HDR_FIELDS) return -4;

    /* HDI ヘッダ解析 (FDI と同一レイアウト、全フィールド LE32) */
    hdr_size  = le32(hdr + 8);
    data_size = le32(hdr + 12);
    sect_size = le32(hdr + 16);
    spt       = le32(hdr + 20);
    surfaces  = le32(hdr + 24);
    cyls      = le32(hdr + 28);

    /* バリデーション */
    if (hdr_size < 32 || hdr_size > 65536) return -2;
    if (sect_size == 0 || sect_size > 4096) return -2;
    if (spt == 0 || spt > 255) return -2;
    if (surfaces == 0 || surfaces > 255) return -2;
    if (cyls == 0 || cyls > 65535) return -2;
    if (hdr_size + data_size > fsize) return -2;

    s->data_offset   = hdr_size;
    s->file_size     = fsize;
    s->cyls          = (u16)cyls;
    s->heads         = (u8)surfaces;
    s->spt           = (u8)spt;
    s->lba_sect_size = (u16)sect_size;
    s->total_lba     = cyls * surfaces * spt;
    s->fmt           = LOOP_FMT_HDI;
    s->dev.blk_read  = raw_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_nhd — NHD (T98-Next r0) イメージのアタッチ処理                    */
/*                                                                          */
/*  512B ヘッダ + RAW データ。HDI と違いマジックがあるので必ず照合する。      */
/*  ジオメトリはヘッダの C/H/S/BPS をそのまま使う (docs/NHD_FORMAT.md)。     */
/* ======================================================================== */
static int attach_nhd(LoopSlot *s, int fd, u32 fsize)
{
    u8  hdr[NHD_OFF_BPS + 2];
    u32 hdr_size, cyls, heads, spt, bps;
    int rd, i;
    static const char sig[] = NHD_SIG;

    if (fsize < NHD_HDR_SIZE) return -2;

    vfs_seek(fd, 0, 0);
    rd = vfs_read_fd(fd, hdr, sizeof(hdr));
    if (rd < (int)sizeof(hdr)) return -4;

    for (i = 0; sig[i]; i++) {
        if (hdr[i] != (u8)sig[i]) return -2;
    }

    hdr_size = le32(hdr + NHD_OFF_HDRSIZE);
    cyls     = le32(hdr + NHD_OFF_CYLS);
    heads    = le16(hdr + NHD_OFF_HEADS);
    spt      = le16(hdr + NHD_OFF_SPT);
    bps      = le16(hdr + NHD_OFF_BPS);

    /* バリデーション (LoopSlot.cyls は u16 なので上限 65535)。
     * C*H*S*BPS は u32 を溢れ得るので段階的に上限を確かめる。
     * ファイルサイズが u32 なのでイメージは 4GB 未満しか扱えない。 */
    if (hdr_size < NHD_HDR_SIZE || hdr_size > 65536) return -2;
    if (hdr_size >= fsize) return -2;
    if (bps == 0 || bps > 4096) return -2;
    if (spt == 0 || spt > 255) return -2;
    if (heads == 0 || heads > 255) return -2;
    if (cyls == 0 || cyls > 65535) return -2;
    {
        u32 per_cyl = heads * spt;              /* <= 255*255, 溢れない */
        if (cyls > 0xFFFFFFFFU / per_cyl) return -2;
        if (cyls * per_cyl > 0xFFFFFFFFU / bps) return -2;
        if (cyls * per_cyl * bps > fsize - hdr_size) return -2;
    }

    s->data_offset   = hdr_size;
    s->file_size     = fsize;
    s->cyls          = (u16)cyls;
    s->heads         = (u8)heads;
    s->spt           = (u8)spt;
    s->lba_sect_size = (u16)bps;
    s->total_lba     = cyls * heads * spt;
    s->fmt           = LOOP_FMT_NHD;
    s->dev.blk_read  = raw_blk_read;
    return 0;
}

/* ======================================================================== */
/*  attach_raw — RAW ディスクイメージのアタッチ処理                              */
/*                                                                          */
/*  ヘッダなし。ファイルサイズの完全一致で既知容量テーブルから C/H/S/BPS を決定。 */
/*  未知サイズの場合は拒否する (誤判定防止)。                              */
/* ======================================================================== */
static int attach_raw(LoopSlot *s, int fd, u32 fsize)
{
    /* 既知容量テーブル */
    static const struct {
        u32 size;
        u16 cyls;
        u8  heads;
        u8  spt;
        u16 bps;
    } known[] = {
        { 1261568, 77, 2, 8,  1024 }, /* 2HD 1232KB (PC-98) */
        {  655360, 80, 2, 8,   512 }, /* 2DD 640KB */
        {  737280, 80, 2, 9,   512 }, /* 2DD 720KB */
        { 1474560, 80, 2, 18,  512 }, /* 2HD 1.44MB (IBM) */
        {  327680, 40, 2, 8,   512 }, /* 2D 320KB */
        {  163840, 40, 1, 8,   256 }, /* 1D 160KB */
        { 0, 0, 0, 0, 0 }
    };
    int i;

    for (i = 0; known[i].size != 0; i++) {
        if (fsize == known[i].size) {
            s->data_offset   = 0;
            s->file_size     = fsize;
            s->cyls          = known[i].cyls;
            s->heads         = known[i].heads;
            s->spt           = known[i].spt;
            s->lba_sect_size = known[i].bps;
            s->total_lba     = (u32)s->cyls * (u32)s->heads * (u32)s->spt;
            s->fmt           = LOOP_FMT_RAW;
            s->dev.blk_read  = raw_blk_read;
            return 0;
        }
    }
    return -2;  /* 未知サイズ: 拒否 */
}

/* ======================================================================== */
/*  CHS コア API                                                            */
/* ======================================================================== */

/* ------------------------------------------------------------------------ */
/*  loop_dev_seek_d88 — D88 FDCエミュレーション用 CHS セクタ検索            */
/*                                                                          */
/*  トラック選択 (trk_cyl/trk_head) とセクタID照合 (id_c/id_h/id_r) を分離。*/
/*  Ys 等のコピープロテクション対応:                                         */
/*    SEEK cyl=1 → READ C=0, H=1 のように物理トラックと論理IDが異なる。     */
/*                                                                          */
/*  戻り値: セクタデータのファイルオフセット (0=未発見)                       */
/* ------------------------------------------------------------------------ */
u32 loop_dev_seek_d88(int slot,
                      u8 trk_cyl, u8 trk_head,
                      u8 id_c, u8 id_h, u8 id_r,
                      u32 *out_data_len, u16 *out_spt)
{
    LoopSlot *s;
    u32 track_idx, trk_off, pos;
    u8  shdr[D88_SECT_HDR_SIZE];
    u16 track_spt, data_len;
    int si, rd;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return 0;
    s = &loop_slots[slot];
    if (!s->in_use || s->fmt != LOOP_FMT_D88) return 0;

    /* トラック選択: trk_cyl * heads + trk_head */
    track_idx = (u32)trk_cyl * (u32)s->heads + (u32)trk_head;
    if (track_idx >= D88_MAX_TRACKS) return 0;

    trk_off = s->track_offset[track_idx];
    if (trk_off == 0 || trk_off >= s->file_size) return 0;

    /* トラック先頭セクタヘッダから SPT 取得 */
    vfs_seek(s->fd, (int)trk_off, 0);
    rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
    if (rd < D88_SECT_HDR_SIZE) return 0;
    track_spt = le16(shdr + 4);

    if (track_spt == 0 || track_spt > 64) return 0;
    if (out_spt) *out_spt = track_spt;

    /* セクタヘッダ走査: id_c/id_h/id_r で照合 */
    pos = trk_off;
    for (si = 0; si < (int)track_spt; si++) {
        vfs_seek(s->fd, (int)pos, 0);
        rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
        if (rd < D88_SECT_HDR_SIZE) return 0;

        data_len = le16(shdr + 14);
        if (data_len == 0) {
            pos += D88_SECT_HDR_SIZE;
            continue;
        }

        if (shdr[0] == id_c &&
            shdr[1] == id_h &&
            shdr[2] == id_r) {
            if (out_data_len) *out_data_len = (u32)data_len;
            return pos + D88_SECT_HDR_SIZE;  /* データ部の先頭 */
        }

        pos += D88_SECT_HDR_SIZE + (u32)data_len;
    }
    return 0;  /* 未発見 */
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_track_id_d88 — D88 の物理トラック上の index 番目のセクタ ID     */
/*                                                                          */
/*  FDC の READ ID コマンド用。ヘッドの下を通る順にセクタ ID が並んでいて、  */
/*  READ ID はそれを 1 つずつ返す。ゲストはこれでフォーマットを判定する。    */
/*  (np21w-src/src/diskimage/fd/fdd_d88.c `fdd_readid_d88()` と同じ走査)     */
/*                                                                          */
/*  戻り値: 0=成功, -1=トラック/セクタ不在                                   */
/* ------------------------------------------------------------------------ */
int loop_dev_track_id_d88(int slot, u8 trk_cyl, u8 trk_head, u16 index,
                          u8 *out_c, u8 *out_h, u8 *out_r, u8 *out_n,
                          u16 *out_spt)
{
    LoopSlot *s;
    u32 track_idx, trk_off, pos;
    u8  shdr[D88_SECT_HDR_SIZE];
    u16 track_spt, data_len;
    int si, rd;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    s = &loop_slots[slot];
    if (!s->in_use || s->fmt != LOOP_FMT_D88) return -1;

    track_idx = (u32)trk_cyl * (u32)s->heads + (u32)trk_head;
    if (track_idx >= D88_MAX_TRACKS) return -1;

    trk_off = s->track_offset[track_idx];
    if (trk_off == 0 || trk_off >= s->file_size) return -1;

    vfs_seek(s->fd, (int)trk_off, 0);
    rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
    if (rd < D88_SECT_HDR_SIZE) return -1;
    track_spt = le16(shdr + 4);
    if (track_spt == 0 || track_spt > 64) return -1;
    if (out_spt) *out_spt = track_spt;
    if (index >= track_spt) return -1;

    pos = trk_off;
    for (si = 0; si < (int)track_spt; si++) {
        vfs_seek(s->fd, (int)pos, 0);
        rd = vfs_read_fd(s->fd, shdr, D88_SECT_HDR_SIZE);
        if (rd < D88_SECT_HDR_SIZE) return -1;
        data_len = le16(shdr + 14);

        if (si == (int)index) {
            if (out_c) *out_c = shdr[0];
            if (out_h) *out_h = shdr[1];
            if (out_r) *out_r = shdr[2];
            if (out_n) *out_n = shdr[3];
            return 0;
        }
        pos += D88_SECT_HDR_SIZE + (u32)data_len;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_read_chs — CHS セクタ読み出し (全フォーマット共通)              */
/*                                                                          */
/*  D88: loop_dev_seek_d88 → vfs_read                                      */
/*  FDI/RAW: オフセット計算 → vfs_read                                     */
/*  sect は 1-based (ATA/FDC 準拠)                                          */
/* ------------------------------------------------------------------------ */
/* FDI/HDI/RAW のオフセット計算に入れてよい CHS か。
 *
 * **sect は 1 起算。0 を素通ししてはいけない。**
 * オフセット計算の `(u32)(sect - 1)` が 0xFFFFFFFF に化け、
 * セクタ長を掛けた時点で桁溢れしてイメージ先頭付近へ回り込む。
 * 「offset + セクタ長 がファイル末尾を超えないか」の境界チェックも
 * 通ってしまうので、**範囲外の要求に無関係なセクタを返して成功と
 * 答える**という、いちばん質の悪い壊れ方になる。
 *
 * 実測: MS-DOS 5 が SASI 規約で投げた C=0/H=0/R=0 の読みに対して、
 * FAT 領域の中身を「成功」として返していた。
 * → docs/tasks/v86v2/08_dos5.md §2 */
static int raw_chs_ok(const LoopSlot *s, u16 cyl, u8 head, u8 sect)
{
    if (sect == 0 || (u32)sect > (u32)s->spt)   return 0;
    if ((u32)head >= (u32)s->heads)             return 0;
    if ((u32)cyl  >= (u32)s->cyls)              return 0;
    return 1;
}

int loop_dev_read_chs(int slot, u16 cyl, u8 head, u8 sect, void *buf)
{
    LoopSlot *s;
    int rd;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    s = &loop_slots[slot];
    if (!s->in_use) return -1;

    if (s->fmt == LOOP_FMT_D88) {
        /* D88: 標準 CHS アクセス (物理=論理) */
        u32 data_len = 0;
        u16 trk_spt = 0;
        u32 off;
        u32 to_read;

        off = loop_dev_seek_d88(slot,
            (u8)cyl, head, (u8)cyl, head, sect,
            &data_len, &trk_spt);
        if (off == 0) return -1;
        to_read = data_len;
        if (to_read > (u32)s->lba_sect_size)
            to_read = (u32)s->lba_sect_size;
        vfs_seek(s->fd, (int)off, 0);
        rd = vfs_read_fd(s->fd, buf, to_read);
        if (rd < (int)to_read) return -1;
        /* BPS に満たない場合はゼロ埋め */
        if (to_read < (u32)s->lba_sect_size)
            zero_fill((u8 *)buf + to_read,
                      (u32)s->lba_sect_size - to_read);
        return 0;
    } else {
        /* FDI/HDI/RAW: オフセット計算 */
        u32 offset;
        u32 data_size;

        if (!raw_chs_ok(s, cyl, head, sect)) return -1;

        data_size = s->file_size - s->data_offset;
        offset = s->data_offset
            + (((u32)cyl * (u32)s->heads + (u32)head)
               * (u32)s->spt + (u32)(sect - 1))
            * (u32)s->lba_sect_size;
        if (offset + (u32)s->lba_sect_size >
            s->data_offset + data_size)
            return -1;
        vfs_seek(s->fd, (int)offset, 0);
        rd = vfs_read_fd(s->fd, buf, (u32)s->lba_sect_size);
        if (rd < (int)s->lba_sect_size) return -1;
        return 0;
    }
}

/* ------------------------------------------------------------------------ */
/*  loop_dev_write_chs — CHS セクタ書き込み (全フォーマット共通)             */
/*                                                                          */
/*  D88: loop_dev_seek_d88 → vfs_write                                     */
/*  FDI/HDI/RAW: オフセット計算 → vfs_write                                */
/*  sect は 1-based (ATA/FDC 準拠)                                          */
/* ------------------------------------------------------------------------ */
int loop_dev_write_chs(int slot, u16 cyl, u8 head, u8 sect,
                        const void *buf)
{
    LoopSlot *s;
    int wr;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS) return -1;
    s = &loop_slots[slot];
    if (!s->in_use) return -1;

    if (s->fmt == LOOP_FMT_D88) {
        /* D88: seek_d88 でセクタデータ位置を取得 → 書き込み */
        u32 data_len = 0;
        u16 trk_spt = 0;
        u32 off;
        u32 to_write;

        off = loop_dev_seek_d88(slot,
            (u8)cyl, head, (u8)cyl, head, sect,
            &data_len, &trk_spt);
        if (off == 0) return -1;
        to_write = data_len;
        if (to_write > (u32)s->lba_sect_size)
            to_write = (u32)s->lba_sect_size;
        vfs_seek(s->fd, (int)off, 0);
        wr = vfs_write_fd(s->fd, buf, to_write);
        if (wr < (int)to_write) return -1;
        return 0;
    } else {
        /* FDI/HDI/RAW: オフセット計算 */
        u32 offset;
        u32 data_size;

        if (!raw_chs_ok(s, cyl, head, sect)) return -1;

        data_size = s->file_size - s->data_offset;
        offset = s->data_offset
            + (((u32)cyl * (u32)s->heads + (u32)head)
               * (u32)s->spt + (u32)(sect - 1))
            * (u32)s->lba_sect_size;
        if (offset + (u32)s->lba_sect_size >
            s->data_offset + data_size)
            return -1;
        vfs_seek(s->fd, (int)offset, 0);
        wr = vfs_write_fd(s->fd, buf, (u32)s->lba_sect_size);
        if (wr < (int)s->lba_sect_size) return -1;
        return 0;
    }
}

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
        return -3;

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
        ret = attach_raw(s, fd, fsize);
        fmt_name = "RAW";
        break;
    default:
        return -2;
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

    kprintf(0x0A,
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
    u32 fsize;

    if (slot < 0 || slot >= LOOP_MAX_SLOTS || !vfs_path)
        return -1;

    s = &loop_slots[slot];
    if (s->in_use)
        return -3;

    /* 拡張子でフォーマット判別 (高速パス) */
    fmt = detect_format(vfs_path);
    if (fmt == LOOP_FMT_NONE)
        return -2;

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
    if (s->fmt == LOOP_FMT_D88) return -2;

    offset = s->data_offset + lba * (u32)s->lba_sect_size;
    total_bytes = count * (u32)s->lba_sect_size;

    vfs_seek(s->fd, (int)offset, 0);
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
    if (s->fmt == LOOP_FMT_D88) return -2;
    if (!s->writable) return -3;

    offset = s->data_offset + lba * (u32)s->lba_sect_size;
    total_bytes = count * (u32)s->lba_sect_size;

    vfs_seek(s->fd, (int)offset, 0);
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
    if (s->lba_sect_size == 1024)
        return LOOP_MEDIA_2HD_1232;
    if (s->lba_sect_size == 512) {
        if (s->spt >= 18)
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
    while (tmp > 128 && n < 8) { tmp >>= 1; n++; }
    return n;
}
