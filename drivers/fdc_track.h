/* ======================================================================== */
/*  FDC_TRACK.H — FD の読み出しをトラック単位に束ねる (I/O を触らない)      */
/*                                                                          */
/*  fs/fatfs/diskio.c の disk_read (DRV_FDD) が呼ぶ。ここは「要求をどう     */
/*  区切り、どこまで先読みし、失敗したらどう読み直すか」だけを決める純粋な  */
/*  層で、実際の読み出しは呼び手が渡す関数 (fdc_read_sectors /              */
/*  fdc_read_sector_geom) に任せる。**ホストで偽の fdc を差して試験する**。 */
/*                                                                          */
/*  なぜ要るか (実機 PC-9821Ra266、2026-09-24):                             */
/*    旧 disk_read は 1 セクタずつ SEEK + 20ms 待ち + READ DATA を出して     */
/*    いた。次のセクタはヘッドの下を過ぎているので、1 セクタごとにほぼ       */
/*    1 回転 (360rpm で約 167ms) を待つ。既定フォント (188KB) の読み込みで   */
/*    1 分以上止まった。NP21/W は回転待ちもシーク時間も模擬しないので       */
/*    エミュレータでは再現しない。                                          */
/*                                                                          */
/*  **先読みが要る理由**: FatFs は FF_FS_TINY=1 で、セクタに揃わない読みを  */
/*  1 セクタの窓 (count=1) で回す。フォントの読み込みは 16B のヘッダの後に  */
/*  1024B ずつ読む (kernel/boot_font.c の kcg_read_chunked) ので、**全部    */
/*  count=1 で来る**。要求を束ねるだけでは 1 セクタ 1 コマンドのまま        */
/*  変わらないので、要求したセクタからトラックの終わりまでを 1 回の READ    */
/*  DATA で読み、残りを持っておいて次の要求に当てる。                       */
/*                                                                          */
/*  **持つのは FDC_TRACK_SLOTS 本 (LRU)**。FF_FS_TINY=1 では FAT とデータが  */
/*  1 つの窓を取り合い、クラスタ (2HD は 1 セクタ) を越えるたびに FAT の     */
/*  セクタ (シリンダ 0) を読み直す。1 本だけだと FAT のトラックとデータの    */
/*  トラックが交互に追い出し合い、1KB ごとにシークが 2 回出た (9ed7c80 を   */
/*  NP21/W で起動して速くならなかった理由、2026-09-24)。                    */
/*                                                                          */
/*  **セクタキャッシュ (2026-09-24 夕)**: 6541ef1 の実測で multi=767 /      */
/*  seek=751 — 先読みがほぼ当たらなかった。VFS (fs/fatfs_vfs.c の           */
/*  fatfs_vfs_read_stream) が 1 回の読みごとに f_open → f_lseek → f_read   */
/*  を回すので、1KB ごとにルートディレクトリ・/sys・/sys/font・FAT・       */
/*  データの 4〜5 本のトラックを巡回し、2 本の LRU は 1 回も当たらない。    */
/*  **count=1 の読み (FatFs の窓の出し入れ) だけを FDC_SECTOR_SLOTS 個の    */
/*  LRU で覚える**。毎回使うディレクトリと FAT のセクタが居続け、データは  */
/*  トラックの先読みから出る。                                              */
/*                                                                          */
/*  **中身の世代**: スロットは埋めたときの fdc_media_gen() を覚え、違えば   */
/*  当てない。書き込みは FatFs 以外 (dev.c の fd0/fd1、KAPI の              */
/*  dev_blk_write) からも fdc_write_sector_geom に来るので、diskio.c の     */
/*  破棄だけでは古い中身が残る (レビューの指摘)。                          */
/*                                                                          */
/*  試験: tools/tests/test_fdc_track.py + tools/tests/fdc_track_host.c      */
/*  記録: tools/tests/fdc_track_tdd.md                                      */
/* ======================================================================== */

#ifndef FDC_TRACK_H
#define FDC_TRACK_H

#include "types.h"
#include "fdc.h"    /* struct fdc_geom / FDC_TRACK_MAX_BYTES */

