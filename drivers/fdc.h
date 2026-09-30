/* ======================================================================== */
/*  FDC.H — PC-98 フロッピーディスクコントローラ直接制御ドライバ            */
/*                                                                          */
/*  µPD765A (1MB FDC) + µPD8237A (DMA ch2) を                               */
/*  32ビットプロテクトモードから I/Oポート経由で直接制御する。               */
/*  BIOS呼び出し (INT 1Bh) やリアルモード遷移は一切不要。                   */
/*                                                                          */
/*  出典: PC9800Bible §2-9, §1-5, §4-3 / OSDev Wiki FDC                    */
/* ======================================================================== */

#ifndef FDC_H
#define FDC_H

#include "types.h"       /* u8, u16, u32 型定義 */
#include "fdc_decide.h"  /* ST0 のビット定義と純粋な判定 (ホストで試験する) */

/* ======================================================================== */
/*  µPD765A FDC I/Oポート (PC-98 1MB FDD)                                  */
/* ======================================================================== */
#define FDC_MSR     0x90    /* R:  メインステータスレジスタ */
#define FDC_FIFO    0x92    /* RW: データレジスタ (コマンド/リザルト/データ) */
#define FDC_CTRL    0x94    /* W:  コントロールレジスタ */
                            /* R:  リードスイッチ (FDDタイプ) */

/* --- MSRビットフラグ --- */
#define MSR_RQM     0x80    /* Request for Master: FDC準備完了 */
#define MSR_DIO     0x40    /* Data I/O direction: 1=FDC→CPU, 0=CPU→FDC */
#define MSR_NDMA    0x20    /* Non-DMA実行フェーズ中 */
#define MSR_BUSY    0x10    /* FDCビジー (コマンド実行中) */
#define MSR_ACTD    0x08    /* ドライブD アクティブ */
#define MSR_ACTC    0x04    /* ドライブC アクティブ */
#define MSR_ACTB    0x02    /* ドライブB アクティブ */
#define MSR_ACTA    0x01    /* ドライブA アクティブ */

/* --- コントロールレジスタ (0x94) ビット --- */
/* PC-98固有: FDDコントローラのリセットやモーター制御 */
#define CTRL_MTON   0x08    /* モーターON */
#define CTRL_DMAE   0x10    /* DMA有効 */
#define CTRL_FRY    0x40    /* Forced Ready: RDY を強制的にアクティブ */
#define CTRL_RST    0x80    /* FDCリセット */

/* ======================================================================== */
/*  CTRL_FRY (bit6) — **立てないと実機でコマンドが全部 Not Ready で返る**   */
/*                                                                          */
/*  正典: docs/hw/undocumented/io_fdd.md「I/O 0094h ライトコントロール       */
/*  レジスタ」                                                              */
/*    bit 6: FRY(Forced Ready)                                              */
/*      1= RDY信号を強制的にアクティブ / 0= NOP                             */
/*      * μPD765AのRDY端子への入力信号。ドライブからのRDY信号と            */
/*        ORしたあとFDCに入力される。                                       */
/*                                                                          */
/*  µPD765A は RDY 端子が非アクティブだと、RECALIBRATE / SEEK / READ を     */
/*  受け取っても**実行せずに ST0 の NR (bit3) を立てて即終了する**。        */
/*  RDY をドライブ側から出さない (= ホスト側が FRY で作る前提の) I/F では、  */
/*  FRY を落としたままだと FDC は何もしない。                               */
/*  実機 PC-9821Ra266 の FD 起動が fdc_init() の失敗 → MOUNT の root panic  */
/*  になっていた原因の 1 つ (2026-09-22)。                                  */
/*                                                                          */
/*  NP21/W では露見しない — src/io/fdc.c の FDC_Recalibrate は媒体の入った  */
/*  ドライブを fdd_diskready() で Ready と見なすので、ctrlreg bit6 を        */
/*  参照する分岐 (未装着ドライブ / 媒体無し) にしか効かない。               */
/*                                                                          */
/*  FRY を落とすのは「FDC を動かさないとき」だけでよいので、0x94 へ         */
/*  動作状態を書くところは全部この 3 ビットを揃えて書く。                   */
/*  (モーターを止める fdc_motor_off は 0 を書く — そちらは据え置き。)       */
/* ======================================================================== */

