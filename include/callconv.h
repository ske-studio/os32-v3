/* Internal calling convention; selected independently of SDK contracts. */
#ifndef OS32_CALLCONV_H
#define OS32_CALLCONV_H
#ifndef __cdecl
#if defined(__i386__)
#define __cdecl __attribute__((cdecl))
#else
#define __cdecl
#endif
#endif
#endif
