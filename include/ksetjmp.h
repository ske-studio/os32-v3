/* ======================================================================== */
/*  KSETJMP.H — カーネル内 setjmp/longjmp (kernel/setjmp.asm)               */
/*                                                                          */
/*  ESP/EBP/EBX/ESI/EDI/戻り先 EIP の 6 ワードを保存/復元する。              */
/*  exec のネスト復帰と V86 セッション脱出が使う。                           */
/*                                                                          */
/*  注意:                                                                   */
/*   - exec_longjmp は常に setjmp の戻り値 1 で戻る (値は渡せない)           */
/*   - EFLAGS は復元しない。割り込み/例外文脈から longjmp した場合、         */
/*     IF は落ちたままなので復帰側で戻すこと (exec_run / v86_run 参照)       */
/* ======================================================================== */

#ifndef __KSETJMP_H
#define __KSETJMP_H

#include "types.h"

#define KSETJMP_BUF_LEN 6

int  exec_setjmp(u32 *buf);
void exec_longjmp(u32 *buf);

#endif /* __KSETJMP_H */
