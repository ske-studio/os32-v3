#ifndef IDE_HOSTSHIM_IO_H
#define IDE_HOSTSHIM_IO_H
unsigned int inp(unsigned int port);
void outp(unsigned int port, unsigned int value);
unsigned int inpw(unsigned int port);
void outpw(unsigned int port, unsigned int value);
#endif
