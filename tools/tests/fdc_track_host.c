/* ======================================================================== */
/*  FDC_TRACK_HOST.C — FD のトラック単位の読み出しをホストで回す            */
/*                                                                          */
/*  実物を 1 行も写さずに #include する:                                    */
/*    drivers/fdc_track.c  … 区切り・先読み・読み直し (純粋な層)            */
/*    drivers/fdc_decide.c … 時間上限の式                                   */
/*    drivers/fdc.c        … シークの省略・まとめ読み・覚えたシリンダの破棄 */
/*  fdc.c のポートは tools/tests/fdc_hostshim/io.h が下の µPD765A の模型へ  */
/*  回す。tick_count は -D で「読むたびに 1 進む」時計に差し替える          */
/*  (待ちのループが模型の上で有限に終わるように)。                          */
/*                                                                          */
/*  2 段で見る:                                                             */
/*    (1) 偽の fdc (ops) で fdc_track_read の区切り・先読み・読み直しを見る */
/*    (2) µPD765A の模型で本物の fdc.c のコマンド列を見る                    */
/*  記録: tools/tests/fdc_track_tdd.md                                      */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../../drivers/fdc_decide.c"
#include "../../drivers/fdc_track.c"
#include "../../drivers/fdc.c"
#include "../../fs/fatfs/diskio.c"
/* FatFs 本体も実物を通す (font_replay)。ff.c は外部のコードなので、試験側の
 * 厳しい警告だけ外す (カーネルのビルドでは元の警告のまま)。 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#include "../../fs/fatfs/ff.c"
#pragma GCC diagnostic pop

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* ======================================================================== */
/*  カーネルの下請けの差し替え                                              */
/* ======================================================================== */
static u32 s_now;
volatile u32 *fdc_fake_tick_ptr()
{
    s_now++;            /* 読むたびに 1 tick 進む */
    return &s_now;
}

static int s_kprintf_lines;
void kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    (void)attr;
    s_kprintf_lines++;
    if (getenv("FDC_TRACK_VERBOSE")) {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
}

void *kmemcpy(void *dst, const void *src, u32 n) { return memcpy(dst, src, n); }
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }

/* diskio.c の HDD 側の下請け (FD の試験では呼ばれない) */
int ide_drive_present(int drive) { (void)drive; return 0; }
int ide_get_info(int drive, IdeInfo *info) { (void)drive; (void)info; return -1; }
Device *dev_find(const char *name) { (void)name; return (Device *)0; }
int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{ (void)dev; (void)lba; (void)count; (void)buf; return -1; }
int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{ (void)dev; (void)lba; (void)count; (void)buf; return -1; }
void rtc_read(RTC_Time *t) { memset(t, 0, sizeof(*t)); }

/* ======================================================================== */
/*  DMA (µPD8237A) の模型: マスクと積んだ番地・長さだけ覚える              */
/* ======================================================================== */
static struct {
    int masked;
    u32 phys;
    u32 bytes;
    int dir;
    int setups;
} D;

void dma_chan_mask(unsigned int ch) { (void)ch; D.masked = 1; }
void dma_chan_unmask(unsigned int ch) { (void)ch; D.masked = 0; }
int dma_chan_setup(unsigned int ch, u32 phys, u32 bytes, int dir, int mode)
{
    (void)ch; (void)mode;
    if (!D.masked) return -1;
    D.phys = phys;
    D.bytes = bytes;
    D.dir = dir;
    D.setups++;
    return 0;
}
void dma_above_1mb_raw(u8 *pre, u8 *post) { *pre = 0; *post = 0; }

/* ======================================================================== */
/*  ディスクの中身: (drv, C, H, R, off) から決まるバイト + 書いたセクタ      */
/* ======================================================================== */
/* 媒体の入れ替えを模す (値を変えると全セクタの中身が変わる) */
static int s_media_seed;

static u8 disk_byte(int drv, int c, int h, int r, int off)
{
    return (u8)(c * 7 + h * 31 + r * 13 + off + drv * 101 + s_media_seed * 57);
}

#define WR_MAX 16
static struct { int used, drv, c, h, r; u8 data[1024]; } s_written[WR_MAX];

/* 実物の FD イメージ (LBA 順の生データ)。font_replay だけが読む。 */
static u8 *s_image;
static long s_image_len;

static void disk_sector(int drv, int c, int h, int r, int bps, u8 *out)
{
    int i;
    for (i = 0; i < WR_MAX; i++) {
        if (s_written[i].used && s_written[i].drv == drv && s_written[i].c == c
            && s_written[i].h == h && s_written[i].r == r) {
            memcpy(out, s_written[i].data, (size_t)bps);
            return;
        }
    }
    if (s_image) {
        const struct fdc_geom *g = fdc_get_geom(drv);
        long lba = ((long)c * g->heads + h) * g->spt + (r - 1);
        if ((lba + 1) * bps <= s_image_len) {
            memcpy(out, s_image + lba * bps, (size_t)bps);
            return;
        }
    }
    for (i = 0; i < bps; i++) out[i] = disk_byte(drv, c, h, r, i);
}

static void disk_put(int drv, int c, int h, int r, int bps, const u8 *in)
{
    int i, slot = -1;
    for (i = 0; i < WR_MAX; i++) {
        if (s_written[i].used && s_written[i].drv == drv && s_written[i].c == c
            && s_written[i].h == h && s_written[i].r == r) { slot = i; break; }
        if (!s_written[i].used && slot < 0) slot = i;
    }
    if (slot < 0) { fprintf(stderr, "model: write table full\n"); exit(2); }
    s_written[slot].used = 1; s_written[slot].drv = drv; s_written[slot].c = c;
    s_written[slot].h = h; s_written[slot].r = r;
    memcpy(s_written[slot].data, in, (size_t)bps);
}

/* ======================================================================== */
/*  µPD765A の模型 (ポート 90h / 92h / 94h)                                 */
/* ======================================================================== */
static struct {
    u8 cmd[9];
    int ncmd, need;
    u8 res[7];
    int nres, rpos;
    int pend_n;
    u8 pend_st0[8], pend_pcn[8];
    int pcn[4];
    int present[4];
    int in_reset;
    /* 数 */
    int seeks, recals, reads, multi_reads, writes, resets;
    int mt_used, dma_len_mismatch, dma_cross, cmd_while_masked;
    u8 last_rw[9];
    /* 失敗の差し込み */
    int fail_multi;                 /* EOT > R の READ を DE で落とす */
    int bad_c, bad_h, bad_r;        /* 読めないセクタ (bad_r = 0 で無し) */
    int drop_irq_reads;             /* READ の完了 IRQ を出さない回数 */
    int fail_seeks;                 /* SEEK を異常終了させる回数 */
    /* INT 線 (レベル) とエッジトリガの PIC。**pending の SIS 結果が残って
     * いるか、READ / WRITE のリザルトが読み終わっていないあいだ線は上がった
     * まま**で、立ち上がりでしか fdc_irq_fired は立たない (実機の形)。 */
    int line;
    int rw_int;
    int np21_irq;                   /* 1 = NP21/W の形 (事象ごとに IRQ) */
    int nr_on_seek;                 /* 媒体の無いドライブへの SEEK を NR で返す */
    /* ヘッドの整定: SE 付きの SIS 結果を CPU が読んだ時刻から 2 tick 経たない
     * うちに READ / WRITE が来たら数える (RECALIBRATE の後の整定漏れ)。 */
    int move_pending;
    u32 move_seen;
    int unsettled;
    int foreign_on_seek;            /* SEEK 完了の前に別の通知を積む: 1=drv1 SE, 2=自 Ready 変化, 3=4 件 */
    int fail_reads;                 /* 次の READ を n 回 DE で落とす */
    int force_err;
} M;

static void model_line(void)
{
    int up = (M.pend_n > 0) || M.rw_int;
    if (up && !M.line) fdc_irq_fired = 1;
    M.line = up;
}

/* 完了の通知。NP21/W の形なら線に関係なく毎回 IRQ を出す。 */
static void model_irq(void)
{
    if (M.np21_irq) fdc_irq_fired = 1;
    model_line();
}

static void model_pend(u8 st0, u8 pcn)
{
    if (M.pend_n < 8) {
        M.pend_st0[M.pend_n] = st0;
        M.pend_pcn[M.pend_n] = pcn;
        M.pend_n++;
    }
}

static void model_result(const u8 *r, int n)
{
    memcpy(M.res, r, (size_t)n);
    M.nres = n;
    M.rpos = 0;
}

static int model_cmd_len(u8 c)
{
    switch (c & 0x1F) {
    case 0x03: return 3;        /* SPECIFY */
    case 0x05: return 9;        /* WRITE DATA */
    case 0x06: return 9;        /* READ DATA */
    case 0x07: return 2;        /* RECALIBRATE */
    case 0x08: return 1;        /* SENSE INTERRUPT STATUS */
    case 0x0F: return 3;        /* SEEK */
    }
    return 1;
}

static const struct fdc_geom *model_geom(int drv);

