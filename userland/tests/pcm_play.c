#include "os32api.h"
#include "pcm_wav.h"

#define PCM_RATE 44100u
#define PCM_BUFFER 4096u
#define PCM_MIN_API 61
#define SEEK_BEGIN 0
#define SEEK_END_FILE 2

struct wav_file { KernelAPI *api; int fd; };
static int read_at(void *ctx, unsigned offset, unsigned char *buf, unsigned n);
static void status(KernelAPI *api, unsigned sec);
static unsigned char buffer[PCM_BUFFER];

int main(int argc, char **argv, KernelAPI *api)
{
    struct wav_file f;
    struct wav_info w = {0};
    unsigned remaining, done = 0, offset, n, sent, sec = 0;
    u32 clock_start = 0, now, high;
    int have_clock;
    int size, rc, failed = 0, close_rc;
    if (argc != 2) { api->kprintf(0x41, "usage: pcm_play <path.wav>\n"); return 1; }
    if (api->version < PCM_MIN_API) { api->kprintf(0x41, "KAPI v%d < %d\n", api->version, PCM_MIN_API); return 1; }
    f.api = api; f.fd = api->sys_open(argv[1], KAPI_O_RDONLY);
    if (f.fd < 0) { api->kprintf(0x41, "open -> %d\n", f.fd); return 1; }
    size = api->sys_lseek(f.fd, 0, SEEK_END_FILE);
    rc = size < 0 ? -1 : wav_parse(read_at, &f, (unsigned)size, &w);
    if (rc) {
        if (rc == -2) {
            api->kprintf(0x41, "unsupported: format=%u channels=%u rate=%u bits=%u block_align=%u\n", w.format, w.channels, w.rate, w.bits, w.align);
            api->kprintf(0x41, "ffmpeg -i in -ar 44100 -ac 2 -sample_fmt s16 -map_metadata -1 out.wav\n");
        } else api->kprintf(0x41, "malformed or truncated WAV\n");
        api->sys_close(f.fd); return 1;
    }
    rc = api->pcm_open(PCM_RATE);
    if (rc) { api->kprintf(0x41, "pcm_open -> %d\n", rc); api->sys_close(f.fd); return 1; }
    have_clock = api->sys_time_now(&clock_start, &high) == 0;
    remaining = w.data_size; offset = w.data_offset;
    while (remaining) {
        n = remaining < PCM_BUFFER ? remaining : PCM_BUFFER;
        if (read_at(&f, offset, buffer, n)) { api->kprintf(0x41, "read failed\n"); failed = 1; break; }
        sent = 0;
        while (sent < n) {
            if (api->sys_time_now(&now, &high) == 0) {
                if (!have_clock) { clock_start = now; have_clock = 1; }
                else if ((u32)(now - clock_start) >= 1000000u) {
                    clock_start = now; status(api, ++sec);
                }
            }
            rc = api->pcm_write(buffer + sent, n - sent);
            if (rc < 0 || (unsigned)rc > n - sent || (rc & 3)) { api->kprintf(0x41, "pcm_write -> %d\n", rc); failed = 1; break; }
            if (!rc) { api->sys_yield(); continue; }
            sent += (unsigned)rc; done += (unsigned)rc / 4;
        }
        if (failed) break;
        offset += n; remaining -= n;
    }
    close_rc = api->pcm_close();
    api->kprintf(close_rc ? 0x41 : 0xE1, "frames=%u seconds=%u.%03u pcm_close=%d\n", done, done / PCM_RATE, (done % PCM_RATE) * 1000 / PCM_RATE, close_rc);
    api->sys_close(f.fd);
    return failed || close_rc != 0;
}
static int read_at(void *ctx, unsigned offset, unsigned char *buf, unsigned n)
{
    struct wav_file *f = ctx;
    unsigned got = 0;
    int rc;
    if (f->api->sys_lseek(f->fd, (int)offset, SEEK_BEGIN) != (int)offset) return -1;
    while (got < n) {
        rc = f->api->sys_read(f->fd, buf + got, n - got);
        if (rc <= 0 || (unsigned)rc > n - got) return -1;
        got += (unsigned)rc;
    }
    return 0;
}
static void status(KernelAPI *api, unsigned sec)
{
    u32 freeb, counters;
    int rc = api->pcm_status(&freeb, &counters);
    if (rc) api->kprintf(0x41, "t=%us pcm_status=%d\n", sec, rc);
    else api->kprintf(0xE1, "t=%us under=%u rep=%u resync=%u\n", sec, (counters >> 24) & 255u, (counters >> 16) & 255u, counters & 65535u);
}
