#ifndef USER_READ_CONTRACT_H
#define USER_READ_CONTRACT_H

#include "types.h"

/* Trusted kernel and window-manager direct calls are accepted. For user
 * calls, reject a null start or a wrapping range and require every byte
 * in a nonempty range to be readable by the current caller. */
int ring3_user_range_ok(u32 p, u32 len);

#endif