static void model_rw(int is_write)
{
    u8 r[7];
    int drv = M.cmd[1] & 3, hd = (M.cmd[1] >> 2) & 1;
    int C = M.cmd[2], H = M.cmd[3], R = M.cmd[4], N = M.cmd[5], EOT = M.cmd[6];
    int bps = 128 << N;
    int nsec = EOT - R + 1;
    u32 want = (u32)nsec * (u32)bps;
    int i, err = 0, nr = 0;
    u8 st1 = 0, st2 = 0;

    memcpy(M.last_rw, M.cmd, 9);
    if (is_write) M.writes++; else M.reads++;
    if (M.move_pending) {
        if (s_now - M.move_seen < 2) M.unsettled++;
        M.move_pending = 0;
    }
    if (M.fail_reads > 0 && !is_write) { M.fail_reads--; M.force_err = 1; }
    if (!is_write && nsec > 1) M.multi_reads++;
    if (M.cmd[0] & 0x80) M.mt_used++;
    if (D.masked) M.cmd_while_masked++;
    if (D.bytes != want) M.dma_len_mismatch++;
    if (((D.phys & 0xFFFFUL) + D.bytes) > 0x10000UL) M.dma_cross++;
    (void)model_geom;

    if (nsec < 1) { err = 1; st1 = 0x80; }                  /* EN */
    else if (!M.present[drv]) { err = 1; nr = 1; }
    else if (M.pcn[drv] != C) { err = 1; st1 = 0x04; st2 = 0x10; } /* ND / WC */
    else if (!is_write && M.fail_multi && nsec > 1) { err = 1; st1 = 0x20; }
    else if (M.force_err) { err = 1; st1 = 0x20; }
    else if (!is_write && M.bad_r && M.bad_c == C && M.bad_h == H
             && M.bad_r >= R && M.bad_r <= EOT) { err = 1; st1 = 0x20; st2 = 0x20; }
    M.force_err = 0;

    if (!err) {
        u8 *mem = (u8 *)(unsigned long)D.phys;
        for (i = 0; i < nsec && (u32)(i + 1) * (u32)bps <= D.bytes; i++) {
            if (is_write) disk_put(drv, C, H, R + i, bps, mem + i * bps);
            else          disk_sector(drv, C, H, R + i, bps, mem + i * bps);
        }
    }
    r[0] = (u8)((err ? 0x40 : 0x00) | (nr ? 0x08 : 0x00) | (hd << 2) | drv);
    r[1] = st1; r[2] = st2;
    r[3] = (u8)C; r[4] = (u8)H; r[5] = (u8)(err ? R : EOT); r[6] = (u8)N;
    model_result(r, 7);
    if (!is_write && M.drop_irq_reads > 0) { M.drop_irq_reads--; return; }
    M.rw_int = 1;
    model_irq();
}

static void model_exec(void)
{
    u8 c = M.cmd[0];
    int drv = M.cmd[1] & 3, hd = (M.cmd[1] >> 2) & 1;
    u8 r[2];

    switch (c & 0x1F) {
    case 0x03:
        break;
    case 0x08:
        if (M.pend_n > 0) {
            r[0] = M.pend_st0[0]; r[1] = M.pend_pcn[0];
            memmove(M.pend_st0, M.pend_st0 + 1, 7);
            memmove(M.pend_pcn, M.pend_pcn + 1, 7);
            M.pend_n--;
            model_result(r, 2);
            model_line();
            if (r[0] & 0x20) { M.move_pending = 1; M.move_seen = s_now; }
        } else {
            r[0] = 0x80;
            model_result(r, 1);
        }
        break;
    case 0x07:
        M.recals++;
        if (M.present[drv]) { M.pcn[drv] = 0; model_pend((u8)(0x20 | drv), 0); }
        else model_pend((u8)(0x70 | drv), (u8)M.pcn[drv]);
        model_irq();
        break;
    case 0x0F:
        M.seeks++;
        if (!M.present[drv] && M.nr_on_seek) {
            model_pend((u8)(0x68 | (hd << 2) | drv), (u8)M.pcn[drv]);  /* IC=01 SE NR */
            model_irq();
            break;
        }
        if (M.fail_seeks > 0) {
            /* 途中で止まった: ヘッドは目的と違うシリンダに居る */
            M.fail_seeks--;
            M.pcn[drv] = M.cmd[2] / 2;
            model_pend((u8)(0x60 | (hd << 2) | drv), (u8)M.pcn[drv]);
            model_irq();
            break;
        }
        M.pcn[drv] = M.cmd[2];
        if (M.foreign_on_seek == 1) model_pend(0x21, 0);            /* drv1 */
        if (M.foreign_on_seek == 2) model_pend((u8)(0xC0 | drv), (u8)M.pcn[drv]);
        if (M.foreign_on_seek == 3) {
            /* 件数の上限 (FDC_SIS_DRAIN_MAX = 4) ちょうどの通知を先に積む */
            model_pend(0x21, 0); model_pend(0x22, 0); model_pend(0x23, 0);
            model_pend((u8)(0xC0 | drv), (u8)M.pcn[drv]);
        }
        model_pend((u8)(0x20 | (hd << 2) | drv), (u8)M.pcn[drv]);
        model_irq();
        break;
    case 0x06: model_rw(0); break;
    case 0x05: model_rw(1); break;
    default:
        r[0] = 0x80;
        model_result(r, 1);
        break;
    }
}

unsigned int fdc_shim_inp(unsigned int port)
{
    if (port == 0x90) {
        if (M.rpos < M.nres) return 0x80 | 0x40 | 0x10;     /* RQM DIO CB */
        if (M.ncmd > 0) return 0x80 | 0x10;                 /* RQM CB */
        return 0x80;
    }
    if (port == 0x92) {
        u8 v = 0xFF;
        if (M.rpos < M.nres) v = M.res[M.rpos++];
        if (M.rpos >= M.nres) {
            M.nres = 0; M.rpos = 0;
            M.rw_int = 0;       /* リザルトを読み終えたら READ/WRITE の INT は下りる */
            model_line();
        }
        return v;
    }
    return 0xFF;
}

void fdc_shim_outp(unsigned int port, unsigned int value)
{
    u8 v = (u8)value;
    if (port == 0x92) {
        if (M.ncmd == 0) M.need = model_cmd_len(v);
        M.cmd[M.ncmd++] = v;
        if (M.ncmd >= M.need) { model_exec(); M.ncmd = 0; }
        return;
    }
    if (port == 0x94) {
        if (v & 0x80) {
            M.in_reset = 1;
            M.ncmd = 0; M.nres = 0; M.rpos = 0; M.pend_n = 0;
            M.rw_int = 0; M.line = 0;
            M.resets++;
        } else if (M.in_reset) {
            int d;
            M.in_reset = 0;
            for (d = 0; d < 4; d++) model_pend((u8)(0xC0 | d), (u8)M.pcn[d]);
            model_irq();
        }
    }
}

static const struct fdc_geom *model_geom(int drv) { return fdc_get_geom(drv); }

/* 模型と fdc.c を起動直後に戻して fdc_init() を通す。 */
static void model_boot(int drv1_present)
{
    memset(&M, 0, sizeof(M));
    memset(&D, 0, sizeof(D));
    memset(s_written, 0, sizeof(s_written));
    s_media_seed = 0;
    memset(&s_stats, 0, sizeof(s_stats));
    M.present[0] = 1;
    M.present[1] = drv1_present;
    M.pcn[0] = 33;              /* ローダの後でヘッドは奥に居る */
    fdc_set_media(0, FDC_MEDIA_2HD_1232);
    fdc_set_media(1, FDC_MEDIA_2HD_1232);
    if (fdc_init() != 0) { fprintf(stderr, "model: fdc_init failed\n"); exit(2); }
    s_kprintf_lines = 0;
    memset(&s_stats, 0, sizeof(s_stats));
}

static int check_bytes(const u8 *p, int drv, int c, int h, int r, int bps)
{
    int i;
    for (i = 0; i < bps; i++) {
        if (p[i] != disk_byte(drv, c, h, r, i)) return 0;
    }
    return 1;
}

/* ======================================================================== */
/*  (1) 偽の fdc — fdc_track_read の区切り・先読み・読み直し               */
/* ======================================================================== */
#define CALL_MAX 256
static struct {
    int n;
    struct { int multi, drv, cyl, head, sect, count; } c[CALL_MAX];
    int fail_multi_all;
    int bad_cyl, bad_head, bad_sect;    /* bad_sect = 0 で無し */
    int single_fail_all;
    int nr;
} F;

static void fake_reset(void) { memset(&F, 0, sizeof(F)); }

static void fake_log(int multi, int drv, int cyl, int head, int sect, int count)
{
    if (F.n < CALL_MAX) {
        F.c[F.n].multi = multi; F.c[F.n].drv = drv; F.c[F.n].cyl = cyl;
        F.c[F.n].head = head; F.c[F.n].sect = sect; F.c[F.n].count = count;
    }
    F.n++;
}

static int fake_multi(void *ctx, int drv, int cyl, int head, int sect,
                      int count, const struct fdc_geom *g, void *buf)
{
    int i;
    (void)ctx;
    fake_log(1, drv, cyl, head, sect, count);
    if (sect + count - 1 > (int)g->spt) return -2;      /* トラックをまたいだ */
    if (F.nr) return -3;                                 /* 媒体無し */
    /* 失敗するときは受け皿を途中まで壊してから返す (実行フェーズの途中で
     * 落ちた DMA の姿)。呼び手は半端な中身を当ててはいけない。 */
    if (F.fail_multi_all
        || (F.bad_sect && F.bad_cyl == cyl && F.bad_head == head
            && F.bad_sect >= sect && F.bad_sect <= sect + count - 1)) {
        memset(buf, 0xEE, (size_t)g->bps);
        return -1;
    }
    for (i = 0; i < count; i++) {
        disk_sector(drv, cyl, head, sect + i, g->bps,
                    (u8 *)buf + (u32)i * g->bps);
    }
    return 0;
}

