#include "link_log.c"
static int opens, completions, closes, writes, status_rc = OS32_ERR_AGAIN;
static u32 response = 200;
u32 link_hello_ok = 1;
static struct HostAlive observation = {1, 1, 1};
static char request[160];
static void fail(void) { __asm__ volatile("int $0x80"::"a"(1),"b"(10)); __builtin_unreachable(); }
#define REQUIRE(x) do { if (!(x)) fail(); } while (0)
void host_alive_snapshot(struct HostAlive *a) { *a = observation; }
void boot_token_get(u8 b[16]) { for (int i=0;i<16;i++) b[i]=(u8)i; }
int pm_lan_log_peek(const u8 **p,u32 *n,u64 *s,u64 *r,u64 *b,u32 *f) {
    static const u8 payload[]="abc";
    *p=payload;*n=3;*s=(u64)completions;*r=0;*b=0;*f=0;return LAN_OK;
}
void pm_lan_log_complete(int r) { REQUIRE(r==LAN_OK); completions++; }
i32 link_host_open(const char *p,u32 n,int o) {
    REQUIRE(!o && n<sizeof(request));
    for(u32 i=0;i<n;i++) request[i]=p[i];
    request[n]=0;opens++;return 7;
}
i32 link_host_status(i32 h,u32 *s,u32 *n,int o) {
    (void)n;REQUIRE(h==7 && !o);*s=response;return status_rc;
}
i32 link_host_write(i32 h,const void *p,u32 n,int o) {
    const char *b=p;REQUIRE(h==7 && !o && n==3 && b[0]=='a' && b[2]=='c');writes++;return (i32)n;
}
i32 link_host_close(i32 h,int o) { REQUIRE(h==7 && !o);closes++;return 0; }
static void equal(const char *a,const char *b) { while(*a && *a==*b) {a++;b++;}REQUIRE(*a==*b); }
static void finish(void) { status_rc=0;pm_link_log_poll();status_rc=OS32_ERR_AGAIN; }
void _start(void) {
    pm_link_log_poll();REQUIRE(opens==1 && !completions && !writes);
    equal(request,"LOG 000102030405060708090a0b0c0d0e0f 0 0 0 0 3");
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(request),"d"(sizeof("LOG 000102030405060708090a0b0c0d0e0f 0 0 0 0 3")-1):"memory");
    pm_link_log_poll();REQUIRE(writes==1 && !completions);
    response=503;finish();REQUIRE(!completions && closes==1);
    pm_link_log_poll();equal(request,"LOG 000102030405060708090a0b0c0d0e0f 0 0 0 0 3");
    status_rc=OS32_ERR_STALE;pm_link_log_poll();REQUIRE(!completions);
    status_rc=OS32_ERR_AGAIN;pm_link_log_poll();
    observation.host_generation=2;pm_link_log_poll();
    equal(request,"LOG 000102030405060708090a0b0c0d0e0f 0 1 3 0 3");
    REQUIRE(!completions && lost_records==1);
    link_hello_ok=0;pm_link_log_poll();pm_link_log_poll();REQUIRE(lost_records==2);
    link_hello_ok=1;pm_link_log_poll();pm_link_log_poll();
    response=200;int before=opens;finish();REQUIRE(completions==1 && opens==before);
    pm_link_log_poll();REQUIRE(opens==before+1 && completions==1);
    __asm__ volatile("int $0x80"::"a"(1),"b"(0));__builtin_unreachable();
}