/* 先読み。1 = 要求したセクタからトラックの終わり (EOT = spt) まで読んで
 * 持っておく。0 = 要求した範囲だけを読む (束ねるだけ)。
 * 0 にすると count=1 の読み (FatFs の窓) は速くならない (上の注記)。 */
#define FDC_TRACK_READ_AHEAD  1

/* **2 秒規則** (MS-DOS と同じ考え方。2026-09-24 ラリー 2 の Fable 案)。
 * 最後に読んでから FDC_TRACK_IDLE_TICKS を超えていたら、トラックとセクタの
 * 両方を捨ててから読む。
 *   - FRY=1 (fdc.h の CTRL_FRY) だと Ready 線の変化が FDC に届かず、同じ
 *     形式の媒体の差し替えは Ready 変化の割り込みにならない
 *     (docs/hw/undocumented/io_fdd.md の 0094h: FRY は RDY と OR される)。
 *     NP21/W も交換で IRQ を出さない
 *   - pending の Ready 通知を SIS で読む前にキャッシュが当たる件 (Codex)
 *     も、ここで上限が付く
 * 人が媒体を入れ替えるのに 2 秒はかかる、という前提。**それより速い差し
 * 替えは保証しない** — 媒体を替えたら umount / mount する契約
 * (docs/06_filesystem.md §6-8 の FD の節)。 */
#define FDC_TRACK_IDLE_TICKS  200   /* 2 秒 (PIT 100Hz) */

/* 1 回の要求を 1 トラックの中に切った区間。 */
struct fdc_run {
    int cyl;
    int head;
    int sect;       /* 1 始まり */
    int count;      /* この区間で要求を満たすセクタ数 (>= 1) */
};

/* 持っているトラック 1 本。buf は呼び手が用意する (FDC_TRACK_MAX_BYTES)。
 * **有効なのは valid != 0 かつ gen が今の世代と同じときの [first, last] だけ**。 */
struct fdc_track_slot {
    int valid;
    int drv;
    int cyl;
    int head;
    int first;      /* 持っている最初のセクタ (1 始まり) */
    int last;       /* 持っている最後のセクタ */
    const struct fdc_geom *geom;   /* 読んだときのジオメトリ (変われば捨てる) */
    u32 gen;        /* 読んだときの中身の世代 (ops->gen) */
    u32 used;       /* 最後に使った順番 (LRU) */
    u8 *buf;        /* first のセクタが buf[0] から並ぶ */
};

/* 覚えている 1 セクタ。buf は呼び手が用意する (FDC_SECTOR_SLOT_BYTES)。 */
struct fdc_sector_ent {
    int valid;
    int drv;
    u32 lba;
    const struct fdc_geom *geom;
    u32 gen;
    u32 used;
    u8 *buf;
};

struct fdc_track_cache {
    struct fdc_track_slot slot[FDC_TRACK_SLOTS];
    struct fdc_sector_ent sec[FDC_SECTOR_SLOTS];
    u32 clock;      /* LRU の時計 (スロットとセクタで共通) */
    int touched;    /* last_tick が有効か */
    u32 last_tick;  /* 最後に読んだ時刻 (ops->now、2 秒規則) */
    u32 idle_drops; /* 2 秒規則で捨てた回数 */
    /* 数 (試験と起動時の 1 行が読む) */
    u32 sec_hits;   /* セクタキャッシュで返した */
    u32 trk_hits;   /* トラックの先読みで返した (区間の数) */
    u32 fills;      /* 先読みを埋めた (まとめ読みを出した) */
    /* 先読みが失敗したトラック (1 本だけ覚える)。ここでは先読みをやめ、
     * 要求した範囲だけを束ねて読む — 要求の外に傷んだセクタがあるだけで
     * そのトラックの要求が毎回「まとめ読みの失敗 + 回復 + 1 セクタずつ」を
     * 踏まないように。fdc_track_invalidate で忘れる (書けば直るかもしれない)。 */
    int bad_valid;
    int bad_drv;
    int bad_cyl;
    int bad_head;
    const struct fdc_geom *bad_geom;
};