/* ======================================================================== */
/*  µPD765A コマンドコード                                                  */
/* ======================================================================== */
#define FDC_CMD_SPECIFY         0x03
#define FDC_CMD_SENSE_DRIVE     0x04
#define FDC_CMD_WRITE_DATA      0x05    /* + MF + MT */
#define FDC_CMD_READ_DATA       0x06    /* + MF + MT */
#define FDC_CMD_RECALIBRATE     0x07
#define FDC_CMD_SENSE_INTERRUPT 0x08
#define FDC_CMD_READ_ID         0x0A    /* + MF */
#define FDC_CMD_FORMAT_TRACK    0x0D    /* + MF */
#define FDC_CMD_SEEK            0x0F

/* コマンドオプションビット */
#define FDC_OPT_MT    0x80    /* マルチトラック */
#define FDC_OPT_MF    0x40    /* MFM (倍密度) — 常にセット */
#define FDC_OPT_SK    0x20    /* Skip deleted sectors */

/* ======================================================================== */
/*  SPECIFY のパラメータ (値は据え置き。2 か所から使うので名前を付ける)     */
/* ======================================================================== */
/*  SRT = 16 - (8ms × 500kHz / 500000) = 8、HUT = 0 (最大)                  */
#define FDC_SPECIFY_SRT_HUT   0x80
/*  HLT = 10ms × 500kHz / 1000000 = 5、NDMA = 0 (DMA モード)                */
#define FDC_SPECIFY_HLT_DMA   0x0A

/* ======================================================================== */
/*  DMA (µPD8237A) — FDC が使うチャネル                                    */
/*                                                                          */
/*  **ポート番号とモードバイトは drivers/dma8237.h が正典** ([C4])。        */
/*  かつてここに ch2 決め打ちの 0009h/000Bh/0023h/0015h/0017h/0019h と      */
/*  モード値 46h/4Ah を持っていたが、CS4231 が #1/#3 を使うので装置から     */
/*  切り離した (票 docs/tasks/v3/TASK_HAL_WIRING.md §1-2)。fdc.c は         */
/*  dma_chan_mask / dma_chan_setup / dma_chan_unmask しか呼ばない。         */
/*                                                                          */
/*  チャネルの選択は DIP SW 3-1/3-2 (1MB I/F モードなら ch2、640KB I/F      */
/*  モードなら ch3。io_dma.md 35〜50 行)。OS32 は 1MB I/F 固定なので 2。    */
/* ======================================================================== */
#define FDC_DMA_CHANNEL  2

/* ======================================================================== */
/*  I/O 0439h — DMA アクセス制御 (Undocumented)                             */
/*                                                                          */
/*  **扱いは drivers/dma8237.c の dma8237_init() へ移した** (票 §1-2)。     */
/*  DMA プール (0x2E8000) もカーネル (0x100000〜) も 1MB 超にあるので、     */
/*  これは FDC だけの都合ではない。定数 (SYSPORT_DMA_CTRL /                 */
/*  SYSPORT_DMA_MASK_1MB) と経緯の註は drivers/dma8237.h にある。           */
/*  fdc_get_last_init_status() が返す 0439h の前後は dma_above_1mb_raw()    */
/*  の写し (起動時の状態行の書式を変えないため)。                           */
/* ======================================================================== */

/* ======================================================================== */
/*  割り込み                                                                */
/* ======================================================================== */
/* PC-98: 2HD FDD = スレーブ IR11 → PIC_SLAVE_OFFSET + 3 = 0x2B */
#define FDC_IRQ     11    /* スレーブPIC IR11 */

/* ======================================================================== */
/*  ディスクパラメータ (PC-98 2HD 1MB MFM) — 互換マクロ                    */
/* ======================================================================== */
#define FDC_CYLINDERS    77     /* シリンダ数 (0-76) */
#define FDC_HEADS        2      /* ヘッド数 */
#define FDC_SPT          8      /* セクタ/トラック */
#define FDC_SECTOR_SIZE  1024   /* バイト/セクタ (N=3) */
#define FDC_SECTOR_N     3      /* セクタ長コード (3=1024) */
#define FDC_GAP3         0x74   /* Read/Write ギャップ長 (MFM 1024byte/sec) */
#define FDC_TOTAL_SECTORS (FDC_CYLINDERS * FDC_HEADS * FDC_SPT)  /* 1232 */
#define FDC_TIMEOUT_LOOP 10000  /* BSY等待ちのためのループカウンタ上限 */

