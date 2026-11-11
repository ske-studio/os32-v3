#ifndef OS32_CONSOLE_DUAL_H
#define OS32_CONSOLE_DUAL_H

#include "types.h"

#define CON_OUT_MAX 9     /* 1 + GFX2_MAX_ADAPTERS(4) × head[2] */
enum { CON_OUT_NONE = 0, CON_OUT_TEXT = 1, CON_OUT_DESK = 2 };
enum { CON_CONN_UNKNOWN = 0, CON_CONN_ON = 1, CON_CONN_OFF = 2 };
struct con_out {
    u8 adapter, head, connector, conn_type;
    u8 conn_status;
    u8 running;
    u8 role, live;
};
void con_dual_configure(int on);
int  con_dual_cfg(void);
int  con_dual_active(void);
int  con_dual_was_live(void);
void con_dual_session_end(void);
const struct con_out *con_dual_outputs(int *n);

int display_dual_outputs(struct con_out *o, int max) __attribute__((weak));
u32 display_dual_key(void) __attribute__((weak));
void display_dual_dpms(u32 state) __attribute__((weak));

#endif
