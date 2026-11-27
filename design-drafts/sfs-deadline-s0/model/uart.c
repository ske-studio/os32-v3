#include "uart_ring.h"
#define serial_gate_put driver_gate_put
#define serial_gate_get driver_gate_get
#include "sys/dev/char/serial.c"
#undef serial_gate_put
extern int printf(const char *, ...);
int serial_gate_put(const u8 *buf,u32 size) {
    int rc=driver_gate_put(buf,size);
    printf("put tick=%lu type=%02x rc=%d gate=%d\n",tick_count,buf[3],rc,s_gate);
    return rc;
}
#undef serial_gate_get
static int gets;
int serial_gate_get(void) { gets++; return driver_gate_get(); }
#include "sys/fs/serialfs/sfs_session_fg.c"
#include "sys/fs/serialfs/serialfs.c"
extern int printf(const char *, ...);
unsigned model_if = 1;
volatile u32 tick_count;
static int ready = 1, answer, accepted, byes;
static SfsDec wire;
static u8 rx[SFS_MAX_FRAME];
static int pos, end;
unsigned inp(unsigned port) {
    if (port == s_port_cmd) return (ready ? s_mask_txrdy : 0) | (pos < end ? s_mask_rxrdy : 0);
    if (port == s_port_data && pos < end) return rx[pos++];
    __builtin_trap();
}
void outp(unsigned port, unsigned value) {
    SfsFrame f;
    if (port != s_port_data) __builtin_trap();
    if (sfs_dec_feed(&wire, value, &f) != 1) return;
    accepted++;
    byes += f.type == SFS_T_BYE;
    printf("tick=%lu tx=%02x gate=%d\n",tick_count,f.type,s_gate);
    if (answer && f.type == SFS_T_HELLO) {
        u8 pl[SFS_HELLO_RSP_LEN] = {0};
        sfs_put16(pl+SFS_HELLO_RSP_VER,SFS_VERSION);
        sfs_put16(pl+SFS_HELLO_RSP_MAX,SFS_MAX_PAYLOAD);
        sfs_put32(pl+SFS_HELLO_RSP_NONCE,sfs_get32(f.payload+SFS_HELLO_REQ_NONCE));
        pos=0; end=sfs_encode(rx,SFS_T_HELLO|SFS_T_RESP,7,0,pl,sizeof(pl));
    }
}
void _halt(void) {tick_count++;}
void idle_wait_step(void) {tick_count++;}
void cpu_delay_us(u32 us) {tick_count++;}
int res_owner_get(void) {return APP_ID_SHELL;}
int sysconfig_get_int(const char *p,const char *k,int d) {return d;}
void kprintf(u8 attr,const char *fmt,...) {}
int sys_time_now(u32 *lo,u32 *hi) {*lo=tick_count*10000; *hi=0; return 0;}
const SfsClient *sfs_persistent_client(void) {return 0;}
const char *vfs_fstype(const char *p) {return g_mounted ? SERIALFS_NAME : "";}
int vfs_mount(const char *p,const char *d,const char *f) {return sfs_mount(VFS_DEV_SERIAL << 8) ? 0 : -1;}
void vfs_umount(const char *p) {sfs_umount(&g_cli);}
static void report(const char *name,int rc) {
    printf("%s tick=%lu rc=%d gate=%d retries=%lu accepted=%d bye=%d dead=%u hold=%lu rx=%d/%d gets=%d\n",
      name,tick_count,rc,s_gate,g_cli.resends,accepted,byes,g_cli.dead,s_hold_count,pos,end,gets);
    accepted=byes=gets=0;
}
int main(void) {
    ser_initialized=1; s_setup.actual=9600;
    s_tx_budget_ticks=serial_tx_budget_ticks(9600);
    sfs_dec_reset(&wire);
    printf("trial_ticks=%lu\n",sfs_trial_ticks_limit(9600,SFS_MAX_PAYLOAD));
    report("silent hello",sfs_session_fg.begin());
    ready=0; report("stalled hello",sfs_session_fg.begin());
    ready=answer=1; report("reacquire",sfs_session_fg.begin());
    if (!g_mounted) return 1;
    report("silent call",sfs_path_op(&g_cli,SFS_T_MKDIR,"a"));
    report("ordinary end",sfs_session_fg.end(0));
    report("begin",sfs_session_fg.begin());
    ready=0; report("stalled call",sfs_path_op(&g_cli,SFS_T_MKDIR,"a"));
    ready=1; g_cli.seq=SFS_SEQ_LAST;
    report("exhausted call",sfs_path_op(&g_cli,SFS_T_MKDIR,"a"));
    report("exhausted end",sfs_session_fg.end(0));
    return 0;
}
