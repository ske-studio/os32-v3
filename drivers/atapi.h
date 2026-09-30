/* ======================================================================== */
/*  ATAPI.H — PC-98 IDE/ATAPI CD-ROM PIOドライバ                             */
/*                                                                          */
/*  IDEセカンダリバンク (I/O 0x430/0x432) に接続されたATAPI CD-ROMデバイスに  */
/*  PACKETコマンド (0xA0) を発行し、データ転送を行う。                       */
/*                                                                          */
/*  I/OポートはHDDドライバ (ide.h) と共有。バンク切替で選択する。            */
/*  セクタサイズは2048バイト (HDD=512バイトとは異なる)。                     */
/*                                                                          */
/*  参照: NP21/W (atapicmd.c, ideio.c), UNDOCUMENTED io_ide.md              */
/* ======================================================================== */

#ifndef ATAPI_H
#define ATAPI_H

#include "ide.h"

/* ======== CD-ROM セクタサイズ ======== */
#define ATAPI_SECTOR_SIZE    2048

/* ======== 1 回の READ(10) で読むセクタ数の上限 ========
 * atapi_read_sectors() は連続する count セクタを、この数ずつの READ(10) に
 * 分けて出す (1 セクタずつ出していた頃は、実機の cdinst が 20KB/s を切った)。
 * **実機の CD ドライブとの相性で困ったら、ここを 1 にすれば旧来の読み方に戻る**。
 * 16 = 32KB。PIO の byte count limit (下) はこれより大きくてよい。
 * (ホスト試験は -D で 32 にした版も回す = 1 回の転送が 64KB を超える形) */
#ifndef ATAPI_READ_MAX_SECTORS
#define ATAPI_READ_MAX_SECTORS  16
#endif

/* PACKET の byte count limit (Cylinder Low/High) に書く上限。1 回の DRQ で
 * デバイスが渡してよいバイト数。ATA の規定で偶数、0xFFFF は使わない。
 * セクタの倍数 (31 × 2048) にしておくと、DRQ の区切りがセクタの途中に来ない */
#define ATAPI_PIO_BCL_MAX       0xF800U

/* 読みの失敗の診断の行 ([atapi] READ(10) ...) を出す上限 (起動から数えて) */
#define ATAPI_DIAG_MAX          8

/* ======== センスキー (エラーレジスタの bit7-4) ======== */
#define ATAPI_ERR_SENSE_SHIFT   4
#define ATAPI_SK_NO_SENSE       0x00
#define ATAPI_SK_NOT_READY      0x02
#define ATAPI_SK_UNIT_ATTENTION 0x06   /* 媒体の交換・リセットの後の最初のコマンド */

/* ======== ASC (REQUEST SENSE の byte 12) ======== */
#define ATAPI_ASC_BECOMING_READY      0x04   /* NOT READY: 準備中 (回転の立ち上がり等) */
#define ATAPI_ASC_MEDIUM_NOT_PRESENT  0x3A   /* NOT READY: 媒体が無い (これだけが確定) */
/* ASC 04h の ASCQ 02h = initializing command required (START UNIT が要る)。
 * 待っても変わらないので START STOP UNIT (開始) を 1 回出して出し直す */
#define ATAPI_ASCQ_INIT_CMD_REQUIRED  0x02
#define ATAPI_SENSE_LEN               18     /* REQUEST SENSE で受け取る長さ */
#define ATAPI_SENSE_MIN               14     /* byte 13 (ASCQ) まで要る */

/* READ CAPACITY の出し直し: UNIT ATTENTION は REQUEST SENSE で消して、
 * NOT READY (3Ah 以外) は ATAPI_READY_WAIT_US 待って、この回数まで。
 * 待ちの合計は 1 装置あたり最大 20 × 250ms = 5 秒 — トレイを閉じた直後の
 * becoming ready (2〜5 秒) を待ちきる長さ。atapi_init は装置ごとに待つので、
 * 装置が 2 台なら最大 10 秒。待ちは atapi_delay_us が CPU_DELAY_US_MAX
 * (kernel/cpu_calibrate.h、cpu_delay_us の上限 100ms) 以下の塊に分けて回す */
#define ATAPI_READY_RETRIES     20
#define ATAPI_READY_WAIT_US     250000UL

/* READ(10) が UNIT ATTENTION で落ちたときの出し直しの回数 (1 回目に加えて)。
 * リセットと媒体交換など、UA を複数積む装置がある */
#define ATAPI_UA_RETRIES        3

