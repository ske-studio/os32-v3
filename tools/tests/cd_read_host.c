/* ========================================================================
 *  cd_read_host.c — CD の読み (drivers/atapi.c + fs/iso9660.c) のホスト試験
 *
 *  実行:  python3 -B tools/tests/test_cd_read.py [--target] [--mutate] [case ...]
 *  記録:  tools/tests/cd_read_tdd.md
 *
 *  実物の drivers/atapi.c と fs/iso9660.c を 1 行も写さずに #include する。
 *  atapi.c のポートは tools/tests/atapi_hostshim/io.h がここの ATAPI デバイスの
 *  模型へ回す。模型は PACKET (0xA0) → 12 バイトの CDB → DRQ ごとのデータ、を
 *  レジスタの粒度で写し、READ(10) の回数・セクタ数・(LBA, 数) の列を数える。
 *  iso9660.c の下の dev 層 (dev_find / dev_blk_read_lba) は cd0 だけの贋物で、
 *  そのまま atapi_read_sectors へ渡す (drivers/dev.c の cd0_read と同じ)。
 *  媒体は試験が組む ISO 9660 のイメージ (PVD・根・SUB・ファイル)。
 *
 *  [C1] C89 / GNU89。
 * ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "vfs.h"
#include "dev.h"
#include "kmalloc.h"
#include "lib/kstring.h"
#include "cpu_calibrate.h"

/* ---- 境界の贋物 (kstring / kmalloc / tick) ---- */
volatile u32 tick_count;
static int g_kmalloc_fail;   /* 1 なら kmalloc (先読みの窓) を断る */

u32 kstrlen(const char *s) { return (u32)strlen(s); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
char *kstrncpy(char *dst, const char *src, u32 n)
{
    /* n はバッファ全体の大きさ (lib/kstring.h の注記) */
    if (n == 0) return dst;
    strncpy(dst, src, (size_t)n - 1);
    dst[n - 1] = '\0';
    return dst;
}
void *kmemcpy(void *dst, const void *src, u32 n) { return memcpy(dst, src, (size_t)n); }
void *kmalloc(u32 size) { return g_kmalloc_fail ? (void *)0 : malloc((size_t)size); }
/* 校正済みの µs 待ちの贋物: 待った合計を数えるだけ (NOT READY の出し直しの間、
 * SRST の後)。実物 (kernel/cpu_calibrate.c) どおり 1 回 CPU_DELAY_US_MAX (100ms) で
 * 丸める — 丸めを知らずに 250ms を 1 回で頼む書き方はここで待ちが足りなくなる */
static unsigned long g_delay_us;
static unsigned long g_delay_calls;
void cpu_delay_us(u32 us)
{
    if (us > CPU_DELAY_US_MAX) us = CPU_DELAY_US_MAX;
    g_delay_us += us;
    g_delay_calls++;
}
void *kzalloc(u32 size) { return calloc(1, (size_t)size); }
void kfree(void *p) { free(p); }

/* kprintf の贋物: 出した行を溜める ([atapi] の診断の行を見る) */
static char g_klog[8192];
static size_t g_klog_n;
void kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    (void)attr;
    if (g_klog_n >= sizeof(g_klog) - 1) return;
    va_start(ap, fmt);
    g_klog_n += (size_t)vsnprintf(g_klog + g_klog_n, sizeof(g_klog) - g_klog_n, fmt, ap);
    va_end(ap);
    if (g_klog_n > sizeof(g_klog) - 1) g_klog_n = sizeof(g_klog) - 1;
}

/* ---- 実物 ---- */
#include "../../drivers/atapi.c"
#include "../../fs/iso9660.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

/* ======================================================================== */
/*  ISO 9660 のイメージ                                                      */
/* ======================================================================== */

#define SEC        2048u
#define ROOT_LBA   20u
#define SUB_LBA    21u        /* SUB は 2 セクタ (21, 22) */
#define SUB_LBA_B  24u        /* 媒体 B の SUB (24, 25)。B の 21, 22 には囮の SUB */
#define TINY_LBA   30u
#define DEEP_LBA   32u
#define BIG_LBA_A  40u        /* 媒体 A の BIG.PKG */
#define BIG_LBA_B  45u        /* 媒体 B (入れ替え後) の BIG.PKG */
#define TINY_SIZE  100u
#define DEEP_SIZE  5000u
#define DEEP_SIZE_B 6000u     /* 媒体 B の DEEP.BIN */
/* 媒体 B の LBA 21, 22 は囮の SUB で、2 セクタ目は空 (DEEP.BIN が無い)。
 * 2 セクタ目を読んでも cb が呼ばれないので、読んだ後の世代の確認だけが頼り */
#define BIG_SIZE_DEFAULT (600u * SEC + 777u)   /* 601 セクタ、末尾は半端 */

static u32 g_big_size = BIG_SIZE_DEFAULT;

typedef struct {
    u8  *img;
    u32  secs;
    u32  big_lba;
    u32  sub_lba;
    u32  deep_size;
    u8   salt;      /* 中身の模様を媒体ごとに変える */
} Media;

static Media g_media_a, g_media_b;

static u8 pattern(u8 salt, u32 lba, u32 off)
{
    u32 v = lba * 2654435761u + off * 40503u + (off >> 9) + salt * 97u;
    return (u8)(v ^ (v >> 13) ^ (v >> 24));
}

static void put_both32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
    p[4] = (u8)(v >> 24); p[5] = (u8)(v >> 16); p[6] = (u8)(v >> 8); p[7] = (u8)v;
}

/* ディレクトリレコードを 1 つ書き、長さを返す */
static u32 put_rec(u8 *p, u32 lba, u32 size, u8 flags, const char *name, int nlen)
{
    u32 len = 33u + (u32)nlen;
    if (len & 1u) len++;
    memset(p, 0, len);
    p[0] = (u8)len;
    put_both32(p + 2, lba);
    put_both32(p + 10, size);
    p[25] = flags;
    p[28] = 1; p[31] = 1;          /* volume sequence number */
    p[32] = (u8)nlen;
    memcpy(p + 33, name, (size_t)nlen);
    return len;
}

/* 2 セクタの SUB を at に書く (記録上の自分の位置は self)。1 セクタ目は詰め物、
 * DEEP.BIN (deep_size) は 2 セクタ目。deep_size = 0 なら 2 セクタ目は空 */
