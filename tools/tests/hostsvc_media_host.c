/* D4 MEDIA: real ISO/VFS/Device, fake physical and network media.
 * dev_media_check is the #9 dispatch seam pending stage I. D5/I hooks are
 * invoked explicitly; this test does not claim the future wiring is present. */
#include "fs/isofs/iso9660.h"
#include "fs/isofs/iso9660_media.h"
#include "drivers/base/dev.h"
#include "lib/kstring.h"
#include "kmalloc.h"
#include "endian_le.h"
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
volatile u32 tick_count;
static u8 heap[200000];
static u32 used;
void *kmemset(void *d, int c, u32 n) { u8 *p=d; while(n--) *p++=(u8)c; return d; }
void *kmemcpy(void *d, const void *s, u32 n) { u8 *p=d; const u8 *q=s; while(n--) *p++=*q++; return d; }
u32 kstrlen(const char *s) { u32 n=0; while(s[n]) n++; return n; }
int kstrcmp(const char *a, const char *b) { while(*a && *a==*b) {a++; b++;} return (u8)*a-(u8)*b; }
int kstrncmp(const char *a, const char *b, u32 n) { while(n && *a && *a==*b) {a++; b++; n--;} return n ? (u8)*a-(u8)*b : 0; }
char *kstrncpy(char *d, const char *s, u32 n) { u32 i=0; if(n) {while(i+1<n && s[i]) {d[i]=s[i]; i++;} d[i]=0;} return d; }
char *kstrncat(char *d, const char *s, u32 n) { u32 len=kstrlen(d); if(len<n) kstrncpy(d+len,s,n-len); return d; }
void *kmalloc(u32 n) { void *p=heap+used; if(n>sizeof(heap)-used) return 0; used+=(n+15)&~15u; return p; }
void *kzalloc(u32 n) { void *p=kmalloc(n); if(p) kmemset(p,0,n); return p; }
void kfree(void *p) { (void)p; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
u32 atapi_media_gen(void) { return 0; } /* D5 still uses this in ISO operations. */
static void *staled;
static unsigned stale_calls;
void vfs_fd_stale_mount(void *ctx) { staled=ctx; stale_calls++; }
void vfs_fd_invalidate_mount(void *ctx) { (void)ctx; }
/* I owns the real dispatch; only this unavailable symbol is supplied here. */
int dev_media_check(Device *d, u32 *g) { if(d->media_check) return d->media_check(d,g); *g=0; return 0; }
#include "fs/isofs/iso9660.c"
#include "fs/vfs.c"
#include "platform/pc98/boot_names.c"

#define SECTORS 40u
static u8 images[2][SECTORS * ISO_SECTOR_SIZE];
static u32 gen, checks, reads;
static int image, failure, read_failure, swap_read;
static u32 fail_check;
static int check_media(Device *d, u32 *g) { (void)d; checks++; if(fail_check == checks) return OS32_ERR_HOSTDOWN; if(failure) return failure; *g=gen; return 0; }
static int read_media(Device *d, int lba, int count, void *buf)
{
    u32 bytes=(u32)count*(u32)d->sect_size, off=(u32)lba*(u32)d->sect_size;
    reads++;
    if(read_failure) return read_failure;
    if(off>sizeof(images[0]) || bytes>sizeof(images[0])-off) return OS32_ERR_IO;
    kmemcpy(buf,images[image]+off,bytes);
    if(swap_read) { swap_read=0; image^=1; gen++; }
    return 0;
}
static void make_image(int which)
{
    u8 *p=images[which]+ISO_PVD_LBA*ISO_SECTOR_SIZE;
    u32 root=20u+(u32)which, file=30u+(u32)which;
    p[0]=1; kmemcpy(p+1,"CD001",5); p[6]=1;
    p[ISO_PVD_BLOCK_SIZE+1]=8;
    le32_wr(p+ISO_PVD_VOLUME_SIZE,SECTORS);
    p+=ISO_PVD_ROOT_REC; p[0]=ISO_ROOT_REC_LEN;
    le32_wr(p+ISO_DR_EXTENT,root); le32_wr(p+ISO_DR_DATA_LEN,ISO_SECTOR_SIZE);
    p[ISO_DR_FLAGS]=ISO_FLAG_DIRECTORY;
    p=images[which]+root*ISO_SECTOR_SIZE;
    p[0]=ISO_ROOT_REC_LEN+6; le32_wr(p+ISO_DR_EXTENT,file);
    le32_wr(p+ISO_DR_DATA_LEN,4); p[ISO_DR_NAME_LEN]=7;
    kmemcpy(p+ISO_DR_NAME,"A.TXT;1",7);
    kmemset(images[which]+file*ISO_SECTOR_SIZE,which?'B':'A',4);
}
static int caches_empty(Iso9660Ctx *ctx)
{
    if(ctx->pc_valid || ctx->ra_valid) return 0;
    for(int i=0;i<ISO_SCACHE_SLOTS;i++) if(ctx->scache[i].valid) return 0;
    return 1;
}
static void fill_caches(Iso9660Ctx *ctx)
{
    ctx->pc_valid=ctx->ra_valid=ctx->touched=1;
    for(int i=0;i<ISO_SCACHE_SLOTS;i++) ctx->scache[i].valid=1;
}
static int suite(void)
{
    static Device cd={.name="cd0",.type=DEV_BLOCK,.bus_type=DEV_BUS_ATAPI,
        .sect_size=ISO_SECTOR_SIZE,.total_sects=SECTORS,.blk_read=read_media,.media_check=check_media};
    make_image(0); make_image(1); pc98_boot_names_bind();
    REQUIRE(!dev_register(&cd)); REQUIRE(!vfs_register_fs(&iso9660_ops));
    REQUIRE(!vfs_mount("/cd0","cd0","iso9660"));
    Iso9660Ctx *ctx=mounts[0].fs_ctx;
    u8 *ra=ctx->ra_buf;
    u8 out[4]; u32 observed=999;
    REQUIRE(vfs_read("/cd0/A.TXT",out,4)==4 && out[0]=='A');
    u32 n=reads, c=checks;
    REQUIRE(!iso9660_resource_check(ctx,&observed) && observed==0 && checks==c+1);
    REQUIRE(vfs_read("/cd0/A.TXT",out,4)==4 && reads==n);
    fill_caches(ctx); image=1; gen=1;
    REQUIRE(iso9660_resource_check(ctx,&observed)==OS32_ERR_STALE && observed==1);
    REQUIRE(staled==ctx && stale_calls==1 && caches_empty(ctx));
    REQUIRE(!ctx->root_lba && !ctx->root_size && !ctx->volume_size && !ctx->block_size);
    REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_STALE);
    REQUIRE(!iso9660_resource_refresh(ctx));
    REQUIRE(mounts[0].fs_ctx==ctx && ctx->ra_buf==ra && ctx->root_lba==21);
    REQUIRE(vfs_read("/cd0/A.TXT",out,4)==4 && out[0]=='B');
    fill_caches(ctx); failure=OS32_ERR_HOSTDOWN;
    REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_HOSTDOWN && caches_empty(ctx));
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_HOSTDOWN && !ctx->block_size);
    failure=0; REQUIRE(!iso9660_resource_refresh(ctx));
    failure=OS32_ERR_STALE; fill_caches(ctx);
    REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_STALE && !ctx->block_size && caches_empty(ctx));
    failure=0; REQUIRE(!iso9660_resource_refresh(ctx));
    read_failure=OS32_ERR_HOSTDOWN;
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_HOSTDOWN && !ctx->block_size);
    read_failure=OS32_ERR_STALE;
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_STALE && !ctx->block_size);
    read_failure=OS32_ERR_IO;
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_IO && !ctx->block_size);
    read_failure=0; fail_check=checks+2;
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_HOSTDOWN && !ctx->block_size);
    fail_check=0; swap_read=1;
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_STALE && !ctx->block_size);
    REQUIRE(!iso9660_resource_refresh(ctx) && ctx->root_lba==20);
    /* 512-byte netcd adapter: existing Device wrapper dispatches legacy read;
     * u32 callback priority/high-LBA dispatch is stage I's acceptance. */
    cd.bus_type=DEV_BUS_LOOP; cd.sect_size=512; cd.total_sects=SECTORS*4;
    REQUIRE(!iso9660_resource_refresh(ctx) && ctx->volume_size==SECTORS);
    REQUIRE(vfs_read("/cd0/A.TXT",out,4)==4 && out[0]=='A');
    u8 *p=images[0]+ISO_PVD_LBA*ISO_SECTOR_SIZE;
    for(int i=0;i<7;i++) {
        make_image(0);
        if(i==0) p[1]='X';
        if(i==1) p[6]=2;
        if(i==2) p[ISO_PVD_BLOCK_SIZE+1]=4;
        if(i==3) le32_wr(p+ISO_PVD_VOLUME_SIZE,SECTORS+1);
        if(i==4) p[ISO_PVD_ROOT_REC]=0;
        if(i==5) p[ISO_PVD_ROOT_REC+ISO_DR_FLAGS]=0;
        if(i==6) le32_wr(p+ISO_PVD_ROOT_REC+ISO_DR_DATA_LEN,U32_MAX);
        REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_IO && !ctx->block_size);
    }
    make_image(0); cd.total_sects=ISO_PVD_LBA*4;
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_IO);
    cd.total_sects=SECTORS*4; cd.sect_size=513;
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_IO);
    cd.sect_size=512; cd.media_check=0;
    REQUIRE(!iso9660_resource_refresh(ctx) && !ctx->media_gen);
    REQUIRE(!iso9660_resource_check(ctx,&observed) && !observed);
    cd.media_check=check_media;
    gen=U32_MAX; REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_STALE);
    REQUIRE(!iso9660_resource_refresh(ctx));
    gen=0; REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_STALE);
    REQUIRE(!iso9660_resource_refresh(ctx));
    iso9660_media_drop(ctx); REQUIRE(caches_empty(ctx) && !ctx->block_size);
    REQUIRE(!iso9660_resource_refresh(ctx));
    REQUIRE(!dev_unregister(&cd)); REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_IO);
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_IO);
    REQUIRE(iso9660_resource_check(0,0)==VFS_ERR_NOMOUNT);
    REQUIRE(iso9660_resource_refresh(0)==VFS_ERR_NOMOUNT); iso9660_media_drop(0);
    return 0;
}
void _start(void) { int rc=suite(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory"); __builtin_unreachable(); }
