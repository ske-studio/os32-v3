/* ========================================================================
 *  tools/tests/atapi_hostshim/io.h — ホスト試験用の io.h 差し替え
 *
 *  include/io.h は in / out を特権命令のインライン asm で定義するので、
 *  drivers/atapi.c をホストでそのまま走らせるために -I で先に置いて差し替え、
 *  ポート操作を試験側の ATAPI デバイスの模型 (tools/tests/cd_read_host.c)
 *  へ回す。fdc_hostshim/io.h と同じ作法。
 *
 *  [C1] C89 / GNU89。
 * ======================================================================== */

#ifndef IO_H
#define IO_H

/* 試験側 (tools/tests/cd_read_host.c) が定義する */
unsigned int atapi_shim_inp(unsigned int port);
void atapi_shim_outp(unsigned int port, unsigned int value);
unsigned int atapi_shim_inpw(unsigned int port);
void atapi_shim_outpw(unsigned int port, unsigned int value);

static inline unsigned int inp(unsigned int port) { return atapi_shim_inp(port); }
static inline void outp(unsigned int port, unsigned int value)
{ atapi_shim_outp(port, value); }
static inline unsigned int inpw(unsigned int port) { return atapi_shim_inpw(port); }
static inline void outpw(unsigned int port, unsigned int value)
{ atapi_shim_outpw(port, value); }

static inline void io_wait(void) { }

#endif /* IO_H */
