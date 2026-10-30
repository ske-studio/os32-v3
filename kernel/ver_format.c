/* The single ver/About wording and order. Saved values only; no probes. */
#include "ver_info.h"
#include "config.h"
#include "../arch/x86/kernel/idt.h"
#include "module/module.h"
#include "input_service.h"
#include "bootinfo.h"
#include "../lib/kstring.h"

/* Internal backend values specified by VER_INFO_DESIGN §3. */
#define VER_GFX_PC98 1u
#define VER_GFX_PEGC 2u
#define VER_GFX_CIRRUS 3u
#define VER_GFX_GEFORCE 4u
#define VER_VERSION_MAX 20u
#define VER_DEVICE (OS32_VER_DEVICE | OS32_VER_INDENT)
#define VER_IDENTITY (OS32_VER_IDENTITY | OS32_VER_INDENT)
STATIC_ASSERT(sizeof(SYS_VERSION) - 1 <= VER_VERSION_MAX, ver_version_length);
STATIC_ASSERT(OS32_VER_COLS < OS32_VER_TEXT, ver_nul_capacity);
STATIC_ASSERT(OS32_VER_LINES >= 16, ver_line_capacity);

typedef struct {
    OS32_VerLine *line;
    const char *label;
    u32 used;
    int overflow;
} VerLineBuilder;

static void ver_text(VerLineBuilder *b, const char *text)
{
    while (*text && b->used < OS32_VER_COLS)
        b->line->text[b->used++] = *text++;
    if (*text) b->overflow = 1;
    b->line->text[b->used] = '\0';
}

/* Fixed-size snapshot strings need not be scanned beyond their storage. */
static void ver_saved_text(VerLineBuilder *b, const char *text, u32 size)
{
    u32 n = 0;
    while (n < size && text[n]) n++;
    if (n == size) {
        b->overflow = 1;
        return;
    }
    ver_text(b, text);
}

static void ver_number(VerLineBuilder *b, u32 value, int hex_digits)
{
    static const char digits[] = "0123456789abcdef";
    char buf[U32_DEC_BUFSIZE];
    u32 n = 0;
    u32 base = hex_digits ? 16u : 10u;
    do {
        buf[n++] = digits[value % base];
        value /= base;
    } while (value || (hex_digits && n < (u32)hex_digits));
    while (n) {
        char one[2] = {buf[--n], '\0'};
        ver_text(b, one);
    }
}

static VerLineBuilder ver_line(OS32_VerInfo *out, u32 kind, const char *label)
{
    VerLineBuilder b = {&out->lines[out->count++], label, 0, 0};
    b.line->kind = kind;
    ver_text(&b, label);
    ver_text(&b, ": ");
    return b;
}

static void ver_end(VerLineBuilder *b)
{
    if (b->overflow) {
        b->used = 0;
        kmemset(b->line->text, 0, sizeof(b->line->text));
        ver_text(b, b->label);
        ver_text(b, ": unknown");
    }
}

