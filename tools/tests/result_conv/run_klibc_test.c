/* klibc_test を <名前>_main として取り込む。中身は写さない。
 * newlib の printf を使うので、出力はハーネスの捕捉バッファへ回す。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#define printf rconv_printf
#define main   klibc_test_main
#include "../../../userland/tests/klibc_test.c"
#undef main
#undef printf
