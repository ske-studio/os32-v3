/* ========================================================================
 *  tools/tests/fdc_hostshim/io.h — ホスト試験用の io.h 差し替え
 *
 *  include/io.h は inp / outp を特権命令のインライン asm で定義するので、
 *  ホストで drivers/fdc.c をそのまま走らせるために -I で先に置いて差し替え、
 *  ポート操作を試験側の µPD765A の模型 (tools/tests/fdc_track_host.c) へ回す。
 *  pcm_hostshim/io.h と同じ作法。
 *
 *  [C1] C89 / GNU89。
 * ======================================================================== */

#ifndef IO_H
#define IO_H

/* 試験側 (tools/tests/fdc_track_host.c) が定義する */
unsigned int fdc_shim_inp(unsigned int port);
void fdc_shim_outp(unsigned int port, unsigned int value);

static inline unsigned int inp(unsigned int port) { return fdc_shim_inp(port); }
static inline void outp(unsigned int port, unsigned int value)
{ fdc_shim_outp(port, value); }

static inline void io_wait(void) { }

#endif /* IO_H */
