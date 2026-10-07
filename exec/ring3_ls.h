/* Legacy int80 shim and KAPI v70 value enumeration share one collector. */
#ifndef RING3_LS_H
#define RING3_LS_H

#define RING3_LS_CALL 0x8000000c
#define RING3_LS_BATCH 14
/* 3708-byte packet + 256-byte path + alignment fits about 4KB. */
#define RING3_LS_PATH_SIZE 256
#define RING3_LS_PATH_OFF ((RING3_LS_PACKET_SIZE + 15) & -16)
#define RING3_LS_STACK_SIZE ((RING3_LS_PATH_OFF + RING3_LS_PATH_SIZE + 15) & -16)
#define RING3_LS_ENTRY_SIZE 264
#define RING3_LS_ENTRIES_OFF 12
#define RING3_LS_PACKET_SIZE (RING3_LS_ENTRIES_OFF + RING3_LS_BATCH * RING3_LS_ENTRY_SIZE)
#define RING3_LS_SHIM_CAP 192

#ifndef __ASSEMBLER__
#include "types.h"
#include "os32_ls.h"
int ring3_ls_window(const char *path, u32 skip, OS32_LsPacket *out);
int ring3_ls_dispatch(u32 user_esp);
extern const u8 ring3_ls_start[], ring3_ls_end[];
#endif
#endif
