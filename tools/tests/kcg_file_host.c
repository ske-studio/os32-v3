/* libc-free R5: real VFS FD and lease implementation, synthetic RAM backend. */
#include "types.h"
static void die(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
static u32 strlen(const char *s) { u32 n=0; while(s[n]) n++; return n; }
static void *memcpy(void *d,const void *s,u32 n) { u8 *p=d; const u8 *q=s; while(n--) *p++=*q++; return d; }
#define __KSTRING_H
u32 kstrlen(const char *s) { return (u32)strlen(s); }
/* Match lib/kstring_c.c / kstring_asm.asm: capacity includes NUL, no padding. */
char *kstrlcpy(char *dst, const char *src, u32 size)
{
    u32 i;
    if (size == 0) return dst;
    for (i = 0; i + 1 < size && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}
#include "../../fs/vfs_fd.c"
#ifdef FONT_SOURCE_HOST_APP_TOTAL
#undef VFS_FONT_FD_MAX_APP_TOTAL
#define VFS_FONT_FD_MAX_APP_TOTAL FONT_SOURCE_HOST_APP_TOTAL
#endif
/* kernel/gui.h は ILP32 の構造体の大きさを STATIC_ASSERT するので 64 ビットの
 * ホストでは読めない。font_source.c が使うのは GUI_SHELL_OWNER だけなので、
 * 試験 py が kernel/gui.h から読んだ値を -DHOST_GUI_SHELL_OWNER で渡す */
#define __GUI_H
#define GUI_SHELL_OWNER HOST_GUI_SHELL_OWNER
#include "../../kernel/console/font_source.c"
#include "exec_font_lease.h"

#define MOCK_FILE_SIZE 295956u
#define CHECK(x) do { if (!(x)) die(__LINE__ % 250 + 1); } while (0)
#define MOCK_INO       42u
#define SHELL_OWNER    1
#define APP_OWNER      5

static int current_owner = 7;     /* res_owner_get() — font open は見ない */
static int kind_rc = VFS_KIND_FILE, size_rc = VFS_OK, read_mode;
static int read_calls;
static u32 actual_size = MOCK_FILE_SIZE;
static int stale_read;
static u32 last_off, last_len;
static u8 file_data[MOCK_FILE_SIZE];
static VfsOps mock_ops;
static VfsInoOps mock_ino;
static int mock_ctx;

enum { READ_FULL = 0, READ_SHORT = 1, READ_IO = 2 };

int res_owner_get(void) { return current_owner; }
void res_owner_set(int owner) { current_owner = owner; }
const char *vfs_cwd(void) { return "/"; }
int vfs_resolve_path(const char *in, char *out, int size)
{
    if (!in || !out || size <= 0) return VFS_ERR_INVAL;
    if ((int)strlen(in) + 1 > size) return VFS_ERR_NAMETOOLONG;
    kstrlcpy(out, in, (u32)size);
    return VFS_OK;
}
int vfs_resource_prepare(VfsOps *ops, void *ctx) { (void)ops; (void)ctx; return VFS_OK; }
void vfs_resource_stale_mount(void *ctx) { (void)ctx; }
VfsOps *vfs_route(const char *path, char *out, int size, void **ctx)
{ kstrlcpy(out, path, (u32)size); *ctx = &mock_ctx; return &mock_ops; }
int vfs_path_kind(const char *path) { return kind_rc; }
u32 vfs_path_dev(const char *path) { return 1; }
u32 vfs_path_mount_generation(const char *path) { return 1; }
static int mock_size(void *ctx, const char *path, u32 *size)
{ *size = actual_size; return size_rc; }
static int mock_write(void *ctx, const char *path, const void *buf, u32 size)
{ return VFS_ERR_IO; }
static int mock_read(void *dst, u32 len, u32 off)
{
    u32 n = len;
    read_calls++;
    last_off = off;
    last_len = len;
    if (read_mode == READ_IO) return VFS_ERR_IO;
    if ((read_mode == READ_SHORT || (read_mode == 3 && off == 16)) && n > 0) n--;
    if (off > MOCK_FILE_SIZE) return 0;
    if (n > MOCK_FILE_SIZE - off) n = MOCK_FILE_SIZE - off;
    memcpy(dst, file_data + off, n);
    if (stale_read && off == 16) vfs_fd_invalidate_ino(&mock_ctx, MOCK_INO);
    return (int)n;
}
static int mock_read_stream(void *ctx, const char *path, void *buf,
                            u32 size, u32 offset)
{ return mock_read(buf, size, offset); }
static int mock_write_stream(void *ctx, const char *path, const void *buf,
                             u32 size, u32 offset)
{ return (int)size; }
static int mock_ino_lookup(void *ctx, const char *path, u32 *ino)
{ *ino = MOCK_INO; return VFS_OK; }
static int mock_ino_read(void *ctx, u32 ino, void *buf, u32 size, u32 offset)
{ CHECK(ino == MOCK_INO); return mock_read(buf, size, offset); }
int fd_is_redirected(int fd) { return 0; }
u16 fd_redirect_ifmt(int fd, int *out_file_fd)
{ (void)fd; if (out_file_fd) *out_file_fd = -1; return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *buf, u32 size) { return VFS_ERR_INVAL; }
int fd_redirect_write(int fd, const void *buf, u32 size) { return VFS_ERR_INVAL; }
int kbd_getchar_kapi(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color) { }

static int from_user = 1, caller_ok = 1, copy_fail, kill_calls;
static struct caller_access caller;
int ring3_call_from_user(void) { return from_user; }
int caller_access_get(struct caller_access *out)
{
    if (!caller_ok) return 0;
    *out = caller;
    return 1;
}
int copy_caller_cstr(const struct caller_access *c, const char *src,
                     char *dst, u32 cap)
{
    CHECK(c && c->origin == caller.origin && c->owner == caller.owner);
    if (copy_fail || !src || strlen(src) + 1 > cap) return 0;
    memcpy(dst, src, strlen(src) + 1);
    return 1;
}
void ring3_fault_kill(void) { kill_calls++; }

#define APP_GEN   0x51u
#define APP2      (APP_OWNER + 1)

/* app (owner, AS 世代) として dispatch 中にする */
static void as_app(int owner, u32 gen)
{
    from_user = 1;
    caller_ok = 1;
    caller.origin = CALLER_USER;
    caller.app_id = owner;
    /* AS の所有者の札 (paging の as->owner) は app_id と別の番号 (ゲストでは
     * app 2 に札 18 など)。同じ値にすると取り違えが隠れる (S3b) */
    caller.owner = (u32)owner + 0x40u;
    caller.generation = gen;
    current_owner = owner;
}


void *kmemcpy(void *d, const void *s, u32 n) { return memcpy(d,s,n); }
void *kmemset(void *d, int c, u32 n) { u8 *p=d; while(n--) *p++=(u8)c; return d; }
static u8 heap[295940];
static int allocated, irq, guards;
void *kmalloc(u32 n) { CHECK(n == sizeof(heap) && !allocated); allocated=1; return heap; }
void kfree(void *p) { CHECK(p == heap && allocated); allocated=0; }
int font_guard_run(int (*fn)(void *), void *arg) { guards++; if(irq) return OS32_ERR_BUSY; return fn(arg); }
#include "../../drivers/video/kcg/kcg_file.c"
static void fixture(void) {
    kmemset(file_data,0,sizeof(file_data));
    file_data[0]='K'; file_data[1]='C'; file_data[2]='G'; file_data[3]='1';
    file_data[4]=4; file_data[5]=0x84; file_data[6]=4;
    kmemset(file_data+16+94*94*32,1,94*94);
    kmemset(file_data+MOCK_FILE_SIZE-256,1,256);
    file_data[16]=0xa5;
    file_data[16+KCG_FILE_ANK_OFFSET+65*16]=0x5a;
}
static void glyphs(void) {
    u8 buf[32]; int calls=read_calls;
    irq=1;
    kcg_file_api.read_ank(65,buf); CHECK(buf[0]==0x5a);
    kcg_file_api.read_kanji(0x2121,buf); CHECK(buf[0]==0xa5);
    CHECK(read_calls==calls); irq=0;
}
static void no_fds(void) { int i; for(i=3;i<VFS_MAX_OPEN_FILES;i++) CHECK(!open_files[i].in_use); }
void _start(void) {
    u32 id,size,other; u8 buf[32]; int fd; FontLease *slot;
    mock_ops.get_file_size=mock_size; mock_ops.read_stream=mock_read_stream;
    mock_ops.write_file=mock_write; mock_ops.write_stream=mock_write_stream;
    mock_ops.ino=&mock_ino; mock_ino.lookup=mock_ino_lookup; mock_ino.read=mock_ino_read;
    fixture(); as_app(APP_OWNER,APP_GEN);
    CHECK(font_lease_resident_open("/fonts/test.kcg",&id,&size)==0);
    slot=&g_font_leases[id & FONT_LEASE_IDX_MASK]; fd=slot->vfs.fd;
    CHECK(slot->vfs.owner==FONT_RESIDENT_OWNER && open_files[fd].protect && open_files[fd].pinned);
    CHECK(font_lease_resident_open("/fonts/test.kcg",&other,&size)==OS32_ERR_NOSPC);
    CHECK(font_lease_app_read_at(id,0,buf,1)==OS32_ERR_INVAL);
    CHECK(font_lease_app_close(id)==OS32_ERR_INVAL);
    CHECK(font_lease_shell_close(id,1)==OS32_ERR_INVAL);
    font_lease_close_owned(APP_OWNER); vfs_close_owned(APP_OWNER);
    font_lease_close_shell(); vfs_close_owned(GUI_SHELL_OWNER);
    CHECK(font_lease_resident_validate(id)==0);
    slot->owner_gen++; CHECK(font_lease_resident_validate(id)==OS32_ERR_STALE); slot->owner_gen--;
    CHECK(font_lease_resident_close(id)==0);
    CHECK(font_lease_resident_read_at(id,0,buf,1)==OS32_ERR_STALE);
    CHECK(kcg_file_load("/fonts/test.kcg")==0); no_fds(); glyphs();
    font_lease_close_owned(APP_OWNER); font_lease_close_shell(); glyphs();
    kcg_file_api.invalidate(); glyphs(); kcg_file_unload(); CHECK(!allocated);
    read_mode=READ_SHORT; CHECK(kcg_file_load("/fonts/test.kcg")<0); read_mode=3; CHECK(kcg_file_load("/fonts/test.kcg")<0); read_mode=READ_FULL; no_fds();
    stale_read=1; CHECK(kcg_file_load("/fonts/test.kcg")<0); stale_read=0; no_fds();
    actual_size--; CHECK(kcg_file_load("/fonts/test.kcg")<0); actual_size+=2;
    CHECK(kcg_file_load("/fonts/test.kcg")<0); actual_size--;
    file_data[0]=0; CHECK(kcg_file_load("/fonts/test.kcg")<0); fixture();
    file_data[8]=1; CHECK(kcg_file_load("/fonts/test.kcg")<0); fixture();
    file_data[12]=1; CHECK(kcg_file_load("/fonts/test.kcg")<0); fixture();
    file_data[4]++; CHECK(kcg_file_load("/fonts/test.kcg")<0); fixture();
    file_data[16+94*94*32]=0; CHECK(kcg_file_load("/fonts/test.kcg")<0); fixture();
    file_data[MOCK_FILE_SIZE-1]=2; CHECK(kcg_file_load("/fonts/test.kcg")<0); fixture();
    size_rc=VFS_ERR_NOTFOUND; CHECK(kcg_file_load("/fonts/missing.kcg")<0); size_rc=VFS_OK;
    irq=1; CHECK(kcg_file_load("/fonts/test.kcg")==OS32_ERR_BUSY); irq=0;
    CHECK(guards>0); no_fds(); CHECK(!allocated); die(0);
}