/* ======================================================================== */
/*  IRQ11 待ちタイムアウト (tick 単位。PIT_HZ = 100 なので 1 tick = 10ms)    */
/*                                                                          */
/*  ここは長いあいだ 20 tick (200ms) の一本値だった。あれは **NP21/W に      */
/*  合わせた値**。エミュレータはシークの所要を **シリンダ距離に依らず固定** */
/*  にしている — np21w-src/src/io/fdc.c の FDC_Seek / FDC_Recalibrate は    */
/*  fdc.int_timer[us] に FDC_INT_DELAY (= 6、pccore_exec のフレーム単位で    */
/*  約 100ms) を置くだけで、fdc_intdelay() がそれを 0 まで数えてから         */
/*  fdc_interrupt() を呼び、そこから 512 サイクル後に IRQ を予約する。       */
/*  **実機のシーク時間に比べれば即時**なので 200ms でも足りていた。         */
/*  **実機 PC-9821Ra266 では足りず**、FD 起動が MOUNT... の root panic に    */
/*  なっていた (2026-09-22)。                                               */
/*  用途ごとに実機の機構から引き直す ([C4]: 根拠を数字の隣に置く)。         */
/* ======================================================================== */

/* SEEK / RECALIBRATE。
 *   SPECIFY の SRT は 8ms (下の 0x80)。RECALIBRATE は 1 回で最大 77 ステップ、
 *   SEEK は 80 シリンダ媒体で最大 79 トラック分踏む。
 *     8ms × 80 トラック = 640ms  + ヘッドセトリング (約 15ms) ≒ 655ms
 *   余裕 2 倍で 1.5 秒。
 *   ローダが VMKRNL.LZ4 を読んだ直後のヘッドはシリンダ 20〜40 付近に居るので
 *   (20 × 8ms = 160ms、40 × 8ms = 320ms)、fdc_init() の RECALIBRATE は
 *   ここが 200ms だと頻繁にタイムアウトし、そのあと未回収の割り込みが残った。 */
#define FDC_SEEK_TIMEOUT_TICKS    150

/* READ DATA / WRITE DATA。
 *   目的セクタが直前に通過していると最大 2 回転待つ。300rpm (1.44MB) なら
 *   1 回転 200ms で最悪 400ms、360rpm (2HD 1232KB) なら 167ms で 334ms。
 *   これに HLT 10ms のヘッドロードと 1 セクタの転送を足して約 420ms。
 *   余裕 2 倍で 1 秒。 */
#define FDC_RW_TIMEOUT_TICKS      100

/* まとめ読み (READ DATA で EOT まで複数セクタ) の時間上限。
 *   単発の上の見積もりと同じく機構の最悪値から引く (POLICY_DEBUG §4-51):
 *     最初のセクタを探す      最悪 2 回転
 *     count セクタを読む      count / spt 回転 (切り上げ)
 *     ヘッドロード (HLT)       10ms
 *   回転は遅い方の 300rpm (1 回転 200ms = 20 tick) で取る。
 *   1 トラック全部 (count = spt) なら 2 + 1 = 3 回転 = 600ms + 10ms。
 *   余裕 2 倍で 1.22 秒 (122 tick)。シークは fdc_seek が自分の上限
 *   (FDC_SEEK_TIMEOUT_TICKS) で別に待つので、ここには入れない。
 *   1 セクタなら 2 × (40 + 2 + 1) = 86 tick で、単発の 100 tick を
 *   下回るので **単発の値を下限にする** (fdc.c の fdc_rw_multi_timeout)。 */
#define FDC_ROT_TICKS_WORST       20   /* 300rpm の 1 回転 = 200ms */
#define FDC_FIND_ROTATIONS        2    /* 目的セクタが直前に通過していた場合 */
#define FDC_HEAD_LOAD_TICKS       1    /* HLT 10ms (FDC_SPECIFY_HLT_DMA) */
#define FDC_TIMEOUT_MARGIN        2    /* 余裕 2 倍 */