void __attribute__((cold)) ver_format(const VerSnapshot *s, OS32_VerInfo *out)
{
    VerLineBuilder b;
    const char *backend = s->gfx_kind == VER_GFX_PC98 ? "98" :
                          s->gfx_kind == VER_GFX_PEGC ? "PEGC" :
                          s->gfx_kind == VER_GFX_CIRRUS ? "Cirrus" :
                          s->gfx_kind == VER_GFX_GEFORCE ? "GeForce" : NULL;
    kmemset(out, 0, sizeof(*out));
    b = ver_line(out, OS32_VER_HEAD, "OS32");
    b.used = 0;
    ver_text(&b, "PC-9821 OS32 v" SYS_VERSION " (Ring3 Native)");
    ver_end(&b);

    b = ver_line(out, VER_DEVICE, "CPU");
    ver_text(&b, "x86 32-bit PM+Paging; calib=");
    ver_text(&b, s->cpu_measured ? "measured" : "default");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "PIC");
    ver_text(&b, "8259A x2; vectors=");
    ver_number(&b, PIC_MASTER_OFFSET, 2);
    ver_text(&b, "h/");
    ver_number(&b, PIC_SLAVE_OFFSET, 2);
    ver_text(&b, "h (configured)");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "PIT");
    if (s->pit_valid) {
        ver_text(&b, "8254 tick="); ver_number(&b, s->pit_hz, 0);
        ver_text(&b, "Hz input="); ver_number(&b, s->pit_input, 0);
        ver_text(&b, "Hz");
    } else ver_text(&b, "unknown");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "KBD");
    ver_text(&b, "8251-compatible; IRQ"); ver_number(&b, input_irq(), 0);
    ver_text(&b, " (configured)");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "SER");
    if (!s->ser_ready) ver_text(&b, "not initialized");
    else if (s->ser_mode != KAPI_SER_MODE_COMPAT && s->ser_mode != KAPI_SER_MODE_VFAST)
        ver_text(&b, "unknown");
    else {
        ver_text(&b, "8251 baud="); ver_number(&b, s->ser_baud, 0);
        ver_text(&b, s->ser_mode == KAPI_SER_MODE_VFAST ? " mode=VFAST FIFO=" : " mode=compat FIFO=");
        ver_text(&b, s->ser_fifo ? "yes" : "no");
    }
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "SND");
    ver_text(&b, "OPN configured; PCM=");
    ver_text(&b, s->pcm_present ? "CS4231-family" : "none");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "GFX");
    if (!backend || !s->gfx.width || !s->gfx.height || !s->gfx.bpp)
        ver_text(&b, "unknown");
    else {
        ver_text(&b, backend); ver_text(&b, " ");
        ver_number(&b, s->gfx.width, 0); ver_text(&b, "x");
        ver_number(&b, s->gfx.height, 0); ver_text(&b, "x");
        ver_number(&b, s->gfx.bpp, 0); ver_text(&b, " bpp");
    }
    ver_text(&b, s->gfx_active ? "; active" : "; GUI target");
    ver_end(&b);
    b = ver_line(out, VER_IDENTITY, "API");
    ver_text(&b, "v"); ver_number(&b, s->api_version, 0);
    ver_end(&b);
    b = ver_line(out, VER_IDENTITY, "Build");
    ver_saved_text(&b, s->build, sizeof(s->build));
    ver_end(&b);
    if (s->boot_ok) {
        b = ver_line(out, VER_IDENTITY, "Commit");
        ver_saved_text(&b, s->boot.commit, sizeof(s->boot.commit));
        ver_end(&b);
        b = ver_line(out, VER_IDENTITY, "Image CRC");
        if (s->boot.crc_valid) {
            ver_number(&b, s->boot.image_crc, HEX32_DIGITS);
            ver_text(&b, " ("); ver_number(&b, s->boot.image_size, 0);
            ver_text(&b, " bytes, ");
            ver_text(&b, s->boot.source == BOOTINFO_SRC_HDD ? "HDD" :
                         s->boot.source == BOOTINFO_SRC_FD ? "FD" : "?");
            ver_text(&b, " loader)");
        } else ver_text(&b, "none (loader did not record)");
        ver_end(&b);
    }
    b = ver_line(out, OS32_VER_DEBUG, "kdebug");
    if (!s->debug_ok) ver_text(&b, "unknown");
    else if (s->debug.kdebug_state == KDEBUG_OFF) ver_text(&b, "off");
    else if (s->debug.kdebug_state == KDEBUG_RUNNING) {
        ver_text(&b, "on (pair ");
        ver_number(&b, s->debug.kdebug_pair_crc, HEX32_DIGITS);
        ver_text(&b, ")");
    } else {
        ver_text(&b, "rejected "); ver_number(&b, s->debug.kdebug_state, 0);
        ver_text(&b, " ");
        ver_text(&b, kdebug_state_name((int)s->debug.kdebug_state));
    }
    ver_end(&b);
    if (s->debug_ok && s->debug.kdebug_state == KDEBUG_RUNNING) {
        b = ver_line(out, OS32_VER_DEBUG, "kdebug pages");
        ver_text(&b, "body="); ver_number(&b, s->debug.kdebug_pages, 0);
        ver_text(&b, " owner="); ver_number(&b, s->debug.kdebug_owner_pages, 0);
        ver_end(&b);
    } else if (s->debug_ok && s->debug.kdebug_state != KDEBUG_OFF) {
        b = ver_line(out, OS32_VER_DEBUG, "kdebug file pair");
        ver_number(&b, s->debug.kdebug_pair_crc, HEX32_DIGITS);
        ver_end(&b);
    }
    b = ver_line(out, OS32_VER_DEBUG, "sqlite stack high-water");
    /* This legacy diagnostic has '=' rather than ': '. */
    b.used -= 2;
    ver_text(&b, "="); ver_number(&b, s->services.stack_used, 0);
    ver_text(&b, " of "); ver_number(&b, s->services.stack_size, 0);
    ver_end(&b);
    b = ver_line(out, OS32_VER_DEBUG | OS32_VER_INDENT, "services");
    ver_saved_text(&b, s->services.state, sizeof(s->services.state));
    ver_text(&b, " public="); ver_number(&b, s->services.public, 0);
    ver_end(&b);
}