static void put_sub(u8 *img, u32 at, u32 self, u32 deep_size)
{
    u8 *d = img + at * SEC;
    u32 o = 0, i;
    memset(d, 0, 2u * SEC);
    o += put_rec(d + o, self, 2u * SEC, ISO_FLAG_DIRECTORY, "\0", 1);
    o += put_rec(d + o, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\1", 1);
    for (i = 0; o + 48u <= SEC; i++) {
        char nm[16];
        sprintf(nm, "F%04u.TXT;1", (unsigned)i);
        o += put_rec(d + o, TINY_LBA, TINY_SIZE, 0, nm, 11);
    }
    if (deep_size) put_rec(d + SEC, DEEP_LBA, deep_size, 0, "DEEP.BIN;1", 10);
}

static void build_media(Media *m, u32 big_lba, u32 sub_lba, u32 deep_size, u8 salt)
{
    u32 big_secs = (g_big_size + SEC - 1) / SEC;
    u8 *d;
    u32 o, i, k;

    m->big_lba = big_lba;
    m->sub_lba = sub_lba;
    m->deep_size = deep_size;
    m->salt = salt;
    m->secs = big_lba + big_secs;    /* BIG.PKG が媒体の最後 (その先は読めない) */
    m->img = (u8 *)calloc(m->secs, SEC);
    CHECK(m->img != NULL);

    /* データ (全セクタに模様。ファイルの中身もこの模様の一部) */
    for (i = 0; i < m->secs; i++)
        for (k = 0; k < SEC; k++) m->img[i * SEC + k] = pattern(salt, i, k);

    /* PVD (LBA 16) */
    d = m->img + 16u * SEC;
    memset(d, 0, SEC);
    d[0] = 1; memcpy(d + 1, "CD001", 5); d[6] = 1;
    put_both32(d + 80, m->secs);
    d[128] = (u8)(SEC & 0xFF); d[129] = (u8)(SEC >> 8);
    put_rec(d + 156, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\0", 1);

    /* 根 */
    d = m->img + ROOT_LBA * SEC;
    memset(d, 0, SEC);
    o = 0;
    o += put_rec(d + o, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\0", 1);
    o += put_rec(d + o, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\1", 1);
    o += put_rec(d + o, big_lba, g_big_size, 0, "BIG.PKG;1", 9);
    o += put_rec(d + o, sub_lba, 2u * SEC, ISO_FLAG_DIRECTORY, "SUB", 3);
    o += put_rec(d + o, TINY_LBA, TINY_SIZE, 0, "TINY.TXT;1", 10);

    /* SUB。SUB が 21 でない媒体では、21, 22 に中身の違う囮の SUB を置く
     * (旧媒体の根で得た LBA 21 を新媒体で読むと、別の答えになる) */
    put_sub(m->img, sub_lba, sub_lba, deep_size);
    if (sub_lba != SUB_LBA) put_sub(m->img, SUB_LBA, SUB_LBA, 0);
}

/* 媒体 m の LBA lba から始まるファイルの off バイト目 */
static u8 file_byte(const Media *m, u32 lba, u32 off)
{
    return m->img[lba * SEC + off];
}

/* ======================================================================== */
/*  ATAPI デバイスの模型                                                    */
/* ======================================================================== */

#define ST_BSY  0x80
#define ST_DRDY 0x40
#define ST_DRQ  0x08
#define ST_ERR  0x01
#define LOG_MAX 8192

static struct {
    Media *m;          /* いま入っている媒体 */
    int bank;
    u8  status;
    u8  err;
    u16 bcl;           /* ホストが書いた byte count limit */
    int phase;         /* 0 = 待ち, 1 = CDB, 2 = データ */
    u8  cdb[12];
    int cdb_words;
    const u8 *data;
    u32 data_len;
    u32 pos;
    u32 blk_end;
    u32 blk_size;
    int lag;           /* ブロックの境目の後、古い状態を見せる回数 */
    u8  lag_status;
    u8  cap[9];
    /* 設定 */
    u32 drq_max;       /* 1 回の DRQ の上限 (0 = bcl どおり) */
    int fail_multi;    /* count > 1 の READ(10) を MEDIUM ERROR で断る */
    long fail_lba;     /* この LBA を含む READ(10) を断る (-1 = なし) */
    int short_multi;   /* count > 1 の READ(10) で 1 セクタ少なく渡す */
    int ua_next;       /* 次のこの数のコマンドで UNIT ATTENTION を返し、媒体を B へ */
    int stale;         /* 境目の後の古い状態の回数 */
    int cap_nodata;    /* READ CAPACITY がデータ無しで終わる */
    u32 cap_len;       /* READ CAPACITY の応答の長さ (0 = 8)。7 / 9 は規定外の装置 */
    /* 数 */
    u32 n_packets;
    u32 n_read10;
    u32 n_read10_multi;
    u32 n_sectors;
    u32 max_count;
    u32 max_bcl;
    int bcl_odd;
    u32 log_lba[LOG_MAX];
    u32 log_cnt[LOG_MAX];
    u32 n_log;
} M;

static void model_reset_counts(void)
{
    M.n_packets = M.n_read10 = M.n_read10_multi = M.n_sectors = 0;
    M.max_count = M.max_bcl = 0;
    M.bcl_odd = 0;
    M.n_log = 0;
}

static u32 lba_reads(u32 lba)   /* lba を含む READ(10) の数 */
{
    u32 i, n = 0;
    for (i = 0; i < M.n_log; i++)
        if (lba >= M.log_lba[i] && lba - M.log_lba[i] < M.log_cnt[i]) n++;
    return n;
}

static void model_error(u8 sense)
{
    M.status = ST_DRDY | ST_ERR;
    M.err = (u8)(sense << 4);
    M.phase = 0;
}

static void model_start_block(void)
{
    u32 limit = M.bcl & ~1u;
    u32 left = M.data_len - M.pos;
    if (M.drq_max && M.drq_max < limit) limit = M.drq_max;
    if (limit == 0) limit = 2;
    M.blk_size = (left < limit) ? left : limit;
    M.blk_end = M.pos + M.blk_size;
    M.status = ST_DRDY | ST_DRQ;
    M.phase = 2;
}

static void model_exec(void)
{
    u8 op = M.cdb[0];
    M.err = 0;
    if (M.ua_next > 0) {
        M.ua_next--;
        M.m = &g_media_b;
        model_error(ATAPI_SK_UNIT_ATTENTION);
        if (op == SCSI_CMD_READ_10) M.n_read10++;
        return;
    }
    if (op == SCSI_CMD_READ_10) {
        u32 lba = ((u32)M.cdb[2] << 24) | ((u32)M.cdb[3] << 16)
                | ((u32)M.cdb[4] << 8) | M.cdb[5];
        u32 cnt = ((u32)M.cdb[7] << 8) | M.cdb[8];
        M.n_read10++;
        if (cnt > 1) M.n_read10_multi++;
        if (cnt > M.max_count) M.max_count = cnt;
        if (M.n_log < LOG_MAX) {
            M.log_lba[M.n_log] = lba;
            M.log_cnt[M.n_log] = cnt;
            M.n_log++;
        }
        if (cnt == 0 || lba + cnt > M.m->secs
            || (M.fail_multi && cnt > 1)
            || (M.fail_lba >= 0 && (u32)M.fail_lba >= lba
                && (u32)M.fail_lba - lba < cnt)) {
            model_error(0x03);   /* MEDIUM ERROR */
            return;
        }
        M.data = M.m->img + lba * SEC;
        M.data_len = cnt * SEC;
        if (M.short_multi && cnt > 1) M.data_len -= SEC;
        M.n_sectors += M.data_len / SEC;
        M.pos = 0;
        model_start_block();
        return;
    }
    if (op == SCSI_CMD_READ_CAPACITY && M.cap_nodata) {
        M.status = ST_DRDY;
        M.phase = 0;
        return;
    }
    if (op == SCSI_CMD_READ_CAPACITY) {
        u32 last = M.m->secs - 1;
        M.cap[0] = (u8)(last >> 24); M.cap[1] = (u8)(last >> 16);
        M.cap[2] = (u8)(last >> 8);  M.cap[3] = (u8)last;
        M.cap[4] = 0; M.cap[5] = 0; M.cap[6] = (u8)(SEC >> 8); M.cap[7] = 0;
        M.cap[8] = 0xAA;   /* 9 バイト目 (規定外の装置が余分に渡す屑) */
        M.data = M.cap;
        M.data_len = M.cap_len ? M.cap_len : 8;
        M.pos = 0;
        model_start_block();
        return;
    }
    if (op == SCSI_CMD_TEST_UNIT_READY) {
        M.status = ST_DRDY;
        M.phase = 0;
        return;
    }
    model_error(0x05);   /* ILLEGAL REQUEST */
}

static u8 model_status(void)
{
    if (M.lag > 0) { M.lag--; return M.lag_status; }
    return M.status;
}

static unsigned int m_inp(unsigned int port)
{
    switch (port) {
    case IDE_STATUS:
    case IDE_ALT_STATUS: return model_status();
    case IDE_ERROR:      return M.err;
    case IDE_CYL_LO:     return (M.phase == 2) ? (M.blk_size & 0xFF) : (M.bcl & 0xFF);
    case IDE_CYL_HI:     return (M.phase == 2) ? ((M.blk_size >> 8) & 0xFF) : (M.bcl >> 8);
    case IDE_SECT_CNT:   return (M.phase == 2) ? ATAPI_IR_IO : (ATAPI_IR_IO | ATAPI_IR_CD);
    default:             return 0;
    }
}

static void m_outp(unsigned int port, unsigned int value)
{
    switch (port) {
    case IDE_BANK1:  M.bank = (int)(value & 1); break;
    case IDE_CYL_LO: M.bcl = (u16)((M.bcl & 0xFF00) | (value & 0xFF)); break;
    case IDE_CYL_HI: M.bcl = (u16)((M.bcl & 0x00FF) | ((value & 0xFF) << 8)); break;
    case IDE_COMMAND:
        if (value == ATAPI_CMD_PACKET) {
            CHECK(M.bank == 1);            /* セカンダリを選んでから */
            if (M.bcl > M.max_bcl) M.max_bcl = M.bcl;
            if (M.bcl & 1) M.bcl_odd = 1;
            CHECK(M.bcl != 0);             /* 0 は規定外 (16 ビットに入らない値を書いた) */
            M.n_packets++;
            M.phase = 1;
            M.cdb_words = 0;
            M.status = ST_DRDY | ST_DRQ;
        }
        break;
    default: break;
    }
}

static unsigned int m_inpw(unsigned int port)
{
    unsigned int w;
    if (port != IDE_DATA) return 0xFFFF;
    /* 境目の直後 (デバイスがまだ切り替えている間) や、データの外は屑 */
    if (M.lag > 0 || M.phase != 2 || M.pos >= M.blk_end) return 0xFFFF;
    w = (unsigned int)M.data[M.pos];
    w |= (M.pos + 1 < M.data_len) ? ((unsigned int)M.data[M.pos + 1] << 8) : 0;
    M.pos += 2;
    if (M.pos > M.data_len) M.pos = M.data_len;
    if (M.pos >= M.blk_end) {
        /* ブロックの終わり。しばらく古い状態 (DRQ) を見せる */
        M.lag = M.stale;
        M.lag_status = M.status;
        if (M.pos < M.data_len) {
            model_start_block();
        } else {
            M.status = ST_DRDY;
            M.phase = 0;
        }
    }
    return w;
}

static void m_outpw(unsigned int port, unsigned int value)
{
    if (port != IDE_DATA || M.phase != 1) return;
    M.cdb[M.cdb_words * 2] = (u8)value;
    M.cdb[M.cdb_words * 2 + 1] = (u8)(value >> 8);
    if (++M.cdb_words == 6) model_exec();
}

/* ======================================================================== */
/*  NP21/W の ATAPI の写し (np21w-src/src/cbus/ideio.c + atapicmd.c)         */
/*                                                                          */
/*  同期の経路 (CD_ASYNC 無し) をレジスタの粒度でそのまま写す。上の模型は    */
/*  試験のために意地悪 (古い DRQ・屑) にしてあるが、こちらは「NP21/W で      */
/*  実際にどう返るか」の写しで、2026-09-26 の「NP21/W で 1 セクタも読めない」 */
/*  の回帰を見る。g_np2 = 1 のときだけ使う。                                */
/* ======================================================================== */

#define NP2_STAT_BSY  0x80
#define NP2_STAT_DRDY 0x40
#define NP2_STAT_DSC  0x10
#define NP2_STAT_DRQ  0x08
#define NP2_STAT_CHK  0x01
#define NP2_INTR_CD   0x01
#define NP2_INTR_IO   0x02
#define NP2_TC_END    0
#define NP2_TC_READ   1

static int g_np2;
typedef struct {
    int present;         /* device != IDETYPE_NONE */
    Media *media;        /* 入っている媒体 (loaded のとき) */
    u8  status, error, sc, sk, cmd;
    u16 cy;
    u8  buf[2048];
    u32 bufpos, bufsize;
    int bufdir_in;
    int buftc;
    u32 sector, nsectors;
    int loaded;          /* IDEIO_MEDIA_LOADED */
    int changed;         /* IDEIO_MEDIA_CHANGED */
    int async_lag;       /* CD_ASYNC: 1 セクタの読みの後、BSY のまま見せる回数 */
    int busy_left;
    /* ---- 実機寄りの振る舞い (NP21/W には無い。strict のときの試験用) ---- */
    u8  asc;             /* REQUEST SENSE が返す ASC (NP21/W の drv->asc の下位) */
    int ua;              /* UNIT ATTENTION が立っている (次のコマンドは sk 6) */
    int ua_needs_sense;  /* 1 = REQUEST SENSE でだけ消える (0 = 報告で消える) */
    int ua_on_reset;     /* DEVICE RESET / SRST の後に UNIT ATTENTION を立てる */
    int ready_after;     /* この回数だけ NOT READY / ASC 04h (準備中) を返してから使える */
    int no_medium;       /* トレイが空: 毎回 NOT READY / ASC 3Ah (NP21/W は容量 0 を返す) */
    int stuck;           /* BSY のまま戻らない (DEVICE RESET / SRST で戻る) */
    int stuck_on_select; /* 選ばれた瞬間に固まる (1 回だけ) */
    long stuck_after_lba;/* このセクタを渡した後で stuck になる (-1 = なし) */
    int stuck_pending;
    int ignore_devreset; /* DEVICE RESET を無視する (SRST でだけ戻る) */
    int cy_written;      /* 選ばれてから Byte Count を書かれた (PACKET の前に要る) */
    u32 n_readcap;       /* 受けた READ CAPACITY の数 (断ったものも) */
    u32 n_reqsense;      /* 受けた REQUEST SENSE の数 */
    /* ---- 秒で見せる BSY (TASK_ATAPI_TIMEOUT)。時刻は np2_now() ---- */
    u8  ascq;            /* REQUEST SENSE が返す ASCQ */
    unsigned long busy_until;   /* np2_now() がこの値になるまで BSY */
    unsigned long spinup_us;    /* 次の READ(10) はこの時間 BSY のまま (回転の立ち上がり) */
    unsigned long srst_busy_us; /* SRST を解いた後この時間 BSY (0 = NP2_SRST_BUSY_READS 回) */
    unsigned long cdb_busy_us;  /* 次の PACKET は CDB を受ける DRQ までこの時間 BSY (1 回) */
    int dead;            /* BSY のまま戻らない (DEVICE RESET / SRST でも) */
    int die_on_packet;   /* CDB を受けたところで dead になる */
    int need_start;      /* START UNIT を受けるまで NOT READY / 04h/02h */
    int start_ignored;   /* START UNIT を受けても need_start が消えない */
    unsigned long start_busy_us; /* START UNIT (IMMED=0) が回り始めるまでの BSY */
    u32 n_startunit;     /* 受けた START STOP UNIT (開始) の数 */
} Np2Drv;

static struct {
    int bank;            /* ideio.bank[1] */
    int drivesel;        /* dev->drivesel (DRV_HEAD bit4) */
    u8  ctrl;
    Np2Drv d[2];         /* セカンダリのマスター / スレーブ (NP21/W の ide2 / ide3) */
    u32 n_read10, n_read10_multi;
    /* ---- 実機寄りの設定 (setup_np2_layout が g_np2cfg から写す) ---- */
    int strict;          /* 1 = BSY の装置はレジスタ書き込みを無視する (ATA の規定。
                          * NP21/W は書き込みの前に非同期の読みの完了を待つ = 0) */
    int sel_lag;         /* 選ばれた装置が BSY を見せる回数 (選択の直後の整定) */
    unsigned absent_status; /* 居ない装置を選んだときに読める値 (実機は 0xFF とは限らない) */
    /* ---- 規定違反の数 (試験は 0 を見る) ---- */
    u32 n_sel_while_bsy;    /* BSY の装置が選ばれているのに DRV_HEAD を書いた */
    u32 n_cmd_while_bsy;    /* BSY の装置へ DEVICE RESET 以外のコマンドを書いた */
    u32 n_reg_while_bsy;    /* BSY の装置へ Features / Byte Count を書いた */
    u32 n_packet_stale_cy;  /* 選び直した後 Byte Count を書かずに PACKET を出した */
    u32 n_srst_early;       /* SRST を解いて 2ms 経たないうちにステータスを読んだ / DRV_HEAD を書いた */
    u32 n_devreset, n_srst;
    /* SRST を解いた時点の g_delay_us (srst_settling のあいだ 2ms を数える) */
    int srst_settling;
    unsigned long srst_release_us;
    /* 模型の時計: 贋の cpu_delay_us が待った合計 + ステータスの読み 1 回 = NP2_INP_US */
    unsigned long n_status_reads;
    unsigned long first_devreset_us;   /* 最初の DEVICE RESET を受けた時刻 */
} N;

/* ステータスの読み 1 回の時間 (µs)。C バスの inp は 0.5〜1µs (IDE_TIMEOUT_LOOP
 * = 100 万回が Ra266 で 0.5〜1 秒)。回数で数える待ちはこれで時間に換算される */
#define NP2_INP_US  1ul
static unsigned long np2_now(void) { return g_delay_us + N.n_status_reads * NP2_INP_US; }

/* 実機寄り (strict): SRST を解いた後、装置はこの回数だけ BSY を見せる */
#define NP2_SRST_BUSY_READS  40
/* SRST を解いてからステータスを読むまでに置く時間 (ATA の規定、µs) */
#define NP2_SRST_SETTLE_US   2000ul

static struct { int strict; int sel_lag; unsigned absent_status; } g_np2cfg = { 0, 0, 0xFF };
static int g_np2cfg_no_init;   /* 1 = setup_np2_layout が atapi_init を呼ばない (装置を設定してから呼ぶ) */

/* 装置がコマンドを受けられない (BSY) か */
static int np2_timed_busy(const Np2Drv *D)
{
    return D->dead || np2_now() < D->busy_until;
}

static int np2_busy(const Np2Drv *D)
{
    return D->stuck || np2_timed_busy(D) || D->busy_left > 0 || (D->status & NP2_STAT_BSY);
}

/* 選ばれている装置。居なければ NULL (getidedrv と同じ) */
static Np2Drv *np2_cur(void)
{
    Np2Drv *d = &N.d[N.drivesel];
    return d->present ? d : (Np2Drv *)0;
}


static void np2_set_sk(Np2Drv *D, u8 sk) { D->error = (u8)((D->error & 0x0F) | (sk << 4)); D->sk = sk; }

static void np2_drvreset(Np2Drv *D)
{
    D->sc = 0x01; D->cy = 0xEB14; D->status = 0;
}

static void np2_senderror(Np2Drv *D)
{
    D->sc = NP2_INTR_IO | NP2_INTR_CD;
    D->status &= (u8)~(NP2_STAT_BSY | NP2_STAT_DRQ);
    D->status |= NP2_STAT_CHK | NP2_STAT_DSC;
}

static void np2_cmddone(Np2Drv *D)
{
    D->sc = NP2_INTR_IO | NP2_INTR_CD;
    D->status &= (u8)~(NP2_STAT_BSY | NP2_STAT_DRQ | NP2_STAT_CHK);
    D->status |= NP2_STAT_DRDY | NP2_STAT_DSC;
    D->error = 0; np2_set_sk(D, 0);
}

static void np2_senddata(Np2Drv *D, u32 size, u32 limit)
{
    if (size > limit) size = limit;
    D->sc = NP2_INTR_IO;
    D->cy = (u16)size;
    D->status &= (u8)~(NP2_STAT_BSY | NP2_STAT_CHK);
    D->status |= NP2_STAT_DRQ | NP2_STAT_DSC;
    D->error = 0; np2_set_sk(D, 0);
    D->bufdir_in = 1; D->buftc = NP2_TC_END; D->bufpos = 0; D->bufsize = size;
}

static void np2_dataread(Np2Drv *D)
{
    if (D->status & NP2_STAT_BSY) return;
    if (D->nsectors == 0) { D->sk = 0x0B; D->error = 0x04; np2_senderror(D); return; }
    if (!D->loaded || D->sector >= D->media->secs) {
        np2_set_sk(D, 0x05);      /* ILLEGAL REQUEST, asc 0x21 */
        np2_senderror(D);
        return;
    }
    memcpy(D->buf, D->media->img + D->sector * SEC, SEC);
    if (D->stuck_after_lba >= 0 && (long)D->sector == D->stuck_after_lba) D->stuck_pending = 1;
    D->sector++; D->nsectors--;
    D->sc = NP2_INTR_IO;
    D->cy = 2048;
    D->status &= (u8)~(NP2_STAT_BSY | NP2_STAT_CHK);
    D->status |= NP2_STAT_DRQ;
    D->error = 0; np2_set_sk(D, 0);
    D->bufdir_in = 1;
    D->buftc = D->nsectors ? NP2_TC_READ : NP2_TC_END;
    D->bufpos = 0; D->bufsize = 2048;
    M.n_sectors++;
    /* CD_ASYNC (Windows 版): 読みは別スレッド。2ms で終わらなければ BSY の
     * まま戻り、DRQ は次のフレームの atapi_dataread_asyncwait(0) で立つ */
    D->busy_left = D->async_lag;
}

static void np2_a0(Np2Drv *D)
{
    u8 op = D->buf[0];
    if (op == SCSI_CMD_READ_CAPACITY) D->n_readcap++;
    if (op == SCSI_CMD_REQUEST_SENSE) {
        /* atapicmd.c case 0x03: 直前のエラーの sk / asc を 18 バイトで */
        u32 leng = D->buf[4];
        D->n_reqsense++;
        memset(D->buf, 0, 18);
        D->buf[0] = 0x70;
        D->buf[2] = D->sk;
        D->buf[7] = 11;
        D->buf[12] = D->asc;
        D->buf[13] = D->ascq;
        D->ua = 0;                           /* 報告したので消える */
        np2_senddata(D, 18, leng);
        return;
    }
    /* 実機寄り: リセット・交換の後の最初のコマンドは UNIT ATTENTION (asc 29h) */
    if (D->ua) {
        np2_set_sk(D, ATAPI_SK_UNIT_ATTENTION);
        D->asc = 0x29; D->ascq = 0;
        if (!D->ua_needs_sense) D->ua = 0;
        np2_senderror(D);
        return;
    }
    /* 実機寄り: トレイが空 (NOT READY / 3Ah)、準備中 (NOT READY / 04h) */
    /* 実機寄り: START STOP UNIT (NP21/W は受けない = sendabort) */
    if (op == SCSI_CMD_START_STOP_UNIT) {
        if (D->buf[4] & SCSI_SSU_START) {
            D->n_startunit++;
            if (!D->start_ignored) D->need_start = 0;
            if (D->start_busy_us) D->busy_until = np2_now() + D->start_busy_us;
        }
        np2_cmddone(D);
        return;
    }
    if (D->no_medium) {
        np2_set_sk(D, ATAPI_SK_NOT_READY);
        D->asc = ATAPI_ASC_MEDIUM_NOT_PRESENT; D->ascq = 0;
        np2_senderror(D);
        return;
    }
    /* 実機寄り: initializing command required (START UNIT が要る)。待っても変わらない */
    if (D->need_start) {
        np2_set_sk(D, ATAPI_SK_NOT_READY);
        D->asc = ATAPI_ASC_BECOMING_READY; D->ascq = ATAPI_ASCQ_INIT_CMD_REQUIRED;
        np2_senderror(D);
        return;
    }
    if (D->ready_after > 0) {
        D->ready_after--;
        np2_set_sk(D, ATAPI_SK_NOT_READY);
        D->asc = ATAPI_ASC_BECOMING_READY; D->ascq = 0x01;   /* becoming ready */
        np2_senderror(D);
        return;
    }
    if (op == SCSI_CMD_TEST_UNIT_READY) {
        if (!D->loaded) { np2_set_sk(D, 0x02); np2_senderror(D); return; }
        if (D->changed) { np2_set_sk(D, 0x02); D->changed = 0; np2_senderror(D); return; }
        np2_cmddone(D);
        return;
    }
    if (op == SCSI_CMD_READ_CAPACITY) {
        u32 t = D->loaded ? D->media->secs : 0;   /* NP21/W は最終 LBA ではなく総数を返す */
        D->buf[0] = (u8)(t >> 24); D->buf[1] = (u8)(t >> 16);
        D->buf[2] = (u8)(t >> 8);  D->buf[3] = (u8)t;
        D->buf[4] = 0; D->buf[5] = 0; D->buf[6] = 0x08; D->buf[7] = 0;
        np2_senddata(D, 8, 8);
        return;
    }
    if (op == SCSI_CMD_READ_10) {
        u32 lba = ((u32)D->buf[2] << 24) | ((u32)D->buf[3] << 16)
                | ((u32)D->buf[4] << 8) | D->buf[5];
        u32 len = ((u32)D->buf[7] << 8) | D->buf[8];
        N.n_read10++;
        M.n_read10++;
        if (len > 1) { N.n_read10_multi++; M.n_read10_multi++; }
        if (len > M.max_count) M.max_count = len;
        if (M.n_log < LOG_MAX) { M.log_lba[M.n_log] = lba; M.log_cnt[M.n_log] = len; M.n_log++; }
        D->sector = lba; D->nsectors = len;
        /* 実機寄り: 回転が止まっていれば、最初のデータまで秒の単位で BSY */
        if (D->spinup_us) { D->busy_until = np2_now() + D->spinup_us; D->spinup_us = 0; }
        np2_dataread(D);
        return;
    }
    D->sk = 0x0B; D->error = 0x04; np2_senderror(D);   /* sendabort */
}

/* strict: SRST を解いて 2ms 経つ前にバスへ触ったか (過ぎたら数えるのをやめる) */
static void np2_check_srst_settle(void)
{
    if (!N.srst_settling) return;
    if (g_delay_us - N.srst_release_us >= NP2_SRST_SETTLE_US) { N.srst_settling = 0; return; }
    N.n_srst_early++;
}

static unsigned int np2_inp(unsigned int port)
{
    Np2Drv *D;
    if (N.bank != 1) return 0xFF;
    if (port == IDE_STATUS || port == IDE_ALT_STATUS) {
        N.n_status_reads++;
        /* SRST を立てているあいだの空読み (保持の待ち) は数えない */
        if (!(N.ctrl & IDE_SRST)) np2_check_srst_settle();
    }
    D = np2_cur();
    if (!D) return N.absent_status;    /* 居ない装置: 浮いたバスの値 (どのポートも) */
    switch (port) {
    case IDE_ERROR:      D->status &= (u8)~NP2_STAT_CHK; return D->error;
    case IDE_SECT_CNT:   return D->sc;
    case IDE_CYL_LO:     return (u8)D->cy;
    case IDE_CYL_HI:     return (u8)(D->cy >> 8);
    case IDE_STATUS:
    case IDE_ALT_STATUS:
        if (D->stuck || np2_timed_busy(D)) return (u8)((D->status & ~NP2_STAT_DRQ) | NP2_STAT_BSY);
        if (D->busy_left > 0) {
            D->busy_left--;
            return (u8)((D->status & ~NP2_STAT_DRQ) | NP2_STAT_BSY);
        }
        return D->status;
    default:             return 0xFF;
    }
}

static void np2_outp(unsigned int port, unsigned int v)
{
    Np2Drv *D;
    int k;
    if (port == IDE_BANK1) { if (!(v & 0x80)) N.bank = (int)(v & 0x71); return; }
    if (N.bank != 1) return;
    if (port == IDE_DRV_HEAD) {
        /* ideio_o64c: 書く前に非同期の読みの完了を待つ (asyncwait)。実機 (strict)
         * では BSY の装置は書き込みを受けないので、選択は変わらない */
        int newsel = (int)((v >> 4) & 1);
        np2_check_srst_settle();
        D = np2_cur();
        if (D && np2_busy(D)) {
            if (N.strict) { N.n_sel_while_bsy++; return; }
            D->busy_left = 0;
        }
        if (newsel != N.drivesel) {
            N.drivesel = newsel;
            if (N.d[newsel].present) {
                /* 選ばれた直後の整定 (SRST の後の BSY が残っていればそちら) */
                if (N.d[newsel].busy_left < N.sel_lag) N.d[newsel].busy_left = N.sel_lag;
                N.d[newsel].cy_written = 0;
                if (N.d[newsel].stuck_on_select) {
                    N.d[newsel].stuck_on_select = 0;
                    N.d[newsel].stuck = 1;
                }
            }
        }
        return;
    }
    if (port == IDE_DEV_CTRL) {
        u8 mod = (u8)(N.ctrl ^ v);
        N.ctrl = (u8)v;
        if (mod & IDE_SRST) {
            for (k = 0; k < 2; k++) {
                D = &N.d[k];
                if (!D->present) continue;
                if (v & IDE_SRST) {
                    D->status = 0; D->error = 0;
                    D->stuck = 0; D->stuck_pending = 0; D->busy_left = 0;
                    D->busy_until = 0;
                } else {
                    np2_drvreset(D);
                    D->status = NP2_STAT_DRDY | NP2_STAT_DSC | NP2_STAT_CHK;
                    D->error = 0x01;
                    if (D->ua_on_reset) D->ua = 1;
                    /* 実機寄り: リセットの処理のあいだ BSY (NP21/W はすぐ終わる)。
                     * srst_busy_us があれば秒の単位 (ATA は最大 31 秒を許す) */
                    if (N.strict) D->busy_left = NP2_SRST_BUSY_READS;
                    if (D->srst_busy_us) D->busy_until = np2_now() + D->srst_busy_us;
                }
            }
            if (v & IDE_SRST) N.n_srst++;
            else {
                N.drivesel = 0;                  /* リセット後はマスター */
                if (N.strict) { N.srst_settling = 1; N.srst_release_us = g_delay_us; }
            }
        }
        return;
    }
    D = np2_cur();
    if (!D) return;
    if (port == IDE_COMMAND && v == ATAPI_CMD_DEVICE_RESET) {
        /* ideio_o64e case 0x08: BSY でも受ける。その装置だけ戻す */
        N.n_devreset++;
        if (N.n_devreset == 1) N.first_devreset_us = np2_now();
        if (D->ignore_devreset || D->dead) return;
        np2_drvreset(D);
        D->error = 0x01;
        D->stuck = 0; D->stuck_pending = 0; D->busy_left = 0;
        D->busy_until = 0;
        if (D->ua_on_reset) D->ua = 1;
        return;
    }
    if (np2_busy(D)) {
        if (N.strict) {
            if (port == IDE_COMMAND) N.n_cmd_while_bsy++;
            else N.n_reg_while_bsy++;
            return;                              /* BSY の装置は書き込みを無視 */
        }
        if (port == IDE_COMMAND) D->busy_left = 0;   /* ideio_o64e の asyncwait */
    }
    switch (port) {
    case IDE_SECT_CNT: D->sc = (u8)v; break;
    case IDE_CYL_LO:   D->cy = (u16)((D->cy & 0xFF00) | (v & 0xFF)); D->cy_written = 1; break;
    case IDE_CYL_HI:   D->cy = (u16)((D->cy & 0x00FF) | ((v & 0xFF) << 8)); D->cy_written = 1; break;
    case IDE_COMMAND:
        D->cmd = (u8)v;
        if (v == ATAPI_CMD_PACKET) {
            if (!D->cy_written) N.n_packet_stale_cy++;
            D->sc = (u8)((D->sc & ~NP2_INTR_IO) | NP2_INTR_CD);
            D->status &= (u8)~(NP2_STAT_BSY | NP2_STAT_CHK);
            D->status |= NP2_STAT_DRDY | NP2_STAT_DRQ | NP2_STAT_DSC;
            if (D->cdb_busy_us) { D->busy_until = np2_now() + D->cdb_busy_us; D->cdb_busy_us = 0; }
            D->error = 0;
            D->bufpos = 0; D->bufsize = 12; D->bufdir_in = 0; D->buftc = NP2_TC_END;
            M.n_packets++;
        } else {
            D->error = 0x04; np2_senderror(D);
        }
        break;
    default: break;
    }
}

static unsigned int np2_inpw(unsigned int port)
{
    unsigned int ret = 0;
    Np2Drv *D;
    if (port != IDE_DATA || N.bank != 1) return 0xFFFF;
    D = np2_cur();
    if (!D) return 0xFFFF;
    if (D->stuck || np2_timed_busy(D) || D->busy_left > 0) return 0;   /* まだ DRQ が立っていない */
    if ((D->status & NP2_STAT_DRQ) && D->bufdir_in) {
        ret = (unsigned int)D->buf[D->bufpos] | ((unsigned int)D->buf[D->bufpos + 1] << 8);
        D->bufpos += 2;
        if (D->bufpos >= D->bufsize) {
            D->status &= (u8)~NP2_STAT_DRQ;
            if (D->cmd == ATAPI_CMD_PACKET) {
                /* 実機寄り: このセクタを渡し終えたところで固まる */
                if (D->stuck_pending) { D->stuck_pending = 0; D->stuck = 1; return ret; }
                if (D->status & NP2_STAT_BSY) return ret;
                if (D->buftc == NP2_TC_READ) { np2_dataread(D); return ret; }
                D->sc = NP2_INTR_IO | NP2_INTR_CD;
                D->status &= (u8)~(NP2_STAT_BSY | NP2_STAT_CHK | NP2_STAT_DRQ);
                D->status |= NP2_STAT_DRDY | NP2_STAT_DSC;
                D->error = 0;
            }
        }
    }
    return ret;
}

static void np2_outpw(unsigned int port, unsigned int v)
{
    Np2Drv *D;
    if (port != IDE_DATA || N.bank != 1) return;
    D = np2_cur();
    if (!D) return;
    if ((D->status & NP2_STAT_DRQ) && !D->bufdir_in) {
        D->buf[D->bufpos] = (u8)v;
        D->buf[D->bufpos + 1] = (u8)(v >> 8);
        D->bufpos += 2;
        if (D->bufpos >= D->bufsize) {
            D->status &= (u8)~NP2_STAT_DRQ;
            if (D->cmd == ATAPI_CMD_PACKET && D->die_on_packet) { D->dead = 1; return; }
            if (D->cmd == ATAPI_CMD_PACKET) np2_a0(D);
        }
    }
}

unsigned int atapi_shim_inp(unsigned int port)
{ return g_np2 ? np2_inp(port) : m_inp(port); }
void atapi_shim_outp(unsigned int port, unsigned int value)
{ if (g_np2) np2_outp(port, value); else m_outp(port, value); }
unsigned int atapi_shim_inpw(unsigned int port)
{ return g_np2 ? np2_inpw(port) : m_inpw(port); }
void atapi_shim_outpw(unsigned int port, unsigned int value)
{ if (g_np2) np2_outpw(port, value); else m_outpw(port, value); }

/* ======================================================================== */
/*  dev 層の贋物 (cd0 だけ)                                                 */
/* ======================================================================== */

static Device g_cd0;
static u32 g_dev_calls;

Device *dev_find(const char *name)
{
    return (strcmp(name, "cd0") == 0) ? &g_cd0 : (Device *)0;
}

int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{
    CHECK(dev == &g_cd0);
    g_dev_calls++;
    return atapi_read_sectors(lba, (u32)count, buf);
}

/* ======================================================================== */
/*  共通                                                                     */
/* ======================================================================== */

static void setup(void)
{
    memset(&M, 0, sizeof(M));
    M.fail_lba = -1;
    M.stale = 3;         /* 400ns の整定を置かないと取り違える形 */
    M.bcl = 0xEB14;      /* 起動直後はシグネチャ */
    build_media(&g_media_a, BIG_LBA_A, SUB_LBA, DEEP_SIZE, 1);
    build_media(&g_media_b, BIG_LBA_B, SUB_LBA_B, DEEP_SIZE_B, 2);
    M.m = &g_media_a;
    CHECK(atapi_init() == 1);
}

/* NP21/W の写しで立ち上げる。layout:
 *   0 = マスターに CD (media A)、スレーブは居ない
 *   1 = マスターに空の CD、スレーブに CD (media A) — NP21/W で ide2 も CD の
 *       種別のまま ide3 に ISO を付けた形 (2026-09-26 の報告)
 *   2 = マスターは居ない、スレーブに CD (media A)
 *   3 = マスターに CD (media A)、スレーブに空の CD */
static void setup_np2_layout(int layout)
{
    int k;
    memset(&M, 0, sizeof(M));
    memset(&N, 0, sizeof(N));
    M.fail_lba = -1;
    build_media(&g_media_a, BIG_LBA_A, SUB_LBA, DEEP_SIZE, 1);
    build_media(&g_media_b, BIG_LBA_B, SUB_LBA_B, DEEP_SIZE_B, 2);
    M.m = &g_media_a;
    g_np2 = 1;
    g_delay_us = 0;
    N.strict = g_np2cfg.strict;
    N.sel_lag = g_np2cfg.sel_lag;
    N.absent_status = g_np2cfg.absent_status;
    N.d[0].present = (layout != 2);
    N.d[1].present = (layout != 0);
    for (k = 0; k < 2; k++) {
        N.d[k].media = &g_media_a;
        N.d[k].stuck_after_lba = -1;
        np2_drvreset(&N.d[k]);
    }
    N.d[0].loaded = (layout == 0 || layout == 3);
    N.d[1].loaded = (layout == 1 || layout == 2);
    if (!g_np2cfg_no_init) CHECK(atapi_init() == 1);
}

static void setup_np2(void) { setup_np2_layout(0); }

/* 規定違反が無かったか (strict の試験の後に見る) */
static void np2_check_protocol(void)
{
    if (N.n_sel_while_bsy || N.n_cmd_while_bsy || N.n_reg_while_bsy || N.n_packet_stale_cy
        || N.n_srst_early) {
        fprintf(stderr, "protocol: sel_bsy=%lu cmd_bsy=%lu reg_bsy=%lu stale_cy=%lu srst_early=%lu\n",
                (unsigned long)N.n_sel_while_bsy, (unsigned long)N.n_cmd_while_bsy,
                (unsigned long)N.n_reg_while_bsy, (unsigned long)N.n_packet_stale_cy,
                (unsigned long)N.n_srst_early);
        CHECK(0);
    }
}

/* 選んだ装置 (の構造体) */
static Np2Drv *np2_chosen(void) { return &N.d[atapi_drive_index()]; }

static Iso9660Ctx *do_mount(void)
{
    Iso9660Ctx *c = (Iso9660Ctx *)iso9660_ops.mount(
        VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 0));
    CHECK(c != NULL);
    model_reset_counts();
    return c;
}

static u32 big_secs(void) { return (g_big_size + SEC - 1) / SEC; }
static u32 ceil_div(u32 a, u32 b) { return (a + b - 1) / b; }

/* BIG.PKG を off から chunk ずつ末尾まで read_stream し、中身を確かめる */
static void stream_all(Iso9660Ctx *c, const char *path, const Media *m,
                       u32 start, u32 chunk)
{
    u8 *buf = (u8 *)malloc(chunk);
    u32 off = start;
    CHECK(buf != NULL);
    while (off < g_big_size) {
        int rc = iso9660_ops.read_stream(c, path, buf, chunk, off);
        u32 want = (g_big_size - off < chunk) ? g_big_size - off : chunk;
        u32 i;
        CHECK(rc == (int)want);
        for (i = 0; i < want; i++) {
            if (buf[i] != file_byte(m, m->big_lba, off + i)) {
                fprintf(stderr, "mismatch at %lu\n", (unsigned long)(off + i));
                CHECK(0);
            }
        }
        off += want;
    }
    CHECK(iso9660_ops.read_stream(c, path, buf, chunk, off) == 0);   /* EOF */
    free(buf);
}

static void report(const char *name, Iso9660Ctx *c)
{
    printf("%s: read10=%lu multi=%lu sectors=%lu walks=%lu hits=%lu ra_fill=%lu "
           "bulk=%lu sc_fill=%lu secs=%lu\n", name,
           (unsigned long)M.n_read10, (unsigned long)M.n_read10_multi,
           (unsigned long)M.n_sectors, (unsigned long)c->stats.path_walks,
           (unsigned long)c->stats.path_hits, (unsigned long)c->stats.ra_fills,
           (unsigned long)c->stats.bulk_reads, (unsigned long)c->stats.scache_fills,
           (unsigned long)big_secs());
}

/* 上限: データは N セクタにつき 1 回、メタデータ (根のセクタ 1) と
 * 区切りの端 (先頭と末尾の半端) で + 2 */
static u32 bound(void)
{
    return ceil_div(big_secs(), ATAPI_READ_MAX_SECTORS) + 3u;
}

/* ======================================================================== */
/*  試験                                                                     */
/* ======================================================================== */

/* 4KB ずつ (VFS の既定の小さな区切り) */
static void t_stream_4k(void)
{
    Iso9660Ctx *c;
    setup(); c = do_mount();
    stream_all(c, "/BIG.PKG", &g_media_a, 0, 4096);
    report("stream_4k", c);
    CHECK(c->stats.path_walks == 1);
    CHECK(M.n_read10 <= bound());
    CHECK(M.max_count == ATAPI_READ_MAX_SECTORS);
    CHECK(lba_reads(ROOT_LBA) == 1);
    CHECK(M.n_sectors <= big_secs() + 1u);   /* 同じセクタを読み直さない */
    iso9660_ops.umount(c);
}

/* 1000B ずつ、セクタに揃わない位置から */
static void t_stream_odd(void)
{
    Iso9660Ctx *c;
    setup(); c = do_mount();
    stream_all(c, "BIG.PKG", &g_media_a, 13, 1000);
    report("stream_odd", c);
    CHECK(c->stats.path_walks == 1);
    CHECK(M.n_read10 <= bound());
    CHECK(M.n_sectors <= big_secs() + 1u);
    iso9660_ops.umount(c);
}

/* cdinst (pkg.c) の形: 先頭をセクタ境界へ揃えてから 32KB ずつ */
static void t_stream_32k(void)
{
    Iso9660Ctx *c;
    u8 *buf = (u8 *)malloc(32768);
    u32 off = 1234, i;
    setup(); c = do_mount();
    while (off < g_big_size) {
        u32 room = 32768u - (off % SEC);
        u32 want = (g_big_size - off < room) ? g_big_size - off : room;
        CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, room, off) == (int)want);
        for (i = 0; i < want; i++)
            CHECK(buf[i] == file_byte(&g_media_a, BIG_LBA_A, off + i));
        off += want;
    }
    report("stream_32k", c);
    CHECK(c->stats.path_walks == 1);
    CHECK(M.n_read10 <= bound());
    if (ISO_RA_BYTES <= 32768u)
        CHECK(c->stats.bulk_reads > 0);        /* 窓を通さず直接 (窓 ≤ 32KB のとき) */
    free(buf);
    iso9660_ops.umount(c);
}

/* read_file で全体 */
static void t_read_file(void)
{
    Iso9660Ctx *c;
    u8 *buf = (u8 *)malloc(g_big_size + 64);
    u32 i;
    setup(); c = do_mount();
    CHECK(iso9660_ops.read_file(c, "/BIG.PKG", buf, g_big_size + 64) == (int)g_big_size);
    for (i = 0; i < g_big_size; i++)
        CHECK(buf[i] == file_byte(&g_media_a, BIG_LBA_A, i));
    report("read_file", c);
    CHECK(M.n_read10 <= bound());
    /* 切り詰め (max_size < size) */
    CHECK(iso9660_ops.read_file(c, "/BIG.PKG", buf, 5000) == 5000);
    for (i = 0; i < 5000; i++)
        CHECK(buf[i] == file_byte(&g_media_a, BIG_LBA_A, i));
    CHECK(iso9660_ops.read_file(c, "/SUB", buf, 100) == VFS_ERR_ISDIR);
    CHECK(iso9660_ops.read_file(c, "/NOPE", buf, 100) == VFS_ERR_NOTFOUND);
    free(buf);
    iso9660_ops.umount(c);
}

/* 複数セクタの READ(10) が落ちたら 1 セクタずつ読み直す */
static void t_multi_fallback(void)
{
    Iso9660Ctx *c;
    AtapiStats a0, a1;
    setup(); c = do_mount();
    M.fail_multi = 1;
    atapi_get_stats(&a0);
    stream_all(c, "/BIG.PKG", &g_media_a, 0, 4096);
    atapi_get_stats(&a1);
    report("multi_fallback", c);
    CHECK(a1.multi_fail > a0.multi_fail);
    CHECK(a1.single_retry - a0.single_retry >= big_secs() - 1u);
    CHECK(c->stats.path_walks == 1);
    iso9660_ops.umount(c);
}

/* 渡されたバイト数が足りない複数セクタの READ(10) も失敗として読み直す */
static void t_short_transfer(void)
{
    Iso9660Ctx *c;
    AtapiStats a0, a1;
    setup(); c = do_mount();
    M.short_multi = 1;
    atapi_get_stats(&a0);
    stream_all(c, "/BIG.PKG", &g_media_a, 0, 8192);
    atapi_get_stats(&a1);
    CHECK(a1.multi_fail > a0.multi_fail);
    iso9660_ops.umount(c);
}

/* 窓の先読みが要求の外の不良セクタで落ちても、要求の範囲は読める
 * (Codex レビュー 1 の P2)。窓は読む前に捨てる (途中まで上書きされた窓を
 * 前の範囲として当てない) */
static void t_bad_outside(void)
{
    Iso9660Ctx *c;
    u8 buf[4096];
    AtapiStats a0, a1;
    u32 i;
    setup(); c = do_mount();
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, sizeof(buf), 0) == (int)sizeof(buf));
    /* 要求は窓 2 枚目の頭の 2 セクタ、不良は同じ窓の 6 本目 */
    M.fail_lba = (long)(BIG_LBA_A + ISO_RA_SECTORS + 5u);
    atapi_get_stats(&a0);
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, sizeof(buf),
                                  ISO_RA_SECTORS * SEC) == (int)sizeof(buf));
    for (i = 0; i < sizeof(buf); i++)
        CHECK(buf[i] == file_byte(&g_media_a, BIG_LBA_A, ISO_RA_SECTORS * SEC + i));
    atapi_get_stats(&a1);
    CHECK(a1.multi_fail - a0.multi_fail == 1);
    CHECK(c->ra_valid == 0);
    /* 半端な位置から、不良の手前までは読める */
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 3000,
                                  (ISO_RA_SECTORS + 3u) * SEC + 100u) == 3000);
    for (i = 0; i < 3000; i++)
        CHECK(buf[i] == file_byte(&g_media_a, BIG_LBA_A,
                                  (ISO_RA_SECTORS + 3u) * SEC + 100u + i));
    M.fail_lba = -1;
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, sizeof(buf), 0) == (int)sizeof(buf));
    for (i = 0; i < sizeof(buf); i++)
        CHECK(buf[i] == file_byte(&g_media_a, BIG_LBA_A, i));
    iso9660_ops.umount(c);
}

