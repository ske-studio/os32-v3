#include "support.h"
#include "backend.c"
#include "formatter.c"

static u32 sources;
u32 pm_mpic_source_count(void) { return sources; }

static void row(OS32_VerInfo *out, u32 index, u32 kind, const char *text)
{
    CHECK(out->lines[index].kind == kind);
    CHECK(equal(out->lines[index].text, text));
}

void _start(void)
{
    const u32 dev = OS32_VER_DEVICE | OS32_VER_INDENT;
    const u32 id = OS32_VER_IDENTITY | OS32_VER_INDENT;
    VerSnapshot s = {0};
    OS32_VerInfo out;
    s.api_version = 99;
    s.build[0] = 'B';
    pm_ver_backend.collect(&s);
    ver_format(&s, &out);
    CHECK(out.count == 4);
    row(&out, 0, OS32_VER_HEAD, "PowerMac OS32 v" SYS_VERSION);
    row(&out, 1, dev, "DEC: tick=1000Hz (configured)");
    row(&out, 2, id, "API: v99");
    row(&out, 3, id, "Build: B");

    s.boot_ok = 1;
    s.boot.commit[0] = 'C';
    ver_format(&s, &out);
    CHECK(out.count == 5);
    row(&out, 4, id, "Commit: C");

    pm_ver_note_pvr(0x80010303);
    sources = 64;
    pm_ver_backend.collect(&s);
    s.ser_ready = 1;
    s.ser_baud = 38400;
    s.gfx.width = 800;
    s.gfx.height = 600;
    s.gfx.bpp = 8;
    s.boot.crc_valid = 1;
    s.boot.image_crc = 0xabc123;
    s.boot.image_size = 45678;
    ver_format(&s, &out);
    CHECK(out.count == 10);
    row(&out, 0, OS32_VER_HEAD, "PowerMac OS32 v" SYS_VERSION);
    row(&out, 1, dev, "CPU: PowerPC PVR=80010303");
    row(&out, 2, dev, "IRQ: MPIC sources=64");
    row(&out, 3, dev, "DEC: tick=1000Hz (configured)");
    row(&out, 4, dev, "SER: ESCC baud=38400");
    row(&out, 5, dev, "GFX: OF-fb 800x600x8 bpp; GUI target");
    row(&out, 6, id, "API: v99");
    row(&out, 7, id, "Build: B");
    row(&out, 8, id, "Commit: C");
    row(&out, 9, id, "Root image CRC: 00abc123 (45678 bytes)");
    s.gfx_active = 1;
    ver_format(&s, &out);
    row(&out, 5, dev, "GFX: OF-fb 800x600x8 bpp; active");
    die(0);
}
