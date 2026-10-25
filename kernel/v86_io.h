/* ======================================================================== */
/*  V86_IO.H — V86 ゲストの I/O ポリシーとポートディスパッチ                */
/*                                                                          */
/*  設計原則は「仮想化はコストである」。OS32 と競合するデバイスだけを       */
/*  #GP で捕まえ、それ以外は TSS の I/O 許可ビットマップで素通しにする。    */
/*                                                                          */
/*  分類は推測ではなく実測に基づく (docs/archive/v21/v86v2/03_ys_profile.md)。    */
/*  Ys のタイトル画面では I/O の 80% が FM 音源の BUSY ポーリングで、       */
/*  描画は GVRAM への直接書き込みなので I/O をほとんど使っていなかった。    */
/*  FM を素通しにするかどうかだけで #GP レートが 5〜6 倍変わる。            */
/* ======================================================================== */

#ifndef __V86_IO_H
#define __V86_IO_H

#include "types.h"

/* Four-u32 trap-log record. */
#define V86_IOLOG_N          64
#define V86_IOLOG_KEY        0
#define V86_IOLOG_VALUE      1
#define V86_IOLOG_LOCATION   2
#define V86_IOLOG_REPEAT     3
#define V86_IOLOG_WORDS      (V86_IOLOG_REPEAT + 1)
#define V86_IOLOG_DIR_SHIFT  24
#define V86_IOLOG_SIZE_SHIFT 16
#define V86_IOLOG_CS_SHIFT   16
#define V86_CAPTURE_PORT_END 0x0A00U

/* I/O 許可ビットマップに V86 用ポリシーを適用する (セッション開始時)。 */
void v86_io_apply_policy(void);

/* 全ポートを再び拒否に戻す (セッション終了時)。 */
void v86_io_reset_policy(void);

/* CUI 表示: グラフィック停止 → 表示可 → テキスト開始。VRAM は保持。 */
void v86_cui_display_restore(void);

/* 仮想化対象ポートへのアクセス。size は 1 または 2 バイト。
 * ここに来るのは I/O 許可ビットマップで塞いだポートだけ。 */
u32  v86_io_in(u16 port, int size);
void v86_io_out(u16 port, int size, u32 value);

/* 統計 (デバッグ・検証用) */
u32  v86_io_trap_count(void);
u16  v86_io_last_port(void);

#endif /* __V86_IO_H */
