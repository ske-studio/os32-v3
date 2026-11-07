/* D6: real FD table and C1 generation; backend/path routing are fixtures.
 * C4/D5/I hook wiring and automatic path refresh remain integration tests. */
#include "types.h"
#include "lib/kstring.h"
#include "fs/serialfs/serialfs_persistent.c"
#include PRODUCT_SOURCE
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
static int ctx, other_ctx, checks, calls, failure, notify, swap_probe;
static VfsOps ops;
static int probes, kinds, creates, missing, lookups, unlinks;
static u32 backend_size = 4;
struct VfsStdinRead *exec_stdin_state(void) { return 0; }
int input_getchar_kapi(void) { return -1; }
int res_owner_get(void) { return 1; }
u32 kstrlen(const char *s) { u32 n=0; while(s[n]) n++; return n; }
char *kstrlcpy(char *d,const char *s,u32 n) { u32 i=0; if(n) { while(i+1<n && s[i]) { d[i]=s[i]; i++; } d[i]=0; } return d; }
int vfs_resolve_path(const char *p,char *out,int n) { kstrlcpy(out,p,(u32)n); return 0; }
int vfs_resource_prepare(VfsOps *o, void *c) { (void)o; (void)c; return 0; }
void vfs_resource_stale_mount(void *c) { (void)c; }
VfsOps *vfs_route(const char *p,char *out,int n,void **c) { kstrlcpy(out,p,(u32)n); *c=&ctx; return &ops; }
int vfs_path_kind(const char *p) { (void)p; kinds++; return missing ? VFS_ERR_NOTFOUND : VFS_KIND_FILE; }
u32 vfs_path_dev(const char *p) { (void)p; return 1; }
u32 vfs_path_mount_generation(const char *p) { (void)p; return 1; }
int fd_is_redirected(int fd) { (void)fd; return 0; }
int fd_redirect_read(int fd,void *b,u32 n) { (void)fd;(void)b;(void)n;return -1; }
int fd_redirect_write(int fd,const void *b,u32 n) { (void)fd;(void)b;(void)n;return -1; }
u16 fd_redirect_ifmt(int fd,int *out) { (void)fd;(void)out;return OS_S_IFCHR; }
void console_write(const char *b,u32 n,u8 a) { (void)b;(void)n;(void)a; }
static int check(void *c,u32 *g) { checks++; if(failure) return failure; *g=sfs_persistent_generation(); if(notify) vfs_fd_stale_mount(c); return 0; }
static int size(void *c,const char *p,u32 *n)
{
    (void)c; (void)p; probes++; *n=backend_size;
    if (swap_probe) { swap_probe--; backend_size++; sfs_persistent_stale(); }
    return missing ? VFS_ERR_NOTFOUND : VFS_OK;
}
static int create(void *c,const char *p,const void *b,u32 n)
{
    (void)c; (void)p; (void)b; (void)n; creates++;
    sfs_persistent_stale(); backend_size=9;
    /* missing=2 models a file disappearing again after creation. */
    if (missing == 1) missing=0;
    return VFS_OK;
}
static int create_excl(void *c,const char *p) { return create(c,p,0,0); }
static int read_file(void *c,const char *p,void *b,u32 n,u32 off) { (void)c;(void)p;(void)off; calls++; if(n) *(char *)b='A'; return (int)n; }
static int write_file(void *c,const char *p,const void *b,u32 n,u32 off) { (void)c;(void)p;(void)b;(void)off;calls++;return (int)n; }
static int stat_file(void *c,const char *p,OS32_Stat *b) { (void)c;(void)p;b->st_size=4;calls++;return 0; }
static int lookup(void *c,const char *p,u32 *ino)
{
    (void)c; (void)p; *ino=1;
    return ++lookups == 2 ? VFS_ERR_IO : VFS_OK;
}
static int unlink_file(void *c,const char *p)
{
    (void)c; (void)p; unlinks++; return VFS_OK;
}
static VfsInoOps ino_ops = {.lookup=lookup};
static int suite(void)
{
    char b; OS32_Stat st; int n,fd[4],peer; VfsFontLease font; u32 len;
    enabled=1; resident.generation=1;
    ops.resource_check=check; ops.get_file_size=size;
    ops.read_stream=read_file; ops.write_stream=write_file; ops.stat=stat_file;
    for(int i=0;i<4;i++) { fd[i]=vfs_open("/host/a",O_RDWR); REQUIRE(fd[i]>=3); }
    peer=vfs_open("/host/b",O_RDWR); REQUIRE(peer>=3);
    open_files[peer].fs_ctx=&other_ctx;
    REQUIRE(vfs_open_font("/host/font",1,0,&font,&len)==0);
    REQUIRE(vfs_read_fd(fd[0],&b,1)==1 && b=='A');
    REQUIRE(vfs_write_fd(fd[1],&b,1)==1);
    REQUIRE(vfs_fstat(fd[2],&st)==0 && st.st_dev==1);
    REQUIRE(vfs_seek(fd[3],4,SEEK_SET)==4);
    failure=OS32_ERR_HOSTDOWN; n=calls;
    REQUIRE(vfs_read_fd(fd[0],&b,1)==failure);
    REQUIRE(vfs_write_fd(fd[1],&b,1)==failure);
    REQUIRE(vfs_fstat(fd[2],&st)==failure);
    REQUIRE(vfs_seek(fd[3],0,SEEK_SET)==failure);
    REQUIRE(calls==n && !vfs_fd_is_stale(fd[0]));
    failure=OS32_ERR_STALE; REQUIRE(vfs_fstat(fd[2],&st)==failure); failure=0;
    /* C1 does not notify VFS directly: the FD comparison must detect this. */
    sfs_persistent_stale();
    REQUIRE(vfs_read_fd(fd[0],&b,1)==VFS_ERR_STALE);
    REQUIRE(open_files[fd[0]].fs_ctx==&ctx && open_files[fd[0]].ops==&ops);
    REQUIRE(!vfs_fd_is_stale(peer));
    resident.generation=1; /* Even a wrapped/reused generation cannot revive FDs. */
    n=checks;
    REQUIRE(vfs_write_fd(fd[1],&b,1)==VFS_ERR_STALE);
    REQUIRE(vfs_fstat(fd[2],&st)==VFS_ERR_STALE);
    REQUIRE(vfs_seek(fd[3],0,SEEK_SET)==VFS_ERR_STALE);
    REQUIRE(vfs_font_read_at(&font,0,&b,1)==VFS_ERR_STALE);
    REQUIRE(checks==n);
    REQUIRE(vfs_close_font(&font)==0 && checks==n);
    for(int i=0;i<4;i++) vfs_close(fd[i]);
    REQUIRE(checks==n);
    fd[0]=vfs_open("/host/a",O_RDWR); REQUIRE(fd[0]>=3);
    REQUIRE(vfs_read_fd(fd[0],&b,1)==1); /* refreshed shell, new FD */
    notify=1; REQUIRE(vfs_fstat(fd[0],&st)==VFS_ERR_STALE); notify=0;
    vfs_close(fd[0]);
    /* A reconnect in the first probe must refresh both kind and size. */
    fd[0]=vfs_open("/host/a",O_RDWR); REQUIRE(fd[0]>=3);
    probes=kinds=0; swap_probe=1;
    fd[1]=vfs_open("/host/a",O_RDWR); REQUIRE(fd[1]>=3);
    REQUIRE(probes==2 && kinds==2 && open_files[fd[1]].size==backend_size);
    REQUIRE(open_files[fd[1]].resource_generation==sfs_persistent_generation());
    REQUIRE(vfs_seek(fd[1],0,SEEK_END)==(int)backend_size);
    REQUIRE(vfs_read_fd(fd[0],&b,1)==VFS_ERR_STALE);
    vfs_close(fd[0]); vfs_close(fd[1]);
    /* Two changes, then stable: a third probe would wrongly succeed. */
    probes=0; swap_probe=2;
    REQUIRE(vfs_open("/host/a",O_RDWR)==VFS_ERR_STALE);
    REQUIRE(probes==2 && swap_probe==0);
    ops.write_file=create; ops.create_excl=create_excl;
    for (int excl=0; excl<2; excl++) {
        missing=1; creates=probes=0;
        fd[0]=vfs_open("/host/new",O_CREAT|O_RDWR|(excl ? O_EXCL : 0));
        REQUIRE(fd[0]>=3 && creates==1 && open_files[fd[0]].size==9);
        REQUIRE(probes==(excl ? 1 : 2));
        vfs_close(fd[0]);
        missing=2; creates=0;
        REQUIRE(vfs_open("/host/gone",O_CREAT|O_RDWR|(excl ? O_EXCL : 0))==VFS_ERR_NOTFOUND);
        REQUIRE(creates==1); /* Retry must not recreate an already created file. */
    }
    /* Creation ownership survives the retry through inode-lookup cleanup. */
    missing=1; creates=lookups=unlinks=0;
    ops.ino=&ino_ops; ops.unlink=unlink_file;
    REQUIRE(vfs_open("/host/new",O_CREAT|O_RDWR)==VFS_ERR_IO);
    REQUIRE(creates==1 && lookups==2 && unlinks==1);
    ops.ino=0; ops.unlink=0;
    missing=0;
    fd[0]=vfs_open("/host/a",O_RDWR); REQUIRE(fd[0]>=3);
    vfs_fd_invalidate_mount(&ctx);
    REQUIRE(!open_files[fd[0]].fs_ctx && !open_files[fd[0]].ops);
    REQUIRE(vfs_read_fd(fd[0],&b,1)==VFS_ERR_STALE);
    vfs_close(fd[0]);
    ops.resource_check=0;
    fd[0]=vfs_open("/local/a",O_RDWR); REQUIRE(fd[0]>=3);
    REQUIRE(vfs_read_fd(fd[0],&b,1)==1);
    vfs_close(fd[0]); vfs_close(peer);
    return 0;
}
void _start(void) { int rc=suite(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory"); __builtin_unreachable(); }