/* 要求の範囲の中の不良セクタは失敗。1 セクタずつの読み直しは不良で止まる */
static void t_bad_inside(void)
{
    Iso9660Ctx *c;
    u8 buf[4096];
    AtapiStats a0, a1;
    setup(); c = do_mount();
    M.fail_lba = (long)(BIG_LBA_A + ISO_RA_SECTORS + 1u);
    atapi_get_stats(&a0);
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, sizeof(buf),
                                  ISO_RA_SECTORS * SEC) == VFS_ERR_IO);
    atapi_get_stats(&a1);
    CHECK(a1.multi_fail - a0.multi_fail >= 1);
    /* 窓の読み直し (RA, RA+1) と要るセクタの読み直し (RA, RA+1)。不良で止まる */
    CHECK(a1.single_retry - a0.single_retry <= 4u);
    /* 大きな揃った読み (窓を通さない) の中の不良も失敗 */
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", (u8 *)malloc(65536), 65536, 0)
          == VFS_ERR_IO);
    iso9660_ops.umount(c);
}

/* LRU: いちばん長く使っていないものを追い出す */
static void t_lru_order(void)
{
    Iso9660Ctx *c;
    const u32 L[5] = { 100, 101, 102, 103, 104 };
    const u8 *p;
    u32 i;
    setup(); c = do_mount();
    for (i = 0; i < 4; i++) CHECK(iso_get_sector(c, L[i]) != NULL);
    CHECK(iso_get_sector(c, L[0]) != NULL);        /* 当たり: L0 が新しくなる */
    CHECK(lba_reads(L[0]) == 1);
    CHECK(iso_get_sector(c, L[4]) != NULL);        /* L1 が出る */
    p = iso_get_sector(c, L[0]);                   /* まだいる */
    CHECK(p != NULL && lba_reads(L[0]) == 1);
    for (i = 0; i < SEC; i++) CHECK(p[i] == g_media_a.img[L[0] * SEC + i]);
    p = iso_get_sector(c, L[2]);
    CHECK(p != NULL && lba_reads(L[2]) == 1);
    for (i = 0; i < SEC; i++) CHECK(p[i] == g_media_a.img[L[2] * SEC + i]);
    CHECK(iso_get_sector(c, L[1]) != NULL);        /* 出ていた */
    CHECK(lba_reads(L[1]) == 2);
    iso9660_ops.umount(c);
}