static int fake_one(void *ctx, int drv, int cyl, int head, int sect,
                    const struct fdc_geom *g, void *buf)
{
    (void)ctx;
    fake_log(0, drv, cyl, head, sect, 1);
    if (F.single_fail_all) return -1;
    if (F.bad_sect && F.bad_cyl == cyl && F.bad_head == head
        && F.bad_sect == sect) return -1;
    disk_sector(drv, cyl, head, sect, g->bps, (u8 *)buf);
    return 0;
}

static void fake_copy(void *d, const void *s, u32 n) { memcpy(d, s, n); }

static u32 s_fake_gen;
static u32 fake_gen(void *ctx, int drv) { (void)ctx; (void)drv; return s_fake_gen; }
static u32 s_fake_now;
static u32 fake_now(void *ctx) { (void)ctx; return s_fake_now; }

static const struct fdc_track_ops FAKE_OPS = {
    fake_multi, fake_one, fake_copy, fake_gen, fake_now, 0
};

static u8 s_cache_buf[FDC_TRACK_SLOTS][FDC_TRACK_MAX_BYTES];
static u8 s_sec_buf[FDC_SECTOR_SLOTS][FDC_SECTOR_SLOT_BYTES];
static struct fdc_track_cache s_cache;

/* トラックのスロットと、セクタキャッシュを nsec 個だけ渡す (残りは NULL =
 * 使わない)。区切りと読み直しの試験はセクタキャッシュ無しで回す。 */
static void cache_setup2(int ntrk, int nsec)
{
    int i;
    memset(&s_cache, 0, sizeof(s_cache));
    for (i = 0; i < FDC_TRACK_SLOTS; i++) {
        fdc_track_init(&s_cache, i, i < ntrk ? s_cache_buf[i] : (u8 *)0);
    }
    for (i = 0; i < FDC_SECTOR_SLOTS; i++) {
        fdc_track_init_sector(&s_cache, i, i < nsec ? s_sec_buf[i] : (u8 *)0);
    }
    s_fake_gen = 0;
    s_fake_now = 0;
}

static void cache_setup(int nslots) { cache_setup2(nslots, 0); }
static void cache_reset(void) { cache_setup(1); }

/* 要求 [lba, lba+count) を読み、中身が正しいことと、その外に書いていない
 * ことを確かめる。 */
#define GUARD 64
static int read_and_verify(int drv, const struct fdc_geom *g, u32 lba, u32 count)
{
    static u8 area[GUARD + 40 * 1024 + GUARD];
    u8 *buf = area + GUARD;
    u32 i, per_cyl = (u32)g->spt * g->heads;
    int rc, ok = 1;

    memset(area, 0xA5, sizeof(area));
    rc = fdc_track_read(&s_cache, &FAKE_OPS, drv, g, lba, count, buf);
    if (rc != 0) return rc;
    for (i = 0; i < count; i++) {
        u32 l = lba + i;
        int c = (int)(l / per_cyl), h = (int)((l / g->spt) % g->heads);
        int r = (int)(l % g->spt) + 1;
        if (!check_bytes(buf + i * g->bps, drv, c, h, r, g->bps)) ok = 0;
    }
    for (i = 0; i < GUARD; i++) {
        if (area[i] != 0xA5) ok = 0;
        if (buf[count * g->bps + i] != 0xA5) ok = 0;
    }
    return ok ? 0 : 99;
}

static void split_2hd(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;       /* 8 × 1024 */
    struct fdc_run r;

    /* count=1 */
    CHECK(fdc_track_split(g, 0, 1, &r) == 1);
    CHECK(r.cyl == 0 && r.head == 0 && r.sect == 1 && r.count == 1);
    /* トラックの中に収まる */
    CHECK(fdc_track_split(g, 2, 3, &r) == 3);
    CHECK(r.cyl == 0 && r.head == 0 && r.sect == 3 && r.count == 3);
    /* トラックの終わりちょうど */
    CHECK(fdc_track_split(g, 0, 8, &r) == 8);
    CHECK(r.sect == 1 && r.count == 8);
    /* ヘッドの境目をまたぐ要求はヘッド 0 の残りで切る (MT を使わない) */
    CHECK(fdc_track_split(g, 6, 4, &r) == 2);
    CHECK(r.cyl == 0 && r.head == 0 && r.sect == 7 && r.count == 2);
    /* 最後のセクタだけ */
    CHECK(fdc_track_split(g, 7, 5, &r) == 1);
    CHECK(r.head == 0 && r.sect == 8);
    /* ヘッド 1 */
    CHECK(fdc_track_split(g, 8, 20, &r) == 8);
    CHECK(r.cyl == 0 && r.head == 1 && r.sect == 1);
    /* シリンダの境目 (lba 15 = C0 H1 R8 → lba 16 = C1 H0 R1) */
    CHECK(fdc_track_split(g, 15, 2, &r) == 1);
    CHECK(r.cyl == 0 && r.head == 1 && r.sect == 8);
    CHECK(fdc_track_split(g, 16, 2, &r) == 2);
    CHECK(r.cyl == 1 && r.head == 0 && r.sect == 1);
    /* 最後のシリンダ (76) */
    CHECK(fdc_track_split(g, 76 * 16 + 8 + 7, 1, &r) == 1);
    CHECK(r.cyl == 76 && r.head == 1 && r.sect == 8);
    /* count = 0 は区間無し */
    CHECK(fdc_track_split(g, 3, 0, &r) == 0);
}

static void split_144(void)
{
    const struct fdc_geom *g = &fdc_geom_144;       /* 18 × 512 */
    struct fdc_run r;

    CHECK(fdc_track_split(g, 0, 1, &r) == 1);
    CHECK(r.cyl == 0 && r.head == 0 && r.sect == 1);
    CHECK(fdc_track_split(g, 0, 40, &r) == 18);
    CHECK(fdc_track_split(g, 17, 2, &r) == 1);
    CHECK(r.head == 0 && r.sect == 18);
    CHECK(fdc_track_split(g, 18, 2, &r) == 2);
    CHECK(r.cyl == 0 && r.head == 1 && r.sect == 1);
    CHECK(fdc_track_split(g, 35, 3, &r) == 1);
    CHECK(r.cyl == 0 && r.head == 1 && r.sect == 18);
    CHECK(fdc_track_split(g, 36, 3, &r) == 3);
    CHECK(r.cyl == 1 && r.head == 0 && r.sect == 1);
    /* 先読みの EOT はトラックの終わり */
    CHECK(fdc_track_split(g, 20, 1, &r) == 1);
    CHECK(fdc_track_eot(g, &r) == 18);
    /* 区間がトラックを越えるのは呼び手の誤り */
    r.sect = 17; r.count = 3;
    CHECK(fdc_track_eot(g, &r) == 0);
}

/* count=1 の連続 (FatFs の窓) — 1 トラック 1 コマンドになる。 */
static void readahead_count1(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;
    u32 l;

    fake_reset(); cache_reset();
    for (l = 0; l < 32; l++) CHECK(read_and_verify(0, g, l, 1) == 0);
    /* 32 セクタ = 4 トラック → 束ねた読みが 4 回だけ */
    CHECK(F.n == 4);
    CHECK(F.c[0].multi && F.c[0].cyl == 0 && F.c[0].head == 0
          && F.c[0].sect == 1 && F.c[0].count == 8);
    CHECK(F.c[1].multi && F.c[1].cyl == 0 && F.c[1].head == 1);
    CHECK(F.c[2].multi && F.c[2].cyl == 1 && F.c[2].head == 0);
    CHECK(F.c[3].multi && F.c[3].cyl == 1 && F.c[3].head == 1);

    /* 1.44MB (18 × 512) でも同じ: 40 セクタ → 3 トラック */
    fake_reset(); cache_reset();
    for (l = 0; l < 40; l++) CHECK(read_and_verify(0, &fdc_geom_144, l, 1) == 0);
    CHECK(F.n == 3);
    CHECK(F.c[0].count == 18 && F.c[2].cyl == 1 && F.c[2].head == 0);

    /* 途中から始めたら、そのセクタからトラックの終わりまで読む */
    fake_reset(); cache_reset();
    CHECK(read_and_verify(0, g, 5, 1) == 0);
    CHECK(F.n == 1 && F.c[0].sect == 6 && F.c[0].count == 3);
    /* 手前のセクタは持っていないので読みに行く */
    CHECK(read_and_verify(0, g, 4, 1) == 0);
    CHECK(F.n == 2 && F.c[1].sect == 5);
    /* 持っているセクタは読まない */
    CHECK(read_and_verify(0, g, 7, 1) == 0);
    CHECK(F.n == 2);
}

