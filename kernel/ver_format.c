/* Common ver/About identity lines and bounded line builder. */
#include "platform_ver.h"
#include "config.h"
#include "kstring.h"

#define VER_VERSION_MAX 20u
#define VER_IDENTITY (OS32_VER_IDENTITY | OS32_VER_INDENT)
STATIC_ASSERT(sizeof(SYS_VERSION) - 1 <= VER_VERSION_MAX, ver_version_length);
STATIC_ASSERT(OS32_VER_COLS < OS32_VER_TEXT, ver_nul_capacity);
STATIC_ASSERT(OS32_VER_LINES >= 16, ver_line_capacity);

void ver_text(VerLineBuilder *b, const char *text)
{
    while (*text && b->used < OS32_VER_COLS)
        b->line->text[b->used++] = *text++;
    if (*text) b->overflow = 1;
    b->line->text[b->used] = '\0';
}

/* Fixed-size snapshot strings need not be scanned beyond their storage. */
void ver_saved_text(VerLineBuilder *b, const char *text, u32 size)
{
    u32 n = 0;
    while (n < size && text[n]) n++;
    if (n == size) {
        b->overflow = 1;
        return;
    }
    ver_text(b, text);
}

void ver_number(VerLineBuilder *b, u32 value, int hex_digits)
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

VerLineBuilder ver_line(OS32_VerInfo *out, u32 kind, const char *label)
{
    VerLineBuilder b = {&out->lines[out->count++], label, 0, 0};
    b.line->kind = kind;
    ver_text(&b, label);
    ver_text(&b, ": ");
    return b;
}

void ver_end(VerLineBuilder *b)
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
    const struct ver_backend *backend = platform_ver_backend();
    kmemset(out, 0, sizeof(*out));
    b = ver_line(out, OS32_VER_HEAD, "OS32");
    b.used = 0;
    ver_text(&b, backend && backend->head ? backend->head : "OS32 v" SYS_VERSION);
    ver_end(&b);

    if (backend && backend->devices) backend->devices(s, out);
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
        if (backend && backend->image) backend->image(s, out);
    }
    if (backend && backend->debug) backend->debug(s, out);
}

void ver_gfx_value(VerLineBuilder *b, const VerSnapshot *s, const char *backend_name)
{
    if (!backend_name || !s->gfx.width || !s->gfx.height || !s->gfx.bpp)
        ver_text(b, "unknown");
    else {
        ver_text(b, backend_name); ver_text(b, " ");
        ver_number(b, s->gfx.width, 0); ver_text(b, "x");
        ver_number(b, s->gfx.height, 0); ver_text(b, "x");
        ver_number(b, s->gfx.bpp, 0); ver_text(b, " bpp");
    }
    ver_text(b, s->gfx_active ? "; active" : "; GUI target");
}