/* UNIT ATTENTION の直後の mount (PVD を読む 1 セクタ) も通る */
static void t_ua_mount(void)
{
    Iso9660Ctx *c;
    u8 buf[16];
    u32 gen0;
    setup();
    gen0 = atapi_media_gen();
    M.ua_next = 1;
    c = do_mount();
    CHECK(atapi_media_gen() == gen0 + 1u);
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 16, 0) == 16);
    CHECK(buf[0] == file_byte(&g_media_b, BIG_LBA_B, 0));
    iso9660_ops.umount(c);
}

/* DRQ の区切り方: 2048 ずつ (NP21/W)、bcl どおり、セクタの途中で切れる大きさ */
static void t_multi_drq(void)
{
    static const u32 sizes[] = { 2048, 0, 6144, 1000, 512 };
    unsigned i;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        Iso9660Ctx *c;
        setup(); c = do_mount();
        M.drq_max = sizes[i];
        stream_all(c, "/BIG.PKG", &g_media_a, 0, 65536);
        CHECK(M.n_read10 <= bound());
        CHECK(!M.bcl_odd);
        CHECK(M.max_bcl <= ATAPI_PIO_BCL_MAX);
        iso9660_ops.umount(c);
    }
}

/* 覚えたパス: 違うパスは引き直す、同じなら引かない、マウントし直せば引く */
static void t_path_cache(void)
{
    Iso9660Ctx *c;
    u8 buf[8192];
    u32 i;
    OS32_Stat st;
    setup(); c = do_mount();
    CHECK(iso9660_ops.read_stream(c, "/SUB/DEEP.BIN", buf, 8192, 0) == (int)DEEP_SIZE);
    for (i = 0; i < DEEP_SIZE; i++) CHECK(buf[i] == file_byte(&g_media_a, DEEP_LBA, i));
    CHECK(c->stats.path_walks == 1);
    CHECK(iso9660_ops.read_stream(c, "/SUB/DEEP.BIN", buf, 10, 4990) == 10);
    CHECK(c->stats.path_walks == 1);
    CHECK(iso9660_ops.read_stream(c, "/TINY.TXT", buf, 8192, 0) == (int)TINY_SIZE);
    CHECK(c->stats.path_walks == 2);
    for (i = 0; i < TINY_SIZE; i++) CHECK(buf[i] == file_byte(&g_media_a, TINY_LBA, i));
    CHECK(iso9660_ops.stat(c, "/TINY.TXT", &st) == VFS_OK && st.st_size == TINY_SIZE);
    CHECK(c->stats.path_walks == 2);
    /* 見つからないパスは覚えない (覚えたパスはそのまま) */
    CHECK(iso9660_ops.read_stream(c, "/NOPE.TXT", buf, 10, 0) == VFS_ERR_NOTFOUND);
    CHECK(iso9660_ops.read_stream(c, "/NOPE.TXT", buf, 10, 0) == VFS_ERR_NOTFOUND);
    CHECK(c->stats.path_walks == 4);
    /* ファイルの下はディレクトリではない */
    CHECK(iso9660_ops.read_stream(c, "/TINY.TXT/X", buf, 10, 0) == VFS_ERR_NOTDIR);
    iso9660_ops.umount(c);
    c = do_mount();
    CHECK(iso9660_ops.read_stream(c, "/TINY.TXT", buf, 8192, 0) == (int)TINY_SIZE);
    CHECK(c->stats.path_walks == 1);
    iso9660_ops.umount(c);
}

