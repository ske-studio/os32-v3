#ifndef OS32_V86S_H
#define OS32_V86S_H

typedef struct { unsigned long size; char path[64]; char second[64]; unsigned long flags; } V86_Begin;
typedef struct { unsigned long size, slice_ms, reply_valid; unsigned char reply[4]; unsigned long snd_status, shadow_seq, rsv[2]; } V86_RunIn;
typedef struct { unsigned char bank, reg, kind, byte; } V86_ReadQ;
typedef struct { unsigned long size, reason, port, width, fifo_n, guest_us, code, nq; V86_ReadQ q[4]; } V86_Exit;

#endif
