/* Execute all 15 pacing trials on indexed VGA registers and a byte LFB.
 * Faults model success, pacing-dependent losses, no survivors, and
 * unrelated moving losses. They test observation, not the hardware hypothesis. */
#include "hwb_fake.h"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/hwb_log.c"
static const char *script = SCRIPT_DATA;
static struct hwb_state state;
static struct hwb_program program;
static char source[HWB_FILE_BYTES];
static u8 memory[4096], cr[256], sr[256], gr[256], misc, plla, pllb, mclka, mclkb, hidden;
static u32 ci, si, gi, dacreads, trial, fault, pal, initialized, restored, disabled, logs, events;
static u32 starts, ends, badlogs, before, after, want_start[4096], want_end[4096], ranges, bad;
static u32 depthlogs, buslogs, newlogs, collogs, misses, cleanup, returning;
static u32 variant, writing, stores, pending, pending_off, pending_width, blockcalls, depthcalls;
static u32 seed_for(u32 t) {
    u32 d=t*0x01010101U;
    return t%3==0?(0x41+d)&255:t%3==1?(0x4141+d)&65535:0x41414141+d;
}
static int has(const char *s,const char *p) {
    for(;*s;s++) {u32 n=0;while(p[n]&&p[n]==s[n])n++;if(!p[n])return 1;}return 0;
}
static u32 value(const char *s) {
    while(*s && !(s[0]=='v' && s[1]=='a' && s[2]=='l' && s[3]=='='))s++;
    CHECK(*s);s+=4;CHECK(s[0]=='0' && s[1]=='x');s+=2;u32 v=0;
    while(*s && *s!=' ') {v=(v<<4)|(*s<='9'?*s-'0':*s-'A'+10);s++;}return v;
}
static void check_depth(u32 d) {
    CHECK(cr[0x38]==(d==0?0U:d==1?5U:9U));
    CHECK(cr[0x23]==5U); /* trident_dac.c:619-627: New32 bit7 only on XP-class, never on the 9682. */
    CHECK(cr[0x29]==(d==2?0xD7U:0xC7U) && cr[0x13]==(d==0?0x50U:d==1?0xA0U:0x40U));
    CHECK(hidden==(d==0?0U:d==1?0x30U:0xD0U));
    CHECK(gr[15]==(d==2?0xBAU:0xB2U) && gr[5]==0x40 && gr[6]==5);
    CHECK(sr[13]==0x20 && sr[4]==0x0E);
    CHECK(plla==(d==2?(pal?0x75U:0x77U):(pal?0xA4U:0xE5U)));
    CHECK(pllb==(d==2?(pal?0x1AU:0x18U):(pal?0x0EU:0x1EU)));
    CHECK(cr[0x39]==1);
}
static int survives(u32 n,u32 count,u32 w) {
    if(!fault || (fault==1 && (trial<12 && variant!=0)))return 1;
    (void)w;
    if(fault==2)return 0;
    return n<2 || (n>=8+trial && n<12+trial) || n==count-1;
}
static int access(void *ctx,struct hw_req *r) {
    (void)ctx;events++;r->status=HW_OK;
    if(pending && r->space!=HW_PCI) {
        CHECK(r->action==HW_READ && r->width==pending_width && r->addr==pending_off);
        pending=0;
    }
    if(r->space==HW_PCI) {r->value=r->addr==0x10?0x20000000:0x20400000;return 0;}
    if(r->space==HW_IO) {if(r->addr==0xFAC && r->value==0){CHECK(cleanup);disabled++;}return 0;}
    CHECK(r->space==HW_PHYS);
    if(r->addr>=0x20400000) {
        u32 a=r->addr-0x20400000;
        if(r->action==HW_READ) {
            r->value=(a==0x3D5 || a==0x3B5)?cr[ci]:a==0x3C5?sr[si]:a==0x3CF?gr[gi]:
                     a==0x3CC?misc:a==0x43C8?plla:a==0x43C9?pllb:
                     a==0x43C6?mclka:a==0x43C7?mclkb:0;
            if(a==0x3C8)dacreads=0;
            if(a==0x3C6 && ++dacreads==5)r->value=hidden;
            return 0;
        }
        for(u32 k=0;k<r->width;k++) {
            u32 v=(r->value>>(k*8))&255, port=a+k;
            if(port==0x3D4 || port==0x3B4)ci=v;
            if(port==0x3D5 || port==0x3B5){if(ci==0x23 && trial==12)depthcalls++;cr[ci]=v;}
            if(port==0x3C4)si=v;
            if(port==0x3C5)sr[si]=si==14?(v^2):v;
            if(port==0x3CE)gi=v;
            if(port==0x3CF)gr[gi]=v;
            if(port==0x3C2)misc=v;
            if(port==0x43C8)plla=v;
            if(port==0x43C9)pllb=v;
            if(port==0x43C6)mclka=v;
            if(port==0x43C7)mclkb=v;
            if(port==0x3C6 && dacreads==4){hidden=v;dacreads=0;}
        }
        return 0;
    }
    CHECK(r->addr>=0x20000000);
    u32 off=r->addr-0x20000000;
    if(HW_IS_BLOCK(r->action)) {
        struct hw_block_req *b=(struct hw_block_req *)r;
        CHECK(writing && variant==0 && off==0 && r->width==(1U<<(trial%3)) && b->count==4096/r->width);
        CHECK(b->increment==(r->width==4) && r->value==seed_for(trial));
        CHECK(r->action==(r->width==1?HW_BLOCK8:r->width==2?HW_BLOCK16:HW_BLOCK32));
        for(u32 n=0;n<b->count;n++) {
            if(survives(n,b->count,r->width)) {
                u32 v=r->value+(b->increment?n:0);
                for(u32 k=0;k<r->width;k++)memory[n*r->width+k]=v>>(k*8);
            }
        }
        writing=0;blockcalls++;return 0;
    }
    CHECK(off+r->width<=sizeof(memory));
    if(r->action==HW_READ) {
        r->value=0;
        for(u32 k=0;k<r->width;k++)r->value|=(u32)memory[off+k]<<(k*8);
    } else {
        if(writing) {
            u32 w=1U<<(trial%3), n=stores++;
            CHECK(variant!=0 && r->width==w && off==n*w && r->value==seed_for(trial)+(w==4?n:0));
            if(variant>=2){pending=1;pending_off=variant==2?r->addr:0x204003DA;pending_width=variant==2?w:1;}
            if(survives(n,4096/w,w))for(u32 k=0;k<w;k++)memory[off+k]=r->value>>(k*8);
            if(stores==4096/w)writing=0;
            return 0;
        }
        if(trial<15) {CHECK(r->width==4 && off==initialized*4 && r->value==0xA5A5A5A5);initialized++;}
        else {CHECK(cleanup && r->width==4);check_depth(0);restored++;}
        for(u32 k=0;k<r->width;k++)memory[off+k]=r->value>>(k*8);
    }
    return 0;
}
static void output(void *ctx,const char *s) {
    (void)ctx;logs++;
    if(has(s,"tag=\"depth-bpp\"")){CHECK(value(s)==8 && trial==12);depthlogs++;}
    if(has(s,"tag=\"CR38\"")){CHECK(value(s)==0);buslogs++;}
    if(has(s,"tag=\"CR23\"")){CHECK(value(s)==5U);newlogs++;}
    if(has(s,"tag=\"CR29\"")){CHECK(value(s)==0xC7);collogs++;}
    if(has(s,"tag=\"variant\"")){CHECK(value(s)==(trial<12?trial/3:0));variant=value(s);}
    if(has(s,"tag=\"width\""))CHECK(value(s)==(1U<<(trial%3)));
    if(has(s,"tag=\"B-seed\"")) {
        CHECK(trial<15 && value(s)==seed_for(trial) && initialized==1024);
        CHECK(depthcalls==(trial<12?0U:1U));
        if(trial>=12)check_depth(0);
        else CHECK(cr[0x23]==0x85); /* enable cleared bit5, set_depth has not cleared bit7. */
        for(u32 n=0;n<4096;n++)CHECK(memory[n]==0xA5);
        initialized=0;ranges=bad=starts=ends=misses=stores=0;writing=1;
        u32 w=1U<<(trial%3), active=0;
        for(u32 n=0;n<4096/w;n++) {
            if(survives(n,4096/w,w)) {
                if(!active)want_start[ranges]=n*w;
                active=1;want_end[ranges]=(n+1)*w-1;
            }else {bad++;if(active){ranges++;active=0;}}
        }
        if(active)ranges++;
    }
    if(has(s,"tag=\"CR39-before\"")){CHECK(value(s)==1);before++;}
    if(has(s,"tag=\"CR39-after\"")){CHECK(value(s)==1);after++;}
    if(has(s,"tag=\"match-start\"")){CHECK(starts<ranges && value(s)==want_start[starts]);starts++;}
    if(has(s,"tag=\"match-end\"")){CHECK(ends<ranges && value(s)==want_end[ends]);ends++;}
    if(has(s,"tag=\"mismatch-offset\"")) {
        u32 w=1U<<(trial%3), n=misses?returning+1:0;
        /* trial advances after the comparison summary. */
        while(n<4096/w && survives(n,4096/w,w))n++;
        CHECK(n<4096/w && value(s)==n*w);returning=n;misses++;
    }
    if(has(s,"tag=\"mismatch-count\"")) {
        CHECK(value(s)==bad && starts==ranges && ends==ranges && misses==(bad<8?bad:8));badlogs++;trial++;
    }
    if(has(s,"cleanup-8bpp")){CHECK(trial==15 && badlogs==15 && depthcalls==1);check_depth(0);cleanup++;}
}
static int file(void *ctx,const char *path,char *buf,u32 cap,u32 *n) {
    (void)ctx;(void)path;*n=0;while(script[*n])(*n)++;CHECK(*n<=cap);
    for(u32 k=0;k<*n;k++)buf[k]=script[k];
    return 0;
}
static int point(void *ctx,u32 *flag) {(void)ctx;events++;*flag=0;return 0;}
static struct hwb_backend backend={.access=access,.point=point,.line=output,.read_file=file};
void _start(void) {
    HWB_TEST("tri-burst5");
    for(fault=0;fault<4;fault++) {
        pal=fault&1; /* Both PLL references across all four fault models. */
        for(u32 n=0;n<4096;n++)memory[n]=(u8)(n*13+7);
        hwbf_zero(cr,sizeof(cr));hwbf_zero(sr,sizeof(sr));hwbf_zero(gr,sizeof(gr));
        cr[0x39]=0xD1;cr[0x55]=0xBD;cr[0x23]=0xA5;cr[0x29]=0xF3;cr[0x13]=0x37;
        cr[0x38]=0x42;cr[0xC0]=pal?0x80:0;sr[14]=0x24;sr[13]=0x63;gr[15]=0xB7;
        misc=1;plla=0x12;pllb=0x34;mclka=0x56;mclkb=0x78;hidden=0x9A;
        ci=si=gi=dacreads=trial=initialized=restored=disabled=logs=events=before=after=badlogs=0;
        depthlogs=buslogs=newlogs=collogs=cleanup=returning=0;
        variant=writing=stores=pending=blockcalls=depthcalls=0;
        hwb_init(&state,&backend);
        CHECK(hwb_run(&state,&program,source,sizeof(source),"script",0,0)==HWB_OK);
        CHECK(trial==15 && badlogs==15 && before==15 && after==15 && depthlogs==1 && cleanup==1);
        CHECK(blockcalls==6 && buslogs==1 && newlogs==1 && collogs==1 && !pending);
        CHECK(restored==1024 && disabled==1 && cr[0x39]==0xD1 && cr[0x55]==0xBD);
        CHECK(cr[0x38]==0x42 && cr[0x23]==0xA5 && cr[0x29]==0xF3 && cr[0x13]==0x37);
        CHECK(sr[14]==0x24 && sr[13]==0x63 && gr[15]==0xB7);
        CHECK(plla==0x12 && pllb==0x34 && mclka==0x56 && mclkb==0x78 && hidden==0x9A && misc==1);
        for(u32 n=0;n<4096;n++)CHECK(memory[n]==(u8)(n*13+7));
        CHECK(logs<2097152 && logs+events<8388608);
    }
    hwb_test_write("GREEN tri-burst5: 15 pacing/depth trials, PAL/NTSC, loss ranges/counts, 8bpp cleanup and restoration\n");
    hwb_test_exit(0);
}
