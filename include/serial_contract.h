#ifndef SERIAL_CONTRACT_H
#define SERIAL_CONTRACT_H
/* 共通 (fs/serialfs のセッション・IME の辞書の診断) とシリアルのドライバ (drivers/char/serial.c) の間の口。 */
#include "types.h"

/* 現在の設定を 3 つの数で返す (CPL=3 から KAPI 越しに読むための口。
 * **カーネルのポインタは渡さない** — docs/design/kapi/kapi_db.md#落とし穴 の db_last_error と
 * 同じ事故を繰り返さないため)。NULL は飛ばす。戻りは 0 = 初期化済み、
 * -1 = まだ serial_init を呼んでいない (値は書くが当てにならない)。
 *   mode: SER_MODE_COMPAT / SER_MODE_VFAST
 *   baud: **実効値** (要求値ではない)
 *   fifo: 0136h の判定 (1 = FIFO 搭載機) */
int serial_get_status(u32 *mode, u32 *baud, u32 *fifo);

/* 1 バイト送る。戻りは SER_TX_OK (0) / SER_TX_DROPPED (-1)。説明は drivers/char/serial.h。 */
int serial_putchar(char c);
void serial_puts(const char *str);
int  serial_is_initialized(void);

/* ---- SerialFS のセッションのゲート ---- */
/* ゲートを上げ下げする。どちらでも受信リングを空にする。上げると保留
 * リングも空にする (下ろしても保留リングは残す — 呼び手が汲む)。 */
void serial_gate_set(int on);
/* ゲートを通らずに線へ出す (SerialFS のフレーム)。SER_TX_OK / DROPPED */
int  serial_gate_put(const u8 *buf, u32 n);
/* ゲートを通らずに 1 バイト受ける (RxRDY を直接見て FIFO の残りも汲む)。
 * -1 = 無い */
int  serial_gate_get(void);
/* 保留リングから最大 max バイト取り出す */
u32  serial_hold_take(u8 *buf, u32 max);
/* 保留リングが溢れて捨てたバイト数 (ゲートを上げると 0) */
u32  serial_hold_dropped(void);

#endif