/* リセット完了の割り込み待ち。**ここは短くする**。
 *   - シークを伴わないので、実機ならリセット後の Ready-change 通知は
 *     µs〜ms のオーダーで来る。
 *   - 来なくても困らない。直後の SIS 排水が同じ通知を回収するので、
 *     待ちは「来れば早く進む」ための上乗せでしかない。
 *   - **NP21/W はこの経路で IRQ を出さない**。fdc_o94 のリセット処理は
 *     `dat & 0x08` (モーター ON) を同時に書いたときしか割り込みを積まず、
 *     こちらは 0x80 (RST だけ) を書くため。ここを長くすると、媒体の無い
 *     ドライブを読むたびに丸ごと空待ちする。
 * 100ms。 */
#define FDC_RESET_TIMEOUT_TICKS   10

/* 旧名。**エミュレータに合わせた 200ms の一本値**で、実機のシークには
 * 足りなかった。残してあるのは外部の参照を壊さないためだけで、新しい
 * コードは用途別の定数を使うこと。 */
#define FDC_IRQ_TIMEOUT_TICKS  FDC_RW_TIMEOUT_TICKS

/* ======================================================================== */
/*  回数の上限 (どれも無限ループを作らないための縛り)                       */
/* ======================================================================== */

/* SENSE INTERRUPT STATUS の排水回数。µPD765A は 4 ドライブ分の完了通知を
 * 溜められるので 4 回で必ず尽きる。尽きたかどうかは ST0 = 80h で判る。 */
#define FDC_SIS_DRAIN_MAX      4

/* RECALIBRATE を出す回数。1 回で 77 ステップ踏めるので、80 シリンダ媒体の
 * いちばん奥 (シリンダ 79) からでも 2 回でトラック 0 に届く。 */
#define FDC_RECAL_ATTEMPTS     2

/* READ / WRITE DATA のリトライ回数。 */
#define FDC_RW_RETRIES         3

/* MSR が次のリザルトバイトを出すまで待つループ回数。1 周が fdc_delay()
 * 2 回 (数 µs) なので、200 周でも 1ms に満たない。リザルトフェーズの
 * バイト間隔は µs のオーダーなので十分。
 * fdc_read_results() の「CB=1 のまま RQM/DIO が揃わない」空回りにも
 * **同じ上限を掛ける** — 縛りが無いと壊れた FDC で無限に回る。 */
#define FDC_MSR_SETTLE_LOOP    200

/* ======================================================================== */
/*  DMA バッファの配置 ([HW2])                                              */
/* ======================================================================== */

/* µPD8237A はアドレスの下位 16 ビットしか回さない。64KB 境界をまたぐ転送は
 * バンクの先頭へ巻き戻って別の番地を壊す。
 *
 * **受け皿は 1 本の静的な領域 (FDC_BUF_BYTES) で、DMA の窓と先読みの
 * スロットを兼ねる** (2026-09-24 の見直し)。
 *   - DMA の窓: 1 トラック分。既知のジオメトリで最大のものは 1.44MB の
 *     18 × 512 = 9216B (2HD 1232KB は 8 × 1024 = 8192B)。MT (ヘッド 0 → 1)
 *     は使わないので 1 シリンダ分は要らない (fdc.c の注記)。
 *   - 先読みのスロット: 1 トラック分 × FDC_TRACK_SLOTS。CPU しか触らない
 *     ので 64KB 境界をまたいでよい。
 * 窓の位置は fdc_buf_layout() (fdc_decide.c) が**実行時に**決める: 領域は
 * 64KB より短いので境界は高々 1 本で、窓は先頭か末尾のどちらかに必ず
 * 取れる。**揃え指定に頼らない** — 16KB 揃えにしていたころは、その前に
 * 最大 16KB の詰め物が出ていた (レビューの指摘)。
 *
 * DMA プール (0x2E8000) は使わない: fdc_init() は dma_pool_init() より前に
 * 走り、kselftest_run() がプールを作り直す (kernel/kernel.c の注記)。 */
