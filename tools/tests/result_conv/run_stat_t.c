/* stat_t を <名前>_main として取り込む。中身は写さない。 */
#include "shim.h"

#define main stat_t_main
#include "../../../userland/tests/stat_t.c"
#undef main