/* 境目をまたぐ要求 — トラックごとに 1 回、中身とバッファの外は正しい。 */
static void cross_boundary(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;

    fake_reset(); cache_reset();
    CHECK(read_and_verify(0, g, 6, 4) == 0);    /* H0 R7-8 + H1 R1-2 */
    CHECK(F.n == 2);
    CHECK(F.c[0].head == 0 && F.c[0].sect == 7 && F.c[0].count == 2);
    CHECK(F.c[1].head == 1 && F.c[1].sect == 1 && F.c[1].count == 8);

    fake_reset(); cache_reset();
    CHECK(read_and_verify(0, g, 14, 20) == 0);  /* C0H1 R7 から C2H0 R2 まで */
    CHECK(F.n == 4);
    CHECK(F.c[0].cyl == 0 && F.c[0].head == 1 && F.c[0].sect == 7);
    CHECK(F.c[1].cyl == 1 && F.c[1].head == 0 && F.c[1].count == 8);
    CHECK(F.c[2].cyl == 1 && F.c[2].head == 1);
    CHECK(F.c[3].cyl == 2 && F.c[3].head == 0 && F.c[3].sect == 1);

    fake_reset(); cache_reset();
    CHECK(read_and_verify(0, &fdc_geom_144, 30, 10) == 0);  /* H1 R13-18 + C1 H0 R1-4 */
    CHECK(F.n == 2);
    CHECK(F.c[0].sect == 13 && F.c[0].count == 6);
    CHECK(F.c[1].cyl == 1 && F.c[1].sect == 1 && F.c[1].count == 18);

    /* どのコマンドもトラックをまたがない */
    {
        int i;
        for (i = 0; i < F.n && i < CALL_MAX; i++) {
            CHECK(F.c[i].sect + F.c[i].count - 1 <= 18);
        }
    }
}

/* まとめ読みが失敗したら、**要求した区間だけ** 1 セクタずつ読み直す。 */
static void fallback_single(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;

    fake_reset(); cache_reset();
    F.fail_multi_all = 1;
    CHECK(read_and_verify(0, g, 2, 3) == 0);
    /* multi 1 回 + single 3 回 (R3, R4, R5)。先読みの R6-8 は読み直さない */
    CHECK(F.n == 4);
    CHECK(F.c[0].multi && F.c[0].sect == 3 && F.c[0].count == 6);
    CHECK(!F.c[1].multi && F.c[1].sect == 3);
    CHECK(!F.c[2].multi && F.c[2].sect == 4);
    CHECK(!F.c[3].multi && F.c[3].sect == 5);
    /* 失敗した中身は持たない — 次の読みもコマンドを出す */
    CHECK(s_cache.slot[0].valid == 0);
    F.fail_multi_all = 0;
    CHECK(read_and_verify(0, g, 5, 1) == 0);
    CHECK(F.n == 5 && F.c[4].multi);

    /* 持っていたトラック (C0 H0) の後で別トラックのまとめ読みが失敗しても、
     * 壊れた受け皿を C0 H0 として当てない。 */
    fake_reset(); cache_reset();
    CHECK(read_and_verify(0, g, 0, 1) == 0);        /* C0 H0 R1-8 を持つ */
    F.fail_multi_all = 1;
    CHECK(read_and_verify(0, g, 8, 1) == 0);        /* C0 H1: 失敗 → 単発 */
    F.fail_multi_all = 0;
    CHECK(read_and_verify(0, g, 1, 1) == 0);        /* C0 H0 R2 は読み直す */
    CHECK(F.n == 4 && F.c[3].multi && F.c[3].head == 0);

    /* 要求の外 (先読みの分) に傷んだセクタがあるトラックでは、2 回目から
     * 要求の範囲だけを束ねて読む (毎回の失敗 + 1 セクタずつを踏まない)。 */
    fake_reset(); cache_reset();
    F.bad_cyl = 3; F.bad_head = 0; F.bad_sect = 7;
    CHECK(read_and_verify(0, g, 48 + 0, 2) == 0);   /* R1-2 要求、R1-8 を先読み */
    CHECK(F.n == 3);                                /* multi 失敗 + 単発 2 */
    CHECK(read_and_verify(0, g, 48 + 2, 3) == 0);   /* R3-5 */
    CHECK(F.n == 4);
    CHECK(F.c[3].multi && F.c[3].sect == 3 && F.c[3].count == 3);
    /* 別のトラックでは先読みに戻る */
    CHECK(read_and_verify(0, g, 56, 1) == 0);
    CHECK(F.n == 5 && F.c[4].count == 8);
    /* 捨てたら印も忘れる */
    fdc_track_invalidate(&s_cache);
    F.bad_sect = 0;
    CHECK(read_and_verify(0, g, 48 + 2, 1) == 0);
    CHECK(F.n == 6 && F.c[5].count == 6);

    /* 境目をまたぐ要求の片方だけ失敗 */
    fake_reset(); cache_reset();
    F.bad_cyl = 0; F.bad_head = 1; F.bad_sect = 8;  /* 要求の外 (H1 R8) */
    CHECK(read_and_verify(0, g, 6, 4) == 0);        /* H0 R7-8 + H1 R1-2 */
    CHECK(F.n == 4);
    CHECK(F.c[0].multi && F.c[0].head == 0);        /* H0 は通る */
    CHECK(F.c[1].multi && F.c[1].head == 1);        /* H1 は R8 で落ちる */
    CHECK(!F.c[2].multi && F.c[2].head == 1 && F.c[2].sect == 1);
    CHECK(!F.c[3].multi && F.c[3].head == 1 && F.c[3].sect == 2);
}

/* 読めないセクタが要求の中にあれば失敗を返す。 */
static void single_fail(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;
    static u8 buf[8 * 1024];

    fake_reset(); cache_reset();
    F.bad_cyl = 2; F.bad_head = 0; F.bad_sect = 4;
    CHECK(fdc_track_read(&s_cache, &FAKE_OPS, 0, g, 32 + 1, 5, buf) != 0);
    /* 読めた手前 (R2, R3) の後で R4 の単発が落ちて止まる */
    CHECK(F.n == 4);
    CHECK(!F.c[3].multi && F.c[3].sect == 4);

    fake_reset(); cache_reset();
    F.single_fail_all = 1; F.fail_multi_all = 1;
    CHECK(fdc_track_read(&s_cache, &FAKE_OPS, 0, g, 0, 1, buf) != 0);
}

/* 捨てる条件: invalidate / ドライブ / ジオメトリ / シリンダとヘッド */
static void cache_rules(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;

    fake_reset(); cache_reset();
    CHECK(read_and_verify(0, g, 0, 1) == 0);
    CHECK(F.n == 1);
    fdc_track_invalidate(&s_cache);
    CHECK(read_and_verify(0, g, 1, 1) == 0);
    CHECK(F.n == 2);
    /* 別ドライブは当てない */
    CHECK(read_and_verify(1, g, 2, 1) == 0);
    CHECK(F.n == 3 && F.c[2].drv == 1);
    /* 別ジオメトリ (同じ番号でも) は当てない */
    CHECK(read_and_verify(1, &fdc_geom_144, 2, 1) == 0);
    CHECK(F.n == 4);
    /* 別ヘッド・別シリンダ */
    CHECK(read_and_verify(1, &fdc_geom_144, 18 + 2, 1) == 0);
    CHECK(F.n == 5);
    CHECK(read_and_verify(1, &fdc_geom_144, 36 + 2, 1) == 0);
    CHECK(F.n == 6);
    /* 持っている後ろのセクタは当たる */
    CHECK(read_and_verify(1, &fdc_geom_144, 36 + 17, 1) == 0);
    CHECK(F.n == 6);
}

/* 受け皿に入らないジオメトリは束ねずに 1 セクタずつ。 */
static void oversize_geom(void)
{
    static const struct fdc_geom big = { 80, 2, 12, 3, 1024, 0x74, 0x90 };
    fake_reset(); cache_reset();
    CHECK(read_and_verify(0, &big, 0, 3) == 0);
    CHECK(F.n == 3 && !F.c[0].multi && !F.c[1].multi && !F.c[2].multi);
}

/* 時間上限: 機構の最悪値 (fdc.h の FDC_ROT_TICKS_WORST の注記) */
static void timeout_math(void)
{
#define T(spt, n) fdc_rw_timeout_ticks((spt), (n), FDC_ROT_TICKS_WORST, \
        FDC_FIND_ROTATIONS, FDC_HEAD_LOAD_TICKS, FDC_TIMEOUT_MARGIN, \
        FDC_RW_TIMEOUT_TICKS)
    /* 1 トラック全部 = 2 回転 + 1 回転 + HLT = 61 tick、余裕 2 倍で 122 */
    CHECK(T(8, 8) == 122);
    CHECK(T(18, 18) == 122);
    /* 半分 = 2 回転 + 0.5 回転 (10) + 1 = 51 → 102 */
    CHECK(T(8, 4) == 102);
    /* 端数は切り上げ: 18 セクタ中 5 = 20*5/18 = 5.6 → 6 → 2*(40+6+1) = 94 → 下限 100 */
    CHECK(T(18, 5) == 100);
    /* 18 中 10 = 11.1 → 12 → 2*(40+12+1) = 106 */
    CHECK(T(18, 10) == 106);
    /* 1 セクタは単発の上限 (100) を下回らない */
    CHECK(T(8, 1) == FDC_RW_TIMEOUT_TICKS);
    CHECK(T(0, 1) == FDC_RW_TIMEOUT_TICKS);
    CHECK(T(8, 0) == FDC_RW_TIMEOUT_TICKS);
    /* 多いほど長い (単調) */
    CHECK(T(8, 8) > T(8, 4));
