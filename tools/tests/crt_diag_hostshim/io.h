#ifndef CRT_DIAG_HOST_IO_H
#define CRT_DIAG_HOST_IO_H

unsigned int crt_shim_in(unsigned int port, int width);
void crt_shim_out(unsigned int port, unsigned int value, int width);
static inline unsigned int inp(unsigned int port)
{ return crt_shim_in(port, 8); }
static inline unsigned int inpw(unsigned int port)
{ return crt_shim_in(port, 16); }
static inline void outp(unsigned int port, unsigned int value)
{ crt_shim_out(port, value, 8); }
static inline void outpw(unsigned int port, unsigned int value)
{ crt_shim_out(port, value, 16); }
static inline void io_wait(void)
{ crt_shim_out(0x5F, 0, 8); }

#endif
