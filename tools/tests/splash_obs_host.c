/* Real OBS loader, columns, player and boot owner against a free-running
 * scan oscillator. No edge/row state is taken from the implementation. */
#include "types.h"
#include "pc98.h"
#include "gfx_osri.h"
#include "splash_raster_image.h"
#include "splash_raster_play.h"
#define IO_H

static void finish(u32 line)
{
    if (line) {
        char msg[] = "OBS failed line 00000000\n";
        for (int i = 0; i < 8; i++)
            msg[16 + i] = "0123456789abcdef"[(line >> (28 - 4 * i)) & 15];
        __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(msg), "d"(sizeof(msg)-1) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line ? line % 250 + 1 : 0) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(c) do { if (!(c)) finish(__LINE__); } while (0)
#define HOST_H 100u
#define HOST_WRITES (64u + 4u * 399u * 16u)
static struct SplashRaster state;
static struct OsriImage image;
static struct { u32 before; struct PlV2Plan value; u32 after; } guarded_plan;
static u32 stamp, reads, writes, out_cost, vs_lines, fall_at, cli_at, restores;
static int irq_on, freeze, no_h, no_v_after_f, stuck_v, force_v_at, delay_cli;
static u32 preview_at, preview_pending, uart_inits, stop_v_at;
static struct { u32 port, value, time, open; } output[HOST_WRITES];

static unsigned int inp(unsigned int port)
{
    REQUIRE(port == GDC_GFX_STAT);
    REQUIRE(++reads < 4000000u); /* Includes a full one-second preview. */
    u32 phase = stamp++ % (state.total_lines * HOST_H);
    u32 status = 0;
    if (!freeze && (phase < vs_lines * HOST_H ||
        (stuck_v && stamp >= state.total_lines * HOST_H) ||
        (force_v_at && stamp >= (u32)force_v_at))) status |= GDC_STAT_VSYNC;
    if ((no_v_after_f && stamp >= fall_at) || (stop_v_at && stamp >= stop_v_at))
        status &= ~GDC_STAT_VSYNC;
    if (!no_h && phase % HOST_H >= HOST_H - 20u) status |= GDC_STAT_HBLANK;
    return status;
}
static void outp(unsigned int port, unsigned int value)
{
    if (preview_pending) {
        unsigned long long duration = (unsigned long long)1000000u * state.line_den;
        unsigned long long period = (unsigned long long)state.total_lines * state.line_num_us;
        u32 rounds = (u32)((duration + period - 1u) / period);
        REQUIRE(stamp - preview_at >= rounds * state.total_lines * HOST_H);
        preview_pending = 0;
    }
    REQUIRE(writes < HOST_WRITES);
    output[writes].port = port; output[writes].value = value;
    output[writes].time = stamp; output[writes++].open = irq_on;
    stamp += out_cost;
}
static int _irq_enabled(void) { return irq_on; }
static unsigned int irq_save(void)
{
    u32 flags = irq_on;
    irq_on = 0; cli_at = stamp;
    if (delay_cli) stamp = fall_at + HOST_H;
    return flags;
}
static void irq_restore(unsigned int flags) { restores++; irq_on = flags; }

#define now_us obs_host_now_us
#define finish obs_host_image_finish
#include "../../drivers/video/pc98/splash_raster_image.c"
#include "../../drivers/video/pc98/splash_raster_plan.c"
#include "../../drivers/video/pc98/splash_raster_play.c"
#undef finish
#undef now_us
#include "../../gfx/gfx_osri.c"
#include "../../platform/pc98/splash_obs.c"
#include "../../init/boot_splash.c"
#define plan guarded_plan.value

struct SplashRaster *pc98_splash_raster_state(void) { return &state; }
int sys_time_now(u32 *lo, u32 *hi)
{ (void)lo; (void)hi; REQUIRE(0); return -1; }

static void setup(int mode, int vs)
{
    state = (struct SplashRaster){ .mode = mode, .checked = 1, .dynamic_ok = 1,
        .line_num_us = 100000, .line_den = mode ? 3147 : 2483,
        .total_lines = mode ? 449 : 440, .vbp_lines = mode ? 34 : 25,
        .vs_lines = mode ? 2 : 8 };
    state.active0_lines = vs ? state.vbp_lines : 0;
    vs_lines = vs ? state.vs_lines : state.total_lines - 400;
    fall_at = (state.total_lines + vs_lines) * HOST_H;
    stamp = reads = writes = restores = cli_at = preview_pending = stop_v_at = 0; out_cost = 0;
    irq_on = 1; freeze = no_h = no_v_after_f = stuck_v = force_v_at = delay_cli = 0;
}
static u16 rgb(u32 index) { return (u16)(index * 0x111u); }
static void make_image(u32 n)
{
    image = (struct OsriImage){ .count = 399 * n, .band = 1, .version = 2 };
    for (u32 i = 0; i < 16; i++) image.pal[i] = rgb(i);
    for (u32 y = 1; y < 400; y++) for (u32 j = 0; j < n; j++) {
        u32 index = j;
        image.event[(y - 1) * n + j] = (struct OsriEvent){ .line = y,
            .index = index, .rgb444 = rgb(index) ^ (y & 1 ? 0x10 : 0) };
    }
}
static void test_prepare(void)
{
    guarded_plan.before=0x12345678u; guarded_plan.after=0x87654321u;
    for (u32 n = 1; n <= 16; n++) {
        make_image(n);
        REQUIRE(!pl_v2_prepare(&image, &plan));
        REQUIRE(!plan.row[0].count);
        REQUIRE(plan.max_outs == 2*n-1 && plan.total_outs == 399*(2*n-1));
        for (u32 y = 1; y < 400; y++) {
            struct PlV2Row *r = &plan.row[y];
            REQUIRE(r->count == 2*n-1 && r->first == 0);
            for (u32 j = 0, pos = 0; j < n; j++) {
                if (j) { REQUIRE(r->port[pos] == PAL_IDX_PORT && r->value[pos] == j); pos++; }
                REQUIRE(r->port[pos] == PAL_G_PORT && r->value[pos] == (j ^ (y & 1))); pos++;
            }
        }
    }
    REQUIRE(pl_v2_prepare(0, &plan)); REQUIRE(pl_v2_prepare(&image, 0));
    image.count = OSRI_IMAGE_EVENTS+1; REQUIRE(pl_v2_prepare(&image, &plan));
    make_image(1); image.version = 1; image.count = 200; REQUIRE(!pl_v2_prepare(&image, &plan));
    image.count = 201; REQUIRE(pl_v2_prepare(&image, &plan));
    image.version = 2; image.band = 2; REQUIRE(pl_v2_prepare(&image, &plan));
    make_image(2); image.event[2].line = 0; REQUIRE(pl_v2_prepare(&image, &plan));
    make_image(2); image.event[1].index = image.event[0].index;
    REQUIRE(pl_v2_prepare(&image, &plan));
    /* File order is pi, with all seven component masks and empty rows.
     * Old colors carry across rows, including a same-color event. */
    image = (struct OsriImage){ .version=2, .band=1, .count=9 };
    for (u32 j=0; j<7; j++) image.event[j] = (struct OsriEvent){
        .line=2, .index=7-j, .rgb444=((j+1) & 1 ? 0x100 : 0) |
            ((j+1) & 2 ? 0x020 : 0) | ((j+1) & 4 ? 0x003 : 0) };
    image.event[7]=(struct OsriEvent){ .line=4, .index=7, .rgb444=0x100 };
    image.event[8]=(struct OsriEvent){ .line=5, .index=7, .rgb444=0x123 };
    REQUIRE(!pl_v2_prepare(&image, &plan));
    REQUIRE(!plan.row[1].count && !plan.row[3].count && !plan.row[4].count);
    REQUIRE(plan.row[2].first == 7 && plan.row[2].count == 18);
    u32 pos=0;
    for (u32 j=0; j<7; j++) {
        if (j) { REQUIRE(plan.row[2].port[pos] == PAL_IDX_PORT && plan.row[2].value[pos] == 7-j); pos++; }
        if ((j+1)&2) { REQUIRE(plan.row[2].port[pos] == PAL_G_PORT && plan.row[2].value[pos] == 2); pos++; }
        if ((j+1)&1) { REQUIRE(plan.row[2].port[pos] == PAL_R_PORT && plan.row[2].value[pos] == 1); pos++; }
        if ((j+1)&4) { REQUIRE(plan.row[2].port[pos] == PAL_B_PORT && plan.row[2].value[pos] == 3); pos++; }
    }
    REQUIRE(pos == 18 && plan.row[5].count == 2 && plan.row[5].first == 7);
    REQUIRE(plan.row[5].port[0] == PAL_G_PORT && plan.row[5].port[1] == PAL_B_PORT);
    REQUIRE(plan.max_outs == 18 && plan.total_outs == 20);
    /* B31 accepts sixteen single-component changes. The next component
     * must be rejected before either row array can overflow. */
    make_image(16); image.count=16;
    REQUIRE(!pl_v2_prepare(&image, &plan) && plan.row[1].count == 31);
    image.event[0].rgb444 ^= 0x100;
    REQUIRE(pl_v2_prepare(&image, &plan)); /* 32 */
    for (u32 j=0; j<16; j++) image.event[j].line=399;
    plan.max_outs=0; plan.total_outs=0;
    REQUIRE(pl_v2_prepare(&image, &plan) && !plan.max_outs && !plan.total_outs);
    image = (struct OsriImage){ .version=2, .band=1, .count=12 };
    for (u32 j=0; j<13; j++) image.event[j]=(struct OsriEvent){ .line=1, .index=j, .rgb444=0x123 };
    REQUIRE(pl_v2_prepare(&image, &plan)); /* 47 */
    image.count=13; image.event[12].rgb444=0x020; image.event[0].rgb444=0x120;
    REQUIRE(pl_v2_prepare(&image, &plan)); /* 48 */
    /* Same-color v2 records, including sixteen in a row, cost zero. */
    make_image(16);
    for (u32 j=0; j<image.count; j++) image.event[j].rgb444=rgb(image.event[j].index);
    REQUIRE(!pl_v2_prepare(&image, &plan) && !plan.total_outs);
    for (u32 y=0; y<400; y++) REQUIRE(!plan.row[y].first && !plan.row[y].count);
    /* Generalize bands without skipping the empty rows between them. */
    for (u32 band=2; band<=4; band*=2) {
        image=(struct OsriImage){ .version=2, .band=band, .count=1 };
        image.event[0]=(struct OsriEvent){ .line=band, .index=3, .rgb444=0x100 };
        REQUIRE(!pl_v2_prepare(&image, &plan) && plan.total_outs==1);
        for (u32 y=0; y<400; y++) REQUIRE(plan.row[y].count == (y==band));
        image.event[0].line++;
        REQUIRE(pl_v2_prepare(&image, &plan));
    }
    image.band=1;
    /* v1 keeps its ordering/no-op contract, with no v2 per-row cap. */
    image=(struct OsriImage){ .version=1, .band=1, .count=4 };
    for (u32 j=0; j<4; j++) image.event[j]=(struct OsriEvent){ .line=1, .index=j, .rgb444=1 };
    REQUIRE(!pl_v2_prepare(&image, &plan) && plan.row[1].count==7);
    image.event[0].index=4; REQUIRE(pl_v2_prepare(&image, &plan));
    image.event[0].index=0; image.event[0].rgb444=0; REQUIRE(pl_v2_prepare(&image, &plan));
    image.version=2;
    image.band=4;
    image.count=0; REQUIRE(!pl_v2_prepare(&image, &plan) && !plan.total_outs);
    image.band=1;
    image.count=1; image.event[0].line=400; REQUIRE(pl_v2_prepare(&image, &plan));
    image.event[0].line=0; REQUIRE(pl_v2_prepare(&image, &plan));
    image.event[0].line=1; image.event[0].index=16; REQUIRE(pl_v2_prepare(&image, &plan));
    image.event[0].index=0; image.event[0].reserved1=1; REQUIRE(pl_v2_prepare(&image, &plan));
    image.event[0].reserved1=0; image.event[0].rgb444=0x1000; REQUIRE(pl_v2_prepare(&image, &plan));
    REQUIRE(guarded_plan.before==0x12345678u && guarded_plan.after==0x87654321u);
}
static void test_scan(void)
{
    for (u32 count=0; count<=31; count++) {
        setup(1, 0); out_cost=1;
        image=(struct OsriImage){ .version=2, .band=1, .count=(count+1)/2 };
        for (u32 j=0; j<image.count; j++) image.event[j]=(struct OsriEvent){
            .line=1, .index=j, .rgb444=(count && !(count&1) && !j) ? 0x120 : 0x010 };
        REQUIRE(!pl_v2_prepare(&image, &plan) && plan.row[1].count==count);
        REQUIRE(pl_v2_frame(&state, &image, &plan)==PL_FRAME_OK);
        REQUIRE(writes==64+count+!!count && irq_on && restores==1);
        for (u32 j=0; j<count; j++) {
            REQUIRE(output[65+j].port==plan.row[1].port[j]);
            REQUIRE(output[65+j].value==plan.row[1].value[j]);
        }
    }
    for (int mode = 0; mode < 2; mode++) for (int vs = 0; vs < 2; vs++)
    for (u32 n = 1; n <= 16; n++) {
        setup(mode, vs); out_cost = 1; make_image(n);
        REQUIRE(!pl_v2_prepare(&image, &plan));
        REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_OK);
        REQUIRE(irq_on && restores == 1 && state.frame == 1 && !state.dropped);
        REQUIRE(writes == 64 + 399 * 2 * n);
        REQUIRE(cli_at >= state.total_lines * HOST_H && cli_at < fall_at);
        for (u32 i = 0; i < writes; i++) REQUIRE(!output[i].open);
        for (u32 i = 0; i < 16; i++) {
            REQUIRE(output[4*i].port == PAL_IDX_PORT && output[4*i].value == i);
            REQUIRE(output[4*i+1].port == PAL_G_PORT && output[4*i+1].value == i);
            REQUIRE(output[4*i+2].port == PAL_R_PORT && output[4*i+2].value == i);
            REQUIRE(output[4*i+3].port == PAL_B_PORT && output[4*i+3].value == i);
            REQUIRE(output[4*i+3].time < fall_at);
        }
        for (u32 y = 1; y < 400; y++) {
            u32 pos = 64 + (y - 1) * 2 * n;
            REQUIRE(output[pos].port == PAL_IDX_PORT && output[pos].value == plan.row[y].first);
            REQUIRE(output[pos].time % HOST_H < 80); /* First index in preceding low. */
            REQUIRE(output[pos+1].time == fall_at + (state.active0_lines+y-1)*HOST_H + 81);
            for (u32 j = 0; j < 2*n-1; j++) {
                REQUIRE(output[pos+j+1].port == plan.row[y].port[j]);
                REQUIRE(output[pos+j+1].value == plan.row[y].value[j]);
            }
        }
        REQUIRE(stamp >= fall_at + (state.active0_lines+399)*HOST_H + 80);
        /* A continuation consumes the very next VBLANK, never two. */
        u32 previous = stamp; writes = reads = 0;
        REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_OK);
        REQUIRE(stamp - previous == state.total_lines * HOST_H);
    }
    /* Empty rows must still consume their H edge; pi starts with index 7. */
    setup(1, 0); out_cost=1;
    image=(struct OsriImage){ .version=2, .band=1, .count=3 };
    image.event[0]=(struct OsriEvent){ .line=2, .index=7, .rgb444=0x020 };
    image.event[1]=(struct OsriEvent){ .line=2, .index=1, .rgb444=0x100 };
    image.event[2]=(struct OsriEvent){ .line=5, .index=7, .rgb444=0x023 };
    REQUIRE(!pl_v2_prepare(&image, &plan));
    REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_OK && writes == 70);
    REQUIRE(output[64].port == PAL_IDX_PORT && output[64].value == 7);
    REQUIRE(output[64].time % HOST_H < 80);
    REQUIRE(output[65].port == PAL_G_PORT && output[65].value == 2);
    REQUIRE(output[65].time == fall_at+HOST_H+81);
    REQUIRE(output[66].port == PAL_IDX_PORT && output[66].value == 1);
    REQUIRE(output[67].port == PAL_R_PORT && output[67].value == 1);
    REQUIRE(output[68].port == PAL_IDX_PORT && output[68].value == 7);
    REQUIRE(output[68].time % HOST_H < 80);
    REQUIRE(output[69].port == PAL_B_PORT && output[69].value == 3);
    REQUIRE(output[69].time == fall_at+4*HOST_H+81);
    setup(1, 0); make_image(16); REQUIRE(!pl_v2_prepare(&image, &plan));
    out_cost = 8;
    REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_DROP);
    REQUIRE(irq_on && restores == 1 && state.dropped == 1 && !state.frame);
    REQUIRE(stamp < 3 * state.total_lines * HOST_H);
    /* B1: VSYNC must drop in both the high-wait and low-wait paths. */
    for (u32 extra = 0; extra <= 80; extra += 80) {
        setup(1, 0); make_image(1); REQUIRE(!pl_v2_prepare(&image, &plan));
        force_v_at = fall_at + 10 * HOST_H + extra;
        REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_DROP);
        REQUIRE(irq_on && restores == 1 && stamp <= (u32)force_v_at + 1);
    }
    setup(1, 0); freeze = 1;
    REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_NO_SYNC);
    REQUIRE(irq_on && !restores && reads <= 1000001u);
    setup(1, 0); no_h = no_v_after_f = 1;
    REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_NO_SYNC);
    REQUIRE(irq_on && restores == 1);
    setup(1, 0); stuck_v = 1;
    REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_NO_SYNC);
    REQUIRE(irq_on && restores == 1);
    setup(1, 0); delay_cli = 1;
    REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_DROP);
    REQUIRE(irq_on && restores == 1 && !writes);
    setup(1, 0); irq_on = 0;
    REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_NO_SYNC);
    REQUIRE(!irq_on && !restores && !reads);
}

