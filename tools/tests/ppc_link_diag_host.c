#include "link_diag.c"
static void die(int code) { __asm__ volatile("int $0x80"::"a"(1),"b"(code)); __builtin_unreachable(); }
#define REQUIRE(x) do { if (!(x)) die(__LINE__); } while (0)
void *memcpy(void *p, const void *s, u32 n) { u8 *a=p; const u8 *b=s; while(n--) *a++=*b++; return p; }
void *kmemset(void *p,int c,u32 n) { u8 *a=p; while(n--) *a++=c; return p; }
void *memset(void *p,int c,u32 n) { return kmemset(p,c,n); }
int memcmp(const void *a,const void *b,u32 n) { const u8 *p=a,*q=b; while(n--) { if(*p!=*q) return *p-*q; p++;q++; } return 0; }
static int eq(const char *a,const char *b) { while(*a && *a==*b) { a++;b++; } return *a==*b; }
u32 link_hello_ok=1;
u8 link_peer_mac[6]={2,0,0,0,0,1};
static struct HostAlive alive={7,1,1};
static HostNotice mailbox[16];
static u32 head, tail, staged, commits, opens, sent, declared, read_pos;
static int active_handle, is_ready, fail_status, open_fail, write_wait, copies, clocks;
static char line[128];
static u8 output[256];
static const char *ready_body="gen 7 svc 65532\n";
void host_alive_snapshot(struct HostAlive *a) { *a=alive; }
void boot_token_get(u8 b[16]) { for(u32 i=0;i<16;i++) b[i]=i; }
int host_mailbox_stage(u32 g,u32 svc,HostNotice *n) {
    REQUIRE(g==alive.host_generation && svc==65532); staged++;
    if(head==tail) return OS32_ERR_AGAIN;
    *n=mailbox[head];return 0;
}
void host_mailbox_commit(u32 g,u32 serial) {
    REQUIRE(g==alive.host_generation && serial==mailbox[head].serial); head++;commits++;
}
i32 link_host_open(const char *r,u32 n,int owner) {
    REQUIRE(!owner && !active_handle && n<sizeof(line));
    if(open_fail) {open_fail=0;return OS32_ERR_AGAIN;}
    memcpy(line,r,n);line[n]=0;opens++;active_handle=1;
    is_ready=!memcmp(line,"RSH READY",9);read_pos=sent=0;declared=0;
    if(!is_ready) {
        REQUIRE(!memcmp(line,"PDIAG OUT 000102030405060708090a0b0c0d0e0f ",43));
        u32 start=n;while(start && line[start-1]!=' ') start--;
        for(u32 i=start;i<n;i++) {REQUIRE(line[i]>='0' && line[i]<='9');declared=declared*10+line[i]-'0';}
        REQUIRE(declared==result.payload_bytes);
    }
    return 4;
}
i32 link_host_status(i32 h,u32 *s,u32 *n,int o) {
    (void)n;REQUIRE(h==4 && active_handle && !o);*s=fail_status?503:200;
    if(is_ready || fail_status || sent==declared) return 0;
    return OS32_ERR_AGAIN;
}
i32 link_host_read_stage(i32 h,u32 cap,const u8 **p,int o) {
    REQUIRE(h==4 && !o && is_ready);
    u32 n=0;while(ready_body[n]) n++;
    if(read_pos==n) return 0;
    u32 take=n-read_pos;if(take>3) take=3;
    REQUIRE(take<=cap);*p=(const u8 *)ready_body+read_pos;read_pos+=take;return take;
}
i32 link_host_write(i32 h,const void *p,u32 n,int o) {
    REQUIRE(h==4 && !o && n==declared-sent);
    if(write_wait) {write_wait=0;return OS32_ERR_AGAIN;}
    if(n>7) n=7;
    REQUIRE(sent+n<=sizeof(output));memcpy(output+sent,p,n);sent+=n;return n;
}
i32 link_host_close(i32 h,int o) { REQUIRE(h==4 && active_handle && !o);active_handle=0;return 0; }
static void clock_cb(u32 *t,u32 *hz,u64 *tb,u64 *us) { clocks++;*t=1;*hz=2;*tb=3;*us=4; }
static int translate_cb(u32 va,u32 *pa) { *pa=va;return 0; }
static int copy_cb(u32 va,u8 *p) { copies++;*p=(u8)va;return 0; }
static u32 status_words[]={0x12345678,0xaabbccdd}, memory_words[]={0x87654321};
static u8 diag[256];
static struct pm_lan_range ranges[]={{0x1000,0x200}};
static struct pm_lan_diag diagnostic={status_words,2,memory_words,1,diag,ranges,1,clock_cb,translate_cb,copy_cb};
static u64 now;
static void poll(void) { u32 before=commits;pm_link_diag_poll(&diagnostic,++now,1000);REQUIRE(commits-before<=1); }
static void handshake(void) {
    for(int i=0;i<30 && !ready;i++) poll();
    REQUIRE(ready && handle<0);
}
static void enqueue(const char *s,int kind) {
    REQUIRE(tail<16);HostNotice *n=&mailbox[tail];kmemset(n,0,sizeof(*n));
    n->serial=tail+1;n->host_generation=alive.host_generation;n->service=65532;n->kind=kind;
    while(*s) n->data[n->length++]=*s++;
    tail++;
}
static void finish(u32 expected,u32 length) {
    REQUIRE(pending && result.status==expected && result.payload_bytes==length);
    for(int i=0;i<100 && pending;i++) poll();
    REQUIRE(!pending && sent==declared && declared==length && handle<0);
}
static void run(const char *s,u32 expected,u32 length) { enqueue(s,1);poll();finish(expected,length); }
void _start(void) {
    for(u32 i=0;i<256;i++) diag[i]=i;
    open_fail=1;poll();REQUIRE(!ready && !opens);now=2001;handshake();
    REQUIRE(eq(line,"RSH READY 000102030405060708090a0b0c0d0e0f diag 1 none idle"));
    enqueue("00000001 1\n",1);enqueue("00000002 2\n",1);u32 before=commits;
    poll();REQUIRE(commits==before+1);write_wait=1;finish(0,8);
    REQUIRE(output[0]==0x12 && output[7]==0xdd);poll();finish(0,24);
    REQUIRE(clocks==1 && output[3]==1 && output[23]==4);
    run("00000003 3\n",0,4);REQUIRE(output[0]==0x87 && output[3]==0x21);
    run("00000004 4 00800080\n",0,128);REQUIRE(output[0]==128 && output[127]==255);
    run("00000005 5 0000100001000000\n",0,256);REQUIRE(copies==256 && output[255]==255);
    run("00000005 5 0000100001000000\n",4,0);REQUIRE(copies==256);
    run("00000004 2\n",5,0);REQUIRE(clocks==1);
    run("00000006 5 000011ff00020000\n",3,0);REQUIRE(copies==256);
    run("00000007 4 00000081\n",3,0);
    run("00000008 5 0000100001010000\n",3,0);
    run("00000009 1 00\n",2,0);
    run("0000000a 5 xyz\n",2,0);
    run("0000000b 65535\n",1,0);
    run("0000000c 2\n",0,24);REQUIRE(clocks==2);
    /* Same host across a disconnect retains the monotonic ID guard. */
    link_hello_ok=0;poll();link_hello_ok=1;handshake();
    head=tail=0;run("0000000c 2\n",4,0);REQUIRE(clocks==2);
    /* New generation resets IDs but is usable only after matching READY. */
    alive.host_generation=8;ready_body="gen 7 svc 65532\n";poll();
    for(int i=0;i<10;i++) poll();
    REQUIRE(!ready);
    ready_body="gen 8 svc 65532\n";now+=2000;handshake();
    run("00000001 2\n",0,24);REQUIRE(clocks==3);
    before=opens;enqueue("",2);poll();poll();REQUIRE(opens==before+1);handshake();
    run("00000002 2",2,0);REQUIRE(clocks==3);
    /* Reject corrupt/opcode-overflow/odd-hex/multiple-line notification bodies. */
    const char *bad[]={"badid000 2\n","00000003 65536\n","00000003 5 0\n",
                       "00000003 1\n2\n","00000003 1 \n","00000003 5 00zz\n"};
    for(u32 i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        HostNotice n={0};u32 id;struct pm_diag_message q;
        while(bad[i][n.length]) {n.data[n.length]=bad[i][n.length];n.length++;}
        REQUIRE(!decode(&n,&id,&q));
    }
    die(0);
}
