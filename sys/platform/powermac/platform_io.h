#ifndef OS32_POWERMAC_PLATFORM_IO_H
#define OS32_POWERMAC_PLATFORM_IO_H
#include "ppc_boot.h"
/* Byte ESCC MMIO aliases, not PC-98 port numbers. */
static inline unsigned int inp(unsigned int va) { return ppc_load8(va); }
static inline void outp(unsigned int va,unsigned int v) { ppc_store8(va,v); }
unsigned long pm_wide_io_not_available(unsigned int va,unsigned long value);
void pm_wait_not_available(int n);
static inline unsigned int inpw(unsigned int va) { return pm_wide_io_not_available(va,0); }
static inline void outpw(unsigned int va,unsigned int v) { (void)pm_wide_io_not_available(va,v); }
static inline unsigned long inpd(unsigned int va) { return pm_wide_io_not_available(va,0); }
static inline void outpd(unsigned int va,unsigned long v) { (void)pm_wide_io_not_available(va,v); }
static inline void io_wait(void) { pm_wait_not_available(1); }
static inline void io_wait_n(int n) { pm_wait_not_available(n); }
#endif
