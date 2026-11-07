/* D:2919-2975 full SPR table, no hardware memory probe. */
#include "trident_device_fake.h"
static struct trident_fake f;
static struct trident_device d;
static u8 memory[TRI_FAKE_REAL_BYTES];
static struct trident_fake_event trace[16];
static int test(void) {
    static const u32 kib[16] = {1024,512,6144,1024,8192,1024,10240,2048,12288,1024,14336,1024,16384,1024,20480,4096};
    if (trident_fake_reset(&f,memory,sizeof(memory),trace,16)) return 1;
    trident_fake_bind(&f,&d);
    d.armed=d.pinned=true;
    for (u32 mono=0;mono<2;++mono) {
        d.cur_vga_base=mono?TRI_CR_MONO:TRI_CR_COLOR;
        f.regs[TRI_MISC_WRITE]=mono?0:1;
        u8 *cr=mono?f.cr_mono:f.cr_color;
        for (u32 i=0;i<256;++i) {
            cr[0x1F]=i; cr[0xC1]=0;
            if (trident_capacity_plan(&d) || d.declared_bytes!=kib[i&15]*1024u || d.spr!=i) return 2;
        }
        static const u8 ext[]={0,1,0x10,0x11};
        cr[0x1F]=14;
        for(u32 i=0;i<4;++i) {
            cr[0xC1]=ext[i];
            if(trident_capacity_plan(&d) || d.declared_bytes!=(20480u+i*4096u)*1024u) return 3;
        }
        cr[0x1F]=0;
        if(trident_capacity_plan(&d) || !d.declared_unknown) return 4;
    }
    if(f.reads[2] || f.writes[2]) return 5;
    return 0;
}
/* Design F5/F5b: independent byte arithmetic; never infer expected placement
 * from the allocator under test. Sizes here spell out the reference contract. */
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
static u32 round_to(u32 n, u32 unit)
{ return ((n + unit - 1) / unit) * unit; }
static u32 common(u32 a, u32 b)
{
    u32 x = a, y = b;
    while (y) { u32 r = x % y; x = y; y = r; }
    return a / x * b;
}
static void clear(void *p, u32 n)
{ for (u32 i = 0; i < n; ++i) ((u8 *)p)[i] = 0; }
static void setup(u32 bytes, bool noaccel)
{
    clear(&d, sizeof(d));
    trident_fake_reset(&f, memory, sizeof(memory), trace, 16);
    trident_fake_bind(&f, &d);
    d.declared_bytes = bytes;
    d.settings.no_accel = noaccel;
}
static u32 expected_max(u32 bytes, bool na)
{
    static const u32 heights[] = {480,400,350};
    static const u32 cpp[] = {1,2,4}; /* only HAL-visible formats */
    u32 maximum = 0;
    for (u32 h = 0; h < 3; ++h)
        for (u32 c = 0; c < 3; ++c) {
            u32 size = (na ? 640 : 1024) * cpp[c] * heights[h];
            if (round_to(size, 4096) <= bytes && size > maximum) maximum = size;
        }
    return round_to(maximum, 4096);
}
static int display_tests(void)
{
    REQUIRE(tri_vram_display_max_bytes(0) == 0);
    for (u32 na = 0; na < 2; ++na)
        for (u32 cap = 512*1024; cap <= 4*1024*1024; cap *= 2) {
            setup(cap, na);
            u32 maximum = expected_max(cap, na);
            REQUIRE(tri_vram_display_max_bytes(&d) == maximum);
            for (u32 i = 0; i < 15; ++i) {
                struct tri_mode m;
                if (tri_mode_prepare(&d, &tri_modes[i], &m)) continue;
                REQUIRE(tri_vram_init(&d, &m) == TRI_OK);
                REQUIRE(d.vram.first_off == maximum);
                REQUIRE(d.vram.pitch == m.display_width * (m.bpp / 8u));
                REQUIRE(d.vram.area2d_end ==
                        ((cap / d.vram.pitch < 2047 ? cap / d.vram.pitch : 2047)
                         * d.vram.pitch));
                REQUIRE(tri_vram_display_max_bytes(&d) == maximum);
            }
            /* Each independent capacity ceiling controls enumeration. */
            d.declared_bytes = 4*1024*1024;
            f.resources.mapped_bytes = cap;
            REQUIRE(tri_vram_display_max_bytes(&d) == maximum);
            f.resources.mapped_bytes = 4*1024*1024;
            d.settings.vram_bytes = cap;
            REQUIRE(tri_vram_display_max_bytes(&d) == maximum);
            d.settings.vram_bytes = 0;
            f.resources.decode_bytes = cap;
            REQUIRE(tri_vram_display_max_bytes(&d) == maximum);
        }
    /* Switching pitch cannot move offscreen storage into the fixed aliases. */
    setup(2*1024*1024, false);
    struct tri_mode fixed;
    REQUIRE(tri_mode_prepare(&d, &tri_modes[0], &fixed) == TRI_OK);
    REQUIRE(tri_vram_init(&d, &fixed) == TRI_OK);
    d.settings.no_accel = true;
    REQUIRE(tri_mode_prepare(&d, &tri_modes[0], &fixed) == TRI_OK);
    REQUIRE(tri_vram_init(&d, &fixed) == TRI_OK && d.vram.first_off == 1966080u);
    static const u32 partial[] = {224000,256000,448000,900000};
    for (u32 na = 0; na < 2; ++na)
        for (u32 c = 0; c < 4; ++c) {
            setup(partial[c], na);
            REQUIRE(tri_vram_display_max_bytes(&d) == expected_max(partial[c], na));
        }
    setup(256*1024, false);
    REQUIRE(tri_vram_display_max_bytes(&d) == 0);
    REQUIRE(tri_vram_init(&d, &tri_modes[0]) == TRI_E_INVAL);
    return 0;
}
static int allocation_tests(void)
{
    for (u32 na = 0; na < 2; ++na)
        for (u32 fmt = 0; fmt < 5; ++fmt) {
            setup(4*1024*1024, na);
            struct tri_mode m;
            REQUIRE(tri_mode_prepare(&d, &tri_modes[fmt], &m) == TRI_OK);
            d.mode.selected = m;
            REQUIRE(tri_vram_init(&d, &m) == TRI_OK);
            u32 pitch = (na || fmt == 3 ? 640 : 1024) * (m.bpp / 8u);
            u32 first = expected_max(4*1024*1024, na);
            struct tri_vram_block b[4] = {{0}};
            u32 sizes[] = {pitch,64,round_to(16384,pitch),32*(m.bpp/8u)};
            u32 units[] = {pitch,64,common(1024,pitch),16*(m.bpp/8u)};
            /* Same list mixes all consumers without overlap. */
            u32 end = first;
            for (u32 k = 0; k < 4; ++k) {
                u32 request = k == 3 ? 17*(m.bpp/8u) : 1;
                REQUIRE(tri_vram_alloc(&d, &b[k], k, request, 0) == TRI_OK);
                u32 start = k == 3 ? d.vram.area2d_end : end;
                if (start < first) start = first;
                u32 expected = round_to(start, units[k]);
                /* VIDEO tries linear tail, then the row-managed area. */
                if (k == 3 && (expected > 4*1024*1024 ||
                               sizes[k] > 4*1024*1024 - expected)) {
                    expected = round_to(first, units[k]);
                    for (u32 j = 0; j < 3; ++j) {
                        if (expected + sizes[k] <= b[j].off) break;
                        expected = round_to(b[j].off + b[j].bytes, units[k]);
                    }
                }
                REQUIRE(b[k].off == expected && b[k].bytes == sizes[k]);
                REQUIRE(b[k].allocated && b[k].kind == (enum tri_vram_kind)k);
                end = expected + sizes[k];
                REQUIRE(tri_vram_alloc(&d, &b[k], k, 1, 0) == TRI_E_BUSY);
            }
            REQUIRE(tri_vram_init(&d, &m) == TRI_E_BUSY);
            for (u32 k = 0; k < 4; ++k) {
                REQUIRE(tri_vram_free(&d, &b[k]) == TRI_OK);
                REQUIRE(!b[k].allocated && !b[k].next && !b[k].off && !b[k].bytes);
                REQUIRE(tri_vram_free(&d, &b[k]) == TRI_OK);
            }
            REQUIRE(!d.vram.blocks);
            /* Non-power-of-two caller alignment is combined, not substituted. */
            REQUIRE(tri_vram_alloc(&d, &b[0], TRI_VRAM_2D, pitch+1, 7) == TRI_OK);
            REQUIRE(b[0].off == round_to(first, common(pitch,7)));
            REQUIRE(b[0].bytes == 2*pitch);
            u32 old = b[0].off;
            REQUIRE(tri_vram_resize(&d, &b[0], TRI_VRAM_2D, 1, 7) == TRI_OK);
            REQUIRE(b[0].off == old && b[0].bytes == 2*pitch);
            REQUIRE(tri_vram_resize(&d, &b[0], TRI_VRAM_2D, 3*pitch, 7) == TRI_OK);
            REQUIRE(b[0].off == old && b[0].bytes == 3*pitch);
            REQUIRE(tri_vram_alloc(&d, &b[1], TRI_VRAM_PATTERN, 8*pitch, 0) == TRI_OK);
            REQUIRE(tri_vram_resize(&d, &b[0], TRI_VRAM_2D, 8*pitch, 7) == TRI_OK);
            REQUIRE(b[0].off != old && b[0].bytes == 8*pitch);
            struct tri_vram_block failed = {0};
            REQUIRE(tri_vram_alloc(&d, &failed, TRI_VRAM_2D, ~0u, 0) == TRI_E_CAP_TOO_SMALL);
            REQUIRE(!failed.allocated && !failed.off && !failed.bytes);
            REQUIRE(tri_vram_alloc(&d, &failed, TRI_VRAM_PATTERN, 0, 0) == TRI_E_INVAL);
            REQUIRE(tri_vram_alloc(&d, &failed, TRI_VRAM_2D, 1, ~0u) == TRI_E_CAP_TOO_SMALL);
            REQUIRE(tri_vram_free(&d, &b[0]) == TRI_OK);
            REQUIRE(tri_vram_free(&d, &b[1]) == TRI_OK);
            /* First fit reuses freed space. */
            REQUIRE(tri_vram_alloc(&d, &b[2], TRI_VRAM_PATTERN, 1, 0) == TRI_OK);
            REQUIRE(b[2].off == round_to(first,64));
            REQUIRE(tri_vram_free(&d, &b[2]) == TRI_OK);
            u32 video_align = common(16*(m.bpp/8u), 7);
            u32 video_off = round_to(d.vram.area2d_end, video_align);
            u32 video_size = 16*(m.bpp/8u);
            if (video_off > 4*1024*1024 || video_size > 4*1024*1024-video_off)
                video_off = round_to(first, video_align);
            REQUIRE(tri_vram_alloc(&d, &b[3], TRI_VRAM_VIDEO, 1, 7) == TRI_OK);
            REQUIRE(b[3].off == video_off && b[3].bytes == video_size);
            REQUIRE(tri_vram_free(&d, &b[3]) == TRI_OK);
        }
    return 0;
}
static int surface_tests(void)
{
    setup(4*1024*1024, false);
    struct tri_surface s = {.off=0,.bytes=1966080,.pitch=4096,
                           .width=640,.height=480,.cpp=4,.format=TRI_FMT_XRGB8888};
    REQUIRE(tri_surface_validate(&d, &s) == TRI_OK);
    s.cpp = 3;
    REQUIRE(tri_surface_validate(&d, &s) == TRI_E_SURFACE);
    s.cpp = 4; s.pitch = 2556;
    REQUIRE(tri_surface_validate(&d, &s) == TRI_E_SURFACE);
    s.pitch = 4096; s.bytes--;
    REQUIRE(tri_surface_validate(&d, &s) == TRI_E_CAP_TOO_SMALL);
    s.bytes++; s.off = 4*1024*1024-4096;
    REQUIRE(tri_surface_validate(&d, &s) == TRI_E_CAP_TOO_SMALL);
    s.off = ~3u;
    REQUIRE(tri_surface_validate(&d, &s) == TRI_E_CAP_TOO_SMALL);
    return 0;
}

void _start(void) {
    int rc=test();
    if (!rc) rc=display_tests();
    if (!rc) rc=allocation_tests();
    if (!rc) rc=surface_tests();
    const char *s=rc?"RED vram line failure\n":"GREEN vram\n";
    char failure[] = "RED vram line 00000\n";
    if (rc) {
        u32 value = rc;
        for (u32 i = 0; i < 5; ++i) { failure[18-i] = '0' + value % 10; value /= 10; }
        s = failure;
    }
    u32 n=0;while(s[n])++n;
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory");
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc?1:0):"memory");
    __builtin_unreachable();
}
