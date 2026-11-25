#ifndef KDEBUG_RECOVER_DIAG_H
#define KDEBUG_RECOVER_DIAG_H
#include "types.h"
/* Internal diagnostics; no shared recovery or module ABI changes. */
enum rec_tx_fn { REC_TX_NONE, REC_TX_READY, REC_TX_PUT, REC_TX_WAIT_FRAME,
    REC_TX_PIT_WAIT, REC_TX_BEGIN, REC_TX_EMPTY, REC_TX_BUDGET, REC_TX_ROW };
enum rec_tx_where { REC_TX_NOWHERE, REC_TX_BYTE, REC_TX_BUSY_WAIT,
    REC_TX_AFTER_PUT, REC_TX_FLUSH, REC_TX_SEPARATOR, REC_TX_BUILD };
enum rec_tx_rc { REC_TX_PIT_CONFIG = -11, REC_TX_PIT_COUNT = -12,
    REC_TX_PIT_SAMPLES = -13, REC_TX_POLLS = -31, REC_TX_BYTES = -32,
    REC_TX_TIME = -33, REC_TX_POINT = -34, REC_TX_OVERFLOW = -35 };
struct rec_tx_diag {
    u32 sid, seq, pos, fn; int rc;
    u32 where, sent, polls, bytes, valid, attempted, active;
};
const struct rec_tx_diag *recover_diag_get(void);
const char *recover_diag_fn(u32 fn);
const char *recover_diag_where(u32 where);
#endif
