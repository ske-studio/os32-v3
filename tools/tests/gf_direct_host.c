/* Real G1 code, fake value loan and device windows. */
#include "gfx_hal.h"
#include "memmap.h"
#include "geforce_module.h"
#include "hwdev/gfinit_mode.h"
static u8 vram[GF_MODE_VRAM_BYTES] __attribute__((aligned(4)));
static u32 regs[0x100210U / 4U];
static struct video_fb front;
static u32 width, height;
static int points, fail_at, drains, errors;
static volatile void *mapped(u32 phys)
{
    if (phys == (u32)(uptr)regs) drains++;
    return P2V_IO(phys);
}
#define P2V_IO(phys) mapped(phys)
#include "DIRECT_SOURCE"
static int point(void *unused)
{
    (void)unused;
    return ++points == fail_at ? OS32_ERR_IO : 0;
}
static struct geforce_platform platform = {.point=point};
static GeforceModuleContext context = {.platform=&platform};
const GeforceModuleContext *geforce_context = &context;
void geforce_backend_framebuffer_info(struct video_fb *fb, u32 *w, u32 *h)
{ *fb=front; *w=width; *h=height; }
int geforce_fill_rect(int x,int y,int w,int h,u8 c)
{ (void)x;(void)y;(void)w;(void)h;(void)c;return 0; }
int geforce_blit(int dx,int dy,int sx,int sy,int w,int h)
{ (void)dx;(void)dy;(void)sx;(void)sy;(void)w;(void)h;return 0; }
int geforce_fill_rop(int x,int y,int w,int h,u8 c,u8 r)
{ (void)x;(void)y;(void)w;(void)h;(void)c;(void)r;return 0; }
#define CHECK(c) do { if (!(c)) errors++; } while (0)
static void reset(void)
{
    width=GF_MODE_WIDTH; height=GF_MODE_HEIGHT;
    platform.display_phys=(u32)(uptr)vram; platform.bar0_phys=(u32)(uptr)regs;
    front=(struct video_fb){platform.display_phys, sizeof(vram), GF_MODE_PITCH, GFX_FMT_PACKED8};
    points=fail_at=drains=0;
    for (u32 i=0;i<sizeof(vram);i++) vram[i]=0xa5;
}
void _start(void)
{
    reset(); CHECK(gf_direct_ok());
    front.phys++; CHECK(!gf_direct_ok()); /* complete surface no longer fits */
    front.phys=platform.display_phys-1; CHECK(!gf_direct_ok());
    front.phys=platform.display_phys; front.bytes=0; CHECK(!gf_direct_ok());
    reset(); front.format=GFX_FMT_RGB565; CHECK(!gf_direct_ok());
    reset(); width--; CHECK(!gf_direct_ok());
    reset(); height--; CHECK(!gf_direct_ok());
    CHECK(gf_direct_ops.fill_rect==geforce_fill_rect && gf_direct_ops.blit==geforce_blit);
    CHECK(gf_direct_ops.fill_rop==geforce_fill_rop && !gf_direct_ops.mono);
    CHECK(gf_direct_ops.put==gf_direct_put && gf_direct_ops.get==gf_direct_get);
    u8 src[40], out[40];
    for (int i=0;i<40;i++) src[i]=(u8)i;
    /* Exercise aligned body, every leading alignment, tails and RAM padding. */
    for (int x=0;x<4;x++) for (int w=1;w<=9;w++) {
        reset();
        CHECK(!gf_direct_ops.put(x,2,w,2,src,17,-1));
        CHECK(points==2 && drains==1);
        for (u32 i=0;i<sizeof(vram);i++) {
            u32 row=i/GF_MODE_PITCH, col=i%GF_MODE_PITCH;
            u8 expected=0xa5;
            if (row>=2 && row<4 && col>=(u32)x && col<(u32)(x+w))
                expected=src[(row-2)*17+col-(u32)x];
            CHECK(vram[i]==expected);
        }
        for (int i=0;i<40;i++) out[i]=0xcc;
        points=0;
        CHECK(!gf_direct_ops.get(x,2,w,2,out,19));
        CHECK(points==2 && drains==1);
        for (int i=0;i<40;i++)
            CHECK(out[i]==(i<w ? src[i] : i>=19 && i<19+w ? src[17+i-19] : 0xcc));
    }
    reset(); src[0]=255; src[1]=7; src[2]=255; src[3]=9;
    CHECK(!gf_direct_put(3,1,4,1,src,4,255));
    CHECK(vram[643]==0xa5 && vram[644]==7 && vram[645]==0xa5 && vram[646]==9);
    CHECK(drains==1);
    reset(); fail_at=2;
    CHECK(gf_direct_put(0,0,4,2,src,4,-1)==OS32_ERR_IO);
    CHECK(vram[0]==src[0] && vram[640]==0xa5 && !drains);
    reset(); fail_at=1; out[0]=0xcc;
    CHECK(gf_direct_get(0,0,1,1,out,1)==OS32_ERR_IO && out[0]==0xcc);
    reset();
    CHECK(gf_direct_put(0,0,1,1,src,1,-2)==OS32_ERR_INVAL);
    CHECK(gf_direct_put(0,0,1,1,src,1,256)==OS32_ERR_INVAL);
    CHECK(gf_direct_get(639,479,2,1,out,2)==OS32_ERR_INVAL);
    CHECK(gf_direct_put(0,0,4,1,src,3,-1)==OS32_ERR_INVAL);
    CHECK(!points && !drains);
    front.phys++;
    CHECK(gf_direct_put(0,0,1,1,src,1,-1)==OS32_ERR_NOSYS);
    CHECK(gf_direct_get(0,0,1,1,out,1)==OS32_ERR_NOSYS);
    CHECK(!points && !drains);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(errors ? 1 : 0));
    __builtin_unreachable();
}