#define FDC_TRACK_MAX_BYTES  (18 * 512)  /* 1.44MB の 1 トラック = 9216B */
#define FDC_DMA_BUF_SIZE     FDC_TRACK_MAX_BYTES
/* 先読みのスロット数と、1 セクタの読みを覚えるセクタキャッシュの数。
 *
 * 2026-09-24 夕の NP21/W の実測 (6541ef1、`[fdc] font:` の行) は
 * multi=767 / seek=751 で、先読みがほとんど当たっていなかった。原因は
 * **VFS が 1 回の読みごとにファイルを開き直す**こと —
 * fs/fatfs_vfs.c の fatfs_vfs_read_stream は f_open → f_lseek → f_read →
 * f_close を毎回回すので、1KB のチャンクごとにルートディレクトリ
 * (シリンダ 0)、/sys と /sys/font のディレクトリ (データ域のどこか)、
 * FAT (シリンダ 0)、データの 4〜5 本のトラックを巡回する。巡回の長さが
 * スロットの数を超えると LRU は 1 回も当たらない。
 *
 * トラックのスロットを増やす (1 本 9KB) 代わりに、**1 セクタの読み
 * (FatFs の窓の出し入れ) だけを覚える小さな LRU** を足した。毎回使う
 * ディレクトリと FAT のセクタはここに居続け、データはトラックの先読みから
 * 出る。FatFs は FF_FS_TINY=1 なので、窓の出し入れは全部 count=1 で来る。 */
#define FDC_TRACK_SLOTS      1
#define FDC_SECTOR_SLOTS     8
#define FDC_SECTOR_SLOT_BYTES 1024       /* 既知のジオメトリで最大の bps */
#define FDC_CACHE_BYTES      (FDC_TRACK_SLOTS * FDC_TRACK_MAX_BYTES + \
                              FDC_SECTOR_SLOTS * FDC_SECTOR_SLOT_BYTES)
#define FDC_BUF_BYTES        (FDC_DMA_BUF_SIZE + FDC_CACHE_BYTES)
#define FDC_DMA_BANK_SIZE    0x10000     /* DMA バンク = 64KB */
#define FDC_DMA_BANK_MASK    0xFFFF      /* バンク内オフセットの取り出し */
STATIC_ASSERT(FDC_BUF_BYTES < FDC_DMA_BANK_SIZE, fdc_buf_shorter_than_bank);
/* fdc_buf_layout は「境界が先頭の窓の中なら末尾の窓はまたがない」に頼る。
 * それには領域が窓の 2 倍以上であること。 */
STATIC_ASSERT(FDC_BUF_BYTES >= 2 * FDC_DMA_BUF_SIZE, fdc_buf_twice_window);
STATIC_ASSERT(FDC_SECTOR_SIZE <= FDC_DMA_BUF_SIZE, fdc_dma_buf_holds_sector);

/* ======================================================================== */
/*  メディア種別 (FDI/実FDDのジオメトリ選択に使用)                          */
/* ======================================================================== */
typedef enum {
    FDC_MEDIA_2HD_1232 = 0,  /* 1.2MB (PC-98標準, 77×2×8×1024) */
    FDC_MEDIA_2DD_640  = 1,  /* 640KB (80×2×8×512) */
    FDC_MEDIA_2DD_720  = 2,  /* 720KB (80×2×9×512) */
    FDC_MEDIA_2D_256   = 3,  /* 2D (77×2×16×256) — 古いPC-98ゲーム用 */
    FDC_MEDIA_2HD_1440 = 4   /* 1.44MB (80×2×18×512) — DA/UA 0x30 系 */
} fdc_media_t;

/* メディアジオメトリ構造体 */
struct fdc_geom {
    u8  cyls;       /* シリンダ数 */
    u8  heads;      /* ヘッド数 */
    u8  spt;        /* セクタ/トラック */
    u8  sec_n;      /* セクタ長コード (2=512B, 3=1024B) */
    u16 bps;        /* バイト/セクタ */
    u8  gap3;       /* GAP3長 (R/W用) */
    u8  daua_high;  /* DA/UA上位ニブル (0x90/0x10/0x70) */
};

/* 既知メディア定義 (drivers/fdc.c で実体化) */
extern const struct fdc_geom fdc_geom_2hd;
extern const struct fdc_geom fdc_geom_2dd_640;
extern const struct fdc_geom fdc_geom_2dd_720;
extern const struct fdc_geom fdc_geom_2d_256;
extern const struct fdc_geom fdc_geom_144;

