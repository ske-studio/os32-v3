/* ======================================================================== */
/*  SFS_CLIENT.H — SerialFS の要求 / 応答 (再送・期限・線の死)              */
/*                                                                          */
/*  1 要求 = 1 応答。線への出入りは SfsIo 経由なので、カーネルでは          */
/*  drivers/serial.c のゲートの口 (fs/serialfs_session.c)、ホスト試験では   */
/*  偽の線と偽のホスト (tools/tests/serialfs_host.c) につながる。            */
/*                                                                          */
/*  規則 (票 TASK_SERIAL_HOSTFS §1-v2 B-5' / §1-v3):                        */
/*   - 番号違い・種別違い・セッション違い・CRC 違いのフレームは**数えずに   */
/*     捨て**、同じ期限まで待ち続ける (期限を延ばさない)                    */
/*   - 期限切れは同じ番号で再送 (最大 SFS_RETRIES 回)。再送の前に線が      */
/*     SFS_RESYNC_QUIET_MS 静まるまで読み捨てる                              */
/*   - 要求が SFS_DEAD_AFTER 回続けて失敗したら線を死んだと見なし、以後は   */
/*     即座に OS32_ERR_IO (アンマウントまで)                                */
/*   - SFS_T_ERR (未知のセッション) も線を死んだと見なす                    */
/*   - IF=0 (待てない) で呼ばれたら待たずに OS32_ERR_IO                      */
/* ======================================================================== */
#ifndef FS_SFS_CLIENT_H
#define FS_SFS_CLIENT_H

#include "types.h"
#include "sfs_proto.h"

typedef struct {
    /* n バイト送る。0 = 送れた / 負 = 送れなかった */
    int  (*put)(void *ctx, const u8 *buf, u32 n);
    /* 1 バイト受ける。-1 = 無い */
    int  (*get)(void *ctx);
    /* 10ms tick */
    u32  (*now)(void *ctx);
    /* 次の割り込みまで待つ (カーネルは hlt) */
    void (*idle)(void *ctx);
    /* 1 = 待てる (IF=1)。0 なら要求を出さずに IO */
    int  (*can_wait)(void *ctx);
} SfsIo;

/* 線を死んだと見なした理由 (診断用) */
#define SFS_DEAD_NONE      0
#define SFS_DEAD_FAILS     1   /* 連続失敗 */
#define SFS_DEAD_STALE     2   /* ホストがセッションを知らない */
#define SFS_DEAD_SEQ       3   /* 番号を使い切った */

typedef struct {
    const SfsIo *io;
    void *ctx;
    u32 baud;
    u32 sid;               /* 0 = HELLO 前 */
    u16 seq;               /* 最後に使った番号 */
    u8  dead;              /* SFS_DEAD_* */
    u8  fails;             /* 連続して失敗した要求 */
    /* 計数 (sfs run の終わりに 1 行で出す) */
    u32 calls;
    u32 resends;
    u32 timeouts;
    u32 stray;             /* 揃ったが自分宛てでなかったフレーム */
    SfsDec dec;
    u8  tx[SFS_MAX_FRAME];
} SfsClient;

void sfs_client_init(SfsClient *c, const SfsIo *io, void *ctx, u32 baud);

/* HELLO。0 = セッション ID を得た / OS32_ERR_IO */
int  sfs_client_hello(SfsClient *c, u32 nonce);

/* 要求を 1 つ出して応答を待つ。0 = 応答が来た (*resp / *resp_len は
 * 受信器の中を指し、次の呼び出しで無効になる。先頭 4 バイトが status) /
 * OS32_ERR_IO = 届かなかった・線が死んでいる */
int  sfs_client_call(SfsClient *c, u8 type, const u8 *payload, u16 len,
                     const u8 **resp, u16 *resp_len);

/* 片道フレーム (BYE / LOG / EXIT)。番号は 0。0 / OS32_ERR_IO */
int  sfs_client_oneway(SfsClient *c, u8 type, const u8 *payload, u16 len);

/* 受信を捨てながら quiet_ms 静かになるのを待つ。上限 max_ms。
 * 戻り 1 = 静まった / 0 = 上限に達した (相手が送り続けている)。
 * 待てない (IF=0) ときは読めるだけ捨てて 0。 */
int  sfs_client_quiesce(SfsClient *c, u32 quiet_ms, u32 max_ms);

#endif /* FS_SFS_CLIENT_H */