/* ======== 待ちの上限 (秒単位、2026-09-26 TASK_ATAPI_TIMEOUT) ========
 * BSY / DRQ の待ちは ALT_STATUS を 1 回読むごとに cpu_delay_us(ATAPI_POLL_US) を
 * 挟み、挟んだ時間の合計で上限を数える (atapi_wait_clear)。以前は
 * IDE_TIMEOUT_LOOP (100 万回の inp、Ra266 で 0.5〜1 秒) で、実機のスピンアップ
 * (READ(10) で 2〜4 秒 BSY) に足りず、健全な装置を DEVICE RESET で捨てていた。
 * tick_count で数えないのは、呼ばれる文脈 (起動時の atapi_init、KAPI 経由の読み)
 * で PIT の割り込みが来ていると決められないから (atapi_delay_us と同じ理由)。
 * cpu_delay_us の誤差は ±10% 程度 — 上限はどれも規定・実測より 2 倍以上の余裕で決める。
 * 読み間の inp の時間は数えないので、実際の待ちは上限より少し長い (長い側へ外れる) */
#define ATAPI_POLL_US           100UL
/* 通常の PACKET の上限 (コマンドの BSY / DRQ、装置の選択、DEVICE RESET の後)。
 * スピンアップの 2〜4 秒 (票 §0) の 2 倍 + 余裕。cpu_delay_us が -10% に外れても 9 秒。
 * これを越えて BSY のままの装置だけを「固まった」と見る: コマンドの待ちが
 * 期限切れになっても DEVICE RESET はせず、次のコマンドの装置選択でさらに
 * この時間待っても BSY / DRQ のときに初めて DEVICE RESET する (atapi_select_device) */
#define ATAPI_CMD_TIMEOUT_US    10000000UL
/* atapi_init の間の上限 (シグネチャの確認と、2 台のときの容量確認)。スピンアップの
 * 上限 4 秒は待ちきり、起動の最悪時間 (装置が応答しない) を抑える。電源投入・
 * リセットの直後の長い BSY は SRST の待ち (ATAPI_SRST_TIMEOUT_US) が受け持つ。
 * init で期限切れになった遅い装置も、以後のコマンドは ATAPI_CMD_TIMEOUT_US で待つ */
#define ATAPI_INIT_TIMEOUT_US   5000000UL
/* SRST の後にマスターの BSY=0 を待つ上限。ATA の規定の最大 (31 秒) */
#define ATAPI_SRST_TIMEOUT_US   31000000UL

/* SRST を立てておく長さ (ALT_STATUS の空読みの回数)。規定は 5µs 以上 */
#define ATAPI_SRST_HOLD_LOOP    50000
/* SRST を解いてからステータスを読むまで置く時間 (µs)。規定は 2ms 以上 —
 * それより前は BSY がまだ立っていないことがあり、準備済みに見える */
#define ATAPI_SRST_SETTLE_US    2000UL

/* ======== ATAPI / PACKET コマンド ======== */
#define ATAPI_CMD_PACKET         0xA0   /* PACKETコマンド (CDB送出) */
#define ATAPI_CMD_DEVICE_RESET   0x08   /* DEVICE RESET: 選んだ装置だけ。BSY でも受ける */
#define ATAPI_CMD_IDENTIFY_PKT   0xA1   /* IDENTIFY PACKET DEVICE */

/* ======== 装置の選択 (DRV_HEAD) ======== */
#define ATAPI_DRV_SLAVE     0x10   /* bit4 = 1: スレーブ (セカンダリの 2 台目) */
#define ATAPI_SEL_UNKNOWN   0xFF   /* バスがどちらを選んでいるか分からない (起動直後) */
#define ATAPI_ST_FLOAT      0xFF   /* 居ない装置を選んだときの ALT_STATUS (浮いたバス) */

/* ======== ATAPI シグネチャ (IDENTIFY時にCylLo/CylHiで返る) ======== */
#define ATAPI_SIG_CYL_LO    0x14
#define ATAPI_SIG_CYL_HI    0xEB

/* ======== SCSI CDB オペコード ======== */
#define SCSI_CMD_TEST_UNIT_READY  0x00
#define SCSI_CMD_REQUEST_SENSE    0x03
#define SCSI_CMD_INQUIRY          0x12
#define SCSI_CMD_START_STOP_UNIT  0x1B   /* byte 4 bit0 = Start、byte 1 bit0 = IMMED (使わない) */
#define SCSI_SSU_START            0x01
#define SCSI_CMD_READ_CAPACITY    0x25
#define SCSI_CMD_READ_10          0x28

/* ======== Interrupt Reason (Sector Count レジスタ) ビット ======== */
#define ATAPI_IR_CD    0x01   /* 1=コマンドパケット, 0=データ */
#define ATAPI_IR_IO    0x02   /* 1=デバイス→ホスト, 0=ホスト→デバイス */
#define ATAPI_IR_REL   0x04   /* バスリリース */

