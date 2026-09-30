/* ======================================================================== */
/*  LIBOS32HOST.H — Host Services (KAPI v51) の薄いクライアント層            */
/*                                                                          */
/*  票 docs/archive/network/TASK_N3.md §1。契約の正典は TASK_N0.md 第 5 版   */
/*  §1a (host_open / host_status / host_read / host_write / host_close) と   */
/*  §5 (呼び出し列)。C89 [C1]、静的リンク。KAPI ポインタ `kapi` は crt0 が    */
/*  設定する大域を使う (libos32cfg / cfg_backend.c と同じ作法)。             */
/*                                                                          */
/*  この層が隠すもの:                                                        */
/*    - AGAIN / FULL の間の `sys_yield` 待ち (open も含む — RELEASE 未 ACK の */
/*      間 open が AGAIN を返すため)。                                        */
/*    - 最初の open だけ STALE を上限 (3 秒) 待ち、以降の STALE は即 ELINK。   */
/*    - 無進捗期限 (30 秒): status 0 / read>0 / write>0 / open 成功のたびに    */
/*      基準を取り直し、AGAIN / FULL しか返らない状態が続いたときだけ         */
/*      ETIMEOUT (大きい転送を殺さない絶対期限ではない)。                     */
/*    - PRINT の多段 (OPEN → DATA×n → CLOSE) と途中失敗の後始末 (CLOSE を     */
/*      送らず job を放置、開いた host ハンドルは全経路で close)。            */
/*    - 本文を溜めない sink / src ストリーミング (>64KB でメモリ一定)。       */
/*                                                                          */
/*  業務ステータス (HTTP の 404、print/clip の 4xx/5xx) はエラーにせず out    */
/*  引数で返す。関数の戻り値は 0 か負の HOST_E*。                             */
/* ======================================================================== */

#ifndef LIBOS32HOST_H
#define LIBOS32HOST_H

#ifdef HOST_TEST
/* コマンドのホスト TDD (-DHOST_TEST) は os32api.h を引かず POSIX で回す。
 * この層のプロトタイプに要る整数型だけを stdint から借りる。 */
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  i32;
#else
#include "os32_kapi_shared.h"   /* u8, u16, u32, i32, OS32_ERR_* */
#endif

/* ---- エラー (負値、TASK_N3 §1) ----------------------------------------- */
/*  リンク系 (呼び手の終了コード 2): ELINK / ENODEV / ETIMEOUT              */
/*  業務系:                       ESERVICE (+ *svc_status に業務値)         */
/*  引数:                         EINVAL (終了コード 3)                     */
/*  ローカル / 予期しない:        EIO / EABORT (sink・src 中断)             */
#define HOST_ELINK      -100    /* STALE / リンク未確立 */
#define HOST_ENODEV     -101    /* NIC 無し (KAPI が NOSYS) */
#define HOST_ETIMEOUT   -102    /* 無進捗期限 */
#define HOST_ESERVICE   -103    /* print/clip の 409/500/503 (業務失敗) */
#define HOST_EINVAL     -104    /* 引数不正 (url 長・clip 長・空) */
#define HOST_EIO        -105    /* 予期しない負値 */
#define HOST_EABORT     -106    /* sink / src が中断を要求した */

/* ---- 生存性・寸法の定数 ([C4] ここが管理元) ---------------------------- */
#define HOST_TICK_HZ            100     /* get_tick の周波数 (100Hz) */
#define HOST_OPEN_STALE_TICKS   300     /* 最初の open の STALE 待ち = 3 秒 */
#define HOST_NOPROG_TICKS       3000    /* 無進捗期限 = 30 秒 */
#define HOST_REQ_MAX            1400    /* host_open 要求行の上限 */
#define HOST_DECL_MAX           65536   /* 1 要求の宣言長上限 (Agent DECL_MAX_BYTES) */
#define HOST_WRITE_CHUNK        1400    /* host_write 1 回の上限 */
#define HOST_STREAM_BUF         16384   /* print_stream の内部詰めバッファ */
#define HOST_TIME_LEN           19      /* "YYYY-MM-DD HH:MM:SS" */
#define HOST_TIME_BUF           20      /* 上 + NUL */
#define HOST_CLIP_MAX           4096    /* CLIP PUT の上限 */

/* ---- ストリーミングのコールバック -------------------------------------- */
/*  sink: 受信本文を 1 チャンクずつ渡す。0 = 受理、非 0 = 中断 (EABORT)。   */
typedef int (*host_sink_fn)(void *ud, const u8 *buf, u32 n);
/*  src: 送信本文を cap まで詰める。>0 = 長さ、0 = EOF、<0 = 中断 (EABORT)。 */
typedef i32 (*host_source_fn)(void *ud, u8 *buf, u32 cap);

/* ---- API (TASK_N3 §1) -------------------------------------------------- */
int host_time(char out[HOST_TIME_BUF]);
int host_get(const char *url, host_sink_fn sink, void *ud,
             int *http_status, u32 *nbytes);
int host_print_text(const char *name, const char *buf, u32 len,
                    u32 *pages, u32 *svc_status);
int host_print_stream(const char *name, host_source_fn src, void *ud,
                      u32 *pages, u32 *svc_status);
int host_clip_get(host_sink_fn sink, void *ud, u32 *nbytes, u32 *svc_status);
int host_clip_put(const char *buf, u32 len, u32 *svc_status);

#endif /* LIBOS32HOST_H */