#define HOST_FILE_BYTES (OSRI_EVENTS_OFF + 8 * 6385u)
static u8 file[HOST_FILE_BYTES], pixels[OSRI_PIXELS_BYTES];
static u32 file_bytes, file_pos, file_n, active_n, scenario;
static u32 allocs, frees, opens, closes, commits, blacks, begins, clears, stops, defaults, texts;
static int image_busy, plan_busy;
static char label[4];
static char log_text[8192];
static u32 log_pos;
static void rehash(void)
{
    le_write32(file+48, 0);
    le_write32(file+48, crc32_core_final(crc32_core_update(CRC32_INIT, file, file_bytes)));
}
static void make_file(u32 n)
{
    file_n = n; make_image(n);
    file_bytes = OSRI_EVENTS_OFF + OSRI_EVENT_BYTES * image.count;
    for (u32 i = 0; i < OSRI_HEADER_BYTES; i++) file[i] = 0;
    file[0]='O'; file[1]='S'; file[2]='R'; file[3]='I';
    le_write16(file+4, 2); le_write16(file+6, 64);
    le_write16(file+8, 640); le_write16(file+10, 400); file[12]=4; file[13]=1;
    le_write32(file+16, file_bytes); le_write32(file+20, 64); le_write32(file+24, 96);
    le_write32(file+28, 128000); le_write32(file+32, 128096);
    le_write32(file+36, image.count); le_write16(file+40, 8); le_write16(file+44, 1);
    for (u32 i = 0; i < 16; i++) le_write16(file+64+2*i, rgb(i));
    for (u32 i = 0; i < 128000; i++) file[96+i] = i / 32000 + 1;
    for (u32 i = 0; i < image.count; i++) {
        u8 *e = file + OSRI_EVENTS_OFF + 8*i;
        le_write16(e, image.event[i].line); e[2] = image.event[i].index; e[3] = 0;
        le_write16(e+4, image.event[i].rgb444); le_write16(e+6, 0);
    }
    rehash();
}
int vfs_stat(const char *path, OS32_Stat *st)
{
    (void)st; REQUIRE(irq_on);
    active_n = (path[SPLASH_OBS_DIGITS]-'0')*10 + path[SPLASH_OBS_DIGITS+1]-'0';
    REQUIRE(active_n >= 1 && active_n <= 16);
    REQUIRE(label[0] == 'N' && label[1] == '=' &&
            label[2] == (char)('0'+active_n/10) && label[3] == (char)('0'+active_n%10));
    return (scenario == 1 && active_n == 1) || (scenario >= 3 && active_n != 16) ? -1 : 0;
}
int vfs_open(const char *path, int flags)
{
    (void)path; REQUIRE(flags == O_RDONLY); opens++; file_pos = 0;
    if (scenario != 2) {
        make_file(active_n);
        if (scenario == 1 && active_n == 3) {
            for (u32 j=0; j<13; j++) {
                u8 *e=file+OSRI_EVENTS_OFF+8*j;
                le_write16(e, 1); e[2]=j; le_write16(e+4, rgb(j)^0x111);
            }
            rehash();
        }
        if (scenario == 1 && active_n == 2) file[100] ^= 1;
    }
    return 3;
}
void vfs_close(int fd) { REQUIRE(fd == 3); closes++; }
int vfs_read_fd(int fd, void *dst, u32 n)
{
    REQUIRE(fd == 3);
    if (n > 23) n = 23;
    if (n > file_bytes-file_pos) n = file_bytes-file_pos;
    for (u32 i = 0; i < n; i++) ((u8 *)dst)[i] = file[file_pos++];
    return n;
}
int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
void *kmalloc(u32 size)
{
    REQUIRE(irq_on); allocs++;
    if (size == sizeof(image)) {
        REQUIRE(!image_busy);
        if (scenario == 1 && active_n == 6) return 0;
        image_busy=1; return &image;
    }
    REQUIRE(size == sizeof(plan) && !plan_busy);
    if (scenario == 1 && active_n == 4) return 0;
    plan_busy=1; return &plan;
}
void kfree(void *p)
{
    REQUIRE(irq_on); frees++;
    if (p == &image) { REQUIRE(image_busy); image_busy=0; }
    else { REQUIRE(p == &plan && plan_busy); plan_busy=0; }
}
int gfx_kernel_framebuffer(struct gfx_kernel_fb *fb)
{
    *fb = (struct gfx_kernel_fb){ .width=640, .height=400, .pitch=80, .format=GFX_BB_PLANAR4 };
    for (u32 i = 0; i < 4; i++) fb->planes[i] = pixels + 32000*i;
    return 0;
}
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ REQUIRE(!x && !y && w == 640 && h == 400); }
void gfx_present_nosync(void)
{
    REQUIRE(irq_on && image_busy && plan_busy && blacks >= (commits+1)*16);
    REQUIRE(pixels[0] == 1 && pixels[32000] == 2 && pixels[64000] == 3 && pixels[96000] == 4);
    commits++; stamp=reads=writes=preview_pending=0;
    freeze = scenario == 1 && active_n == 5;
    out_cost = scenario >= 3 ? 8 : 0;
    /* One aligned preview period, then a dropped frame and no next V. */
    stop_v_at = scenario == 4 ? 3 * state.total_lines * HOST_H + HOST_H : 0;
}
void palette_set(int index, u8 r, u8 g, u8 b)
{ REQUIRE(irq_on && index >= 0 && index < 16 && !r && !g && !b); blacks++; }
void serial_puts_polled(const char *s)
{
    REQUIRE(irq_on && uart_inits);
    while (*s) { REQUIRE(log_pos+1 < sizeof(log_text)); log_text[log_pos++] = *s++; }
    log_text[log_pos]=0;
}
void tvram_putchar_at(int x, int y, char ch, u8 attr)
{
    REQUIRE(irq_on && !y && attr == TATTR_WHITE);
    REQUIRE((attr & TATTR_VISIBLE) && !(attr & (TATTR_BLINK | TATTR_REVERSE)));
    if (x == 0) REQUIRE(ch == 'N'); else if (x == 1) REQUIRE(ch == '=');
    else if (x == 2) REQUIRE(ch >= '0' && ch <= '1');
    else REQUIRE(x == 3 && ch >= '0' && ch <= '9');
    label[x] = ch;
}
int platform_splash_begin(void) { begins++; return 0; }
void platform_splash_clear_pages(void) { clears++; }
void gfx_shutdown(void) { stops++; }
void serial_init(unsigned long baud)
{ REQUIRE(irq_on && baud == SYS_SERIAL_BAUD); uart_inits++; }
void palette_init(void)
{
    REQUIRE(irq_on);
    defaults++;
    if (image_busy) { preview_at = stamp; preview_pending = 1; }
}
void tvram_clear(void) { texts++; }
static u32 find_from(const char *text, u32 start)
{
    for (u32 i = start; i < log_pos; i++) {
        u32 j = 0; while (text[j] && log_text[i+j] == text[j]) j++;
        if (!text[j]) return i;
    }
    return U32_MAX;
}
static int contains(const char *text) { return find_from(text, 0) != U32_MAX; }
static void test_preview(void)
{
    for (int mode = 0; mode < 2; mode++) for (int vs = 0; vs < 2; vs++) {
        setup(mode, vs);
        u32 period = state.total_lines * HOST_H;
        unsigned long long duration = (unsigned long long)1000000u * state.line_den;
        unsigned long long physical_period = (unsigned long long)state.total_lines * state.line_num_us;
        u32 rounds = (u32)((duration + physical_period - 1) / physical_period);
        stamp = period / 2; /* A partial first frame does not count. */
        u32 before = defaults;
        REQUIRE(!pl_preview(&state));
        REQUIRE(defaults == before+1 && !writes && irq_on && !restores);
        REQUIRE(stamp >= (rounds+1)*period && stamp <= (rounds+1)*period+2);
        /* The next call really switches to the initial and per-row palette. */
        reads = 0; make_image(2);
        REQUIRE(!pl_v2_prepare(&image, &plan));
        REQUIRE(pl_v2_frame(&state, &image, &plan) == PL_FRAME_OK);
        REQUIRE(writes == 64+399*4 && irq_on);
    }
    setup(1, 0); freeze = 1;
    REQUIRE(pl_preview(&state) && reads < 1100000u && irq_on);
    setup(1, 0); irq_on = 0;
    REQUIRE(pl_preview(&state) && !reads && !writes && !irq_on);
}
static void test_owner(void)
{
    for (scenario = 0; scenario <= 1; scenario++) {
        setup(1, 0);
        REQUIRE(obs_rounds(&state) == 351);
        state.line_den = 2483; state.total_lines = 440;
        REQUIRE(obs_rounds(&state) == 283);
        state.line_num_us = 0; REQUIRE(!obs_rounds(&state));
        setup(1, 0);
        /* Synthetic 5s frame: one round, to exercise all ownership paths. */
        state.line_num_us = 5000000; state.line_den = state.total_lines;
        allocs=frees=opens=closes=commits=blacks=log_pos=0;
        begins=clears=stops=defaults=texts=uart_inits=0; image_busy=plan_busy=0;
        boot_splash();
        REQUIRE(begins == 1 && clears == 1 && stops == 1 && defaults == commits+1 && texts == 1);
        REQUIRE(uart_inits == 1);
        REQUIRE(!image_busy && !plan_busy && opens == closes);
        REQUIRE(frees == allocs - 2*scenario && commits == (scenario ? 11u : 16u));
        REQUIRE(blacks == 32 * commits);
        u32 log_cursor = 0;
        for (u32 n = 1; n <= 16; n++) {
            char start[] = "SPLO n=00 start crc=";
            char end[] = "SPLO n=00 end status=";
            if (n < 10) {
                start[7]='0'+n; end[7]='0'+n;
                for (u32 i=8; start[i]; i++) start[i]=start[i+1];
                for (u32 i=8; end[i]; i++) end[i]=end[i+1];
            } else { start[7]='1'; start[8]='0'+n-10; end[7]='1'; end[8]='0'+n-10; }
            log_cursor = find_from(start, log_cursor); REQUIRE(log_cursor != U32_MAX);
            log_cursor = find_from(end, log_cursor); REQUIRE(log_cursor != U32_MAX);
        }
        if (scenario) {
            REQUIRE(contains("n=1 end status=missing frames=0 drop=0"));
            REQUIRE(contains("n=2 end status=invalid frames=0 drop=0"));
            REQUIRE(contains("n=3 end status=prepare frames=0 drop=0"));
            REQUIRE(contains("n=4 end status=memory frames=0 drop=0"));
            REQUIRE(contains("n=5 end status=no_sync frames=0 drop=0"));
            REQUIRE(contains("n=6 end status=memory frames=0 drop=0"));
        }
        REQUIRE(contains("n=16 out_max=31 out_total=12369"));
        REQUIRE(contains("n=16 end status=ok frames=1 drop=0"));
    }
    scenario=3; setup(1, 0);
    state.line_num_us=5000000; state.line_den=state.total_lines;
    allocs=frees=opens=closes=commits=blacks=log_pos=0;
    image_busy=plan_busy=0;
    boot_splash();
    REQUIRE(allocs == 2 && frees == 2 && commits == 1);
    REQUIRE(contains("n=16 end status=drop frames=0 drop=1"));
    scenario=4; setup(1, 0);
    /* Two rounds: drop must be followed by a failed new VBLANK wait. */
    state.line_num_us=2500000; state.line_den=state.total_lines;
    allocs=frees=opens=closes=commits=blacks=log_pos=0;
    image_busy=plan_busy=0;
    boot_splash();
    REQUIRE(allocs == 2 && frees == 2 && commits == 1 && irq_on);
    REQUIRE(contains("n=16 end status=no_sync frames=0 drop=1"));
}
static void test_loader(void)
{
    struct gfx_kernel_fb fb;
    gfx_kernel_framebuffer(&fb); scenario=2;
    make_file(16); REQUIRE(file_bytes == 179168 && !osri_load("fixture", &fb, &image));
    REQUIRE(image.count == 6384 && file_pos == file_bytes);
    le_write32(file+36, 6385); file_bytes += 8; le_write32(file+16, file_bytes); rehash();
    REQUIRE(osri_load("fixture", &fb, &image) && file_pos == 64);
    make_file(1); le_write16(file+4, 1); le_write16(file+OSRI_EVENTS_OFF+4, 0); rehash(); REQUIRE(osri_load("fixture", &fb, &image));
    make_file(2); file[OSRI_EVENTS_OFF+10] = file[OSRI_EVENTS_OFF+2]; rehash();
    REQUIRE(osri_load("fixture", &fb, &image));
    make_file(2); file[OSRI_EVENTS_OFF+2]=1; file[OSRI_EVENTS_OFF+10]=0; rehash();
    REQUIRE(!osri_load("fixture", &fb, &image));
    make_file(1); file[100] ^= 1; REQUIRE(osri_load("fixture", &fb, &image));
}
void _start(void)
{
    test_prepare(); test_scan(); test_loader(); test_preview(); test_owner(); finish(0);
}
