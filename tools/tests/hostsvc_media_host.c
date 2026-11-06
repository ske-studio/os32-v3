/* D4/D5 MEDIA: real ISO/VFS/Device, fake physical and network media.
 * Real Device dispatch and VFS refresh are connected by stage I. */
#include "fs/isofs/iso9660.h"
#include "fs/isofs/iso9660_media.h"
#include "drivers/base/dev.h"
#include "lib/kstring.h"
#include "kmalloc.h"
#include "endian_le.h"
#define REQUIRE(x) do { if (!(x)) return (__LINE__ % 250) + 1; } while (0)
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
void real_fd_stale_mount(void *ctx);
static void *staled;
static unsigned stale_calls;
void vfs_fd_stale_mount(void *ctx) { staled=ctx; stale_calls++; real_fd_stale_mount(ctx); }
#include "fs/isofs/iso9660.c"
#include "fs/vfs.c"
#define vfs_fd_stale_mount real_fd_stale_mount
#include "fs/vfs_fd.c"
#undef vfs_fd_stale_mount
struct VfsStdinRead *exec_stdin_state(void) { return 0; }
int input_getchar_kapi(void) { return -1; }
int res_owner_get(void) { return 1; }
int fd_is_redirected(int fd) { (void)fd; return 0; }
int fd_redirect_read(int fd, void *b, u32 n) { (void)fd; (void)b; (void)n; return -1; }
#include "platform/pc98/boot_names.c"

