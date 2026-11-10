#include "platform/powermac/lan_boot.h"
static void out(const char *s, int n) { __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n)); }
void *memset(void *p, int c, u32 n) { u8 *q=p; while(n--) *q++=(u8)c; return p; }
static void fail(int n) { char s[]="FAIL 00\n"; s[5]='0'+n/10; s[6]='0'+n%10; out(s,8); __asm__ volatile("int $0x80" : : "a"(1), "b"(n)); __builtin_unreachable(); }
#define CHECK(x,n) do { if (!(x)) fail(n); } while (0)
void _start(void)
{
    struct lan_message m, d;
    u8 b[400], golden[83] = {
      0x50,0x4c,0x4e,0x31,1,0,0,80,
      0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
      1,2,3,4,5,6,7,8, 17,18,19,20,21,22,23,24,
      33,34,35,36,37,38,39,40, 0x31,0x32,0x33,0x34,
      0x41,0x42,0x51,0x52,0,3,0x61,0x62,0,0,0,0,
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0xaa,0xbb,0xcc };
    u8 wire[336] = {
      0x50,0x4c,0x4e,0x31,1,3,0,80,
      0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
      1,2,3,4,5,6,7,8, 17,18,19,20,21,22,23,24,
      33,34,35,36,37,38,39,40, 0x31,0x32,0x33,0x34,
      0x41,0x42,0x51,0x52,0,0,0x61,0x62,0,0,0,0,
      0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,
      0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f };
    u32 i, t, used;
    memset(&m,0,sizeof(m));
    for(i=0;i<16;i++) m.boot_id[i]=(u8)i;
    m.seq=0x0102030405060708ULL; m.drop_records=0x1112131415161718ULL;
    m.drop_bytes=0x2122232425262728ULL; m.request_id=0x31323334;
    m.opcode=0x4142; m.status=0x5152; m.payload_bytes=3; m.flags=0x6162;
    m.payload[0]=0xaa; m.payload[1]=0xbb; m.payload[2]=0xcc;
    out("REACHED\n",8);
    for(t=1;t<=4;t++) {
        m.type=(u8)t; golden[5]=(u8)t;
        for(i=0;i<16;i++) m.host_nonce[i]=golden[64+i]=(t>=3 ? (u8)(0x80+i) : 0);
        CHECK(pm_lan_encode(b,sizeof(b),&m,&used)==0 && used==83,1);
        for(i=0;i<83;i++) CHECK(b[i]==golden[i],2);
        CHECK(pm_lan_decode(b,83,&d)==0,3);
        CHECK(d.seq==m.seq && d.drop_records==m.drop_records && d.drop_bytes==m.drop_bytes && d.request_id==m.request_id && d.opcode==m.opcode && d.status==m.status && d.flags==m.flags && d.type==m.type && d.payload_bytes==3,4);
        for(i=0;i<16;i++) CHECK(d.boot_id[i]==m.boot_id[i] && d.host_nonce[i]==m.host_nonce[i],5);
        for(i=0;i<3;i++) CHECK(d.payload[i]==m.payload[i],6);
    }
    CHECK(pm_lan_encode(b,79,&m,&used)<0 && pm_lan_encode(b,82,&m,&used)<0,7);
    m.type=0; CHECK(pm_lan_encode(b,400,&m,&used)<0,8);
    m.type=5; CHECK(pm_lan_encode(b,400,&m,&used)<0,9);
    m.type=3; m.payload_bytes=257; CHECK(pm_lan_encode(b,400,&m,&used)<0,10);
    m.payload_bytes=3; m.type=1; CHECK(pm_lan_encode(b,400,&m,&used)<0,11);
    m.type=3; CHECK(pm_lan_encode(b,400,&m,&used)==0,12);
    CHECK(pm_lan_decode(b,82,&d)<0,13);
    CHECK(pm_lan_decode(b,79,&d)<0,14);
    CHECK(pm_lan_decode(b,90,&d)==0,15);
    b[0]^=1; memset(&d,0xff,sizeof(d)); CHECK(pm_lan_decode(b,83,&d)<0,16);
    for(i=0;i<sizeof(d);i++) CHECK(((u8 *)&d)[i]==0,17);
    b[0]^=1; b[4]=2; CHECK(pm_lan_decode(b,83,&d)<0,18); b[4]=1;
    b[7]=79; CHECK(pm_lan_decode(b,83,&d)<0,19); b[7]=80;
    b[60]=1; CHECK(pm_lan_decode(b,83,&d)<0,20); b[60]=0;
    b[56]=1; b[57]=1; CHECK(pm_lan_decode(b,400,&d)<0,21);
    for(t=0;t<=5;t+=5) {
        wire[5]=(u8)t; memset(&d,0xff,sizeof(d));
        CHECK(pm_lan_decode(wire,80,&d)<0,22);
        for(i=0;i<sizeof(d);i++) CHECK(((u8 *)&d)[i]==0,23);
    }
    wire[5]=3;
    for(i=0;i<256;i++) wire[80+i]=(u8)(i^0xa5);
    for(t=0;t<=256;t+=256) {
        wire[56]=(u8)(t>>8); wire[57]=(u8)t;
        memset(&d,0xff,sizeof(d));
        CHECK(pm_lan_decode(wire,80+t,&d)==0,24);
        CHECK(d.type==3 && d.seq==0x0102030405060708ULL && d.drop_records==0x1112131415161718ULL && d.drop_bytes==0x2122232425262728ULL && d.request_id==0x31323334 && d.opcode==0x4142 && d.status==0x5152 && d.payload_bytes==t && d.flags==0x6162,25);
        for(i=0;i<16;i++) CHECK(d.boot_id[i]==i && d.host_nonce[i]==0x80+i,26);
        for(i=0;i<256;i++) CHECK(d.payload[i]==(t ? (u8)(i^0xa5) : 0),27);
    }
    out("PASS\n",5); __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); __builtin_unreachable();
}
