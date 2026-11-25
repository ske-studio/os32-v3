#ifndef PCM_WAV_H
#define PCM_WAV_H

/* The reader returns exactly n bytes or an error. Offsets are file-relative. */
typedef int (*wav_read_fn)(void *, unsigned, unsigned char *, unsigned);
struct wav_info {
    unsigned format, channels, rate, align, bits, data_offset, data_size;
};
static unsigned wav_u16(const unsigned char *p)
{ return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned wav_u32(const unsigned char *p)
{ return wav_u16(p) | (wav_u16(p + 2) << 16); }
static int wav_id(const unsigned char *p, const char *s)
{ return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3]; }
/* -1 malformed, -2 unsupported, 0 success. */
static int wav_parse(wav_read_fn read_at, void *ctx, unsigned size, struct wav_info *w)
{
    unsigned char b[16];
    unsigned end, pos, len, padded;
    int have_fmt = 0;
    if (size < 12 || read_at(ctx, 0, b, 12) || !wav_id(b, "RIFF") ||
        !wav_id(b + 8, "WAVE")) return -1;
    len = wav_u32(b + 4);
    if (len < 4 || len > size - 8) return -1;
    end = len + 8;
    pos = 12;
    while (pos < end) {
        if (end - pos < 8 || read_at(ctx, pos, b, 8)) return -1;
        len = wav_u32(b + 4);
        pos += 8;
        if (len > end - pos) return -1;
        padded = len + (len & 1u);
        if (padded < len || padded > end - pos) return -1;
        if (wav_id(b, "fmt ")) {
            if (have_fmt || len < 16 || read_at(ctx, pos, b, 16)) return -1;
            w->format = wav_u16(b); w->channels = wav_u16(b + 2);
            w->rate = wav_u32(b + 4); w->align = wav_u16(b + 12);
            w->bits = wav_u16(b + 14);
            have_fmt = 1;
            if (w->format != 1 || w->channels != 2 || w->rate != 44100 ||
                w->align != 4 || w->bits != 16) return -2;
        } else if (wav_id(b, "data")) {
            if (!have_fmt || (len & 3u)) return -1;
            w->data_offset = pos; w->data_size = len;
            return 0;
        }
        pos += padded;
    }
    return -1;
}
#endif