#define SECTORS 40u
static u8 images[2][SECTORS * ISO_SECTOR_SIZE];
static u32 gen, checks, reads;
static int image, failure, read_failure, swap_read, ua_read, fail_once, post_failure;
static u32 fail_check;
static int check_media(Device *d, u32 *g) { (void)d; checks++; if(fail_check == checks) return OS32_ERR_HOSTDOWN; if(failure) return failure; *g=gen; return 0; }
static int read_media(Device *d, int lba, int count, void *buf)
{
    u32 bytes=(u32)count*(u32)d->sect_size, off=(u32)lba*(u32)d->sect_size;
    reads++;
    if(ua_read) { ua_read=0; gen++; }
    if(read_failure) { int rc=read_failure; if(fail_once) read_failure=0; return rc; }
    if(post_failure) { post_failure=0; fail_check=checks+1; }
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
static unsigned listed;
static void list_cb(const VfsDirEntry *entry, void *raw)
{
    (void)raw;
    if(kstrcmp(entry->name,"A.TXT")==0) listed++;
}
static void down_cb(const VfsDirEntry *entry, void *raw)
{
    list_cb(entry,raw);
    failure=OS32_ERR_HOSTDOWN;
}

/* Exercise both pointer-returning directory helpers and all data read paths. */
static int operations(Iso9660Ctx *ctx)
{
    u8 out[ISO_RA_BYTES];
    OS32_Stat st;
    u32 size;
    u8 *ra=ctx->ra_buf;
    REQUIRE(iso9660_ops.resource_check==iso9660_resource_check);
    REQUIRE(iso9660_ops.resource_refresh==iso9660_resource_refresh);
    REQUIRE(iso9660_ops.read_file(ctx,"A.TXT",out,4)==4);
    fail_check=checks+1;
    REQUIRE(iso9660_ops.stat(ctx,"A.TXT",&st)==OS32_ERR_HOSTDOWN);
    fail_check=0;
    u32 n=reads;
    failure=OS32_ERR_HOSTDOWN;
    REQUIRE(iso9660_ops.read_file(ctx,"A.TXT",out,4)==OS32_ERR_HOSTDOWN);
    REQUIRE(iso9660_ops.read_stream(ctx,"A.TXT",out,4,0)==OS32_ERR_HOSTDOWN);
    REQUIRE(iso9660_ops.stat(ctx,"A.TXT",&st)==OS32_ERR_HOSTDOWN);
    REQUIRE(iso9660_ops.get_file_size(ctx,"A.TXT",&size)==OS32_ERR_HOSTDOWN);
    REQUIRE(iso9660_ops.list_dir(ctx,"/",list_cb,0)==OS32_ERR_HOSTDOWN && reads==n);
    failure=0;
    REQUIRE(iso9660_ops.read_file(ctx,"A.TXT",out,4)==4);
    gen++; image^=1;
    REQUIRE(iso9660_ops.stat(ctx,"A.TXT",&st)==OS32_ERR_STALE && caches_empty(ctx));
    REQUIRE(iso9660_ops.stat(ctx,"A.TXT",&st)==OS32_ERR_STALE);
    REQUIRE(!iso9660_ops.resource_refresh(ctx));
    REQUIRE(iso9660_ops.read_file(ctx,"A.TXT",out,4)==4 && out[0]==(image?'B':'A'));
    for(int window=0;window<2;window++) {
        ctx->ra_buf=window?ra:0;
        const int errors[]={OS32_ERR_HOSTDOWN,OS32_ERR_STALE,OS32_ERR_IO};
        for(unsigned e=0;e<sizeof(errors)/sizeof(errors[0]);e++) {
            iso_drop_caches(ctx); read_failure=errors[e]; fail_once=1; n=reads;
            int stat_rc=iso9660_ops.stat(ctx,"A.TXT",&st);
            REQUIRE(stat_rc==errors[e] || (window && errors[e]==OS32_ERR_IO && stat_rc==0));
            if(errors[e]!=OS32_ERR_IO) REQUIRE(reads==n+1);
            read_failure=0; fail_once=0;
            iso_drop_caches(ctx); read_failure=errors[e];
            REQUIRE(iso9660_ops.list_dir(ctx,"/",list_cb,0)==errors[e]);
            read_failure=0;
            iso_drop_caches(ctx);
            REQUIRE(!iso9660_ops.stat(ctx,"A.TXT",&st));
            /* Path is cached; next failure belongs to the file extent. */
            read_failure=errors[e]; fail_once=1; n=reads;
            int rc=iso9660_ops.read_file(ctx,"A.TXT",out,4);
            REQUIRE(rc==errors[e] || (window && errors[e]==OS32_ERR_IO && rc==4));
            if(errors[e]!=OS32_ERR_IO) REQUIRE(reads==n+1);
            read_failure=0; fail_once=0;
        }
        iso_drop_caches(ctx); post_failure=1;
        REQUIRE(iso9660_ops.stat(ctx,"A.TXT",&st)==OS32_ERR_HOSTDOWN);
        fail_check=0;
        iso_drop_caches(ctx); swap_read=1;
        REQUIRE(iso9660_ops.read_file(ctx,"A.TXT",out,4)==OS32_ERR_STALE);
        REQUIRE(caches_empty(ctx) && !ctx->block_size);
        REQUIRE(!iso9660_ops.resource_refresh(ctx));
    }
    ctx->ra_buf=ra;
    listed=0;
    REQUIRE(iso9660_ops.list_dir(ctx,"/",down_cb,0)==OS32_ERR_HOSTDOWN && listed==1);
    failure=0;
    /* Direct bulk read and cached-window copy preserve post-read failures. */
    iso_drop_caches(ctx); post_failure=1;
    REQUIRE(iso_read_extent(ctx,20,ISO_RA_SECTORS,0,out,ISO_RA_BYTES)==OS32_ERR_HOSTDOWN);
    fail_check=0;
    iso_drop_caches(ctx); read_failure=OS32_ERR_STALE;
    REQUIRE(iso_read_extent(ctx,20,ISO_RA_SECTORS,0,out,ISO_RA_BYTES)==OS32_ERR_STALE);
    read_failure=0;
    REQUIRE(iso9660_ops.read_file(ctx,"A.TXT",out,4)==4);
    post_failure=1; /* force a physical file read while keeping its path */
    ctx->ra_valid=0;
    for(int i=0;i<ISO_SCACHE_SLOTS;i++) ctx->scache[i].valid=0;
    REQUIRE(iso9660_ops.read_file(ctx,"A.TXT",out,4)==OS32_ERR_HOSTDOWN);
    fail_check=0;
    return 0;
}

static u32 dispatched_lba;
static int u32_reads;
static int read_u32(Device *dev, u32 lba, int count, void *buf)
{
    dispatched_lba = lba;
    u32_reads++;
    if (lba > S32_MAX) return 0;
    return read_media(dev, (int)lba, count, buf);
}

static int wiring(Iso9660Ctx *ctx, Device *cd)
{
    u8 out[4];
    OS32_Stat st;
    Device wide = {.blk_read = read_media, .blk_read_u32 = read_u32};
    int n = u32_reads;
    REQUIRE(!dev_blk_read_lba(&wide, 0x80000000u, 1, out));
    REQUIRE(u32_reads == n + 1 && dispatched_lba == 0x80000000u);
    wide.blk_read_u32 = 0;
    REQUIRE(dev_blk_read_lba(&wide, 0x80000000u, 1, out) == OS32_ERR_INVAL);
    REQUIRE(dev_blk_read_lba(&wide, S32_MAX, 2, out) == OS32_ERR_INVAL);
    REQUIRE(dev_media_check(0, &dispatched_lba) == OS32_ERR_INVAL);
    REQUIRE(dev_media_check(cd, 0) == OS32_ERR_INVAL);
    cd->blk_read_u32 = read_u32;
    REQUIRE(vfs_read("/cd0/A.TXT", out, 4) == 4);
    image ^= 1; gen++;
    REQUIRE(vfs_read("/cd0/A.TXT", out, 4) == OS32_ERR_STALE);
    REQUIRE(!ctx->block_size && mounts[0].fs_ctx == ctx);
    REQUIRE(vfs_read("/cd0/A.TXT", out, 4) == 4 && out[0] == (image ? 'B' : 'A'));
    image ^= 1; gen++;
    REQUIRE(vfs_stat("/cd0", &st) == OS32_ERR_STALE);
    failure = OS32_ERR_HOSTDOWN;
    REQUIRE(vfs_stat("/cd0", &st) == OS32_ERR_HOSTDOWN);
    REQUIRE(!ctx->block_size);
    failure = 0;
    REQUIRE(!vfs_stat("/cd0", &st) && ctx->block_size == ISO_SECTOR_SIZE);
    fail_check = checks + 2;
    REQUIRE(vfs_stat("/cd0", &st) == OS32_ERR_HOSTDOWN);
    fail_check = 0;
    fail_check = checks + 2;
    REQUIRE(vfs_read("/cd0/./A.TXT", out, 4) == OS32_ERR_HOSTDOWN);
    fail_check = 0;
    image ^= 1; gen++;
    REQUIRE(vfs_path_kind("/cd0") == OS32_ERR_STALE);
    REQUIRE(vfs_path_kind("/cd0") == VFS_KIND_DIR);
    /* A check from an existing FD/backend schedules the very next path. */
    image ^= 1; gen++;
    REQUIRE(iso9660_resource_check(ctx, 0) == OS32_ERR_STALE);
    REQUIRE(vfs_read("/cd0/A.TXT", out, 4) == 4 && out[0] == (image ? 'B' : 'A'));
    Device physical = {.type = DEV_BLOCK};
    REQUIRE(!dev_register_cd(&physical, -1));
    REQUIRE(!kstrcmp(physical.name, "cd1"));
    Device duplicate = {.name = "unchanged"};
    REQUIRE(dev_register_cd(&duplicate, 0) == OS32_ERR_EXIST);
    REQUIRE(!kstrcmp(duplicate.name, "unchanged"));
    REQUIRE(!dev_unregister(&physical));
    cd->blk_read_u32 = 0;
    return 0;
}

static int suite(void)
{
    static Device cd={.name="cd0",.type=DEV_BLOCK,.bus_type=DEV_BUS_NONE,
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
    REQUIRE(staled==ctx && stale_calls==2 && caches_empty(ctx));
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
    /* 512-byte media uses the common Device wrapper. */
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
    /* An unpublished mount can rebuild once after a PVD-time exchange. */
    swap_read=1;
    Iso9660Ctx *fresh=iso9660_ops.mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_CD,0));
    REQUIRE(fresh && fresh->root_lba==20u+(u32)image);
    iso9660_ops.umount(fresh);
    REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_STALE);
    REQUIRE(!iso9660_resource_refresh(ctx));
    int rc=operations(ctx); if(rc) return rc;
    rc=wiring(ctx,&cd); if(rc) return rc;
    /* Real FD + same-media UA/reset: no replacement, no permanent stale. */
    vfs_umount("/cd0");
    cd.bus_type=DEV_BUS_ATAPI;
    REQUIRE(!vfs_mount("/cd0","cd0","iso9660"));
    ctx=mounts[0].fs_ctx;
    int fd=vfs_open("/cd0/A.TXT",O_RDONLY);
    REQUIRE(fd>=3);
    unsigned old_stale=stale_calls;
    u32 old_gen=gen;
    ctx->ra_valid=0;
    for(int i=0;i<ISO_SCACHE_SLOTS;i++) ctx->scache[i].valid=0;
    ua_read=1;
    REQUIRE(vfs_read_fd(fd,out,4)==4 && out[0]==(image?'B':'A'));
    REQUIRE(gen==old_gen+1 && ctx->media_gen==gen);
    REQUIRE(stale_calls==old_stale && !vfs_fd_is_stale(fd));
    REQUIRE(vfs_seek(fd,0,SEEK_SET)==0 && vfs_read_fd(fd,out,4)==4);
    /* Also detect UA at the FD entry, before a physical read. */
    gen++;
    REQUIRE(!iso9660_resource_check(ctx,0) && caches_empty(ctx));
    REQUIRE(vfs_seek(fd,0,SEEK_SET)==0 && vfs_read_fd(fd,out,4)==4);
    REQUIRE(!vfs_fd_is_stale(fd) && stale_calls==old_stale);
    vfs_close(fd);
    for(int error=-1;error>=-4;error--) {
        read_failure=error;
        REQUIRE(iso9660_resource_refresh(ctx)==VFS_ERR_IO);
    }
    read_failure=0; REQUIRE(!iso9660_resource_refresh(ctx));
    REQUIRE(!dev_unregister(&cd)); REQUIRE(iso9660_resource_check(ctx,0)==OS32_ERR_IO);
    REQUIRE(iso9660_resource_refresh(ctx)==OS32_ERR_IO);
    REQUIRE(iso9660_resource_check(0,0)==VFS_ERR_NOMOUNT);
    REQUIRE(iso9660_resource_refresh(0)==VFS_ERR_NOMOUNT); iso9660_media_drop(0);
    return 0;
}
void _start(void) { int rc=suite(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory"); __builtin_unreachable(); }
