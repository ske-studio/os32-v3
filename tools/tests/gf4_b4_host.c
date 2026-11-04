/* EDID 1.4 sections 3.4, 3.10, 3.10.2 and CTA-861-D section 7;
 * independent byte fixtures, linked with edid.c, A1 UNIT 4. */
#define GF4_HOST_UNIT 4
#include "gf4_display_fake.h"
static struct gf4_edid_state state;
static u8 b[128];
enum { HEADER_END=8, CHECKSUM=127, EXTENSIONS=126, DTD=54,
       CTA_TAG=2, CTA_DATA_END=6, CTA_VIDEO_ONE=0x41, CTA_VIC_720P60=4 };
static void seal(void)
{
 u32 sum=0; b[CHECKSUM]=0;
 for(u32 i=0;i<CHECKSUM;i++) sum+=b[i];
 b[CHECKSUM]=(u8)(0U-sum);
}
static void setup(void)
{
 gf2_host_memset(b,0,sizeof(b));
 for(u32 i=1;i<HEADER_END-1;i++) b[i]=255;
 b[18]=1; b[19]=4; b[20]=0x80; b[24]=2; b[EXTENSIONS]=2;
 for(u32 i=38;i<54;i+=2) b[i]=b[i+1]=1;
 /* VESA DMT 640x480@60, 25.175 MHz encoded in 10 kHz units. */
 b[DTD]=0xd5; b[DTD+1]=0x09; b[DTD+2]=0x80; b[DTD+3]=0xa0;
 b[DTD+4]=0x20; b[DTD+5]=0xe0; b[DTD+6]=0x2d; b[DTD+7]=0x10;
 b[DTD+8]=16; b[DTD+9]=96; b[DTD+10]=0xa2; b[DTD+17]=0x18;
 seal();
}
static void tests(void)
{
 setup(); assert(gf4_edid_modes(b,0,&state)==0);
 assert(state.base_valid && state.digital && state.ntiming==1 && state.preferred==0);
 assert(state.timing[0].hd==640 && state.timing[0].ht==800 && state.timing[0].vd==480 && state.timing[0].vt==525);
 assert(state.timing[0].khz==25170 && state.timing[0].flags==(GF4_NEG_H|GF4_NEG_V));
 assert(gf4_edid_modes(b,2,&state)==GF4_ESTATE && state.next_block==1);
 gf2_host_memset(b,0,sizeof(b)); b[0]=CTA_TAG; b[2]=CTA_DATA_END;
 b[4]=CTA_VIDEO_ONE; b[5]=CTA_VIC_720P60; seal();
 assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==2);
 assert(state.timing[1].khz==74250 && state.timing[1].ht==1650 && state.timing[1].vt==750);
 gf2_host_memset(b,0,sizeof(b)); b[0]=CTA_TAG; b[2]=5; b[4]=CTA_VIDEO_ONE; seal();
 assert(gf4_edid_modes(b,2,&state)==GF4_EEDID && state.next_block==3 && state.ntiming==2);
 setup(); b[0]=1; seal(); assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && !state.base_valid);
 setup(); b[10]++; assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && !state.base_valid);
}
void _start(void) { tests(); gf2_host_exit(0); }