/* 2 本のパスを交互に小さく読む: パスは引き直すが、ディレクトリのセクタは
 * LRU から出るので、媒体からは 1 回ずつしか読まない */
static void t_dir_lru(void)
{
    Iso9660Ctx *c;
    u8 buf[64];
    u32 k;
    setup(); c = do_mount();
    for (k = 0; k < 40; k++) {
        CHECK(iso9660_ops.read_stream(c, "/SUB/DEEP.BIN", buf, 64, k * 64) == 64);
        CHECK(buf[0] == file_byte(&g_media_a, DEEP_LBA, k * 64));
        CHECK(iso9660_ops.read_stream(c, "/TINY.TXT", buf, 1, k) == 1);
        CHECK(buf[0] == file_byte(&g_media_a, TINY_LBA, k));
    }
    report("dir_lru", c);
    CHECK(lba_reads(ROOT_LBA) == 1);
    CHECK(lba_reads(SUB_LBA) == 1);
    CHECK(lba_reads(SUB_LBA + 1) == 1);
    iso9660_ops.umount(c);
}

/* list_dir の cb が同じ FS を読んでも一覧が崩れない */
static Iso9660Ctx *g_ls_ctx;
static int g_ls_count, g_ls_big, g_ls_sub, g_ls_tiny;
static void ls_cb(const VfsDirEntry *e, void *u)
{
    u8 buf[200];
    (void)u;
    g_ls_count++;
    if (strcmp(e->name, "BIG.PKG") == 0) g_ls_big = (e->size == g_big_size);
    if (strcmp(e->name, "SUB") == 0) g_ls_sub = (e->type == VFS_TYPE_DIR);
    if (strcmp(e->name, "TINY.TXT") == 0) g_ls_tiny = 1;
    /* cb の中で同じ FS を読む。窓なしなので端のセクタも LRU に入り、根の
     * セクタより後に 4 本以上の別のセクタを使う = 根のスロットが追い出される */
    CHECK(iso9660_ops.read_stream(g_ls_ctx, "/SUB/DEEP.BIN", buf, 200, 100) == 200);
    CHECK(iso9660_ops.read_stream(g_ls_ctx, "/BIG.PKG", buf, 200, 70000) == 200);
    CHECK(iso9660_ops.read_stream(g_ls_ctx, "/BIG.PKG", buf, 200, 90000) == 200);
    CHECK(iso9660_ops.read_stream(g_ls_ctx, "/BIG.PKG", buf, 200, 110000) == 200);
    CHECK(iso9660_ops.read_stream(g_ls_ctx, "/BIG.PKG", buf, 200, 130000) == 200);
    CHECK(buf[0] == file_byte(&g_media_a, BIG_LBA_A, 130000));
}

static void t_list_reentrant(void)
{
    Iso9660Ctx *c;
    int i;
    setup();
    g_kmalloc_fail = 1;      /* 窓なし: データの端も LRU を通る */
    c = do_mount();
    g_kmalloc_fail = 0;
    g_ls_ctx = c;
    for (i = 0; i < 2; i++) {
        g_ls_count = g_ls_big = g_ls_sub = g_ls_tiny = 0;
        CHECK(iso9660_ops.list_dir(c, "/", ls_cb, NULL) == VFS_OK);
        CHECK(g_ls_count == 3 && g_ls_big && g_ls_sub && g_ls_tiny);
    }
    iso9660_ops.umount(c);
}

/* UNIT ATTENTION: 読みの途中で媒体が替わった。atapi は世代を進めて出し直し、
 * iso9660 は覚えていたものを捨てて新しい媒体で引き直す */
static void t_unit_attention(void)
{
    Iso9660Ctx *c;
    u8 buf[4096];
    u32 i, gen0;
    setup(); c = do_mount();
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 4096, 0) == 4096);
    CHECK(buf[0] == file_byte(&g_media_a, BIG_LBA_A, 0));
    gen0 = atapi_media_gen();
    M.ua_next = 1;
    /* 窓の外 (次の READ(10) が要る位置) を読む */
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 4096, 40u * SEC) == 4096);
    CHECK(atapi_media_gen() == gen0 + 1u);
    for (i = 0; i < 4096; i++)
        CHECK(buf[i] == file_byte(&g_media_b, BIG_LBA_B, 40u * SEC + i));
    CHECK(c->stats.path_walks == 2);
    /* 以後は新しい媒体の窓から */
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 4096, 0) == 4096);
    for (i = 0; i < 4096; i++)
        CHECK(buf[i] == file_byte(&g_media_b, BIG_LBA_B, i));
    iso9660_ops.umount(c);
}

/* 解決の途中で媒体が替わる (Codex レビュー 1 の P2)。根は A のものがキャッシュに
 * あり SUB = 21 と答える。SUB を読む READ(10) が UNIT ATTENTION → 出し直しで
 * B の 21 (囮) を読む。世代を見て B の根から引き直し、B の本物 (24) の答えを返す */
static void t_stat_swap(void)
{
    Iso9660Ctx *c;
    OS32_Stat st;
    u32 sz = 0;
    setup(); c = do_mount();
    CHECK(iso9660_ops.stat(c, "/TINY.TXT", &st) == VFS_OK);   /* A の根を LRU へ */
    M.ua_next = 1;
    CHECK(iso9660_ops.stat(c, "/SUB/DEEP.BIN", &st) == VFS_OK);
    CHECK(st.st_size == DEEP_SIZE_B);
    CHECK(lba_reads(SUB_LBA_B + 1u) == 1);

    /* get_file_size も同じ */
    iso9660_ops.umount(c);
    setup(); c = do_mount();
    CHECK(iso9660_ops.get_file_size(c, "/TINY.TXT", &sz) == VFS_OK && sz == TINY_SIZE);
    M.ua_next = 1;
    CHECK(iso9660_ops.get_file_size(c, "/SUB/DEEP.BIN", &sz) == VFS_OK);
    CHECK(sz == DEEP_SIZE_B);
    iso9660_ops.umount(c);
}

/* list_dir: 解決のあいだの交換は引き直す、一覧の途中 (cb の後) の交換は中断 */
static Iso9660Ctx *g_sw_ctx;
static int g_sw_calls, g_sw_arm, g_sw_read, g_sw_deep_b;
static void sw_cb(const VfsDirEntry *e, void *u)
{
    (void)u;
    g_sw_calls++;
    if (strcmp(e->name, "DEEP.BIN") == 0 && e->size == DEEP_SIZE_B) g_sw_deep_b = 1;
    if (g_sw_calls == 1 && g_sw_arm) {
        M.ua_next = 1;
        if (g_sw_read) {
            u8 b[16];
            /* cb の中で FS を読み、その読みが交換を踏む */
            (void)iso9660_ops.read_stream(g_sw_ctx, "/BIG.PKG", b, 16, 300000);
        }
    }
}

static void t_list_swap(void)
{
    Iso9660Ctx *c;
    OS32_Stat st;
    /* (1) 解決の途中 (根の読み) で交換 → 引き直して B の SUB (24) を一覧する */
    setup(); c = do_mount();
    M.ua_next = 1;
    g_sw_calls = g_sw_arm = g_sw_read = g_sw_deep_b = 0;
    g_sw_ctx = c;
    CHECK(iso9660_ops.list_dir(c, "/SUB", sw_cb, NULL) == VFS_OK);
    CHECK(g_sw_deep_b);
    iso9660_ops.umount(c);

    /* (2) 2 セクタの SUB の 1 セクタ目を読んだ後、次のセクタの読みで交換 → 中断 */
    setup(); c = do_mount();
    g_sw_calls = g_sw_deep_b = 0; g_sw_arm = 1; g_sw_read = 0;
    g_sw_ctx = c;
    CHECK(iso9660_ops.list_dir(c, "/SUB", sw_cb, NULL) == VFS_ERR_IO);
    CHECK(!g_sw_deep_b);
    iso9660_ops.umount(c);

    /* (3) 1 セクタの根: cb の中の読みが交換を踏む → 残りを読まずとも中断 */
    setup(); c = do_mount();
    g_sw_calls = 0; g_sw_arm = 1; g_sw_read = 1;
    g_sw_ctx = c;
    CHECK(iso9660_ops.list_dir(c, "/", sw_cb, NULL) == VFS_ERR_IO);
    CHECK(g_sw_calls == 1);
    /* 次の操作は新しい媒体で引き直す */
    CHECK(iso9660_ops.stat(c, "/SUB/DEEP.BIN", &st) == VFS_OK && st.st_size == DEEP_SIZE_B);
    iso9660_ops.umount(c);
}

/* 2 秒規則: 最後に媒体を読んでから ISO_IDLE_TICKS を超えたら捨てる (ちょうどは捨てない)。
 * 捨てるのはパス・LRU・先読みの窓の全部 */
static void t_idle_rule(void)
{
    Iso9660Ctx *c;
    u8 buf[16];
    u32 i;
    setup(); c = do_mount();
    tick_count = 1000;
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 16, 0) == 16);
    CHECK(c->stats.path_walks == 1);
    tick_count = 1000 + ISO_IDLE_TICKS;
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 16, 16) == 16);
    CHECK(c->stats.path_walks == 1);
    CHECK(lba_reads(ROOT_LBA) == 1);
    /* 当たりは時刻を進めない: 最後に媒体を読んだのは 1000 */
    tick_count = 1000 + ISO_IDLE_TICKS + 1;
    /* 入れ替えた媒体 (UNIT ATTENTION を出さない形)。B の BIG.PKG (LBA 45) は
     * A の窓 (LBA 40..) の中にある */
    M.m = &g_media_b;
    CHECK(iso9660_ops.read_stream(c, "/BIG.PKG", buf, 16, 0) == 16);
    for (i = 0; i < 16; i++) CHECK(buf[i] == file_byte(&g_media_b, BIG_LBA_B, i));
    CHECK(c->stats.path_walks == 2);
    CHECK(lba_reads(ROOT_LBA) == 2);
    iso9660_ops.umount(c);
}

