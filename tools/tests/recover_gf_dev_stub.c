#include "../../kdebug/recover.h"
u32 recover_gf_test_dev = RECOVER_GF_DEV_FALLBACK;
u32 recover_gf_dev(void) { return recover_gf_test_dev; }
u32 recover_gf_second(void) { return U32_MAX; }
