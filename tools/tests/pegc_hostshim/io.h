/* ========================================================================
 *  tools/tests/pegc_hostshim/io.h — ホスト試験用の io.h 差し替え
 *
 *  include/io.h は in / out を特権命令のインライン asm で定義するので、
 *  gfx/backend_pegc.c をホストでそのまま走らせるために -I で先に置いて
 *  差し替え、ポート操作を試験側 (tools/tests/pegc_mode_host.c) の偽の I/O へ
 *  回す。atapi_hostshim/io.h と同じ作法。
 *
 *  [C1] C89 / GNU89。
 * ======================================================================== */

#ifndef IO_H
#define IO_H

/* 試験側 (tools/tests/pegc_mode_host.c) が定義する */
unsigned int pegc_shim_inp(unsigned int port);
void pegc_shim_outp(unsigned int port, unsigned int value);

static inline unsigned int inp(unsigned int port) { return pegc_shim_inp(port); }
static inline void outp(unsigned int port, unsigned int value)
{ pegc_shim_outp(port, value); }
static inline unsigned int inpw(unsigned int port)
{ (void)port; return 0xFFFFU; }
static inline void outpw(unsigned int port, unsigned int value)
{ (void)port; (void)value; }

/* 実物は I/O 5Fh への書き込み (約 0.6us の待ち)。6Eh の後の待ちを列で確かめる
 * ために、偽の I/O へ 5Fh の OUT として記録する。 */
static inline void io_wait(void) { pegc_shim_outp(0x5FU, 0U); }

#endif /* IO_H */
