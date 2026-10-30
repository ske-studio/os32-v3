/* ======================================================================== */
/*  KBD.H — PC-98 キーボードドライバ定義                                   */
/*                                                                          */
/*  μPD8251A (USART) 経由でスキャンコードを取得                            */
/*  出典: PC9800Bible §2-5                                                  */
/* ======================================================================== */

#ifndef OS32_PC98_KBD_H
#define OS32_PC98_KBD_H

#include "types.h"
#include "../../include/kbd.h"
#include "os32_kapi_shared.h"   /* KbdDiag / OS32_ERR_* */
#include "kbd_status.h"        /* KBD_STAT_* / kbd_status_classify */

#define KBD_KEY_COUNT (SCANCODE_KEY + 1)
#define KBD_BITMAP_BYTES BITMAP_BYTES(KBD_KEY_COUNT)

/* ======== I/Oポート ======== */
#define KBD_DATA    0x41    /* データリード */
#define KBD_CMD     0x43    /* コマンドライト / ステータスリード */

/* ステータスビット (0043h READ) と判定は kbd_status.h (I/O を持たない純粋部) */

/* 8251A コマンド語 (0043h WRITE)。
 *   bit4 ER  = 1  FE/OE/PE をクリア
 *   bit2 RxE = 1  キーボードから受信する
 *   bit1 DTR = 1  **RTY# を HIGH にする = 再送要求を出さない**
 *                 (io_kb.md の信号レベルの定義:「1= RTY#信号をHIGHレベルに
 *                 する / 0= LOWレベルにする」「通常はHIGH。LOWのとき、
 *                 キーボードにデータの再送を要求する」)。
 *                 Bible の「D1: リトライ 1:有効」という書き方は極性の根拠に
 *                 しない — 信号レベルで書いてある io_kb.md を採る。
 *   bit5 RTS = 0  RDY# は RxRDY に従う / bit0 TxE = 0 / bit3 = 0 (RST# HIGH)
 * = 0x16。**BIOS が起動時に最後に書く定常値と同じ** (NP21/W の BIOS
 * src/bios/bios09.c も 0x3A → 0x32 → 0x16)。以前の 0x14 は DTR = 0、
 * つまり RTY# を LOW に張り付けてキーボードに再送を要求し続ける値で、
 * 実機 PC-9821Ra266 で打鍵が一切届かなかった (2026-09-23)。
 * **NP21/W の keyboard_o43 は bit1 (DTR) も bit5 (RTS) も見ない**
 * (bit3 の立ち下がりと bit4 だけ) ので、エミュレータでは 0x14 でも動いていた。 */
#define KBD_CMD_ERRRST_RXE_RTYHIGH  0x16
#define KBD_CMD_DTR                 0x02   /* 上の bit1 (kselftest が見る) */

/* 第 2 段の候補 (いまは書かない): 8251A のモード語からやり直す場合の値。
 * 0x5E = ST 01b (1 stop) / P 01b (奇数パリティ) / L 11b (8bit) / B 10b (×16)
 * (io_kb.md のモードライトの節)。モード語を書くには内部リセット
 * (コマンド語 bit6) が要り、BIOS の初期化を捨てることになるので、0x16 で
 * 直らなかったときの次の手として残すだけにする。 */
#define KBD_MODE_1S_ODD_8B_X16      0x5E

/* IRQ番号 */
#define KBD_IRQ          1       /* キーボードIRQ */

/* ======== キーバッファ ======== */
#define KBD_BUF_SIZE  32
#define KBD_TIMEOUT_TICKS 300   /* 入力待ちタイムアウト時間(ticks) */

/* ======== 公開API ======== */
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

#endif /* OS32_PC98_KBD_H */
