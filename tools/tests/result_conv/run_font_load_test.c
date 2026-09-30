/* font_load_test を <名前>_main として取り込む。中身は写さない。 */
#include <stdio.h>

#include "shim.h"

#define printf rconv_printf
#define main   font_load_test_main
#include "../../../userland/tests/font_load_test.c"
#undef main
#undef printf
