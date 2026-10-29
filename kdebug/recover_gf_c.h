#ifndef KDEBUG_RECOVER_GF_C_H
#define KDEBUG_RECOVER_GF_C_H
#include "../hwdev/gf_load_state.h"
/* Recover adapter namespace. Hardware descriptors/order live in hwdev. */
#define REC_GF_C_RULE_FIRST 578U
#define REC_GF_C_PHASE 9U
#define REC_GF_C_FP_ON GF_LOAD_FP_ON
/* Compare coefficients only in CHECK after 68050Ch selects VPLL_PROG.
 * With bit9 clear, readback exposes the fixed source, not these writes. */
#define REC_GF_C_CHECK_508_MASK 0x0007ffffU
#define REC_GF_C_CHECK_578_MASK 0x8000ffffU
#endif
