/* Real VFS mount/FD state and LAN client; only wire and clock are fixtures. */
#include "hostsvc_fixture.h"
#include "fs/serialfs/serialfs.h"
#include "fs/vfs_device.h"
#include "net/host_mailbox.h"
#include "os32_kapi_shared.h"
#define IO_H
static unsigned int irq_save(void) { unsigned int f=hs_if(); hs_set_if(0); return f; }
static void irq_restore(unsigned int f) { hs_set_if(f); }
static u8 heap[16384];
void *kmalloc(u32 n) { return n==sizeof(heap) ? heap : 0; }
#include "net/host_mailbox.c"
#include "fs/serialfs/serialfs_persistent.c"
#include PRODUCT_SOURCE

static u8 response[SFS_BUFFER_FRAME];
static u32 pos, end, hellos, server_size=2048;
static int unknown, restarts, restart_retry, stat_in_probe;
static int put(void *ctx,const u8 *buf,u32 n)
{
    SfsDec dec;
    SfsFrame f;
    (void)ctx;
    sfs_dec_reset(&dec);
    dec.max_payload=SFS_LAN_MAX_PAYLOAD;
    for (u32 i=0;i<n;i++) {
        if (sfs_dec_feed(&dec,buf[i],&f)!=1) continue;
        u8 pl[SFS_LAN_MAX_PAYLOAD]={0};
        u16 len=SFS_STATUS_LEN;
        u32 sid=f.sid;
        u8 type=f.type|SFS_T_RESP;
        if (f.type==SFS_T_HELLO) {
            sid=++hellos;
            sfs_put16(pl+SFS_HELLO_RSP_VER,SFS_VERSION);
            sfs_put16(pl+SFS_HELLO_RSP_MAX,SFS_LAN_MAX_PAYLOAD);
            sfs_put32(pl+SFS_HELLO_RSP_NONCE,sfs_get32(f.payload+SFS_HELLO_REQ_NONCE));
            len=SFS_HELLO_RSP_LEN;
        } else if (f.type==SFS_T_READ) {
            static const char manifest[]="format=2\n";
            u32 off=sfs_get32(f.payload);
            u16 want=sfs_get16(f.payload+SFS_READ_COUNT_OFF);
            u16 count=0;
            while (count<want && off+count<sizeof(manifest)-1) {
                pl[SFS_STATUS_LEN+count]=(u8)manifest[off+count];
                count++;
            }
            sfs_put32(pl,count);
            len+=count;
        } else {
            if (f.type!=SFS_T_STAT) __builtin_trap();
            len+=SFS_STAT_RSP_LEN;
            pl[SFS_STATUS_LEN]=SFS_KIND_FILE;
            sfs_put32(pl+SFS_STATUS_LEN+1,server_size);
            if (restarts) {
                if (!stat_in_probe && !restart_retry) {
                    unknown=1; restart_retry=1; restarts--;
                } else if (restart_retry) {
                    restart_retry=0; stat_in_probe=1;
                } else stat_in_probe=0;
            }
            if (unknown) {
                type=SFS_T_ERR;
                sfs_put32(pl,(u32)OS32_ERR_STALE);
                unknown=0;
            }
        }
        pos=0;
        end=sfs_encode_limit(response,type,sid,f.seq,pl,len,SFS_LAN_MAX_PAYLOAD);
    }
    return 0;
}
static int get(void *ctx) { (void)ctx; return pos<end ? response[pos++] : -1; }
static u32 now(void *ctx) { (void)ctx; return hs_now(); }
static void idle(void *ctx) { (void)ctx; hs_advance(1); }
static int can_wait(void *ctx) { (void)ctx; return hs_if(); }
static int put_until(void *ctx,const u8 *buf,u32 n,u32 deadline)
{
    (void)deadline; return put(ctx,buf,n);
}
const SfsIo hs_sfs_io={put,get,now,idle,can_wait,put_until};
int res_owner_get(void) { return 1; }
struct VfsStdinRead *exec_stdin_state(void) { return 0; }
int input_getchar_kapi(void) { return -1; }
int fd_is_redirected(int fd) { (void)fd; return 0; }
int fd_redirect_read(int fd,void *b,u32 n) { (void)fd; (void)b; (void)n; return -1; }
#define CHECK(e) do { if (!(e)) return __LINE__; } while (0)
static int suite(void)
{
    static const struct vfs_device_name names[] = {
        {SERIALFS_DEVNAME, VFS_NAME_EXACT, VFS_DEV_SERIAL, 0, 1}
    };
    struct SfsPersistentConfig cfg = {&hs_sfs_io, 0, 1};
    hs_reset(0);
    CHECK(host_mailbox_init() == 0);
    vfs_device_names_register(names, 1);
    CHECK(serialfs_init() == 0 && sfs_persistent_start(&cfg) == 0);
    int old_fd=vfs_open("/host/a",O_RDONLY);
    CHECK(old_fd>=3 && open_files[old_fd].size==2048);
    unknown=1; server_size=4096;
    int first=vfs_open("/host/a",O_RDONLY);
    CHECK(first>=3 && open_files[first].size==4096);
    CHECK(open_files[first].resource_generation==sfs_persistent_generation());
    CHECK(vfs_seek(first,0,SEEK_END)==4096);
    CHECK(vfs_seek(old_fd,0,SEEK_SET)==VFS_ERR_STALE);
    vfs_close(first); vfs_close(old_fd);
    /* Each probe reconnects once; a third probe would succeed. */
    restarts=2; restart_retry=stat_in_probe=0;
    CHECK(vfs_open("/host/a",O_RDONLY)==VFS_ERR_STALE);
    CHECK(restarts==0);
    /* The resident sid expires while idle; the first manifest open/read
     * must reconnect and use the new generation. H4 tests the hsync retry. */
    restarts=0; unknown=0;
    CHECK(sfs_persistent_scope_begin()==0);
    unknown=1;
    int manifest=vfs_open("/host/.deploy/manifest.txt",O_RDONLY);
    CHECK(manifest>=3);
    CHECK(open_files[manifest].resource_generation==sfs_persistent_generation());
    char text[10]={0};
    CHECK(vfs_read_fd(manifest,text,9)==9 && kstrcmp(text,"format=2\n")==0);
    vfs_close(manifest);
    CHECK(sfs_persistent_scope_end(0)==0);
    /* Known generation changes are absorbed before the first child open. */
    u32 value=1;
    CHECK(host_mailbox_put(1,1,1,&value,sizeof(value),1)==0);
    CHECK(sfs_persistent_scope_begin()==0);
    value=2;
    CHECK(sfs_persistent_scope_end(0)==0);
    CHECK(host_mailbox_put(2,1,1,&value,sizeof(value),1)==0);
    CHECK(sfs_persistent_scope_begin()==0);
    manifest=vfs_open("/host/.deploy/manifest.txt",O_RDONLY);
    CHECK(manifest>=3);
    vfs_close(manifest);
    CHECK(sfs_persistent_scope_end(0)==0);
    return 0;
}
void _start(void)
{
    int rc=suite();
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