/* ======================================================================== */
/*  ドライブごとの現在ジオメトリ                                            */
/*                                                                          */
/*  上の FDC_* マクロは 2HD 1232KB の値で、**互換のために残してある**。      */
/*  実際に読み書きする側は必ず fdc_get_geom() を通すこと — マクロを直に     */
/*  使うと 1.44MB のディスクを 1024B/8セクタとして読んでゴミを掴む          */
/*  (2026-09-18 に MOUNT root panic として実測)。                           */
/* ======================================================================== */

/* ======================================================================== */
/*  3モードFD I/F 制御 (I/O 04BEh、Undocumented)                            */
/*                                                                          */
/*  正典: docs/hw/undocumented/io_fdd.md の「I/O 04BEh 3モードFD I/F制御」。 */
/*  1.44MB アクセスは **1MB I/F モードのときだけ**可能で、640KB I/F モード   */
/*  ではどのドライブも不可を示す。                                          */
/*                                                                          */
/*  資料の注意 2 つ:                                                        */
/*   - **リードの前に必ずライトしてドライブを指定する。**                    */
/*   - **読んだ値が FFh かどうかで搭載を判断してはいけない** —               */
/*     I/O 00BEh のデコードイメージが 04BEh に出る機種がある。              */
/* ======================================================================== */
#define FDC_IO_3MODE        0x04BE

/* WRITE */
#define FDC_3M_DRV_SHIFT    5       /* bit6,5: ドライブ指定 (00b=1台目) */
#define FDC_3M_APPLY        0x10    /* bit4=1: bit0 のモード指定を有効にする */
#define FDC_3M_MODE_144     0x01    /* bit0=1: 1.44MB アクセスモード */

/* READ */
#define FDC_3M_CAP_144      0x10    /* bit4: 1 = 1.44MB アクセス可能 */
#define FDC_3M_CUR_144      0x01    /* bit0: 1 = いま 1.44MB アクセスモード */

/* drv のアクセスモードを切り替える。on!=0 で 1.44MB、0 で 1MB/640KB。
 * 0 = 読み戻しで要求どおりになった / -1 = ならなかった。
 *
 * **搭載判定には使えない** (上の注意)。呼ぶのは「1.44MB の I/F が居ると
 * 分かっているとき」だけにする — 起動した DA/UA が 0x30 系なら BIOS が
 * その I/F を使った証拠になる。 */
int fdc_set_3mode(int drv, int on);

/* drv のいま選ばれているジオメトリ。既定は 2HD 1232KB。 */
const struct fdc_geom *fdc_get_geom(int drv);

/* drv のメディアを選ぶ。0 で成功、-1 で未知の種別。
 * **dev_init() より前に呼ぶこと** — デバイス記述子がここから値を取る。 */
int fdc_set_media(int drv, fdc_media_t media);

/* ブートローダから渡された DA/UA でメディアを選ぶ (0x30 系 = 1.44MB)。
 * 対応する種別が無ければ 2HD のまま 0 を返す (既存構成を壊さない)。 */
int fdc_set_media_by_daua(int drv, u32 daua);

/* ======================================================================== */
/*  FDCドライバAPI                                                          */
/* ======================================================================== */

/* FDC初期化: リセット → Specify → Recalibrate */
int fdc_init(void);

/* 直近の fdc_init() が何を見たかを返す (起動時の状態行用)。
 * 戻り値は fdc_init() の戻り値そのもの (0 = 成功)。
 * まだ呼ばれていなければ -1 と 0 が返る。
 *
 * 実機で kprintf の診断行が 1 行も読めなかった (属性が PC/AT 流のまま
 * 書かれていた) ため、**画面に必ず 1 行出す**ための保険として足した。
 * 引数はどれも NULL 可。
 *   st0           最後の RECALIBRATE で見た ST0
 *   p0439_before  I/O 0439h の書き換え前の値
 *   p0439_after   書き換え後の読み戻し (書かなかったときは before と同じ) */
int fdc_get_last_init_status(u8 *st0, u8 *p0439_before, u8 *p0439_after);

/* セクタ読み込み (2HD固定ラッパ)
 *   drv:   ドライブ (0-3)
 *   cyl:   シリンダ (0-76)
 *   head:  ヘッド (0-1)
 *   sect:  セクタ (1ベース)
 *   buf:   データバッファ (>= FDC_SECTOR_SIZE バイト)
 * 戻り値: 0=成功
 */
int fdc_read_sector(int drv, int cyl, int head, int sect, void *buf);

