#ifndef USER_READ_CONTRACT_H
#define USER_READ_CONTRACT_H

#include "types.h"

/* Read permission through the fixed caller's managed page walk (PDE/PTE
 * PRESENT|USER and registered backing). No CR3 switch. CPL0/WM direct calls
 * retain their explicit trusted convention. NULL/overflow/NP refuse. */
int ring3_user_range_ok(u32 p, u32 len);

#endif
