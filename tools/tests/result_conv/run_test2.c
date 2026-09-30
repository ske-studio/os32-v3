/* test2 を <名前>_main として取り込む。中身は写さない。 */
#include "shim.h"

#define main test2_main
#include "../../../userland/tests/test2.c"
#undef main