/* ======== エラーコード ======== */
#define ATAPI_OK           0
#define ATAPI_ERR_TIMEOUT -1
#define ATAPI_ERR_NO_DRIVE -2
#define ATAPI_ERR_IO      -3
#define ATAPI_ERR_NO_MEDIA -4

/* ======== CD-ROM 容量情報 ======== */
typedef struct {
    u32 total_sectors;   /* 総セクタ数 (2048B/セクタ) */
    u32 sector_size;     /* セクタサイズ (通常 2048) */
} AtapiCapacity;

/* ======== 公開API ======== */

/* ATAPI初期化: セカンダリバンクのCD-ROMを検出
 * 戻り値: 1=CD-ROM検出, 0=未検出
 * 呼び直すとバスの再試行になる: バスが死んだ印 (下の「バスが死んだ印」) を
 * 解くのはこれだけ */
int atapi_init(void);

/* CD-ROM 存在チェック */
int atapi_present(void);

/* 使っている装置: 0 = セカンダリのマスター、1 = スレーブ。atapi_init は
 * 両方のシグネチャを見て、2 台あれば媒体の入っている方 (マスター優先) を選ぶ */
int atapi_drive_index(void);

/* バスが死んだ印: SRST の後もマスターが ATAPI_SRST_TIMEOUT_US (31 秒) BSY の
 * ままなら立つ。立っているあいだ atapi_test_unit_ready / atapi_read_capacity /
 * atapi_read_sectors はバスに触らず即 ATAPI_ERR_TIMEOUT を返す (触れば 1 回
 * 約 61 秒 = 選択の前後の待ち 10 + 10、DEVICE RESET の後 10、SRST 31)。最初に断ったとき
 * だけ "[atapi] bus dead since SRST timeout, ..." の 1 行を出す。
 * 解くのは次の atapi_init */

/* TEST UNIT READY: メディア挿入確認
 * 戻り値: ATAPI_OK=メディアあり, ATAPI_ERR_NO_MEDIA=なし,
 * ATAPI_ERR_TIMEOUT=バスが死んだ印 */
int atapi_test_unit_ready(void);

/* READ CAPACITY: メディア容量取得 */
int atapi_read_capacity(AtapiCapacity *cap);

/* セクタ読み出し (2048バイト/セクタ, LBA指定)
 *   lba:   読み出し開始LBA
 *   count: 読み出しセクタ数
 *   buf:   データバッファ (count * 2048 バイト)
 * 戻り値: ATAPI_OK=成功
 *
 * 連続する count セクタを ATAPI_READ_MAX_SECTORS ずつの READ(10) で読む。
 * 複数セクタの READ(10) が失敗したら、その範囲だけ 1 セクタずつ読み直す。
 * UNIT ATTENTION (媒体の交換の後の最初のコマンド) を受けたら媒体の世代を
 * 進めて同じコマンドを ATAPI_UA_RETRIES (3) 回まで出し直す。 */
int atapi_read_sectors(u32 lba, u32 count, void *buf);

/* 媒体の世代。UNIT ATTENTION / NOT READY を見るたびに進む。上の層 (iso9660)
 * は覚えた値と違えばキャッシュを捨てる。**NP21/W は READ(10) で UNIT
 * ATTENTION を返さない** (交換は TEST UNIT READY だけが報告する) ので、
 * エミュレータではこの経路は動かない */
u32 atapi_media_gen(void);

/* 読みの統計 (試験と起動時の行のため) */
typedef struct {
    u32 read10_cmds;      /* 出した READ(10) の数 (出し直しを含む) */
    u32 read10_sectors;   /* 成功した READ(10) で受け取ったセクタ数 */
    u32 multi_fail;       /* 複数セクタの READ(10) が失敗して 1 セクタずつへ落ちた数 */
    u32 single_retry;     /* 落ちた後に 1 セクタずつ出した READ(10) の数 */
    u32 unit_attention;   /* UNIT ATTENTION / NOT READY を見た数 */
    u32 dev_resets;       /* コマンドが終わらない装置へ出した DEVICE RESET の数 */
    u32 soft_resets;      /* DEVICE RESET でも戻らず SRST した数 */
    u32 ready_retries;    /* READ CAPACITY を UNIT ATTENTION / NOT READY で出し直した数 */
    u32 start_units;      /* NOT READY / 04h/02h に START STOP UNIT (開始) を出した数 */
    u32 dead_fails;       /* バスが死んだ印 (SRST の 31 秒切れ) のためにバスに触らず断った数 */
} AtapiStats;

void atapi_get_stats(AtapiStats *out);

#endif /* ATAPI_H */