#undef T
}

/* ======================================================================== */
/*  (2) µPD765A の模型 — 本物の fdc.c                                       */
/* ======================================================================== */

/* まとめ読みのコマンド: MT=0、R=sect、EOT=sect+count-1、DMA 長 = count × bps */
static void fdc_multi_cmd(void)
{
    static u8 buf[9 * 1024];
    int i;

    model_boot(0);
    CHECK(fdc_read_sectors(0, 5, 1, 3, 6, &fdc_geom_2hd, buf) == 0);
    CHECK(M.reads == 1 && M.multi_reads == 1);
    CHECK(M.last_rw[0] == (FDC_OPT_MF | FDC_CMD_READ_DATA));   /* MT 無し */
    CHECK(M.last_rw[1] == ((1 << 2) | 0));
    CHECK(M.last_rw[2] == 5 && M.last_rw[3] == 1);
    CHECK(M.last_rw[4] == 3);                   /* R */
    CHECK(M.last_rw[5] == 3);                   /* N = 1024B */
    CHECK(M.last_rw[6] == 8);                   /* EOT = 3 + 6 - 1 */
    CHECK(D.bytes == 6 * 1024);
    CHECK(M.dma_len_mismatch == 0 && M.dma_cross == 0 && M.mt_used == 0);
    CHECK(M.cmd_while_masked == 0);
    for (i = 0; i < 6; i++) CHECK(check_bytes(buf + i * 1024, 0, 5, 1, 3 + i, 1024));

    /* 1.44MB: 18 × 512 = 9216B をちょうど 1 回で */
    fdc_set_media(0, FDC_MEDIA_2HD_1440);
    CHECK(fdc_read_sectors(0, 2, 0, 1, 18, &fdc_geom_144, buf) == 0);
    CHECK(M.last_rw[5] == 2 && M.last_rw[6] == 18 && D.bytes == 9216);
    CHECK(M.dma_len_mismatch == 0 && M.dma_cross == 0);
    CHECK(check_bytes(buf + 17 * 512, 0, 2, 0, 18, 512));

    /* 受け皿の [HW2]: 1 トラックの最大が 64KB 境界をまたがない */
    CHECK((((u32)(unsigned long)dma_buffer & FDC_DMA_BANK_MASK) + FDC_DMA_BUF_SIZE)
          <= FDC_DMA_BANK_SIZE);

    /* トラックをまたぐ・受け皿を越える引数は I/O の前に断る */
    i = M.reads;
    CHECK(fdc_read_sectors(0, 2, 0, 17, 3, &fdc_geom_144, buf) == -2);
    CHECK(fdc_read_sectors(0, 2, 0, 0, 1, &fdc_geom_144, buf) == -2);
    CHECK(fdc_read_sectors(0, 2, 0, 1, 0, &fdc_geom_144, buf) == -2);
    CHECK(M.reads == i);
    fdc_set_media(0, FDC_MEDIA_2HD_1232);
}

