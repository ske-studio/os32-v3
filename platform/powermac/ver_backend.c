#include "platform_ver.h"
#include "mpic.h"
#include "ppc_trap.h"
#include "config.h"
#include "kstring.h"

static u32 saved_pvr, pvr_valid;

void pm_ver_note_pvr(u32 pvr)
{
    saved_pvr = pvr;
    pvr_valid = 1;
}

static void pm_ver_collect(VerSnapshot *s)
{
    s->plat[0] = saved_pvr;
    s->plat[1] = pvr_valid;
    s->plat[2] = pm_mpic_source_count();
}

static void pm_ver_devices(const VerSnapshot *s, OS32_VerInfo *out)
{
    const u32 kind = OS32_VER_DEVICE | OS32_VER_INDENT;
    VerLineBuilder b;
    if (s->plat[1]) {
        b = ver_line(out, kind, "CPU");
        ver_text(&b, "PowerPC PVR=");
        ver_number(&b, s->plat[0], HEX32_DIGITS);
        ver_end(&b);
    }
    if (s->plat[2]) {
        b = ver_line(out, kind, "IRQ");
        ver_text(&b, "MPIC sources=");
        ver_number(&b, s->plat[2], 0);
        ver_end(&b);
    }
    b = ver_line(out, kind, "DEC");
    ver_text(&b, "tick=");
    ver_number(&b, PM_DEC_HZ, 0);
    ver_text(&b, "Hz (configured)");
    ver_end(&b);
    if (s->ser_ready) {
        b = ver_line(out, kind, "SER");
        ver_text(&b, "ESCC baud=");
        ver_number(&b, s->ser_baud, 0);
        ver_end(&b);
    }
    if (s->gfx.width && s->gfx.height && s->gfx.bpp) {
        b = ver_line(out, kind, "GFX");
        ver_text(&b, "OF-fb ");
        ver_number(&b, s->gfx.width, 0);
        ver_text(&b, "x");
        ver_number(&b, s->gfx.height, 0);
        ver_text(&b, "x");
        ver_number(&b, s->gfx.bpp, 0);
        ver_text(&b, s->gfx_active ? " bpp; active" : " bpp; GUI target");
        ver_end(&b);
    }
}

static void pm_ver_image(const VerSnapshot *s, OS32_VerInfo *out)
{
    if (!s->boot.crc_valid) return;
    VerLineBuilder b = ver_line(out, OS32_VER_IDENTITY | OS32_VER_INDENT, "Root image CRC");
    ver_number(&b, s->boot.image_crc, HEX32_DIGITS);
    ver_text(&b, " (");
    ver_number(&b, s->boot.image_size, 0);
    ver_text(&b, " bytes)");
    ver_end(&b);
}

const struct ver_backend pm_ver_backend = {
    .head = "PowerMac OS32 v" SYS_VERSION,
    .collect = pm_ver_collect,
    .devices = pm_ver_devices,
    .image = pm_ver_image,
};
