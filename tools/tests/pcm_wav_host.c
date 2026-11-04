#include "../../userland/tests/pcm_wav.h"
enum {
    RIFF_HEADER_SIZE = 12, CHUNK_HEADER_SIZE = 8, FMT_SIZE = 16,
    FMT_FORMAT = 0, FMT_CHANNELS = 2, FMT_RATE = 4,
    FMT_ALIGN = 12, FMT_BITS = 14,
    CANONICAL_FMT = RIFF_HEADER_SIZE + CHUNK_HEADER_SIZE,
    CANONICAL_DATA = RIFF_HEADER_SIZE + CHUNK_HEADER_SIZE + FMT_SIZE,
    CANONICAL_DATA_OFFSET = CANONICAL_DATA + CHUNK_HEADER_SIZE,
    LIST_CHUNK_SIZE = CHUNK_HEADER_SIZE + 4
};
static unsigned char file[128];
static unsigned length;
static void put32(unsigned at, unsigned n) { file[at]=n; file[at+1]=n>>8; file[at+2]=n>>16; file[at+3]=n>>24; }
static void id(unsigned at, const char *s) { unsigned i; for (i=0;i<4;i++) file[at+i]=s[i]; }
static int read_at(void *ctx, unsigned off, unsigned char *out, unsigned n)
{
    unsigned i; (void)ctx;
    if (off > length || n > length-off) return -1;
    for (i=0;i<n;i++) out[i]=file[off+i];
    return 0;
}
static void setup(int extra)
{
    unsigned i, p = 12;
    for (i=0;i<sizeof(file);i++) file[i]=0;
    id(0,"RIFF"); id(8,"WAVE");
    if (extra) { id(p,"LIST"); put32(p+4,3); p+=12; }
    id(p,"fmt "); put32(p+4,16); file[p+8]=1; file[p+10]=2;
    put32(p+12,44100); put32(p+16,176400); file[p+20]=4; file[p+22]=16; p+=24;
    id(p,"data"); put32(p+4,8); p+=16; length=p; put32(4,p-8);
}
static int run(void)
{
    struct wav_info w;
    unsigned fields[] = {CANONICAL_FMT + FMT_FORMAT, CANONICAL_FMT + FMT_CHANNELS,
                         CANONICAL_FMT + FMT_RATE, CANONICAL_FMT + FMT_ALIGN,
                         CANONICAL_FMT + FMT_BITS};
    unsigned i;
    setup(0); if (wav_parse(read_at,0,length,&w) || w.data_offset != CANONICAL_DATA_OFFSET || w.data_size != 8) return 1;
    setup(1); if (wav_parse(read_at,0,length,&w) || w.data_offset != CANONICAL_DATA_OFFSET + LIST_CHUNK_SIZE) return 2;
    for (i=0;i<5;i++) { setup(0); file[fields[i]]=0; if (wav_parse(read_at,0,length,&w) != -2) return 3+i; }
    setup(0); if (wav_parse(read_at,0,length-1,&w) != -1) return 8;
    setup(0); put32(CANONICAL_DATA + 4,12); if (wav_parse(read_at,0,length,&w) != -1) return 9;
    setup(0); put32(CANONICAL_DATA + 4,6); if (wav_parse(read_at,0,length,&w) != -1) return 10;
    setup(1); put32(16,0xffffffffu); if (wav_parse(read_at,0,length,&w) != -1) return 11;
    setup(1); put32(4,length-9); if (wav_parse(read_at,0,length,&w) != -1) return 12;
    return 0;
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
