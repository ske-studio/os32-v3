/* ======================================================================== */
/*  SFS_PROTO.H — SerialFS のフレームと時間の規則 (純粋、ホストで試験する)  */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 B (§1-v2 B-5' / §1-v3)。 */
/*  ホスト側の写しは tools/serialfs_host.py。**両方を同時に変えること** —   */
/*  tools/tests/test_serialfs.py が C の組み立てた列を Python で読み、その  */
/*  逆も確かめる。                                                          */
/*                                                                          */
/*  フレーム (両向き同じ形、数値はすべてリトルエンディアン):                */
/*                                                                          */
/*    off  size  内容                                                       */
/*    0    1     ENQ (0x05)                                                 */
/*    1    2     'S' 'F'                                                    */
/*    3    1     種別 (SFS_T_*)。応答は要求 | 0x80、拒否は SFS_T_ERR         */
/*    4    4     セッション ID (HELLO の要求だけ 0)                          */
/*    8    2     番号 (u16、HELLO と片道フレームは 0。**周回させない**)      */
/*    10   2     ペイロード長 (0〜SFS_MAX_PAYLOAD)                           */
/*    12   len   ペイロード                                                 */
/*    12+len 4   CRC32 (zlib.crc32 と同じ) — 種別からペイロードの末尾まで    */
/*                                                                          */
/*  [C1] C89 / GNU89。依存は types.h と os32_kapi_shared.h (エラー番号) だけ。*/
/* ======================================================================== */
#ifndef FS_SFS_PROTO_H
#define FS_SFS_PROTO_H

#include "types.h"

#define SFS_ENQ            0x05
#define SFS_MAGIC1         0x53    /* 'S' */
#define SFS_MAGIC2         0x46    /* 'F' */
#define SFS_VERSION        1

#define SFS_HDR_LEN        12
#define SFS_CRC_LEN        4
/* ペイロードの上限 (§1-v2 B-5')。ゲストの受信器はフレーム 1 つ分
 * (SFS_MAX_FRAME) だけを持ち、長さ欄はこれと照合してから読む。 */
#define SFS_MAX_PAYLOAD    512
#define SFS_MAX_FRAME      (SFS_HDR_LEN + SFS_MAX_PAYLOAD + SFS_CRC_LEN)

/* ---- 種別 ---- */
#define SFS_T_HELLO        0x01    /* ver u16, max u16, nonce u32 → 応答で ID */
#define SFS_T_BYE          0x02    /* 片道。以後ホストはこの ID に答えない */
#define SFS_T_STAT         0x10    /* path */
#define SFS_T_LIST         0x11    /* cookie u32, path */
#define SFS_T_READ         0x12    /* offset u32, count u16, path */
#define SFS_T_WRITE        0x13    /* offset u32, flags u8, path, data */
#define SFS_T_MKDIR        0x14    /* path */
#define SFS_T_RMDIR        0x15    /* path */
#define SFS_T_UNLINK       0x16    /* path */
#define SFS_T_RENAME       0x17    /* old path, new path */
#define SFS_T_LOG          0x20    /* 片道 (ゲスト → ホスト): 溜めた出力の断片 */
#define SFS_T_EXIT         0x21    /* 片道: code i32, dropped u32, flags u32 */
#define SFS_T_RESP         0x80    /* 応答 = 要求の種別 | 0x80 */
#define SFS_T_ERR          0xFF    /* 拒否 (未知のセッションなど)。status だけ */

/* path は「長さ u8 + バイト列 (NUL なし)」。`/` 始まりのルート相対。 */
#define SFS_PATH_MAX       255

/* WRITE の flags */
#define SFS_WF_TRUNC       0x01    /* 書く前に作り直す (write_file の 1 回目) */

/* STAT / LIST の種別欄 */
#define SFS_KIND_FILE      1
#define SFS_KIND_DIR       2
#define SFS_KIND_OTHER     3

/* EXIT の flags */
#define SFS_XF_NOT_QUIET   0x01    /* 隔離の上限に達した (線が静まらなかった) */
#define SFS_XF_DEAD        0x02    /* 途中で線を死んだと見なした */

/* 応答のペイロードは必ず status (i32) で始まる。値は OS32_ERR_* (0 以上は
 * 成功。READ / WRITE では読み書きしたバイト数)。 */
#define SFS_STATUS_LEN     4

/* READ の count の上限 = 応答ペイロードから status を引いた分 */
#define SFS_READ_MAX       (SFS_MAX_PAYLOAD - SFS_STATUS_LEN)

/* ---- 時間 (§1-v2 B-5' / §1-v3「時間切れの式は今の速度から」) ---- */
#define SFS_FIRST_BYTE_MS  2000    /* 送り終えてから応答が始まるまで */
#define SFS_GAP_MS         100     /* フレームの途中のバイト間 */
#define SFS_SLACK_MS       500     /* 全体の式の余裕 */
#define SFS_RETRIES        3       /* 同じ番号での再送の回数 (試行は 1 + 3) */
#define SFS_DEAD_AFTER     3       /* 連続して失敗した要求の数で線を死んだと見なす */
#define SFS_RESYNC_QUIET_MS 200    /* 再送の前に線が静まるのを待つ長さ */
#define SFS_RESYNC_MAX_MS  1000    /* その待ちの上限 (相手が送り続けるとき) */
#define SFS_QUIET_MS       500     /* セッションの終わりの隔離: 静かな時間 */
#define SFS_QUIET_MAX_MS   5000    /* 隔離の上限 (決裁 1B) */
#define SFS_TICK_MS        10      /* tick_count の周期 */
/* 番号は 1〜SFS_SEQ_LAST を使う。次が SFS_SEQ_LAST を越える要求は IO で
 * 失敗させ、セッションを閉じる (u16 を周回させない、§1-v3 Codex N4)。 */
#define SFS_SEQ_LAST       0xFFFEu

/* ---- 小さな読み書き ---- */
void sfs_put16(u8 *p, u16 v);
void sfs_put32(u8 *p, u32 v);
u16  sfs_get16(const u8 *p);
u32  sfs_get32(const u8 *p);

/* zlib.crc32 と同じ CRC32 */
u32  sfs_crc32(const u8 *p, u32 n);

/* フレームを out へ組む。戻りはフレーム長、len が上限を越えれば 0
 * (1 バイトも書かない)。out は SFS_MAX_FRAME 以上。 */
u16  sfs_encode(u8 *out, u8 type, u32 sid, u16 seq,
                const u8 *payload, u16 len);

/* ---- 受信器 ---- */
typedef struct {
    u8  type;
    u32 sid;
    u16 seq;
    u16 len;
    const u8 *payload;     /* 受信器の buf の中。次の feed で無効になる */
} SfsFrame;

typedef struct {
    u16 pos;               /* buf に溜まったバイト数 (0 = ENQ 待ち) */
    u16 need;              /* このフレームの全長 (ヘッダを読むまでは 0) */
    u32 bad_crc;           /* CRC が合わず捨てたフレーム */
    u32 bad_len;           /* 長さ欄が上限を越えて捨てたヘッダ */
    u8  buf[SFS_MAX_FRAME];
} SfsDec;

void sfs_dec_reset(SfsDec *d);
/* 途中のフレームを捨てる (バイト間の時間切れ)。数は変えない。 */
void sfs_dec_abort(SfsDec *d);
/* 1 = フレーム途中 (ENQ を読んだ後) */
int  sfs_dec_busy(const SfsDec *d);
/* 1 バイト入れる。戻り 1 = フレームが揃った (*out に)、0 = まだ、
 * -1 = 壊れたフレームを捨てた (長さ・CRC)。**フレーム全体を消費してから
 * 判定する** (旧版の残留バイト問題を持ち込まない)。 */
int  sfs_dec_feed(SfsDec *d, u8 byte, SfsFrame *out);

/* ---- 時間の式 (tick = 10ms) ---- */
/* 8N1 の 1 バイト = 10 ビットの時間 [µs]。baud 0 は 9600 と見なす。 */
u32  sfs_byte_us(u32 baud);
/* ms を tick に切り上げ、さらに 1 tick 足す (tick の途中から数え始めるので) */
u32  sfs_ms_ticks(u32 ms);
/* 1 試行の期限 [tick]: 最初のバイトまで 2s + 最大フレーム × バイト時間 × 2
 * + 500ms。**番号違い・CRC 違いのフレームで延ばさない** (呼び手の規則)。 */
u32  sfs_trial_ticks(u32 baud);

/* 番号を 1 つ進める。戻り 0 = *seq に次の番号 / -1 = 使い切った
 * (*seq は変えない)。 */
int  sfs_seq_next(u16 *seq);

/* path を「長さ u8 + バイト列」で out へ。戻りは書いたバイト数、
 * 長すぎれば -1。 */
int  sfs_put_path(u8 *out, u16 room, const char *path);

#endif /* FS_SFS_PROTO_H */
