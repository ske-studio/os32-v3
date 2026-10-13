#ifndef CRT_DIAG_H
#define CRT_DIAG_H

#include "types.h"

/* BIOS work and memory switches: raw bytes only; no bank selection. */
#define CRT_DIAG_W0400       0x0400UL
#define CRT_DIAG_W0402       0x0402UL
#define CRT_DIAG_W0459       0x0459UL
#define CRT_DIAG_W053C       0x053CUL
#define CRT_DIAG_W054C       0x054CUL
#define CRT_DIAG_W054D       0x054DUL
#define CRT_DIAG_WORK_COUNT  6
#define CRT_DIAG_MSW_BASE    0xA3FE2UL
#define CRT_DIAG_MSW_STRIDE  2UL
#define CRT_DIAG_MSW_COUNT   15
#define CRT_DIAG_SDIP_BASE   0x841E
#define CRT_DIAG_SDIP_STRIDE 0x100
#define CRT_DIAG_SDIP_COUNT  12
#define CRT_DIAG_SYSPORT_A   0x0031

/* Two labelled lines, each at most 220 bytes excluding newline. */
#define CRT_DIAG_LINE_MAX    220
#define CRT_DIAG_BUFSZ       (2 * (CRT_DIAG_LINE_MAX + 1) + 1)

typedef struct {
    u8 hsync, sysa;
    u8 w[CRT_DIAG_WORK_COUNT];
    u8 msw[CRT_DIAG_MSW_COUNT];
    u8 sdip[CRT_DIAG_SDIP_COUNT];
} CrtDiagRaw;

void crt_diag_read(CrtDiagRaw *r);
/* Length excluding NUL, or -1 on truncation (NUL-terminated if bufsz > 0).
 * The single embedded newline separates [crt] from [crt2]. */
int crt_diag_format(const CrtDiagRaw *r, char *buf, int bufsz);
void crt_diag_bootlog(void);

#endif
