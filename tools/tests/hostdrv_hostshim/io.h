/* ========================================================================
 *  tools/tests/hostdrv_hostshim/io.h — ホスト試験用の io.h 差し替え
 *
 *  include/io.h は inp / outp を**特権命令のインライン asm**で定義するので、
 *  ホストでそのまま走らせると落ちる。この版を -I で先に置いて差し替え、
 *  ポート操作を試験側の関数へ回す
 *  (tools/tests/mtar_freestanding と同じ作法)。
 *
 *  票: B8 / Codex 実装レビュー P1-4。fs/hostdrvfs.c の **実物**をホストで
 *  動かし、hdrv_get_file_size() などを直接通すために要る。
 *
 *  [C1] C89 / GNU89。
 * ======================================================================== */

#ifndef IO_H
#define IO_H

/* 試験側 (tools/tests/b8_hostdrv_host.c) が定義する */
unsigned int hdrv_shim_inp(unsigned int port);
void hdrv_shim_outp(unsigned int port, unsigned int value);

static inline unsigned int inp(unsigned int port) { return hdrv_shim_inp(port); }
static inline void outp(unsigned int port, unsigned int value)
{ hdrv_shim_outp(port, value); }

static inline unsigned int inpw(unsigned int port) { return hdrv_shim_inp(port); }
static inline void outpw(unsigned int port, unsigned int value)
{ hdrv_shim_outp(port, value); }

static inline void insw_rep(unsigned int port, void *buf, unsigned int count)
{ (void)port; (void)buf; (void)count; }
static inline void outsw_rep(unsigned int port, const void *buf, unsigned int count)
{ (void)port; (void)buf; (void)count; }

static inline void io_wait(void) { }

#endif /* IO_H */