/* セクタ書き込み (2HD固定ラッパ) */
int fdc_write_sector(int drv, int cyl, int head, int sect, const void *buf);

/* セクタ読み込み (ジオメトリ指定版) */
int fdc_read_sector_geom(int drv, int cyl, int head, int sect,
                         const struct fdc_geom *g, void *buf);

/* セクタ書き込み (ジオメトリ指定版) */
int fdc_write_sector_geom(int drv, int cyl, int head, int sect,
                          const struct fdc_geom *g, const void *buf);

/* 同じトラック (cyl/head) の sect から count セクタを **1 回の READ DATA**
 * で読む (EOT = sect + count - 1、MT = 0)。buf は count × bps バイト。
 *   0 = 成功 / -1 = 失敗 / -2 = 引数がトラックや受け皿に収まらない
 *   -3 = NR (媒体もドライブも無い。まとめ読みの失敗には数えず、失敗行も
 *        出さない — 1 セクタずつの読みも NR で即座に終わる)
 * **リトライしない 1 回きり**。失敗したら呼び手 (drivers/fdc_track.c) が
 * その範囲を fdc_read_sector_geom で 1 セクタずつ読み直す。失敗の後は
 * DMA を閉じ、覚えているシリンダを捨てて戻る (次の単発がシークし直す)。
 * 最終失敗ではないので失敗行は最初の数回だけ出す (fdc.c)。 */
int fdc_read_sectors(int drv, int cyl, int head, int sect, int count,
                     const struct fdc_geom *g, void *buf);

/* 診断の数 (起動時の 1 行と試験が読む)。
 * 実機やエミュレータで「シークが何回出たか」「期限切れを何回待ったか」
 * 「束ねた読みが落ちて 1 セクタずつに戻っていないか」を見るため。 */
struct fdc_stats {
    u32 seek_issued;    /* SEEK を出した */
    u32 seek_skipped;   /* 同じシリンダなので SEEK を省いた */
    u32 recal_issued;   /* RECALIBRATE を出した (試行ごと) */
    u32 seek_timeout;   /* SEEK / RECALIBRATE の IRQ 待ちが期限切れになった */
    u32 sis_foreign;    /* 完了待ちの SIS が別ドライブ / Ready 変化を返した */
    u32 ready_change;   /* SIS で Ready 変化 (IC=11b) を見た */
    u32 multi_ok;       /* まとめ読みの成功 */
    u32 multi_fail;     /* まとめ読みの失敗 (NR を除く) */
    u32 multi_nr;       /* まとめ読みが NR (媒体無し) で終わった */
    u32 single_reads;   /* 1 セクタの読み (呼び出し回数) */
    u32 single_retries; /* 1 セクタの読みのリトライ (回復を挟んだ回数) */
    u32 writes;         /* 1 セクタの書き込み (呼び出し回数) */
};
void fdc_get_stats(struct fdc_stats *out);

/* 数を 1 行で出す (tag は場面の名前。何も起きていなければ出さない)。 */
void fdc_print_stats(const char *tag);

/* ドライブの中身の世代。**書き込み (fdc_write_sector_geom を通るもの全部 —
 * FatFs、dev.c の fd0/fd1、KAPI の dev_blk_write)、Ready 変化 (媒体の
 * 入れ替え)、fdc_set_media のたびに増える**。先読み (drivers/fdc_track.c)
 * はスロットを埋めたときの世代と比べ、違えば当てない。 */
u32 fdc_media_gen(int drv);

/* 先読みのスロット i (0 ≦ i < FDC_TRACK_SLOTS) の先頭。各
 * FDC_TRACK_MAX_BYTES バイト。DMA の窓とは重ならない (同じ静的領域の中)。
 * 範囲外は NULL。 */
u8 *fdc_track_slot(int i);

/* セクタキャッシュのスロット j (0 ≦ j < FDC_SECTOR_SLOTS) の先頭。各
 * FDC_SECTOR_SLOT_BYTES バイト。範囲外は NULL。 */
u8 *fdc_sector_slot(int j);

/* 覚えている現在のシリンダ (診断と試験用)。-1 = 知らない (次はシークする)。 */
int fdc_get_known_cyl(int drv);

/* IRQ11完了フラグ (isr_handlers.cからセット) */
extern volatile u32 fdc_irq_fired;

#endif /* FDC_H */

