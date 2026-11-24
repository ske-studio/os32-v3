#ifndef OPN_PROBE_HOST_IO_H
#define OPN_PROBE_HOST_IO_H
unsigned inp(unsigned port);
void outp(unsigned port, unsigned value);
void io_wait_n(int n);
#endif
