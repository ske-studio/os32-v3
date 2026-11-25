/* Run the distributed OS32X app at its SDK entry with the distributed
 * library. Linux/i386 supplies only page mapping, console and process exit. */
#include "os32api.h"

extern const unsigned char sound_blob[], app_blob[];
static int saw_pass;
#ifdef TEST_SOFT
static SND2_Pos pos;
static unsigned tick, written;
static unsigned char heap[512 * 1024] __attribute__((aligned(16)));
static unsigned top;
static void *allocate(u32 n)
{
    if (n > sizeof heap - top) return 0;
    void *p = heap + top;
    top += (n + 15) & ~15UL;
    return p;
}
static void release(void *p) { (void)p; }
static int clock_now(u32 *lo, u32 *hi) { *lo = tick; *hi = 0; return 0; }
static int devices(void) { return 1; }
static int info(u32 id, SND2_DevInfo *o)
{
    *o = (SND2_DevInfo){.kind=SND2_KIND_PCM_OUT,.flags=SND2_DEV_SOFT_CPU};
    return id ? -1 : 0;
}
static int caps(u32 id, SND2_Caps *o, u32 size)
{
    (void)size;
    o->fmt_count = 1;
    o->fmt[0] = (SND2_Fmt){44100,16,2,SND2_ENC_S16LE,0};
    return id ? -1 : 0;
}
static int open_pcm(const SND2_StreamDesc *d, u32 *h)
{
    pos.cap = d->period_frames * (1+d->lead_periods);
    *h = 1;
    return 0;
}
static int position(u32 h, SND2_Pos *out)
{
    (void)h;
    if (pos.state == SND2_ST_RUN) {
        tick += 5000;
        pos.played = tick / 5000 * 441 / 2;
    }
    *out = pos;
    return 0;
}
static int write_pcm(u32 h, const void *p, u32 bytes)
{
    (void)h;
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(4), "b"(1), "c"(p), "d"(bytes) : "memory");
    if (rc > 0) written += rc;
    pos.written = written / 4;
    return rc;
}
static int start(u32 h) { (void)h; pos.state = SND2_ST_RUN; return 0; }
static int close_pcm(u32 h, u32 how) { (void)h; return how == SND2_CLOSE_DRAIN ? 0 : -1; }
#endif

static void finish(int rc)
{
    if (!rc && !saw_pass) rc = 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}

static void print(unsigned char attr, const char *message, ...)
{
#ifdef TEST_SOFT
    const char expected[] = "sndl soft OPNA left=fnum0269 block4 right=12345 period=256\n";
#else
    const char expected[] = "sndl_test PASS\n";
#endif
    unsigned n = 0;
    (void)attr;
    while (message[n] && n < sizeof(expected) && message[n] == expected[n]) n++;
    if (n == sizeof(expected) - 1 && !message[n]) saw_pass = 1;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(message), "d"(n) : "memory");
}

static void *map_image(const unsigned char *blob)
{
    const OS32Header *h = (const OS32Header *)blob;
    unsigned length = h->text_size + h->bss_size;
    unsigned args[6] = {h->load_addr, length, 3, 0x32, ~0u, 0};
    unsigned addr;
    __asm__ volatile("int $0x80" : "=a"(addr) : "0"(90), "b"(args) : "memory");
    if (addr != h->load_addr) finish(3);
    unsigned char *dst = (unsigned char *)addr;
    for (unsigned i = 0; i < h->text_size; i++) dst[i] = blob[h->header_size + i];
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(addr),
                     "c"(h->code_size), "d"(5) : "memory");
    if (rc) finish(4);
    return dst + h->entry_offset;
}

void _start(void)
{
    static KernelAPI api;
    api.version = KAPI_VERSION;
    api.kprintf = print;
    api.sys_exit = finish;
    map_image(sound_blob);
    typedef void (*entry_fn)(int, char **, KernelAPI *, void *);
    entry_fn entry = (entry_fn)map_image(app_blob);
    char name[] = "sndl_test";
    char *argv[] = {name, 0};
#ifdef TEST_SOFT
    api.snd2_dev_count = devices;
    api.snd2_dev_info = info;
    api.snd2_caps = caps;
    api.snd2_stream_open = open_pcm;
    api.snd2_stream_pos = position;
    api.snd2_stream_write = write_pcm;
    api.snd2_stream_start = start;
    api.snd2_stream_close = close_pcm;
    api.sys_time_now = clock_now;
    api.mem_alloc = allocate;
    api.mem_free = release;
    char mode[] = "soft";
    char *soft_argv[] = {argv[0],mode,0};
    entry(2, soft_argv, &api, 0);
#else
    entry(1, argv, &api, 0);
#endif
    finish(5);
}
