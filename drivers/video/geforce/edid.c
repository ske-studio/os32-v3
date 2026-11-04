/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/* nouveau_connector.c:938-955 delegates EDID modes to DRM. This independent
 * integer parser replaces that GPL dependency; no DRM code is incorporated. */
#include "../../../hwdev/gf/display/data.h"

#define EDID_BYTES 128U
#define EDID_DTD 18U
#define NO_PREFERRED (~(u32)0)
struct known_mode { u16 w,h,refresh; struct gf4_timing t; };
/* VESA DMT v1.0 rev. 13, timing tables; CEA-861-D table 1. */
static const struct known_mode dmt[] = {
 {640,480,60,{25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V}},
 {640,480,67,{30240,640,704,768,864,480,483,486,525,GF4_NEG_H|GF4_NEG_V}},
 {640,480,72,{31500,640,664,704,832,480,489,492,520,GF4_NEG_H|GF4_NEG_V}},
 {640,480,75,{31500,640,656,720,840,480,481,484,500,GF4_NEG_H|GF4_NEG_V}},
 {800,600,56,{36000,800,824,896,1024,600,601,603,625,0}},
 {800,600,60,{40000,800,840,968,1056,600,601,605,628,0}},
 {800,600,72,{50000,800,856,976,1040,600,637,643,666,0}},
 {800,600,75,{49500,800,816,896,1056,600,601,604,625,0}},
 {832,624,75,{57284,832,864,928,1152,624,625,628,667,GF4_NEG_H|GF4_NEG_V}},
 {1024,768,87,{44900,1024,1032,1208,1264,768,768,772,817,GF4_INTERLACE}},
 {1024,768,60,{65000,1024,1048,1184,1344,768,771,777,806,GF4_NEG_H|GF4_NEG_V}},
 {1024,768,70,{75000,1024,1048,1184,1328,768,771,777,806,GF4_NEG_H|GF4_NEG_V}},
 {1024,768,75,{78750,1024,1040,1136,1312,768,769,772,800,0}},
 {1280,1024,75,{135000,1280,1296,1440,1688,1024,1025,1028,1066,0}},
 {1280,1024,60,{108000,1280,1328,1440,1688,1024,1025,1028,1066,0}},
 {1152,870,75,{100000,1152,1168,1280,1456,870,871,874,915,0}},
 {720,400,70,{28320,720,738,846,900,400,412,414,449,GF4_NEG_H}},
 {720,400,88,{35500,720,738,846,900,400,421,423,449,GF4_NEG_V}}
};
static bool same(const struct gf4_timing *a,const struct gf4_timing *b)
{
 return a->khz==b->khz && a->hd==b->hd && a->hs==b->hs && a->he==b->he && a->ht==b->ht && a->vd==b->vd && a->vs==b->vs && a->ve==b->ve && a->vt==b->vt && a->flags==b->flags;
}
static void add(struct gf4_edid_state *s,const struct gf4_timing *t,bool preferred)
{
 u32 i;
 for(i=0;i<s->ntiming;i++) if(same(&s->timing[i],t)) { if(preferred) s->preferred=i; return; }
 if(s->ntiming==GF4_TIMINGS) {
  s->truncated=true;
  if(!preferred || s->preferred!=NO_PREFERRED) return;
  i=GF4_TIMINGS-1;
 } else i=s->ntiming++;
 s->timing[i]=*t;
 if(preferred) s->preferred=i;
}
static void known(struct gf4_edid_state *s,u32 w,u32 h,u32 hz)
{
 for(u32 i=0;i<sizeof(dmt)/sizeof(dmt[0]);i++)
  if(dmt[i].w==w && dmt[i].h==h && dmt[i].refresh==hz) { add(s,&dmt[i].t,false); return; }
 /* VESA GTF 1.1, section 3: default M=600,C=40,K=128,J=20.
  * All intermediates are 32-bit; horizontal input is an 8-pixel multiple. */
 if(!s->range_valid || !hz || w<256 || h<200) return;
 if(1000000U/hz<=550U) return;
 u32 period=(1000000U/hz-550U)*1000U/(h+1U);
 if(!period) return;
 u32 vsbp=(550000U+period/2U)/period;
 u32 vt=h+1U+vsbp;
 if(300U*period/1000U>=30000U) return;
 u32 duty=30000U-(300U*period/1000U);
 if(duty>=100000U || duty<10000U) return;
 u32 blank=(w*duty/(100000U-duty)+8U)&~15U;
 u32 ht=w+blank, sync=((ht*8U/100U)+4U)&~7U;
 u32 clock=(ht*1000000U/period+125U)/250U*250U;
 if(ht>65535U || vt>65535U || blank/2U<sync || !clock) return;
 struct gf4_timing t={clock,w,w+blank/2U-sync,w+blank/2U,ht,h,h+1U,h+4U,vt,GF4_NEG_H};
 if(hz<s->min_v_hz || hz>s->max_v_hz || clock>s->max_khz ||
    clock< s->min_h_khz*ht || clock>s->max_h_khz*ht) return;
 add(s,&t,false);
}
static bool dtd(struct gf4_edid_state *s,const u8 *p,bool preferred)
{
 u32 clk=(p[0]|(u32)p[1]<<8)*10U;
 if(!clk) return false;
 u32 hd=p[2]|(u32)(p[4]&0xf0)<<4, hb=p[3]|(u32)(p[4]&15)<<8;
 u32 vd=p[5]|(u32)(p[7]&0xf0)<<4, vb=p[6]|(u32)(p[7]&15)<<8;
 u32 ho=p[8]|(u32)(p[11]&0xc0)<<2, hw=p[9]|(u32)(p[11]&0x30)<<4;
 u32 vo=(p[10]>>4)|((u32)(p[11]&12)<<2), vw=(p[10]&15)|((u32)(p[11]&3)<<4);
 if(!hd || !vd || !hb || !vb || !hw || !vw || ho+hw>hb || vo+vw>vb) return false;
 u32 flags=0;
 if((p[17]&0x18)==0x18) { if(!(p[17]&2)) flags|=GF4_NEG_H; if(!(p[17]&4)) flags|=GF4_NEG_V; }
 if(p[17]&0x80) { vd*=2; vo*=2; vw*=2; vb=vb*2+1; flags|=GF4_INTERLACE; }
 struct gf4_timing t={clk,hd,hd+ho,hd+ho+hw,hd+hb,vd,vd+vo,vd+vo+vw,vd+vb,flags};
 add(s,&t,preferred); return true;
}
static bool checksum(const u8 *p)
{
 u32 sum=0; for(u32 i=0;i<EDID_BYTES;i++) sum+=p[i]; return (sum&255U)==0;
}
static void range(struct gf4_edid_state *s,const u8 *p)
{
 if(p[0] || p[1] || p[2] || p[3]!=0xfd || p[4]) return;
 if(!p[5] || p[5]>p[6] || !p[7] || p[7]>p[8]) return;
 s->min_v_hz=p[5]; s->max_v_hz=p[6]; s->min_h_khz=p[7]; s->max_h_khz=p[8];
 s->max_khz=p[9] ? (u32)p[9]*10000U : ~(u32)0; s->range_valid=true;
}
static void base(struct gf4_edid_state *s,const u8 *p)
{
 static const u8 est[][4]={{35,7,16,0},{35,6,17,0},{35,5,0,0},{35,4,1,0},{35,3,2,0},{35,2,3,0},{35,1,4,0},{35,0,5,0},{36,7,6,0},{36,6,7,0},{36,5,8,0},{36,4,9,0},{36,3,10,0},{36,2,11,0},{36,1,12,0},{36,0,13,0},{37,7,15,0}};
 for(u32 i=54;i<126;i+=EDID_DTD) range(s,p+i);
 for(u32 i=54;i<126;i+=EDID_DTD) dtd(s,p+i,i==54 && (p[24]&2));
 for(u32 i=0;i<sizeof(est)/sizeof(est[0]);i++) if(p[est[i][0]]&(1U<<est[i][1])) add(s,&dmt[est[i][2]].t,false);
 for(u32 i=38;i<54;i+=2) {
  if(p[i]==1 && p[i+1]==1) continue;
  u32 w=((u32)p[i]+31U)*8U, aspect=p[i+1]>>6, h;
  if(aspect==0) h=(p[19]<3 ? w : w*10U/16U);
  else if(aspect==1) h=w*3U/4U; else if(aspect==2) h=w*4U/5U; else h=w*9U/16U;
  known(s,w,h,(p[i+1]&63U)+60U);
 }
}
static bool cta(struct gf4_edid_state *s,const u8 *p)
{
 u32 end=p[2]; if(!end) return true; if(end<4 || end>127) return false;
 for(u32 i=4;i<end;) { u32 n=p[i]&31U; i++; if(n>end-i) return false; i+=n; }
 for(u32 i=4;i<end;) {
  u32 tag=p[i]>>5, n=p[i]&31U; i++;
  if(n>end-i) return false;
  if(tag==2) for(u32 j=0;j<n;j++) {
   u32 vic=p[i+j]&127U;
   if(vic==1) add(s,&dmt[0].t,false);
   /* CTA-861-D table 1: VIC 4 and 16. */
   else if(vic==4) { const struct gf4_timing t={74250,1280,1390,1430,1650,720,725,730,750,0}; add(s,&t,false); }
   else if(vic==16) { const struct gf4_timing t={148500,1920,2008,2052,2200,1080,1084,1089,1125,0}; add(s,&t,false); }
  }
  i+=n;
 }
 for(u32 i=end;i+EDID_DTD<=127;i+=EDID_DTD) dtd(s,p+i,false);
 return true;
}
i32 gf4_edid_modes(const u8 block[128],u32 block_no,struct gf4_edid_state *s)
{
 static const u8 header[8]={0,255,255,255,255,255,255,0};
 if(!block || !s) return GF4_EINVAL;
 if(!block_no) {
  *s=(struct gf4_edid_state){0}; s->version=GF4_EDID_VERSION; s->size=sizeof(*s); s->preferred=NO_PREFERRED;
  for(u32 i=0;i<8;i++) if(block[i]!=header[i]) { s->last_rc=GF4_EEDID; return GF4_EEDID; }
  if(!checksum(block) || block[18]!=1 || block[19]>4) { s->last_rc=GF4_EEDID; return GF4_EEDID; }
  s->base_valid=true; s->digital=(block[20]&0x80)!=0; s->extensions=block[126]; s->next_block=1;
  base(s,block); return 0;
 }
 if(!s->base_valid || block_no!=s->next_block || block_no>s->extensions) return GF4_ESTATE;
 s->next_block++;
 if(!checksum(block)) { s->last_rc=GF4_EEDID; return GF4_EEDID; }
 if(block[0]==2 && !cta(s,block)) { s->last_rc=GF4_EEDID; return GF4_EEDID; }
 if(block[0]!=2) s->last_rc=GF4_EUNSUPPORTED;
 return 0;
}