/* 先読みの窓が取れなくても正しく読める (遅いだけ) */
static void t_no_window(void)
{
    Iso9660Ctx *c;
    setup();
    g_kmalloc_fail = 1;
    c = do_mount();
    g_kmalloc_fail = 0;
    CHECK(c->ra_buf == NULL);
    stream_all(c, "/BIG.PKG", &g_media_a, 7, 3000);
    CHECK(c->stats.path_walks == 1);
    iso9660_ops.umount(c);
}

/* 読みの列を再生する (tools/tests/test_cd_read.py が pkg.c の読み方から作る)。
 * 行は "offset length"。CD_READ_TRACE_MAX があれば READ(10) の数の上限 */
static void t_replay(void)
{
    const char *path = getenv("CD_READ_TRACE");
    const char *mx = getenv("CD_READ_TRACE_MAX");
    FILE *f;
    Iso9660Ctx *c;
    unsigned long off, len;
    u8 *buf = (u8 *)malloc(1u << 20);
    u32 reads = 0, bytes = 0, i;
    if (!path) { printf("replay: SKIP (CD_READ_TRACE なし)\n"); free(buf); return; }
    f = fopen(path, "r");
    CHECK(f != NULL);
    setup(); c = do_mount();
    while (fscanf(f, "%lu %lu", &off, &len) == 2) {
        int rc;
        CHECK(len <= (1u << 20));
        rc = iso9660_ops.read_stream(c, "/BIG.PKG", buf, (u32)len, (u32)off);
        CHECK(rc >= 0);
        for (i = 0; i < (u32)rc; i++)
            CHECK(buf[i] == file_byte(&g_media_a, BIG_LBA_A, (u32)off + i));
        reads++;
        bytes += (u32)rc;
    }
    fclose(f);
    printf("replay: reads=%lu bytes=%lu read10=%lu sectors=%lu walks=%lu\n",
           (unsigned long)reads, (unsigned long)bytes, (unsigned long)M.n_read10,
           (unsigned long)M.n_sectors, (unsigned long)c->stats.path_walks);
    CHECK(c->stats.path_walks == 1);
    if (mx) CHECK(M.n_read10 <= (u32)strtoul(mx, NULL, 10));
    free(buf);
    iso9660_ops.umount(c);
}

/* NP21/W の写しの上で、起動後の読み (2026-09-26 に「1 セクタも読めない」と
 * 報告された形): 容量、LBA 16 の 1 セクタ、16 セクタ、半端な数、iso9660 の mount と読み */
static void t_np2_read(void)
{
    AtapiCapacity cap;
    u8 *buf = (u8 *)malloc(64u * SEC);
    Iso9660Ctx *c;
    setup_np2();
    CHECK(atapi_read_capacity(&cap) == ATAPI_OK);
    CHECK(cap.total_sectors >= g_media_a.secs);
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
    CHECK(atapi_read_sectors(16, 16, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, 16u * SEC) == 0);
    CHECK(atapi_read_sectors(40, 37, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 40u * SEC, 37u * SEC) == 0);
    CHECK(N.n_read10_multi > 0);
    c = (Iso9660Ctx *)iso9660_ops.mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 0));
    CHECK(c != NULL);
    stream_all(c, "/BIG.PKG", &g_media_a, 0, 4096);
    iso9660_ops.umount(c);
    free(buf);
}

/* CD_ASYNC の形: 各セクタの後に BSY がしばらく続いても、待って読む */
static void t_np2_async(void)
{
    u8 *buf = (u8 *)malloc(40u * SEC);
    setup_np2();
    np2_chosen()->async_lag = 7;
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
    CHECK(atapi_read_sectors(40, 37 > 40 ? 40 : 37, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 40u * SEC, 37u * SEC) == 0);
    free(buf);
}

/* 空のドライブ (NP21/W の CD 入れ替えの途中 = sxsi の totals 0): 容量は
 * 「1 セクタ」になり (報告の cd0: block 1 sects と同じ)、READ(10) は ILLEGAL
 * REQUEST。診断の行が出る。媒体が入れば、dev を作り直さずに読める */
static void t_np2_empty(void)
{
    AtapiCapacity cap;
    u8 buf[SEC];
    Iso9660Ctx *c;
    setup_np2();
    N.d[0].loaded = 0;
    CHECK(atapi_read_capacity(&cap) == ATAPI_OK);
    CHECK(cap.total_sectors == 1);
    g_klog_n = 0; g_klog[0] = '\0';
    CHECK(atapi_read_sectors(16, 1, buf) != ATAPI_OK);
    CHECK(strstr(g_klog, "[atapi] READ(10) drv=0 lba=16 n=1") != NULL);
    CHECK(strstr(g_klog, "sense=5") != NULL);
    CHECK(iso9660_ops.mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 0)) == NULL);
    /* 入った */
    N.d[0].loaded = 1;
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
    c = (Iso9660Ctx *)iso9660_ops.mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 0));
    CHECK(c != NULL);
    iso9660_ops.umount(c);
}

/* セカンダリのスレーブの CD (マスターは空の CD / 居ない): 媒体の入っている方を
 * 選んで読む。マスターに入っていればマスター (2026-09-26 の回帰) */
static void t_np2_slave(void)
{
    static const int layouts[4] = { 0, 1, 2, 3 };
    static const int want[4]    = { 0, 1, 1, 0 };
    int k;
    for (k = 0; k < 4; k++) {
        AtapiCapacity cap;
        u8 buf[SEC];
        Iso9660Ctx *c;
        setup_np2_layout(layouts[k]);
        if (atapi_drive_index() != want[k]) {
            fprintf(stderr, "layout %d: drive %d\n", layouts[k], atapi_drive_index());
            CHECK(0);
        }
        CHECK(atapi_read_capacity(&cap) == ATAPI_OK);
        CHECK(cap.total_sectors > 1);
        CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
        CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
        c = (Iso9660Ctx *)iso9660_ops.mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 0));
        CHECK(c != NULL);
        stream_all(c, "/BIG.PKG", &g_media_a, 0, 32768);
        iso9660_ops.umount(c);
    }
}

/* READ CAPACITY がデータ無しで終わったら失敗 (受け皿の残りを容量にしない) */
static void t_cap_nodata(void)
{
    AtapiCapacity cap;
    setup();
    M.cap_nodata = 1;
    CHECK(atapi_read_capacity(&cap) != ATAPI_OK);
    M.cap_nodata = 0;
    CHECK(atapi_read_capacity(&cap) == ATAPI_OK);
    CHECK(cap.total_sectors == g_media_a.secs);
}


/* READ CAPACITY の応答の長さ: 8 バイトちょうどだけ通す。7 (Byte Count=7 →
 * 4 ワード読むが有効は 7) と 9 (8 + 1 の 2 回の DRQ) は失敗 (Codex 2 回目の P2) */
static void t_cap_len(void)
{
    static const u32 lens[] = { 7, 9, 6, 8 };
    unsigned i;
    setup();
    for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        AtapiCapacity cap;
        int rc;
        M.cap_len = lens[i];
        rc = atapi_read_capacity(&cap);
        if (lens[i] == 8) {
            CHECK(rc == ATAPI_OK);
            CHECK(cap.total_sectors == g_media_a.secs);
        } else {
            if (rc == ATAPI_OK) {
                fprintf(stderr, "cap_len %lu accepted (total=%lu)\n",
                        (unsigned long)lens[i], (unsigned long)cap.total_sectors);
                CHECK(0);
            }
        }
    }
}

/* 実機寄り: 選んだ直後の装置はしばらく BSY で、BSY の装置はレジスタ書き込みを
 * 無視する。装置選択 → 整定 → BSY/DRQ クリア待ち → Byte Count → PACKET の順で
 * ないと、旧装置に Byte Count を書き、BSY の装置に PACKET を書いて無視される
 * (Codex 2 回目の P1)。4 通りの構成で選ぶ装置と読み、規定違反 0 */
static void t_np2_sel_lag(void)
{
    static const int want[4] = { 0, 1, 1, 0 };
    int k;
    for (k = 0; k < 4; k++) {
        AtapiCapacity cap;
        u8 buf[4u * SEC];
        Iso9660Ctx *c;
        AtapiStats st;
        g_np2cfg.strict = 1;
        g_np2cfg.sel_lag = 7;
        setup_np2_layout(k);
        if (atapi_drive_index() != want[k]) {
            fprintf(stderr, "layout %d: drive %d\n", k, atapi_drive_index());
            CHECK(0);
        }
        np2_check_protocol();
        CHECK(atapi_read_capacity(&cap) == ATAPI_OK && cap.total_sectors > 1);
        CHECK(atapi_read_sectors(16, 4, buf) == ATAPI_OK);
        CHECK(memcmp(buf, g_media_a.img + 16u * SEC, 4u * SEC) == 0);
        c = (Iso9660Ctx *)iso9660_ops.mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD, 0));
        CHECK(c != NULL);
        stream_all(c, "/BIG.PKG", &g_media_a, 0, 32768);
        iso9660_ops.umount(c);
        np2_check_protocol();
        atapi_get_stats(&st);
        CHECK(st.dev_resets == 0 && st.soft_resets == 0);   /* 整定は待ちで足りる */
    }
}

/* 実機寄り: 居ない装置を選ぶと 0xFF でなく 0x00 や BSY 付きの値が読める。
 * 居ない装置の BSY は「コマンド未完了」ではないので待たず、リセットもしない */
static void t_np2_absent(void)
{
    static const unsigned vals[2] = { 0x00, 0x80 };
    static const int layouts[2] = { 0, 2 };      /* マスターだけ / スレーブだけ */
    int i, k;
    for (i = 0; i < 2; i++) {
        for (k = 0; k < 2; k++) {
            u8 buf[SEC];
            AtapiStats st;
            g_np2cfg.strict = 1;
            g_np2cfg.sel_lag = 3;
            g_np2cfg.absent_status = vals[i];
            setup_np2_layout(layouts[k]);
            CHECK(atapi_drive_index() == (layouts[k] == 2 ? 1 : 0));
            CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
            CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
            np2_check_protocol();
            atapi_get_stats(&st);
            CHECK(st.dev_resets == 0 && st.soft_resets == 0);
        }
    }
    g_np2cfg.absent_status = 0xFF;
}

/* バスが (居る) 別の装置を BSY のまま選んでいる: 終わるのを待ってから選ぶ。
 * 待っても終わらなければ DEVICE RESET で戻してから選ぶ (Codex 2 回目の P2)。
 * 白箱: バスの選択 (s_cursel / N.drivesel) を試験が直接スレーブへ向ける */
static void t_np2_sel_busy(void)
{
    u8 buf[SEC];
    AtapiStats st, st0;
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 0;
    setup_np2_layout(3);                     /* マスターに媒体、スレーブは空 (居る) */
    CHECK(atapi_drive_index() == 0);
    /* (1) スレーブが選ばれていて、しばらく BSY (5 秒 = 期限 ATAPI_CMD_TIMEOUT_US の内に終わる) */
    s_cursel = ATAPI_DRV_SLAVE; N.drivesel = 1; N.d[1].busy_until = np2_now() + 5000000ul;
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
    np2_check_protocol();
    atapi_get_stats(&st);
    CHECK(st.dev_resets == 0 && st.soft_resets == 0);
    /* (2) スレーブが固まったまま: DEVICE RESET (スレーブだけ) → 選ぶ → 読める */
    s_cursel = ATAPI_DRV_SLAVE; N.drivesel = 1; N.d[1].stuck = 1; N.d[1].ua_on_reset = 1;
    CHECK(atapi_read_sectors(17, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 17u * SEC, SEC) == 0);
    np2_check_protocol();
    atapi_get_stats(&st);
    CHECK(st.dev_resets == 1 && st.soft_resets == 0);
    CHECK(N.n_devreset == 1 && N.n_srst == 0);
    CHECK(!N.d[1].stuck && N.d[1].ua);        /* スレーブは戻り、UA を立てている */
    CHECK(!N.d[0].ua);                        /* マスターには触っていない */
    /* (3) DEVICE RESET を受けない装置: SRST で戻す (2 台とも UA) */
    s_cursel = ATAPI_DRV_SLAVE; N.drivesel = 1; N.d[1].stuck = 1; N.d[1].ignore_devreset = 1;
    N.d[0].ua_on_reset = 1;
    CHECK(atapi_read_sectors(18, 1, buf) == ATAPI_OK);   /* マスターの UA は出し直しで吸う */
    CHECK(memcmp(buf, g_media_a.img + 18u * SEC, SEC) == 0);
    np2_check_protocol();
    atapi_get_stats(&st);
    CHECK(st.dev_resets == 2 && st.soft_resets == 1);
    CHECK(!N.d[1].stuck);
    /* (4) 使っている装置 (スレーブ) が選ばれた瞬間に固まり、DEVICE RESET も
     * 受けない: 選んだ後の待ちが期限切れ → SRST → SRST はマスターを選ぶので、
     * スレーブを選び直してから PACKET (選び直さないと空のマスターへ READ(10) が行く)。
     * バスはマスターを選んでいる (白箱) */
    g_np2cfg.strict = 1;
    setup_np2_layout(1);                     /* マスターは空、スレーブに媒体 */
    CHECK(atapi_drive_index() == 1);
    s_cursel = 0x00; N.drivesel = 0;
    N.d[1].stuck_on_select = 1; N.d[1].ignore_devreset = 1; N.d[1].ua_on_reset = 1;
    atapi_get_stats(&st0);                   /* 統計は起動から累積 */
    CHECK(atapi_read_sectors(19, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 19u * SEC, SEC) == 0);
    np2_check_protocol();
    atapi_get_stats(&st);
    CHECK(st.dev_resets - st0.dev_resets == 1 && st.soft_resets - st0.soft_resets == 1);
    CHECK(N.drivesel == 1 && !N.d[1].stuck);
}

/* 使っている装置が途中で固まる: 複数セクタの READ(10) が期限切れ → 1 セクタ
 * ずつ (固まった装置は DEVICE RESET で戻す) → 固まるセクタで失敗。診断の行は
 * 最後に落ちた 1 セクタのコマンドと、その終わり方 (期限切れ、BSY = d0、そこまでの
 * バイト数) を出す (Codex 2 回目の P3)。呼び手の範囲は req= */
