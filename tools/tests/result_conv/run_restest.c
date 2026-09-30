/* restest を <名前>_main として取り込む。中身は写さない。
 * newlib の printf を使うので、出力はハーネスの捕捉バッファへ回す。 */
#include <stdio.h>
#include <string.h>

#include "shim.h"

#define printf rconv_printf
#define main   restest_main
#include "../../../userland/tests/restest.c"
#undef main
#undef printf
