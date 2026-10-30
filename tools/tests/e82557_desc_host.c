/* Independent byte oracles for the LB3 descriptor and RFD ring contract. ILP32/qemu. */
#include "../../drivers/net/e82557/e82557_desc.c"
STATIC_ASSERT(sizeof(u32) == 4, ilp32);
static int failed;
#define CHECK(x) do { if (!(x)) failed++; } while (0)
static void fill(u8 *p, u32 n, u8 v) { while (n--) *p++ = v; }
static int same(const u8 *a, const u8 *b, u32 n)
{ while (n--) if (*a++ != *b++) return 0; return 1; }
static int number(const char *s)
{ int n=0; while (*s) n=n*10+*s++-'0'; return n; }
int main(int argc, char **argv)
{
    int which=argc>1?number(argv[1]):0;
    u8 cb[1600], frame[1519], cfg[22], loop[22], cus, rus, stat;
    const u8 normal[]={0x16,8,0,0,0,0x80,0x32,3,1,0,0x2e,0,0x60,0,0xf2,0x48,0,0x40,0xf2,0x80,0x3f,5};
    const u8 mac[]={0,0,0x4c,7,0x21,0xe};
    fill(cb,sizeof(cb),0xcc);
    for (u32 i=0;i<sizeof(frame);i++) frame[i]=(u8)(i*7+3);
    if (which==0) {
        for (u8 cu=0;cu<8;cu++) for (u8 ru=0;ru<8;ru++) CHECK(e82557_scb_cmd(cu,ru)==cu*16+ru);
    } else if (which==1) {
        for (u32 s=0;s<=0xffff;s++) {
            e82557_scb_decode((u16)s,&cus,&rus,&stat);
            CHECK(cus==(s/64)%4 && rus==(s/4)%16 && stat==s/256);
        }
    } else if (which==2) {
        e82557_wr16(cb+1,0x1234); e82557_wr32(cb+3,0x89abcdef);
        const u8 expected[]={0xcc,0x34,0x12,0xef,0xcd,0xab,0x89,0xcc};
        CHECK(same(cb,expected,8));
        CHECK(e82557_rd16(cb+1)==0x1234 && e82557_rd32(cb+3)==0x89abcdef);
    } else if (which>=3 && which<=10) {
        int bits=which-3;
        e82557_cb_header(cb,2,bits&1,bits&2,bits&4,0x12345678);
        const u8 expected[]={0,0,2,(u8)((bits&1?0x80:0)|(bits&2?0x40:0)|(bits&4?0x20:0)),0x78,0x56,0x34,0x12,0xcc};
        CHECK(same(cb,expected,9));
    } else if (which==11) {
        e82557_cb_ia(cb,mac);
        const u8 header[]={0,0,1,0x80,0xff,0xff,0xff,0xff};
        CHECK(same(cb,header,8) && same(cb+8,mac,6) && cb[14]==0xcc);
    } else if (which==12 || which==13) {
        e82557_config_bytes(cfg,0); e82557_config_bytes(loop,1);
        CHECK(same(cfg,normal,22));
        for (u32 i=0;i<22;i++) CHECK(loop[i]==(i==10?0x6e:normal[i]));
        e82557_cb_config(cb,which==12?cfg:loop);
        const u8 header[]={0,0,2,0x80,0xff,0xff,0xff,0xff};
        CHECK(same(cb,header,8) && same(cb+8,which==12?cfg:loop,22) && cb[30]==0xcc);
    } else if (which>=14 && which<=18) {
        const u32 lengths[]={14,60,1518,13,1519}; u32 n=lengths[which-14];
        int rc=e82557_tcb_build(cb,frame,n,0x10,0x76543210,1);
        if (which>=17) {
            CHECK(rc<0); for (u32 i=0;i<sizeof(cb);i++) CHECK(cb[i]==0xcc);
        } else {
            const u8 expected[]={0,0,4,0xa0,0x10,0x32,0x54,0x76,0xff,0xff,0xff,0xff,(u8)n,(u8)(0x80+(n>>8)),0x10,0};
            CHECK(rc==0 && same(cb,expected,16) && same(cb+16,frame,n) && cb[16+n]==0xcc);
        }
    } else if (which>=19 && which<=25) {
        struct e82557_stop stop={1,0}; u32 before=0; u16 st=0;
        if (which==20) stop.reset_seq=0; /* no reset */
        if (which==21) stop.start_seq=1; /* CU Start after reset, transient Idle */
        if (which==22) before=1; /* old reset, not this stop procedure */
        if (which==23) st=0x40;
        if (which==24) st=0x10;
        if (which==25) st=0xffff;
        CHECK(e82557_stop_evidence(&stop,before,st)==(which==19));
    } else if (which==26) {
        cb[0]=0x34; cb[1]=0x80; CHECK(e82557_rd16(cb)==0x8034);
        cb[1]=0xa0; CHECK(e82557_rd16(cb)==0xa034);
    } else if (which==27) {
        CHECK(e82557_mdi_ctl(2,1,1,0)==0x08210000);
        CHECK(e82557_mdi_ctl(1,1,1,0xabcd)==0x0421abcd);
    } else if (which==28) {
        CHECK(e82557_mdi_ctl(2,31,31,0xffff)==0x0bffffff);
        CHECK(e82557_mdi_ctl(1,63,95,0x1234)==0x07ff1234);
        CHECK(e82557_mdi_ctl(2,2,5,0)==0x08450000);
        CHECK(e82557_mdi_ctl(2,63,0,0)==0x0be00000);
        CHECK(e82557_mdi_ctl(2,0,63,0)==0x081f0000);
    } else if (which==29) {
        CHECK(e82557_mdi_ready(0x10000000));
        CHECK(!e82557_mdi_ready(0x08000000));
        CHECK(!e82557_mdi_ready(0));
    } else if (which==30) {
        CHECK(e82557_mdi_data(0xdeadbeef)==0xbeef);
    } else if (which==31) {
        CHECK(!e82557_phy_id_valid(0) && !e82557_phy_id_valid(0xffff));
        CHECK(e82557_phy_id_valid(0x2000) && e82557_phy_id_valid(1));
    } else if (which==32) {
        int link,done,en;
        e82557_phy_decode(0x1000,0x24,&link,&done,&en); CHECK(link && done && en);
        e82557_phy_decode(0,0,&link,&done,&en); CHECK(!link && !done && !en);
    } else if (which==42) {
        CHECK(e82557_link_wait_step(1,1,1,0,300)==E82557_DONE);
        CHECK(e82557_link_wait_step(1,0,0,0,300)==E82557_DONE);
        CHECK(e82557_link_wait_step(1,0,1,0,300)==E82557_WAIT);
    } else if (which==43) {
        CHECK(e82557_link_wait_step(0,1,1,299,300)==E82557_WAIT);
        CHECK(e82557_link_wait_step(0,1,1,300,300)==E82557_TIMEOUT);
        CHECK(e82557_link_wait_step(1,1,1,300,300)==E82557_DONE);
    } else if (which>=50 && which<=62) {
        u8 ring[8*1552]; struct e82557_rx rx; int rnr=0;
        fill(ring,sizeof(ring),0xcc); e82557_rx_init(ring,0x2e8650,&rx);
        CHECK(rx.cons==0 && rx.dev==0 && rx.ready==0);
        for(u32 i=0;i<8;i++) {
            u8 *r=ring+i*1552;
            CHECK(e82557_rd16(r)==0 && e82557_rd16(r+12)==0);
            CHECK(e82557_rd16(r+2)==(i==7?0x8000:0));
            CHECK(e82557_rd32(r+4)==0x2e8650+((i+1)%8)*1552);
            CHECK(e82557_rd32(r+8)==0xffffffffu && e82557_rd16(r+14)==1536);
        }
        if(which>=51 && which<=58) {
            u32 n=(u32)which-50;
            for(u32 i=0;i<n;i++) { e82557_wr16(ring+i*1552,0xa000); e82557_wr16(ring+i*1552+12,0xc03c); }
            e82557_rx_scan(ring,&rx,&rnr); CHECK(rx.ready==n && rx.dev==n%8);
            for(u32 i=0;i<n;i++) e82557_rfd_recycle(ring,&rx);
            CHECK(rx.ready==0 && rx.cons==n%8 && rx.dev==n%8);
            for(u32 i=0;i<8;i++) CHECK(e82557_rd16(ring+i*1552+2)==(i==(n+7)%8?0x8000:0));
        }
        if(which==59) {
            for(u32 i=0;i<8;i++) e82557_wr16(ring+i*1552,0xa000);
            e82557_rx_scan(ring,&rx,&rnr); CHECK(rx.ready==8 && rx.dev==rx.cons);
            e82557_rx_reown(ring,&rx);
            for(u32 i=0;i<8;i++) CHECK(e82557_rd16(ring+i*1552)==0xa000);
        }
        if(which==60) {
            e82557_wr16(ring,0x8200); e82557_rx_scan(ring,&rx,&rnr); CHECK(rnr && rx.ready==1);
        }
        if(which==61) {
            e82557_wr16(ring,0xa000); e82557_wr16(ring+12,0xc03c);
            e82557_rx_scan(ring,&rx,&rnr); e82557_rx_reown(ring,&rx);
            CHECK(e82557_rd16(ring)==0xa000 && e82557_rd16(ring+12)==0xc03c);
        }
        if(which==62) {
            e82557_wr16(ring,0xa000); e82557_wr16(ring+12,0xc03c);
            e82557_rx_scan(ring,&rx,&rnr); e82557_rfd_recycle(ring,&rx);
            CHECK(e82557_rd16(ring)==0 && e82557_rd16(ring+12)==0);
        }
    } else CHECK(0);
    return !!failed;
}
__asm__(".global _start\n_start:\n"
        "mov (%esp), %eax\nlea 4(%esp), %edx\npush %edx\npush %eax\ncall main\n"
        "mov %eax, %ebx\nmov $1, %eax\nint $0x80\n");