static void t_np2_stuck(void)
{
    u8 *buf = (u8 *)malloc(8u * SEC);
    AtapiStats st;
    int rc;
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 0;
    setup_np2_layout(0);
    N.d[0].stuck_after_lba = 17;
    g_klog_n = 0; g_klog[0] = '\0';
    rc = atapi_read_sectors(16, 4, buf);
    CHECK(rc == ATAPI_ERR_TIMEOUT);
    atapi_get_stats(&st);
    CHECK(st.multi_fail == 1);
    CHECK(st.dev_resets >= 1);
    if (strstr(g_klog, "[atapi] READ(10) drv=0 lba=17 n=1 ret=-1 st=d0 err=00 sense=0 got=2048 req=16+4") == NULL) {
        fprintf(stderr, "klog: %s", g_klog);
        CHECK(0);
    }
    /* 固まる前のセクタは読める (固まった装置は次の選択で戻す) */
    N.d[0].stuck_after_lba = -1;
    CHECK(atapi_read_sectors(16, 2, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, 2u * SEC) == 0);
    np2_check_protocol();
    free(buf);
}

/* 電源投入・リセットの後の最初のコマンドは UNIT ATTENTION (仕様どおり)。
 * 空のマスター + 媒体のあるスレーブで、スレーブの最初の READ CAPACITY が UA
 * でも、REQUEST SENSE で消して出し直し、スレーブを選ぶ (Codex 2 回目の P2)。
 * UA が REQUEST SENSE でだけ消える装置でも同じ */
static void t_np2_ua_init(void)
{
    int needs;
    for (needs = 0; needs < 2; needs++) {
        u8 buf[SEC];
        AtapiStats st;
        g_np2cfg.strict = 1;
        g_np2cfg.sel_lag = 3;
        g_np2cfg_no_init = 1;
        setup_np2_layout(1);
        g_np2cfg_no_init = 0;
        N.d[0].ua = 1; N.d[0].ua_needs_sense = needs;
        N.d[1].ua = 1; N.d[1].ua_needs_sense = needs;
        CHECK(atapi_init() == 1);
        if (atapi_drive_index() != 1) {
            fprintf(stderr, "needs_sense=%d: drive %d\n", needs, atapi_drive_index());
            CHECK(0);
        }
        CHECK(N.d[1].n_reqsense >= 1 && !N.d[1].ua);
        atapi_get_stats(&st);
        CHECK(st.ready_retries >= 1 && st.ready_retries <= 4);
        CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
        CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
        np2_check_protocol();
    }
}

/* NOT READY / ASC 04h (準備中) は短く待って出し直す。3 回準備中でも媒体のある
 * スレーブを選ぶ。待ちは cpu_delay_us で 3 回 */
static void t_np2_becoming_ready(void)
{
    AtapiStats st;
    u8 buf[SEC];
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 0;
    g_np2cfg_no_init = 1;
    setup_np2_layout(1);
    g_np2cfg_no_init = 0;
    N.d[1].ready_after = 3;
    CHECK(atapi_init() == 1);
    CHECK(atapi_drive_index() == 1);
    atapi_get_stats(&st);
    CHECK(st.ready_retries == 3);
    CHECK(g_delay_us == 3ul * ATAPI_READY_WAIT_US);
    CHECK(N.d[1].n_readcap == 4);
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    np2_check_protocol();
}

/* NOT READY / ASC 3Ah (媒体なし) だけは待たずに確定する。空のマスター (実機の
 * 空のトレイ) は READ CAPACITY 1 回で「媒体なし」、スレーブを選ぶ。公開の
 * atapi_read_capacity も ATAPI_ERR_NO_MEDIA を 1 回で返す */
static void t_np2_no_medium(void)
{
    AtapiCapacity cap;
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 0;
    g_np2cfg_no_init = 1;
    setup_np2_layout(1);
    g_np2cfg_no_init = 0;
    N.d[0].no_medium = 1;
    CHECK(atapi_init() == 1);
    CHECK(atapi_drive_index() == 1);
    CHECK(N.d[0].n_readcap == 1);
    CHECK(N.d[0].n_reqsense == 1);
    CHECK(g_delay_us == 0);
    np2_check_protocol();
    /* 使っている装置 (スレーブ) のトレイが空いた */
    N.d[1].no_medium = 1;
    CHECK(atapi_read_capacity(&cap) == ATAPI_ERR_NO_MEDIA);
    CHECK(N.d[1].n_readcap == 2);
    CHECK(g_delay_us == 0);
}

/* 準備中の待ちは実際に 4〜5 秒 (Fable レビューの P2)。cpu_delay_us は 1 回 100ms で
 * 丸めるので、250ms を 1 回で頼むと 16 × 100ms ≒ 1.6 秒しか待たず、トレイを閉じた
 * 直後 (becoming ready 2〜5 秒) のスレーブを待ちきれずに空のマスターへ固定していた。
 * (1) スレーブが 18 回 (4.5 秒) 準備中 → 待ちきってスレーブを選ぶ
 * (2) スレーブがずっと準備中 → 待ちの合計が 4〜5 秒で諦め、マスターに戻る */
static void t_np2_ready_total(void)
{
    AtapiStats st;
    u8 buf[SEC];
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 0;
    g_np2cfg_no_init = 1;
    setup_np2_layout(1);
    g_np2cfg_no_init = 0;
    N.d[1].ready_after = 18;
    CHECK(atapi_init() == 1);
    if (atapi_drive_index() != 1) {
        fprintf(stderr, "drive %d, waited %lu us\n", atapi_drive_index(), g_delay_us);
        CHECK(0);
    }
    CHECK(g_delay_us == 18ul * 250000ul);
    CHECK(g_delay_calls > 18ul);             /* 100ms 以下の塊に分けている */
    atapi_get_stats(&st);
    CHECK(st.ready_retries == 18);
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
    np2_check_protocol();

    g_np2cfg_no_init = 1;
    setup_np2_layout(1);
    g_np2cfg_no_init = 0;
    N.d[1].ready_after = 1000000;
    g_delay_calls = 0;
    CHECK(atapi_init() == 1);
    CHECK(atapi_drive_index() == 0);
    if (g_delay_us < 4000000ul || g_delay_us > 5000000ul) {
        fprintf(stderr, "waited %lu us (want 4..5 s)\n", g_delay_us);
        CHECK(0);
    }
    np2_check_protocol();
}

/* SRST の順序 (Fable レビューの P3): 解いてから 2ms 置き、マスターの BSY=0 を待って
 * から DRV_HEAD → 400ns → BSY=0。strict の模型は SRST を解いた後しばらく BSY を
 * 見せ、2ms 前のステータスの読みと BSY 中の DRV_HEAD を規定違反と数える。
 * 使う装置は選ばれた瞬間に固まり DEVICE RESET も受けない (SRST まで行く) ×
 * 構成 (1) マスターは空・スレーブに媒体 (2) マスター無し・スレーブに媒体
 * (3) マスターに媒体・スレーブは空 */
static void t_np2_srst(void)
{
    static const int layouts[3] = { 1, 2, 3 };
    int k;
    for (k = 0; k < 3; k++) {
        u8 buf[SEC];
        AtapiStats st0, st;
        int drv = (layouts[k] == 3) ? 0 : 1;
        g_np2cfg.strict = 1;
        g_np2cfg.sel_lag = 2;
        setup_np2_layout(layouts[k]);
        CHECK(atapi_drive_index() == drv);
        /* バスは使わない方を選んでいる (白箱)。居なければ浮いたバス */
        s_cursel = drv ? 0x00 : ATAPI_DRV_SLAVE;
        N.drivesel = drv ? 0 : 1;
        N.d[drv].stuck_on_select = 1; N.d[drv].ignore_devreset = 1;
        N.d[0].ua_on_reset = N.d[1].ua_on_reset = 1;
        atapi_get_stats(&st0);
        g_delay_us = 0;
        CHECK(atapi_read_sectors(20, 1, buf) == ATAPI_OK);
        CHECK(memcmp(buf, g_media_a.img + 20u * SEC, SEC) == 0);
        atapi_get_stats(&st);
        CHECK(st.soft_resets - st0.soft_resets == 1);
        CHECK(g_delay_us >= 2000ul);
        CHECK(N.drivesel == drv && !N.d[drv].stuck);
        np2_check_protocol();
    }
}

/* READ(10) の UNIT ATTENTION は 3 回まで出し直す (Fable レビューの P3、UA を
 * 複数積む装置)。3 回続けば 4 回目で読める、4 回続けば落ちる */
static void t_ua_multi(void)
{
    u8 buf[SEC];
    u32 gen0, n0;
    setup();
    gen0 = atapi_media_gen();
    n0 = M.n_read10;
    M.ua_next = 3;
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_b.img + 16u * SEC, SEC) == 0);
    CHECK(atapi_media_gen() == gen0 + 3u);
    CHECK(M.n_read10 - n0 == 4);
    M.ua_next = 4;
    n0 = M.n_read10;
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_ERR_IO);
    CHECK(M.n_read10 - n0 == 4);
    CHECK(M.ua_next == 0);
}


/* ======================================================================== */
/*  待ちの上限を秒で (TASK_ATAPI_TIMEOUT)                                    */
/* ======================================================================== */

/* T1: 回転が止まった後の最初の READ(10) は秒の単位で BSY (スピンアップ)。
 * 3 秒・9 秒 (上限 ATAPI_CMD_TIMEOUT_US の内) は 1 回目の READ(10) で読め、
 * DEVICE RESET は 0 回。25 秒 (固まったのと区別できない) は、BSY が上限を越えた
 * 後でだけ DEVICE RESET し、1 セクタずつの読み直しで読める */
static void t_np2_spinup(void)
{
    static const unsigned long spin[3] = { 3000000ul, 9000000ul, 25000000ul };
    u8 *buf = (u8 *)malloc(16u * SEC);
    int k;
    for (k = 0; k < 3; k++) {
        AtapiStats st0, st;
        unsigned long t0;
        g_np2cfg.strict = 1;
        g_np2cfg.sel_lag = 0;
        setup_np2_layout(0);
        model_reset_counts();
        atapi_get_stats(&st0);
        N.d[0].spinup_us = spin[k];
        t0 = np2_now();
        CHECK(atapi_read_sectors(16, 16, buf) == ATAPI_OK);
        CHECK(memcmp(buf, g_media_a.img + 16u * SEC, 16u * SEC) == 0);
        atapi_get_stats(&st);
        printf("spinup %lu us: read10=%lu dev_resets=%lu waited=%lu us\n", spin[k],
               (unsigned long)M.n_read10, (unsigned long)(st.dev_resets - st0.dev_resets),
               np2_now() - t0);
        if (spin[k] < ATAPI_CMD_TIMEOUT_US) {
            CHECK(np2_now() - t0 >= spin[k]);
            CHECK(M.n_read10 == 1);
            CHECK(st.dev_resets == st0.dev_resets && st.soft_resets == st0.soft_resets);
            CHECK(st.multi_fail == st0.multi_fail);
        } else {
            CHECK(st.dev_resets - st0.dev_resets == 1 && st.soft_resets == st0.soft_resets);
            CHECK(N.first_devreset_us - t0 >= ATAPI_CMD_TIMEOUT_US);
        }
        np2_check_protocol();
    }
    /* PACKET を受けてから CDB の DRQ までが 3 秒 (atapi_wait_drq の上限も秒) */
    {
        AtapiStats st0, st;
        g_np2cfg.strict = 1;
        g_np2cfg.sel_lag = 0;
        setup_np2_layout(0);
        model_reset_counts();
        atapi_get_stats(&st0);
        N.d[0].cdb_busy_us = 3000000ul;
        CHECK(atapi_read_sectors(16, 16, buf) == ATAPI_OK);
        CHECK(memcmp(buf, g_media_a.img + 16u * SEC, 16u * SEC) == 0);
        atapi_get_stats(&st);
        CHECK(M.n_read10 == 1 && st.dev_resets == st0.dev_resets);
        np2_check_protocol();
    }
    free(buf);
}

/* T1: SRST の後、装置は秒の単位で BSY (ATA は最大 31 秒)。使う装置 (マスター) が
 * 選ばれた瞬間に固まり DEVICE RESET も受けないので SRST まで行く。
 * 10 秒・25 秒は待ちきって読める (BSY の装置へ DRV_HEAD を書かない)。
 * 40 秒は 31 秒で諦め、DRV_HEAD も PACKET も書かずに期限切れを返す */
static void t_np2_srst_long(void)
{
    static const unsigned long busy[3] = { 10000000ul, 25000000ul, 40000000ul };
    int k;
    for (k = 0; k < 3; k++) {
        u8 buf[SEC];
        AtapiStats st0, st;
        unsigned long t0;
        u32 pk0;
        int rc;
        g_np2cfg.strict = 1;
        g_np2cfg.sel_lag = 2;
        setup_np2_layout(3);                 /* マスターに媒体、スレーブは空 */
        CHECK(atapi_drive_index() == 0);
        s_cursel = ATAPI_DRV_SLAVE; N.drivesel = 1;   /* 白箱: バスはスレーブ */
        N.d[0].stuck_on_select = 1; N.d[0].ignore_devreset = 1;
        N.d[0].ua_on_reset = N.d[1].ua_on_reset = 1;
        N.d[0].srst_busy_us = N.d[1].srst_busy_us = busy[k];
        atapi_get_stats(&st0);
        g_klog_n = 0; g_klog[0] = '\0';
        t0 = np2_now();
        pk0 = M.n_packets;
        rc = atapi_read_sectors(20, 1, buf);
        atapi_get_stats(&st);
        printf("srst busy %lu us: rc=%d waited=%lu us\n", busy[k], rc, np2_now() - t0);
        CHECK(st.soft_resets - st0.soft_resets == 1);
        np2_check_protocol();
        if (busy[k] < ATAPI_SRST_TIMEOUT_US) {
            CHECK(rc == ATAPI_OK);
            CHECK(memcmp(buf, g_media_a.img + 20u * SEC, SEC) == 0);
            CHECK(N.drivesel == 0 && !N.d[0].stuck);
        } else {
            CHECK(rc == ATAPI_ERR_TIMEOUT);
            CHECK(M.n_packets == pk0);
            CHECK(np2_now() - t0 >= ATAPI_SRST_TIMEOUT_US);
            if (strstr(g_klog, "[atapi] SRST") == NULL) {
                fprintf(stderr, "klog: %s", g_klog);
                CHECK(0);
            }
        }
    }
}

