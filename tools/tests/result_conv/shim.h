/* ========================================================================= *
 *  SHIM.H — 実物の試験プログラムをホストで 1 本ずつ走らせるための足場
 *
 *  試験プログラムは翻訳単位ごとに `static` の同名変数 (g_total / g_passed /
 *  check ...) を持つので、1 つの .c にまとめて #include すると衝突する。
 *  そこで**プログラム 1 本につき 1 つの翻訳単位**を作り、そこで
 *
 *      #define main <名前>_main
 *      #include "../../../userland/tests/<名前>.c"
 *
 *  と名前だけ付け替える。中身は 1 行も写さない。
 *
 *  ハーネス側 (tools/tests/test_result_conv_host.c) は
 *  `extern int <名前>_main(int, char **, KernelAPI *)` としてだけ知る。
 *  **`void main` に戻す変異を実行時に踏ませるため**、呼び出し側は
 *  宣言を別の翻訳単位に置いてある (同じ .c にあるとコンパイルで止まってしまい、
 *  試験の目が働いたことにならない)。
 *
 *  newlib の printf を使うプログラムは rconv_printf へ差し替えて、出力を
 *  ハーネスの捕捉バッファへ流す (ホストの stdout はハーネス自身の診断で使う)。
 * ========================================================================= */

#ifndef RESULT_CONV_SHIM_H
#define RESULT_CONV_SHIM_H

#include "os32api.h"

/* ハーネスが定義する。捕捉バッファへ追記する printf。 */
int rconv_printf(const char *fmt, ...);

#endif /* RESULT_CONV_SHIM_H */
