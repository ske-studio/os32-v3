/* Fixed value ABI for ver / About. Basic type u32 is supplied by the caller
 * (os32_kapi_shared.h in the public API). C89-compatible; no pointers. */
#ifndef OS32_VER_INFO_H
#define OS32_VER_INFO_H

#define OS32_VER_LINES 20
#define OS32_VER_TEXT 64
#define OS32_VER_COLS 52

/* kind low byte selects the display group; INDENT is independent. */
#define OS32_VER_KIND_MASK 0xffu
#define OS32_VER_HEAD 0u
#define OS32_VER_DEVICE 1u
#define OS32_VER_IDENTITY 2u
#define OS32_VER_DEBUG 3u
#define OS32_VER_INDENT 0x100u

typedef struct {
    u32 kind;
    char text[OS32_VER_TEXT];
} OS32_VerLine;

typedef struct {
    u32 count;
    OS32_VerLine lines[OS32_VER_LINES];
} OS32_VerInfo;

#if defined(__i386__)
/* Fixed ILP32 ABI; typedef assertions also work with C89 consumers. */
typedef char os32_ver_line_size_68[(sizeof(OS32_VerLine) == 68) ? 1 : -1];
typedef char os32_ver_info_size_1364[(sizeof(OS32_VerInfo) == 1364) ? 1 : -1];
#endif

#endif
