#ifndef KBD_CONTRACT_H
#define KBD_CONTRACT_H
/* キーボードの従来の口 (cooked/raw のリングと状態)。実体はドライバ (drivers/input/kbd*.c) と
 * 入力の HAL (input/hal/legacy_*.c)、使い手は IME・KAPI・GUI。キーコードは include/kbd.h。 */
#include "types.h"
#include "kbd.h"
#include "os32_kapi_shared.h"   /* KbdDiag / KbdDiagLogEnt */

#define KBD_BUF_SIZE  128
void kbd_init(void);
/* 診断カウンタを写す (KAPI v62、`kbdstat`)。0 = 成功 / OS32_ERR_INVAL = out が NULL。
 * 並びは sdk/include/os32/os32_kapi_shared.h の KbdDiag (24 バイト)。 */
int  kbd_diag(KbdDiag *out);
/* 受信記録を写す (KAPI v67、`kbdstat -w`、票 TASK_KBD_NAV §3)。after_seq より新しい
 * KbdDiagLogEnt を古い順に最大 max 件 (KBD_DLOG_CAP で頭打ち)。戻り = 件数 /
 * OS32_ERR_INVAL (out が NULL・max <= 0)。 */
int  kbd_diag_log(u32 after_seq, KbdDiagLogEnt *out, int max);
int  kbd_getchar(void);     /* ブロッキング: ASCII部のみ返す */
int  kbd_getkey(void);      /* ブロッキング: 上位=スキャンコード, 下位=ASCII */
int  kbd_trygetchar(void);
/* ローカルの打鍵だけ (シリアルも注入リングも見ない)。無ければ -1。
 * rshell が「この 1 バイトはシリアル由来か」を知るための口 (往復 3 ④)。 */
int  kbd_trygetchar_local(void);  /* ノンブロッキング: -1=なし, >=0 ASCII */
int  kbd_trygetkey(void);   /* ノンブロッキング: -1=なし, >=0 キーコードデータ(u16) */
int  kbd_peekkey(void);     /* 覗くだけ (取り出さない): -1=なし, >=0 キーコードデータ(u16) */
int  kbd_has_key(void);     /* バッファにキーがあるか */
u32  kbd_get_modifiers(void);/* 修飾キー状態取得 */
int  kbd_is_pressed(int scancode); /* スキャンコード押下状態 (1=押下中) */
u32  kbd_dropped_count(void); /* 満杯で捨てた打鍵の累計 (契約 T3, GUI v1.1)。cooked+raw を合算 */
int  kbd_trygetrawkey(void);  /* 生 make/break イベント (keycode | down<<8)、無ければ -1。GUI Key 用 (レビュー ④/⑥) */
void kbd_set_gui_mode(int on);/* GUI (WM 常駐) 中は cooked リングに積まない (K2-B)。kernel/gui.c だけが呼ぶ */

/* シフトキー状態 */
extern volatile u8 kbd_shift_state;
/* Internal: discard coalesced CTRL+STOP makes after an ABORTED owner exit. */
void kbd_discard_stop(void);
void kbd_stop_mouse_sample(u8 buttons);

int kbd_getchar_kapi(void);
int kbd_getkey_kapi(void);
/* Only explicit input waits may request interruption; returns -1, no unwind. */
int kbd_getkey_wait(int interruptible);

#endif