/* 同じシリンダならシークを省く (ヘッドが違っても)。違えばシークする。 */
static void fdc_seek_skip(void)
{
    static u8 buf[8 * 1024];
    int s0;

    model_boot(0);
    /* fdc_init は drv0 → drv1 の順に RECALIBRATE する。最後が drv1 なので
     * drv0 の値は「ドライブの切り替え」で捨てられている。 */
    CHECK(fdc_get_known_cyl(0) == -1);
    s0 = M.seeks;
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 1);
    CHECK(fdc_get_known_cyl(0) == 3);
    CHECK(fdc_read_sectors(0, 3, 1, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 1);                   /* ヘッドだけ違う: 省く */
    CHECK(fdc_read_sector_geom(0, 3, 0, 5, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 1);                   /* 単発も省く */
    CHECK(check_bytes(buf, 0, 3, 0, 5, 1024));
    CHECK(fdc_read_sectors(0, 4, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 2);                   /* シリンダが違う */
    CHECK(fdc_get_known_cyl(0) == 4);
    /* 書き込みも同じ仕組みに乗る (覚えた値を更新する) */
    CHECK(fdc_write_sector_geom(0, 4, 1, 2, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 2);
    CHECK(fdc_write_sector_geom(0, 9, 0, 1, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 3 && fdc_get_known_cyl(0) == 9);
    CHECK(M.pcn[0] == 9);
}

/* 覚えた値を捨てる条件 */
static void fdc_forget_rules(void)
{
    static u8 buf[8 * 1024];
    int s0;

    /* (a) まとめ読みの失敗 (リザルトまで読めた異常終了)。コマンドは終わって
     *     いるので DMA を閉じるだけでリセットはしない。覚えた値は捨てる。 */
    model_boot(0);
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    M.fail_multi = 1;
    s0 = M.resets;
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == -1);
    CHECK(D.masked == 1);                       /* DMA は閉じて戻る */
    CHECK(M.resets == s0);
    CHECK(fdc_get_known_cyl(0) == -1);
    CHECK(s_kprintf_lines > 0);                 /* 黙らない */
    M.fail_multi = 0;
    s0 = M.seeks;
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 1);                   /* シークし直す */

    /* (b) メディアの変更 */
    fdc_set_media(0, FDC_MEDIA_2HD_1232);
    CHECK(fdc_get_known_cyl(0) == -1);

    /* (c) ドライブの切り替え */
    model_boot(1);
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(fdc_read_sectors(1, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    s0 = M.seeks;
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0 + 1);

    /* (d) 単発の最終失敗 */
    model_boot(0);
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    M.bad_c = 3; M.bad_h = 0; M.bad_r = 2;
    CHECK(fdc_read_sector_geom(0, 3, 0, 2, &fdc_geom_2hd, buf) == -1);
    CHECK(fdc_get_known_cyl(0) == -1);
    CHECK(D.masked == 1);

    /* (e) IRQ が来ない (時間上限) — 待ちが有限に終わって捨てる */
    model_boot(0);
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    M.drop_irq_reads = 1;
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == -1);
    CHECK(fdc_get_known_cyl(0) != 3);
    CHECK(D.masked == 1);
    CHECK(M.pcn[0] == 0);                       /* リセット + RECALIBRATE まで済ませる */
    CHECK(fdc_get_known_cyl(0) == 0);

    /* (e2) まとめ読みが期限切れで落ち、回復の RECALIBRATE も落ちる (ドライブ
     * が外れた) — 覚えた値は残さない */
    model_boot(0);
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(fdc_get_known_cyl(0) == 3);
    M.drop_irq_reads = 1;
    M.present[0] = 0;
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) != 0);
    CHECK(fdc_get_known_cyl(0) == -1);
    M.present[0] = 1;

    /* (f) シークの失敗 — READ を出さずに失敗し、覚えた値は残さない */
    model_boot(0);
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    s0 = M.reads;
    M.fail_seeks = 1;
    {
        int r0 = M.resets;
        CHECK(fdc_read_sectors(0, 20, 0, 1, 8, &fdc_geom_2hd, buf) == -1);
        CHECK(M.reads == s0);
        CHECK(fdc_get_known_cyl(0) != 20);
        /* シークの失敗も回復 (リセット + RECALIBRATE) を通す (ラリー 2) */
        CHECK(M.resets == r0 + 1);
        CHECK(M.pcn[0] == 0 && fdc_get_known_cyl(0) == 0);
    }
    /* 次はシークし直して読める */
    CHECK(fdc_read_sectors(0, 20, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(check_bytes(buf, 0, 20, 0, 1, 1024));
    /* 同じシリンダの 2 回目は省かれる — 覚えた値が戻っている */
    s0 = M.seeks;
    CHECK(fdc_read_sectors(0, 20, 1, 1, 8, &fdc_geom_2hd, buf) == 0);
    CHECK(M.seeks == s0);
}

/* 覚えた値が実際とずれていても別のシリンダは読まない (WC で落ちて読み直す)。
 * 経路は diskio と同じ: fdc_track_read + 本物の fdc。 */
static int real_multi(void *ctx, int drv, int cyl, int head, int sect,
                      int count, const struct fdc_geom *g, void *buf)
{
    (void)ctx;
    return fdc_read_sectors(drv, cyl, head, sect, count, g, buf);
}
static int real_one(void *ctx, int drv, int cyl, int head, int sect,
                    const struct fdc_geom *g, void *buf)
{
    (void)ctx;
    return fdc_read_sector_geom(drv, cyl, head, sect, g, buf);
}
static u32 real_gen(void *ctx, int drv) { (void)ctx; return fdc_media_gen(drv); }
static u32 real_now(void *ctx) { (void)ctx; return s_now; }
static const struct fdc_track_ops REAL_OPS = {
    real_multi, real_one, fake_copy, real_gen, real_now, 0
};

static void fdc_end_to_end(void)
{
    static u8 buf[1024];
    const struct fdc_geom *g = &fdc_geom_2hd;
    u32 l;
    int ok = 1;

    /* FatFs の窓と同じ count=1 の連続 48 セクタ (3 シリンダ) */
    model_boot(0);
    cache_reset();
    for (l = 0; l < 48; l++) {
        int c = (int)(l / 16), h = (int)((l / 8) % 2), r = (int)(l % 8) + 1;
        if (fdc_track_read(&s_cache, &REAL_OPS, 0, g, l, 1, buf) != 0) ok = 0;
        else if (!check_bytes(buf, 0, c, h, r, 1024)) ok = 0;
    }
    CHECK(ok);
    CHECK(M.reads == 6 && M.multi_reads == 6);  /* 1 トラック 1 コマンド */
    CHECK(M.seeks == 3);                        /* 1 シリンダ 1 シーク */
    CHECK(M.dma_len_mismatch == 0 && M.dma_cross == 0 && M.mt_used == 0);

    /* 裏でヘッドが動いた (V86 の BIOS など): 覚えた 2 と実際の 40 がずれる */
    fdc_track_invalidate(&s_cache);
    M.pcn[0] = 40;
    CHECK(fdc_track_read(&s_cache, &REAL_OPS, 0, g, 32 + 3, 1, buf) == 0);
    CHECK(check_bytes(buf, 0, 2, 0, 4, 1024));
    CHECK(M.pcn[0] == 2);
}


/* ======================================================================== */
/*  (3) 2026-09-24 の NP21/W の結果とレビューを受けて足したもの            */
/* ======================================================================== */

/* FatFs (FF_FS_TINY=1) の読み方: 1KB ずつ 16B ずれて読むと、セクタを
 * 越えるたびに FAT のセクタ (C0 H0 の LBA 1) とデータのセクタが交互に来る。
 * FAT のセクタはセクタキャッシュに居続け、データはトラックごとに 1 回。 */
static void fat_data_interleave(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;
    u32 l;
    int i, fat_reads = 0;

    fake_reset(); cache_setup2(1, FDC_SECTOR_SLOTS);
    for (l = 100; l < 132; l++) {               /* データ 32 セクタ = 4 トラック */
        CHECK(read_and_verify(0, g, 1, 1) == 0);    /* FAT */
        CHECK(read_and_verify(0, g, l, 1) == 0);    /* データ */
    }
    for (i = 0; i < F.n && i < CALL_MAX; i++) {
        CHECK(F.c[i].multi);
        if (F.c[i].cyl == 0 && F.c[i].head == 0) fat_reads++;
    }
    CHECK(fat_reads == 1);                      /* FAT のトラックは 1 回だけ */
    /* データ: lba 100 = C6 H0 R5 から R8 (1 回)、C6 H1、C7 H0、C7 H1、
     * C8 H0 (lba 128..131) の 5 回 + FAT 1 回 */
    CHECK(F.n == 6);
    CHECK(s_cache.sec_hits >= 31);              /* 2 回目からの FAT は全部 */

    /* 本物の fdc.c で同じ読み方をしたときのシーク: FAT (C0) 1 回、
     * データ C6 / C7 / C8 で 3 回。期限切れは 0。 */
    {
        static u8 one[1024];
        struct fdc_stats st;
        int ok = 1;

        model_boot(0);
        cache_setup2(1, FDC_SECTOR_SLOTS);
        for (l = 100; l < 132; l++) {
            if (fdc_track_read(&s_cache, &REAL_OPS, 0, g, 1, 1, one) != 0) ok = 0;
            if (fdc_track_read(&s_cache, &REAL_OPS, 0, g, l, 1, one) != 0) ok = 0;
            else if (!check_bytes(one, 0, (int)(l / 16), (int)((l / 8) % 2),
                                  (int)(l % 8) + 1, 1024)) ok = 0;
        }
        fdc_get_stats(&st);
        CHECK(ok);
        CHECK(st.seek_issued == 4);
        CHECK(st.seek_timeout == 0);
        CHECK(M.reads == 6);
    }

    /* セクタキャッシュ無し・1 本だけなら取り合って毎回読む
     * (9ed7c80 の姿を記録しておく) */
    fake_reset(); cache_setup2(1, 0);
    for (l = 100; l < 104; l++) {
        CHECK(read_and_verify(0, g, 1, 1) == 0);
        CHECK(read_and_verify(0, g, l, 1) == 0);
    }
    CHECK(F.n == 8);
}

/* 世代が進んだら (書き込み・Ready 変化・メディアの変更) トラックもセクタも
 * 当てない。セクタキャッシュは LRU (毎回使うセクタは追い出さない)。 */
static void gen_and_lru(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;
    u32 k;

    /* 世代: トラック */
    fake_reset(); cache_setup2(1, 0);
    CHECK(read_and_verify(0, g, 0, 1) == 0);
    CHECK(F.n == 1);
    s_fake_gen++;
    CHECK(read_and_verify(0, g, 1, 1) == 0);
    CHECK(F.n == 2);
    CHECK(read_and_verify(0, g, 2, 1) == 0);    /* 新しい世代は当たる */
    CHECK(F.n == 2);

    /* 世代: セクタ (トラックは別のものに入れ替えておく) */
    fake_reset(); cache_setup2(1, FDC_SECTOR_SLOTS);
    CHECK(read_and_verify(0, g, 5, 1) == 0);    /* C0H0 を埋め、LBA 5 を覚える */
    CHECK(read_and_verify(0, g, 100, 1) == 0);  /* トラックは C6H0 に */
    CHECK(read_and_verify(0, g, 5, 1) == 0);    /* セクタキャッシュで返す */
    CHECK(F.n == 2);
    s_fake_gen++;
    CHECK(read_and_verify(0, g, 5, 1) == 0);    /* 世代違い → 読み直す */
    CHECK(F.n == 3);

    /* LRU: 4 本のトラックを巡回するメタデータ (VFS の開き直し) の形。
     * A, B, C は毎回使い、データ D は毎回違うトラック。 */
    fake_reset(); cache_setup2(1, FDC_SECTOR_SLOTS);
    for (k = 0; k < 40; k++) {
        CHECK(read_and_verify(0, g, 5, 1) == 0);         /* ルート (C0H0) */
        CHECK(read_and_verify(0, g, 15, 1) == 0);        /* /SYS (C0H1) */
        CHECK(read_and_verify(0, g, 997, 1) == 0);       /* /SYS/FONT (C62H0) */
        CHECK(read_and_verify(0, g, 2, 1) == 0);         /* FAT */
        CHECK(read_and_verify(0, g, 998 + k, 1) == 0);   /* データ */
    }
    /* 1 巡目: A (C0H0)、B (C0H1)、C (C62H0)、FAT (C0H0 をもう一度 — トラック
     * は 1 本しか持たない)、D (C62H0 をもう一度) の 5 回。2 巡目からは
     * A・B・C・FAT がセクタキャッシュで返り、データだけがトラックを埋める:
     * C62H1 / C63H0 / C63H1 / C64H0 / C64H1 の 5 回 (998..1037)。
     * 巡回でメタデータが追い出されない (LRU)。 */
    CHECK(F.n == 10);
    CHECK(s_cache.sec_hits == 39 * 4);
}

/* SEEK の完了の前に別ドライブの通知 / 自ドライブの Ready 変化が積まれて
 * いても、**1 本のエッジで全部読む**ので期限切れを待たない。 */
static void seek_edge_foreign(void)
{
    static u8 buf[8 * 1024];
    struct fdc_stats st;

    model_boot(1);
    M.foreign_on_seek = 1;
    CHECK(fdc_read_sectors(0, 5, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    fdc_get_stats(&st);
    CHECK(st.seek_timeout == 0);
    CHECK(st.sis_foreign >= 1);
    CHECK(check_bytes(buf, 0, 5, 0, 1, 1024));

    model_boot(0);
    M.foreign_on_seek = 2;
    {
        u32 g0 = fdc_media_gen(0);
        CHECK(fdc_read_sectors(0, 6, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
        fdc_get_stats(&st);
        CHECK(st.seek_timeout == 0);
        CHECK(fdc_media_gen(0) != g0);          /* Ready 変化で世代が進む */
    }
}

/* 同じシリンダでシークを省くときも、**先に排水する**。取り残しの通知で
 * INT 線が上がったままだと、READ の完了にエッジが来ない。 */
static void drain_before_skip(void)
{
    static u8 buf[8 * 1024];
    struct fdc_stats st;

    model_boot(1);
    CHECK(fdc_read_sectors(0, 3, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    model_pend(0x21, 0);                        /* 取り残し (drv1 の完了) */
    model_line();
    CHECK(fdc_read_sectors(0, 3, 1, 1, 8, &fdc_geom_2hd, buf) == 0);
    fdc_get_stats(&st);
    CHECK(st.seek_skipped >= 1);
    CHECK(st.multi_fail == 0);
    CHECK(check_bytes(buf, 0, 3, 1, 1, 1024));
}

/* Ready 変化 (媒体の入れ替え) を SIS で見たら、持っている先読みを当てない。 */
static void readychange_invalidates(void)
{
    static u8 buf[1024];
    const struct fdc_geom *g = &fdc_geom_2hd;

    model_boot(0);
    cache_setup2(1, FDC_SECTOR_SLOTS);
    CHECK(fdc_track_read(&s_cache, &REAL_OPS, 0, g, 32, 1, buf) == 0);  /* C2 H0 */
    CHECK(check_bytes(buf, 0, 2, 0, 1, 1024));
    s_media_seed = 1;                           /* 入れ替え */
    model_pend(0xC0, 2);                        /* Ready 変化の通知 */
    model_line();
    CHECK(fdc_track_read(&s_cache, &REAL_OPS, 0, g, 80, 1, buf) == 0);  /* 別シリンダ */
    CHECK(fdc_track_read(&s_cache, &REAL_OPS, 0, g, 33, 1, buf) == 0);  /* C2 H0 R2 */
    CHECK(check_bytes(buf, 0, 2, 0, 2, 1024));  /* 新しい媒体の中身 */
}

/* NR (媒体無し) はまとめ読みの失敗に数えず、行も出さない。 */
static void multi_nr_quiet(void)
{
    static u8 buf[8 * 1024];
    struct fdc_stats st;

    /* (a) READ のリザルトの NR */
    model_boot(1);
    M.present[0] = 0;
    CHECK(fdc_read_sectors(0, 7, 0, 1, 8, &fdc_geom_2hd, buf) == -3);
    fdc_get_stats(&st);
    CHECK(st.multi_fail == 0);
    CHECK(st.multi_nr == 1);
    CHECK(s_kprintf_lines == 0);

    /* (b) SEEK の NR */
    model_boot(1);
    M.present[0] = 0;
    M.nr_on_seek = 1;
    CHECK(fdc_read_sectors(0, 7, 0, 1, 8, &fdc_geom_2hd, buf) == -3);
    fdc_get_stats(&st);
    CHECK(st.multi_fail == 0);
    CHECK(st.multi_nr == 1);
    CHECK(M.reads == 0);
    CHECK(s_kprintf_lines == 0);
}

/* diskio.c の結線: 読む → 書く → 読む。FatFs の disk_write でも、dev.c や
 * KAPI の dev_blk_write と同じ fdc_write_sector 直でも、古い先読みを返さない。
 * disk_initialize は (Ready 変化の無い) 媒体の入れ替えでも読み直させる。 */
static void diskio_rw(void)
{
    static u8 buf[1024], data[1024];
    int i;

    model_boot(0);
    diskio_set_fdd_drive(0);
    CHECK(disk_read(0, buf, 40, 1) == RES_NOTRDY);  /* 初期化の前 */
    CHECK(disk_initialize(0) == 0);
    CHECK(disk_read(0, buf, 40, 1) == RES_OK);      /* C2 H1 R1、トラックを持つ */
    CHECK(check_bytes(buf, 0, 2, 1, 1, 1024));
    CHECK(disk_read(0, buf, 47, 1) == RES_OK);      /* 同じトラックは読まない */
    CHECK(M.reads == 1 && M.multi_reads == 1);

    for (i = 0; i < 1024; i++) data[i] = (u8)(0x5A ^ i);
    CHECK(disk_write(0, data, 41, 1) == RES_OK);    /* 持っているトラックの中 */
    CHECK(disk_read(0, buf, 41, 1) == RES_OK);
    CHECK(memcmp(buf, data, 1024) == 0);

    CHECK(disk_read(0, buf, 42, 1) == RES_OK);      /* 同じトラックをまた持つ */
    for (i = 0; i < 1024; i++) data[i] = (u8)(0xC3 ^ i);
    CHECK(fdc_write_sector(0, 2, 1, 4, data) == 0); /* dev.c / KAPI の経路 */
    CHECK(disk_read(0, buf, 43, 1) == RES_OK);
    CHECK(memcmp(buf, data, 1024) == 0);

    /* 媒体の入れ替え (Ready 変化無し) → disk_initialize で読み直す */
    CHECK(disk_read(0, buf, 44, 1) == RES_OK);
    s_media_seed = 3;
    memset(s_written, 0, sizeof(s_written));
    CHECK(disk_initialize(0) == 0);
    CHECK(disk_read(0, buf, 45, 1) == RES_OK);
    CHECK(check_bytes(buf, 0, 2, 1, 6, 1024));
}


/* 受け皿の割り付け: DMA の窓は 64KB 境界をまたがず、先読みの領域と
 * 重ならない。境界が領域のどこにあっても取れる。 */
static void buf_layout(void)
{
    const u32 slot = FDC_DMA_BUF_SIZE, total = FDC_BUF_BYTES;
    u32 base, d, r;
    int bad = 0;

    for (base = 0x160000 - total - 64; base <= 0x160000 + 64; base += 4) {
        if (fdc_buf_layout(base, total, slot, &d, &r) != 0) { bad++; continue; }
        if ((((base + d) & 0xFFFF) + slot) > 0x10000) bad++;       /* またいだ */
        if (d + slot > total || r + (total - slot) > total) bad++;  /* はみ出し */
        if (!(d + slot <= r || r + (total - slot) <= d)) bad++;      /* 重なり */
    }
    CHECK(bad == 0);
    /* 境界が先頭の窓の中 → 窓は末尾 */
    CHECK(fdc_buf_layout(0x10000 - 100, total, slot, &d, &r) == 0);
    CHECK(d == total - slot && r == 0);
    /* 境界が領域の外 → 窓は先頭 */
    CHECK(fdc_buf_layout(0x20000, total, slot, &d, &r) == 0);
    CHECK(d == 0 && r == slot);
    /* 取れない引数 */
    CHECK(fdc_buf_layout(0, slot - 1, slot, &d, &r) != 0);
    CHECK(fdc_buf_layout(0, 0x10000, slot, &d, &r) != 0);
    CHECK(fdc_buf_layout(0, total, 0, &d, &r) != 0);
    /* 本物の領域も */
    CHECK(fdc_track_slot(0) != 0 && fdc_track_slot(FDC_TRACK_SLOTS) == 0);
    CHECK((((u32)(unsigned long)dma_buffer & 0xFFFF) + slot) <= 0x10000);
}



/* ======================================================================== */
/*  (5) ラリー 2 (Codex / Fable) の指摘                                     */
/* ======================================================================== */

/* RECALIBRATE の後も整定してから READ を出す: C=0 の読みが 1 回落ちると
 * 回復 (リセット + RECALIBRATE) → 0 を覚える → C=0 の SEEK は省かれる。 */
static void recal_settle(void)
{
    static u8 buf[1024];

    model_boot(0);
    CHECK(fdc_read_sector_geom(0, 0, 0, 1, &fdc_geom_2hd, buf) == 0);
    M.unsettled = 0;
    M.fail_reads = 1;                           /* 1 回だけ落ちる */
    CHECK(fdc_read_sector_geom(0, 0, 0, 2, &fdc_geom_2hd, buf) == 0);
    CHECK(M.recals >= 1);
    CHECK(M.unsettled == 0);
    CHECK(check_bytes(buf, 0, 0, 0, 2, 1024));
}

/* 2 秒規則: 最後の読みから FDC_TRACK_IDLE_TICKS を超えたら両方捨てる。
 * ちょうどの間隔までは当てる。 */
static void idle_rule(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;

    fake_reset(); cache_setup2(1, FDC_SECTOR_SLOTS);
    CHECK(read_and_verify(0, g, 5, 1) == 0);    /* トラック C0H0 + セクタ 5 */
    CHECK(F.n == 1);
    s_fake_now += FDC_TRACK_IDLE_TICKS;         /* ちょうど → まだ当てる */
    CHECK(read_and_verify(0, g, 5, 1) == 0);
    CHECK(read_and_verify(0, g, 6, 1) == 0);
    CHECK(F.n == 1);
    s_fake_now += FDC_TRACK_IDLE_TICKS + 1;     /* 超えた → 両方捨てる */
    s_media_seed = 7;                           /* その間に差し替えられた */
    CHECK(read_and_verify(0, g, 5, 1) == 0);    /* 新しい中身 (セクタも捨てた) */
    CHECK(F.n == 2);
    CHECK(read_and_verify(0, g, 7, 1) == 0);    /* 新しいトラックは当たる */
    CHECK(F.n == 2);
    CHECK(s_cache.idle_drops == 1);
    s_media_seed = 0;
}

/* NR (-3) は 1 セクタずつへ落とさずに打ち切る。 */
static void track_nr_stop(void)
{
    const struct fdc_geom *g = &fdc_geom_2hd;
    static u8 buf[1024];

    fake_reset(); cache_setup2(1, FDC_SECTOR_SLOTS);
    F.nr = 1;
    CHECK(fdc_track_read(&s_cache, &FAKE_OPS, 0, g, 3, 1, buf) != 0);
    CHECK(F.n == 1 && F.c[0].multi);            /* 単発は出ない */
}

/* 単発の READ の NR も回復 (リセット / RECALIBRATE) を通さない。 */
static void single_nr_no_recover(void)
{
    static u8 buf[1024];
    int r0, c0;

    model_boot(0);
    CHECK(fdc_read_sector_geom(0, 5, 0, 1, &fdc_geom_2hd, buf) == 0);
    M.present[0] = 0;
    r0 = M.resets; c0 = M.recals;
    CHECK(fdc_read_sector_geom(0, 5, 0, 2, &fdc_geom_2hd, buf) == -1);
    CHECK(M.resets == r0 && M.recals == c0);
    CHECK(M.reads == 2);                        /* リトライもしない */
    CHECK(D.masked == 1);
    M.present[0] = 1;
}

/* 単発の WRITE の NR も読みと同じく回復とリトライを通さない。 */
static void write_nr_no_recover(void)
{
    static u8 buf[1024];
    int r0, c0, w0;

    memset(buf, 0x5A, sizeof(buf));
    model_boot(0);
    CHECK(fdc_write_sector_geom(0, 5, 0, 1, &fdc_geom_2hd, buf) == 0);
    M.present[0] = 0;
    r0 = M.resets; c0 = M.recals; w0 = M.writes;
    CHECK(fdc_write_sector_geom(0, 5, 0, 2, &fdc_geom_2hd, buf) == -1);
    CHECK(M.resets == r0 && M.recals == c0);
    CHECK(M.writes == w0 + 1);                  /* リトライもしない */
    CHECK(D.masked == 1);
    M.present[0] = 1;
}

/* 1 本のエッジで件数の上限 (4) ちょうどの別の通知が先に積まれていても、
 * エッジを待たずに読み続けて期限切れにならない。 */
static void sis_edge_limit(void)
{
    static u8 buf[8 * 1024];
    struct fdc_stats st;

    model_boot(1);
    M.foreign_on_seek = 3;
    CHECK(fdc_read_sectors(0, 9, 0, 1, 8, &fdc_geom_2hd, buf) == 0);
    fdc_get_stats(&st);
    CHECK(st.seek_timeout == 0);
    CHECK(st.sis_foreign == 4);
}

/* ======================================================================== */
/*  (4) 実物のフォントの読み方の再現 (2026-09-24 夕、6541ef1 の実測を受けて)*/
/*                                                                          */
/*  イメージ: images/os32_boot.d88 を LBA 順に直したもの (試験の Python が   */
/*  作って FDC_TRACK_IMAGE で渡す)。期待する中身は Python が FAT をたどって  */
/*  取り出したファイル (FDC_TRACK_FONT)、その FatFs のパスは FDC_TRACK_PATH。*/
/*  2026-09-25 に既定フォントが MINIMAL から外れて FD に載らなくなったので、*/
/*  同じ読み方を FD に残る大きいファイル (/sys/unicode.bin) で回す。         */
/*  読み方: kernel/boot_font.c → drivers/kcg.c の kcg_load_font と           */
/*  fs/vfs_fd.c / fs/fatfs_vfs.c のとおり:                                  */
/*    vfs_open        = stat + get_file_size (どちらも f_stat)               */
/*    vfs_read_fd(16) = read_stream = f_open → f_lseek → f_read → f_close   */
/*    vfs_get_size    = f_stat                                               */
/*    kcg_read_chunked = 1024B ずつ read_stream                              */
/*  read_stream の形が変わっていないことは Python 側が静的に見る。          */
/* ======================================================================== */
static u8 *s_font;
static long s_font_len;
static const char *s_font_path;
/* 上限 (FDC_TRACK_SEEK_MAX / FDC_TRACK_MULTI_MAX)。無ければ 0 = 失敗にする */
static long s_seek_max, s_multi_max;

static int replay_read_stream(void *buf, u32 size, u32 offset)
{
    FIL fil;
    FRESULT fr;
    UINT br;

    fr = f_open(&fil, s_font_path, FA_READ);
    if (fr != FR_OK) return -1;
    fr = f_lseek(&fil, (FSIZE_t)offset);
    if (fr != FR_OK) { f_close(&fil); return -1; }
    fr = f_read(&fil, buf, size, &br);
    f_close(&fil);
    if (fr != FR_OK) return -1;
    return (int)br;
}

static void font_replay(void)
{
    static FATFS fs;
    static u8 hdr[16], chunk[1024];
    FILINFO fno;
    struct fdc_stats st;
    long off, total;
    int ok = 1, n;

    if (!s_image || !s_font || !s_font_path) {
        fprintf(stderr, "font_replay: FDC_TRACK_IMAGE / FDC_TRACK_FONT / "
                "FDC_TRACK_PATH が無い\n");
        failed++;
        return;
    }
    model_boot(0);
    diskio_set_fdd_drive(0);
    CHECK(f_mount(&fs, "0:", 1) == FR_OK);
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&fdd_track.sec_hits, 0, 3 * sizeof(u32));
    M.seeks = 0; M.reads = 0; M.multi_reads = 0;

    /* vfs_open: stat + get_file_size */
    CHECK(f_stat(s_font_path, &fno) == FR_OK);
    CHECK(f_stat(s_font_path, &fno) == FR_OK);
    CHECK((long)fno.fsize == s_font_len);
    /* ヘッダ 16B */
    CHECK(replay_read_stream(hdr, 16, 0) == 16);
    CHECK(memcmp(hdr, s_font, 16) == 0);
    /* vfs_get_size */
    CHECK(f_stat(s_font_path, &fno) == FR_OK);
    /* 本体を 1024B ずつ */
    total = s_font_len - 16;
    for (off = 0; off < total; off += n) {
        int want = (total - off > 1024) ? 1024 : (int)(total - off);
        n = replay_read_stream(chunk, (u32)want, (u32)(16 + off));
        if (n != want || memcmp(chunk, s_font + 16 + off, (size_t)want) != 0) {
            ok = 0;
            break;
        }
    }
    CHECK(ok);

    fdc_get_stats(&st);
    printf("font_replay: bytes=%ld seek=%u skip=%u multi=%u/%u single=%u "
           "tmo=%u sec_hit=%u trk_hit=%u fill=%u (model READ=%d SEEK=%d)\n",
           s_font_len, (unsigned)st.seek_issued, (unsigned)st.seek_skipped,
           (unsigned)st.multi_ok, (unsigned)st.multi_fail,
           (unsigned)st.single_reads, (unsigned)st.seek_timeout,
           (unsigned)fdd_track.sec_hits, (unsigned)fdd_track.trk_hits,
           (unsigned)fdd_track.fills, M.reads, M.seeks);
    /* 上限は**実物のイメージの配置から** test_fdc_track.py の font_bounds が
     * 出して渡す (PM 判断 2026-09-24)。シーク ≦ データのシリンダ数 + データの
     * 外のメタデータのシリンダ数 + FAT への往復、まとめ読み ≦ データのトラック数
     * + データの外のメタデータのトラック数 + 往復の読み直し。以前の 14 / 27 は
     * 1 つの配置に合わせた固定値で、ローダが 1 クラスタ増えただけで落ちた。 */
    CHECK(s_seek_max > 0 && s_multi_max > 0);
    CHECK((long)st.multi_ok <= s_multi_max);
    CHECK(st.multi_fail == 0);
    CHECK((long)st.seek_issued <= s_seek_max);
    CHECK(st.seek_timeout == 0);
    CHECK(st.single_reads == 0);
    CHECK(M.reads == (int)st.multi_ok);
}

static u8 *load_file(const char *env, long *len)
{
    const char *path = getenv(env);
    FILE *f;
    u8 *b;
    if (!path) return (u8 *)0;
    f = fopen(path, "rb");
    if (!f) return (u8 *)0;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (u8 *)malloc((size_t)*len + 1);
    if (b && fread(b, 1, (size_t)*len, f) != (size_t)*len) { free(b); b = 0; }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    int i;
    static const struct { const char *name; void (*fn)(void); } cases[] = {
        { "split_2hd", split_2hd },
        { "split_144", split_144 },
        { "readahead_count1", readahead_count1 },
        { "cross_boundary", cross_boundary },
        { "fallback_single", fallback_single },
        { "single_fail", single_fail },
        { "cache_rules", cache_rules },
        { "oversize_geom", oversize_geom },
        { "timeout_math", timeout_math },
        { "fdc_multi_cmd", fdc_multi_cmd },
        { "fdc_seek_skip", fdc_seek_skip },
        { "fdc_forget_rules", fdc_forget_rules },
        { "fdc_end_to_end", fdc_end_to_end },
        { "fat_data_interleave", fat_data_interleave },
        { "gen_and_lru", gen_and_lru },
        { "seek_edge_foreign", seek_edge_foreign },
        { "drain_before_skip", drain_before_skip },
        { "readychange_invalidates", readychange_invalidates },
        { "multi_nr_quiet", multi_nr_quiet },
        { "diskio_rw", diskio_rw },
        { "buf_layout", buf_layout },
        { "font_replay", font_replay },
        { "recal_settle", recal_settle },
        { "idle_rule", idle_rule },
        { "track_nr_stop", track_nr_stop },
        { "single_nr_no_recover", single_nr_no_recover },
        { "write_nr_no_recover", write_nr_no_recover },
        { "sis_edge_limit", sis_edge_limit },
    };
    if (argc != 2) { fprintf(stderr, "usage: %s CASE\n", argv[0]); return 2; }
    if (strcmp(argv[1], "font_replay") == 0) {
        s_image = load_file("FDC_TRACK_IMAGE", &s_image_len);
        s_font = load_file("FDC_TRACK_FONT", &s_font_len);
        s_font_path = getenv("FDC_TRACK_PATH");
        s_seek_max  = getenv("FDC_TRACK_SEEK_MAX") ? atol(getenv("FDC_TRACK_SEEK_MAX")) : 0;
        s_multi_max = getenv("FDC_TRACK_MULTI_MAX") ? atol(getenv("FDC_TRACK_MULTI_MAX")) : 0;
    }
    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            cases[i].fn();
            return failed ? 1 : 0;
        }
    }
    fprintf(stderr, "unknown case %s\n", argv[1]);
    return 2;
}