/* 着地後の P3 (1): SRST が 31 秒で期限切れ (バスが死んだ) になったら印を立て、
 * 以後の読み・容量確認・TEST UNIT READY はバスに触らず即 ATAPI_ERR_TIMEOUT。
 * 1 回目は t_np2_srst_long の 40 秒と同じ形で 31 秒待って落ちる。2 回目以後は
 * 時計が進まず、PACKET / DEVICE RESET / SRST を 1 つも出さない。「即失敗」の行は
 * 1 回だけ。装置が戻った後の atapi_init で印が解け、読めるようになる */
static void t_np2_bus_dead(void)
{
    u8 buf[SEC];
    AtapiCapacity cap;
    AtapiStats st0, st;
    unsigned long t0;
    u32 pk0, sr0, dr0;
    const char *p;
    int lines = 0;
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 2;
    setup_np2_layout(3);                 /* マスターに媒体、スレーブは空 */
    s_cursel = ATAPI_DRV_SLAVE; N.drivesel = 1;   /* 白箱: バスはスレーブ */
    N.d[0].stuck_on_select = 1; N.d[0].ignore_devreset = 1;
    N.d[0].srst_busy_us = N.d[1].srst_busy_us = 40000000ul;
    g_klog_n = 0; g_klog[0] = '\0';
    t0 = np2_now();
    CHECK(atapi_read_sectors(20, 1, buf) == ATAPI_ERR_TIMEOUT);
    CHECK(np2_now() - t0 >= ATAPI_SRST_TIMEOUT_US);
    if (strstr(g_klog, "[atapi] SRST: BSY did not clear, bus marked dead drv=0") == NULL
        || strstr(g_klog, "limit=31s") == NULL) {
        fprintf(stderr, "klog: %s", g_klog);
        CHECK(0);
    }
    np2_check_protocol();

    /* 2 回目以後: 待たずに断る */
    atapi_get_stats(&st0);
    pk0 = M.n_packets; sr0 = N.n_srst; dr0 = N.n_devreset;
    t0 = np2_now();
    CHECK(atapi_read_sectors(20, 1, buf) == ATAPI_ERR_TIMEOUT);
    CHECK(atapi_read_capacity(&cap) == ATAPI_ERR_TIMEOUT);
    CHECK(atapi_test_unit_ready() == ATAPI_ERR_TIMEOUT);
    printf("bus dead: second reads waited %lu us\n", np2_now() - t0);
    CHECK(np2_now() == t0);
    CHECK(M.n_packets == pk0 && N.n_srst == sr0 && N.n_devreset == dr0);
    atapi_get_stats(&st);
    CHECK(st.dead_fails - st0.dead_fails == 3);
    CHECK(st.soft_resets == st0.soft_resets && st.dev_resets == st0.dev_resets);
    CHECK(st.read10_cmds == st0.read10_cmds);
    for (p = g_klog; (p = strstr(p, "[atapi] bus dead since SRST timeout")) != NULL; p++) lines++;
    if (lines != 1 || strstr(g_klog, "failing at once until atapi_init drv=0") == NULL) {
        fprintf(stderr, "bus dead lines=%d klog: %s", lines, g_klog);
        CHECK(0);
    }

    /* 装置が戻った後の atapi_init (再試行) で印が解ける */
    N.d[0].busy_until = N.d[1].busy_until = 0;
    N.d[0].busy_left = N.d[1].busy_left = 0;
    N.d[0].srst_busy_us = N.d[1].srst_busy_us = 0;
    CHECK(atapi_init() == 1);
    CHECK(atapi_drive_index() == 0);
    CHECK(atapi_read_sectors(20, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 20u * SEC, SEC) == 0);
    atapi_get_stats(&st0);
    CHECK(st0.dead_fails == st.dead_fails);
    np2_check_protocol();
}

/* T2: 起動の最悪時間。atapi_init の待ちの合計 (贋の cpu_delay_us が数えた µs) を
 * 票と docs/05_drivers.md §5-6 の数に固定する。
 * (a) 2 台とも電源投入から BSY のまま (シグネチャも出ない): マスター 5 秒 +
 *     スレーブ 5 秒 + SRST の 2ms + SRST の後 31 秒 = 41.002 秒で「無し」
 * (b) 2 台ともシグネチャは出るが、最初の PACKET で固まる: マスターの容量確認 5 秒 +
 *     スレーブを選ぶ前の待ち 5 秒 + DEVICE RESET の後 5 秒 + SRST 2ms + 31 秒
 *     = 46.002 秒で「マスター」。どちらも init の後の上限は ATAPI_CMD_TIMEOUT_US */
static void t_np2_boot_worst(void)
{
    AtapiStats st;
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 0;
    g_np2cfg_no_init = 1;
    setup_np2_layout(1);
    g_np2cfg_no_init = 0;
    N.d[0].dead = N.d[1].dead = 1;
    CHECK(atapi_init() == 0);
    printf("boot worst (a): %lu us\n", g_delay_us);
    CHECK(g_delay_us == 2ul * ATAPI_INIT_TIMEOUT_US + ATAPI_SRST_SETTLE_US + ATAPI_SRST_TIMEOUT_US);
    CHECK(g_delay_us == 41002000ul);
    CHECK(s_wait_limit_us == ATAPI_CMD_TIMEOUT_US);

    g_np2cfg_no_init = 1;
    setup_np2_layout(3);
    g_np2cfg_no_init = 0;
    N.d[0].die_on_packet = N.d[1].die_on_packet = 1;
    atapi_get_stats(&st);
    CHECK(atapi_init() == 1);
    CHECK(atapi_drive_index() == 0);
    printf("boot worst (b): %lu us\n", g_delay_us);
    CHECK(g_delay_us == 3ul * ATAPI_INIT_TIMEOUT_US + ATAPI_SRST_SETTLE_US + ATAPI_SRST_TIMEOUT_US);
    CHECK(g_delay_us == 46002000ul);
    CHECK(s_wait_limit_us == ATAPI_CMD_TIMEOUT_US);
    np2_check_protocol();

    /* (c) 居ない装置が BSY (0x80) に見える機械で、1 台は居る: 居ない方のシグネチャの
     * 確認だけ 5 秒延びる (SRST はしない) */
    g_np2cfg.absent_status = 0x80;
    g_np2cfg_no_init = 1;
    setup_np2_layout(0);
    g_np2cfg_no_init = 0;
    CHECK(atapi_init() == 1);
    printf("boot worst (c): %lu us\n", g_delay_us);
    CHECK(g_delay_us == ATAPI_INIT_TIMEOUT_US);
    /* (d) 同じ機械で 1 台も居ない: シグネチャが無いので SRST し、s_present_mask が空の
     * まま 0x80 を「マスターの BSY」と見て 31 秒待つ = (a) と同じ 41.002 秒 */
    g_np2cfg_no_init = 1;
    setup_np2_layout(0);
    g_np2cfg_no_init = 0;
    N.d[0].present = 0;
    CHECK(atapi_init() == 0);
    printf("boot worst (d): %lu us\n", g_delay_us);
    CHECK(g_delay_us == 2ul * ATAPI_INIT_TIMEOUT_US + ATAPI_SRST_SETTLE_US + ATAPI_SRST_TIMEOUT_US);
    g_np2cfg.absent_status = 0xFF;
}

/* 票 §2: NOT READY / ASC 04h ASCQ 02h (initializing command required) は待っても
 * 変わらない。START STOP UNIT (開始) を 1 回出して出し直す。診断の行に ASC/ASCQ。
 * (1) START UNIT で回り始める (3 秒 BSY) → 容量が読める。250ms の待ちは挟まない
 * (2) START UNIT を受けても直らない装置 → START UNIT は 1 回だけ、媒体なしで返し
 *     最後の ASC/ASCQ を 1 行出す */
static void t_np2_start_unit(void)
{
    AtapiCapacity cap;
    AtapiStats st0, st;
    u8 buf[SEC];
    g_np2cfg.strict = 1;
    g_np2cfg.sel_lag = 0;
    setup_np2_layout(0);
    N.d[0].need_start = 1;
    N.d[0].start_busy_us = 3000000ul;
    atapi_get_stats(&st0);
    g_klog_n = 0; g_klog[0] = '\0';
    CHECK(atapi_read_capacity(&cap) == ATAPI_OK);
    CHECK(cap.total_sectors >= g_media_a.secs);
    CHECK(N.d[0].n_startunit == 1);
    atapi_get_stats(&st);
    CHECK(st.start_units - st0.start_units == 1);
    CHECK(st.dev_resets == st0.dev_resets);
    /* 3 秒の BSY を待った分だけ (時計は読み 1 回 1µs を含むので待ちは 3 秒弱)。250ms の待ちは無い */
    CHECK(g_delay_us >= 2900000ul && g_delay_us < 3000000ul);
    if (strstr(g_klog, "[atapi] NOT READY, START UNIT drv=0") == NULL
        || strstr(g_klog, "asc/ascq=04/02") == NULL) {
        fprintf(stderr, "klog: %s", g_klog);
        CHECK(0);
    }
    CHECK(atapi_read_sectors(16, 1, buf) == ATAPI_OK);
    CHECK(memcmp(buf, g_media_a.img + 16u * SEC, SEC) == 0);
    np2_check_protocol();

    setup_np2_layout(0);
    N.d[0].need_start = 1;
    N.d[0].start_ignored = 1;
    g_klog_n = 0; g_klog[0] = '\0';
    CHECK(atapi_read_capacity(&cap) == ATAPI_ERR_NO_MEDIA);
    CHECK(N.d[0].n_startunit == 1);
    if (strstr(g_klog, "[atapi] NOT READY, gave up drv=0 st=50 asc/ascq=04/02") == NULL) {
        fprintf(stderr, "klog: %s", g_klog);
        CHECK(0);
    }
    np2_check_protocol();
}

int main(int argc, char **argv)
{
    const char *cs = (argc > 1) ? argv[1] : "";
    const char *bs = getenv("CD_READ_BIGSIZE");
    if (bs) g_big_size = (u32)strtoul(bs, NULL, 10);
    if (!strcmp(cs, "stream_4k"))            t_stream_4k();
    else if (!strcmp(cs, "stream_odd"))      t_stream_odd();
    else if (!strcmp(cs, "stream_32k"))      t_stream_32k();
    else if (!strcmp(cs, "read_file"))       t_read_file();
    else if (!strcmp(cs, "multi_fallback"))  t_multi_fallback();
    else if (!strcmp(cs, "short_transfer"))  t_short_transfer();
    else if (!strcmp(cs, "bad_outside"))     t_bad_outside();
    else if (!strcmp(cs, "bad_inside"))      t_bad_inside();
    else if (!strcmp(cs, "stat_swap"))       t_stat_swap();
    else if (!strcmp(cs, "list_swap"))       t_list_swap();
    else if (!strcmp(cs, "lru_order"))       t_lru_order();
    else if (!strcmp(cs, "ua_mount"))        t_ua_mount();
    else if (!strcmp(cs, "multi_drq"))       t_multi_drq();
    else if (!strcmp(cs, "path_cache"))      t_path_cache();
    else if (!strcmp(cs, "dir_lru"))         t_dir_lru();
    else if (!strcmp(cs, "list_reentrant"))  t_list_reentrant();
    else if (!strcmp(cs, "unit_attention"))  t_unit_attention();
    else if (!strcmp(cs, "idle_rule"))       t_idle_rule();
    else if (!strcmp(cs, "no_window"))       t_no_window();
    else if (!strcmp(cs, "replay"))          t_replay();
    else if (!strcmp(cs, "np2_read"))        t_np2_read();
    else if (!strcmp(cs, "np2_async"))       t_np2_async();
    else if (!strcmp(cs, "np2_empty"))       t_np2_empty();
    else if (!strcmp(cs, "cap_nodata"))      t_cap_nodata();
    else if (!strcmp(cs, "np2_slave"))       t_np2_slave();
    else if (!strcmp(cs, "np2_slave_strict")) { g_np2cfg.strict = 1; t_np2_slave(); }
    else if (!strcmp(cs, "cap_len"))         t_cap_len();
    else if (!strcmp(cs, "np2_sel_lag"))     t_np2_sel_lag();
    else if (!strcmp(cs, "np2_absent"))      t_np2_absent();
    else if (!strcmp(cs, "np2_sel_busy"))    t_np2_sel_busy();
    else if (!strcmp(cs, "np2_stuck"))       t_np2_stuck();
    else if (!strcmp(cs, "np2_ua_init"))     t_np2_ua_init();
    else if (!strcmp(cs, "np2_becoming_ready")) t_np2_becoming_ready();
    else if (!strcmp(cs, "np2_no_medium"))   t_np2_no_medium();
    else if (!strcmp(cs, "np2_ready_total")) t_np2_ready_total();
    else if (!strcmp(cs, "np2_srst"))        t_np2_srst();
    else if (!strcmp(cs, "ua_multi"))        t_ua_multi();
    else if (!strcmp(cs, "np2_spinup"))      t_np2_spinup();
    else if (!strcmp(cs, "np2_srst_long"))   t_np2_srst_long();
    else if (!strcmp(cs, "np2_boot_worst"))  t_np2_boot_worst();
    else if (!strcmp(cs, "np2_start_unit"))  t_np2_start_unit();
    else if (!strcmp(cs, "np2_bus_dead"))    t_np2_bus_dead();
    else { fprintf(stderr, "unknown case '%s'\n", cs); return 2; }
    return 0;
}
