#include "crt_diag.h"
#include "io.h"
#include "memmap.h"
#include "pegc.h"
#include "kprintf.h"
#include "generated/kdebug_imports.h"

static const u32 crt_work_addr[CRT_DIAG_WORK_COUNT] = {
    CRT_DIAG_W0400, CRT_DIAG_W0402, CRT_DIAG_W0459,
    CRT_DIAG_W053C, CRT_DIAG_W054C, CRT_DIAG_W054D
};

void crt_diag_read(CrtDiagRaw *r)
{
    r->hsync = (u8)inp(PEGC_HSYNC_PORT);
    r->sysa = (u8)inp(CRT_DIAG_SYSPORT_A);
    for (int i = 0; i < CRT_DIAG_WORK_COUNT; i++)
        r->w[i] = kdi_ptr_v86_session()->real_lowmem[crt_work_addr[i]];
    for (int i = 0; i < CRT_DIAG_MSW_COUNT; i++)
        r->msw[i] = *(const volatile u8 *)P2V_IO(
            CRT_DIAG_MSW_BASE + CRT_DIAG_MSW_STRIDE * i);
    for (int i = 0; i < CRT_DIAG_SDIP_COUNT; i++)
        r->sdip[i] = (u8)inp(CRT_DIAG_SDIP_BASE + CRT_DIAG_SDIP_STRIDE * i);
}

/* Count the full length even after the destination fills, keeping its prefix. */
static void crt_put_char(char *buf, int cap, int *pos, char c)
{
    if (*pos < cap - 1) buf[*pos] = c;
    (*pos)++;
}

static void crt_put_str(char *buf, int cap, int *pos, const char *s)
{
    while (*s) crt_put_char(buf, cap, pos, *s++);
}

static void crt_put_hex(char *buf, int cap, int *pos, u32 value, int digits)
{
    static const char hex[] = "0123456789abcdef";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        crt_put_char(buf, cap, pos, hex[(value >> shift) & 0xF]);
}

int crt_diag_format(const CrtDiagRaw *r, char *buf, int bufsz)
{
    static const char *const work_key[CRT_DIAG_WORK_COUNT] = {
        " w0400=", " w0402=", " w0459=", " w053c=", " w054c=", " w054d="
    };
    int pos = 0;
    if (!buf || bufsz <= 0) return -1;
    crt_put_str(buf, bufsz, &pos, "[crt] 09a8=");
    crt_put_hex(buf, bufsz, &pos, r->hsync, 2);
    crt_put_str(buf, bufsz, &pos, " sysa=");
    crt_put_hex(buf, bufsz, &pos, r->sysa, 2);
    for (int i = 0; i < CRT_DIAG_WORK_COUNT; i++) {
        crt_put_str(buf, bufsz, &pos, work_key[i]);
        crt_put_hex(buf, bufsz, &pos, r->w[i], 2);
    }
    crt_put_str(buf, bufsz, &pos, " msw=");
    for (int i = 0; i < CRT_DIAG_MSW_COUNT; i++) {
        if (i) crt_put_char(buf, bufsz, &pos, ',');
        crt_put_hex(buf, bufsz, &pos, CRT_DIAG_MSW_BASE + CRT_DIAG_MSW_STRIDE * i, 5);
        crt_put_char(buf, bufsz, &pos, ':');
        crt_put_hex(buf, bufsz, &pos, r->msw[i], 2);
    }
    /* BOOTLOG_HDR_MAX limits only the file header. The body has no line cap;
     * split here to keep each diagnostic line within the design's 220 bytes. */
    crt_put_str(buf, bufsz, &pos, "(15)\n[crt2] sdip_bank=unknown sdip=");
    for (int i = 0; i < CRT_DIAG_SDIP_COUNT; i++) {
        if (i) crt_put_char(buf, bufsz, &pos, ',');
        crt_put_hex(buf, bufsz, &pos, CRT_DIAG_SDIP_BASE + CRT_DIAG_SDIP_STRIDE * i, 4);
        crt_put_char(buf, bufsz, &pos, ':');
        crt_put_hex(buf, bufsz, &pos, r->sdip[i], 2);
    }
    crt_put_str(buf, bufsz, &pos, "(12)");
    buf[pos < bufsz ? pos : bufsz - 1] = '\0';
    return pos < bufsz ? pos : -1;
}

void crt_diag_bootlog(void)
{
    CrtDiagRaw raw;
    char buf[CRT_DIAG_BUFSZ];
    crt_diag_read(&raw);
    if (crt_diag_format(&raw, buf, sizeof(buf)) >= 0)
        kprintf(KLOG_ATTR, "%s\n", buf);
}
