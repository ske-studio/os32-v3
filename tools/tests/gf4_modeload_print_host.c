/* Unobserved dependency for component hosts; c0log observes the real line. */
#include "kprintf.h"
void __cdecl kprintf(u8 attr,const char *fmt,...)
{ (void)attr; (void)fmt; }
