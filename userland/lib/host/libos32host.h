/* Host Services SDK. Public declarations remain C89 compatible. */
#ifndef LIBOS32HOST_H
#define LIBOS32HOST_H

#include "os32_host_notice.h"

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
#define HOST_ELINK      -100    /* STALE / HOSTDOWN */
#define HOST_ENODEV     -101    /* NIC 無し (KAPI が NOSYS) */
#define HOST_ETIMEOUT   -102    /* 要求全体の期限 */
#define HOST_ESERVICE   -103    /* print/clip の 409/500/503 (業務失敗) */
#define HOST_EINVAL     -104    /* 引数不正 (url 長・clip 長・空) */
#define HOST_EIO        -105    /* 予期しない負値 */
#define HOST_EABORT     -106    /* sink / src が中断を要求した */

/* ---- 生存性・寸法の定数 ([C4] ここが管理元) ---------------------------- */
#define HOST_TICK_HZ            100     /* get_tick の周波数 (100Hz) */
#define HOST_CALL_TIMEOUT_MS    30000u  /* 用途別 API の 1 要求の全体期限 */
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

/* os32. は OS32 標準、ske-studio が定めた。
 * req is NUL terminated. Success returns body bytes (including service errors).
 * Insufficient output returns NOSPC; timeout is the whole call in ms.
 * timeout=0 tries immediately and returns AGAIN without yielding.
 * HOSTDOWN (no link / 1000ms silence) comes from the KAPI. */
int host_call(const char *req, const void *in, u32 in_len,
              void *out, u32 cap, u32 timeout);
int os32_host_notice_read(const char *name, HostNotice *out);

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

/* Standard names; existing host_* source and request lines remain supported. */
#define os32_host_time host_time
#define os32_host_get host_get
#define os32_host_print_text host_print_text
#define os32_host_print_stream host_print_stream
#define os32_host_clip_get host_clip_get
#define os32_host_clip_put host_clip_put

#endif /* LIBOS32HOST_H */
