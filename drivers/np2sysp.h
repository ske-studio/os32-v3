/* ======================================================================== */
/*  NP2SYSP.H — NP21/Wエミュレータ通信ドライバ                               */
/*                                                                          */
/*  ポート0x7EF/0x7ED経由でホストエミュレータと通信する                       */
/* ======================================================================== */
#ifndef NP2SYSP_H
#define NP2SYSP_H

#include "types.h"

/* I/Oポート定義 */
#define NP2PORT_STR  0x7EF   /* 文字列コマンド送受信 */
#define NP2PORT_VAL  0x7ED   /* 32bit値送受信 */

/* NP21/W検出 (戻り値: 1=検出, 0=非検出) */
int np2_detect(void);

/* コマンド送信 (ポート0x7EFに1バイトずつ出力) */
void np2_send_cmd(const char *cmd);

/* レスポンス受信 (ポート0x7EFから\0まで読出し, 戻り値: 読み取りバイト数) */
int np2_recv_str(char *buf, int maxlen);

/* 32bit値送信 (ポート0x7EDに4バイト出力) */
void np2_send_val(u32 val);

/* 32bit値受信 (ポート0x7EDから4バイト入力) */
u32 np2_recv_val(void);

/* 情報取得ヘルパー */
int np2_get_version(char *buf, int maxlen);    /* バージョン文字列 */
int np2_get_cpu(char *buf, int maxlen);        /* CPU種類 */
int np2_get_clock(char *buf, int maxlen);      /* クロック情報 */
int np2_check_hostdrv(char *buf, int maxlen);  /* hostdrv対応確認 */

#endif /* NP2SYSP_H */