/* 実際の読み出し。呼び手 (diskio.c) が fdc の関数を差す。
 *   read_multi : 同じ cyl/head の sect から count セクタを 1 コマンドで読む。
 *                0 = 成功 / 負 = 失敗 (**リトライしない 1 回きり**でよい —
 *                失敗したら下で 1 セクタずつ読み直す)。
 *   read_one   : 1 セクタを読む (リトライと回復は fdc 側が持つ)。
 *   copy       : dst へ n バイト写す (カーネルは kmemcpy)。
 *   gen        : ドライブの中身の世代 (カーネルは fdc_media_gen)。
 *   now        : いまの時刻 (tick。カーネルは tick_count。2 秒規則)。 */
struct fdc_track_ops {
    int  (*read_multi)(void *ctx, int drv, int cyl, int head, int sect,
                       int count, const struct fdc_geom *g, void *buf);
    int  (*read_one)(void *ctx, int drv, int cyl, int head, int sect,
                     const struct fdc_geom *g, void *buf);
    void (*copy)(void *dst, const void *src, u32 n);
    u32  (*gen)(void *ctx, int drv);
    u32  (*now)(void *ctx);
    void *ctx;
};

/* スロット i の受け皿を渡す (FDC_TRACK_MAX_BYTES)。中身は捨てる。 */
void fdc_track_init(struct fdc_track_cache *c, int i, u8 *buf);

/* セクタキャッシュのスロット j の受け皿を渡す (FDC_SECTOR_SLOT_BYTES)。
 * NULL ならそのスロットは使わない。 */
void fdc_track_init_sector(struct fdc_track_cache *c, int j, u8 *buf);

/* lba から count セクタの要求のうち、最初の 1 トラック分を *run に返す。
 * 戻り値はその区間のセクタ数 (= run->count)。count == 0 か、ジオメトリが
 * 壊れている (spt / heads が 0) ときは 0。 */
int fdc_track_split(const struct fdc_geom *g, u32 lba, u32 count,
                    struct fdc_run *run);

/* 区間 run を 1 回で読むとき、何番のセクタまで読むか (EOT)。
 * 先読みが有効なら spt、無効なら run の最後のセクタ。
 * run が spt を越えていれば 0 (呼び手の誤り)。 */
int fdc_track_eot(const struct fdc_geom *g, const struct fdc_run *run);

/* 中身 (と先読みをやめたトラックの印) を捨てる。書き込み・メディアの
 * 変更・ドライブの切り替え・disk_initialize のたびに呼ぶ。 */
void fdc_track_invalidate(struct fdc_track_cache *c);

/* 持っている中身で run を満たせるスロットの番号。無ければ -1。
 * gen は今の世代 (違うスロットは当てない)。 */
int fdc_track_hit(const struct fdc_track_cache *c, int drv,
                  const struct fdc_geom *g, u32 gen,
                  const struct fdc_run *run);

/* lba から count セクタを buff へ読む。
 *   - 要求をトラックの境目で区切る
 *   - 持っている中身で満たせる区間はそこから写す
 *   - そうでなければ sect から EOT までを read_multi で c->buf へ読み、
 *     要求分を写して中身として持つ
 *   - **入口で 2 秒規則**: 最後の読みから FDC_TRACK_IDLE_TICKS を超えて
 *     いたら両方のキャッシュを捨てる
 *   - **count == 1 の要求はまずセクタキャッシュを見て、返した後に覚える**
 *   - read_multi が -3 (NR = 媒体無し) を返したら、1 セクタずつへ落とさずに
 *     打ち切る (-1)
 *   - read_multi が失敗したら中身を捨て、**要求した区間だけ**を read_one で
 *     1 セクタずつ buff へ読み直す (先読みの分は読み直さない)。先読みの
 *     読みが落ちたトラックは印を付け、以後そこでは要求の範囲だけを読む
 * 0 = 成功 / -1 = どこかのセクタが読めなかった。
 * spt × bps が FDC_TRACK_MAX_BYTES を超えるジオメトリでは束ねずに
 * 1 セクタずつ読む (受け皿に入らない)。 */
int fdc_track_read(struct fdc_track_cache *c, const struct fdc_track_ops *ops,
                   int drv, const struct fdc_geom *g,
                   u32 lba, u32 count, u8 *buff);

#endif /* FDC_TRACK_H */
