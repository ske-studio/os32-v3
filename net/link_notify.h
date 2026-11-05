/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_NET_LINK_NOTIFY_H
#define OS32_NET_LINK_NOTIFY_H
#include "types.h"

/* 新 net/link_notify.h: sessionはsess/epochの組、時刻は10ms tick */
void link_notify_session(u16 sess, u16 epoch, int extended, u32 generation, u16 alive_id);
void link_notify_rx(u32 seq, const u8 *payload, u16 len); void link_notify_tick(u32 now);
void link_notify_reset(void);
int link_notify_control(u8 op, u32 ack, u16 window); /* A8がlink.cに定義、rid=0 */

/* Session=(sess,epoch), now is 10ms ticks; old HELLO uses extended=0.
 * A8 calls tick at the end of link_tick. control is ACK/WINDOW only, rid=0,
 * window in messages; 0/AGAIN/negative. B2 retains pending control for retry.
 * Session reset discards notification sequence, not the mailbox. */

#endif
