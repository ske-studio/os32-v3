/* ======================================================================== */
/*  PCI_VERBOSE.H — `lspci -v` の行づくり (純粋関数)                        */
/*                                                                          */
/*  未知の PCI カード (例: I-O DATA GV-MVP/HX2) を実機で識別するための      */
/*  読み取り専用の診断。config 空間の先頭 64 バイト (DWORD 16 個) を受け、  */
/*  1 行ずつ文字列にする。I/O は持たない — cmd_pci.c が `pci_cfg_read32`    */
/*  で集めた値を渡す。**書き込みは一切しない** (BAR の大きさを調べる       */
/*  0xFFFFFFFF の書き込みもしない)。                                        */
/*                                                                          */
/*  なぜ文字列で組むのか: `sh.bin` (CPL=3) の kprintf は引数の窓が 64 バイト */
/*  (exec/exec.c の RING3_ARG_WINDOW)。1 行を `"%s\n"` 1 引数で出せば窓を    */
/*  気にしなくてよく、しかも**ホストで出力そのものを突き合わせられる**。     */
/*                                                                          */
/*  試験: tools/tests/pci_decode_host.c の verbose_* (test_pci_decode.py)   */
/* ======================================================================== */

#ifndef PCI_VERBOSE_H
#define PCI_VERBOSE_H

#include "types.h"

/* 受け取る config の DWORD 数 (0x00〜0x3F)。Type 0 の Interrupt Pin (0x3D)
 * までがこの中に収まる。 */
#define PCI_VERBOSE_CFG_DWORDS  16

/* 1 行の最大長 (終端込み)。いちばん長い行は BAR の行で 60 文字弱。 */
#define PCI_VERBOSE_LINE_MAX    96

/* 行番号の上限。0 から順に呼び、PCI_VERBOSE_END が返るまで回す。 */
#define PCI_VERBOSE_END   (-1)  /* もう行が無い */
#define PCI_VERBOSE_SKIP  0     /* この番号の行は出さない (例: 実装外の BAR 欄) */
#define PCI_VERBOSE_LINE  1     /* buf に 1 行書いた (改行は含まない) */

/* `idx` 番目の行を buf へ書く。`cfg` は config 0x00〜0x3F の DWORD 16 個。
 * 戻り値は PCI_VERBOSE_*。buf は常に NUL 終端される (cap >= 1 のとき)。 */
int pci_verbose_line(const u32 *cfg, u32 bus, u32 dev, u32 fn,
                     int idx, char *buf, int cap);

/* "B:D.F" (各欄 10 進、`lspci` の表示と同じ) を読む。成功で 0、形や範囲が
 * 違えば -1。bus 0〜255 / dev 0〜31 / fn 0〜7。 */
int pci_parse_bdf(const char *s, u32 *bus, u32 *dev, u32 *fn);

#endif /* PCI_VERBOSE_H */
