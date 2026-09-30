/* ======================================================================== */
/*  KBD.H — PC-98 キーボードドライバ定義                                   */
/*                                                                          */
/*  μPD8251A (USART) 経由でスキャンコードを取得                            */
/*  出典: PC9800Bible §2-5                                                  */
/* ======================================================================== */

#ifndef __KBD_H
#define __KBD_H

#include "types.h"
#include "os32_kapi_shared.h"   /* KbdDiag / OS32_ERR_* */
#include "kbd_status.h"        /* KBD_STAT_* / kbd_status_classify */

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

/* スキャンコードのビット構成 */
#define SCANCODE_BREAK  0x80    /* ビット7: 1=ブレイク(離した), 0=メイク(押した) */
#define SCANCODE_KEY    0x7F    /* ビット6-0: キーコード */

/* ======== 特殊キーコード (PC9800Bible 表2-13) ======== */
#define KEY_ESC     0x00
#define KEY_BS      0x0E
#define KEY_TAB     0x0F
#define KEY_RETURN  0x1C
#define KEY_SPACE   0x34
#define KEY_XFER    0x35    /* 変換 */
#define KEY_ROLLUP  0x36
#define KEY_ROLLDOWN 0x37
#define KEY_INS     0x38
#define KEY_DEL     0x39
#define KEY_UP      0x3A
#define KEY_LEFT    0x3B
#define KEY_RIGHT   0x3C
#define KEY_DOWN    0x3D
#define KEY_HOME    0x3E    /* HOME/CLR */
#define KEY_HELP    0x3F

/* テンキー (0x40-0x50) */

/* NFER/VFキー */
#define KEY_NFER    0x51    /* 無変換 */
#define KEY_VF1     0x52
#define KEY_VF2     0x53
#define KEY_VF3     0x54
#define KEY_VF4     0x55
#define KEY_VF5     0x56

/* STOP/COPY */
#define KEY_STOP    0x60
#define KEY_COPY    0x61

/* ファンクションキー */
#define KEY_F1      0x62
#define KEY_F2      0x63
#define KEY_F3      0x64
#define KEY_F4      0x65
#define KEY_F5      0x66
#define KEY_F6      0x67
#define KEY_F7      0x68
#define KEY_F8      0x69
#define KEY_F9      0x6A
#define KEY_F10     0x6B

/* シフトキー */
#define KEY_SHIFT   0x70
#define KEY_CAPS    0x71
#define KEY_KANA    0x72
#define KEY_GRPH    0x73
#define KEY_CTRL    0x74

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
#define SHIFT_SHIFT  0x01
#define SHIFT_CAPS   0x02
#define SHIFT_KANA   0x04
#define SHIFT_GRPH   0x08
#define SHIFT_CTRL   0x10

#endif /* __KBD_H */
