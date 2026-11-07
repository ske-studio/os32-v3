/* Indexed CRTC and byte-addressed LFB; dropped stores vary on every trial. */
#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/hwb_log.c"
static const char *script = SCRIPT_DATA;
static struct hwb_state state;
static struct hwb_program program;
static char source[HWB_FILE_BYTES];
static u8 memory[4096], cr[256];
static u32 ci, trial, fault, initialized, restored, disabled, logs, events;
static u32 starts, ends, badlogs, before, after, want_start[4096], want_end[4096], ranges, bad;
static const u32 widths[9]={4,4,4,1,2,4,4,4,1};
static const u32 counts[9]={1024,1024,1024,4096,2048,256,128,64,4096};
static const u32 seeds[9]={0x5A000000,0x5B000000,0x5C000000,0x31,0x6100,0x5D000000,0x5E000000,0x5F000000,0x71};
static int has(const char *s,const char *p) {
    for(;*s;s++) {u32 n=0;while(p[n]&&p[n]==s[n])n++;if(!p[n])return 1;}return 0;
}
static u32 value(const char *s) {
    while(*s && !(s[0]=='v' && s[1]=='a' && s[2]=='l' && s[3]=='='))s++;
    CHECK(*s);s+=4;CHECK(s[0]=='0' && s[1]=='x');s+=2;u32 v=0;
    while(*s && *s!=' ') {v=(v<<4)|(*s<='9'?*s-'0':*s-'A'+10);s++;}return v;
}
static int survives(u32 n) {
    /* Full success, moving partial ranges, or a length threshold. */
    if(!fault)return 1;
    if(fault==2 && counts[trial]<=128)return 1;
    return n<2 || (n>=8+trial && n<12+trial) || n==counts[trial]-1;
}
static int access(void *ctx,struct hw_req *r) {
    (void)ctx;events++;r->status=HW_OK;
    if(r->space==HW_PCI) {r->value=r->addr==0x10?0x20000000:0x20400000;return 0;}
    if(r->space==HW_IO) {if(r->addr==0xFAC && r->value==0)disabled++;return 0;}
    CHECK(r->space==HW_PHYS);
    if(r->addr>=0x20400000) {
        u32 a=r->addr-0x20400000;
        if(r->action==HW_READ) {r->value=(a==0x3D5 || a==0x3B5)?cr[ci]:0;return 0;}
        for(u32 k=0;k<r->width;k++) {
            u32 v=(r->value>>(k*8))&255;
            if(a+k==0x3D4 || a+k==0x3B4)ci=v;
            if(a+k==0x3D5 || a+k==0x3B5)cr[ci]=v;
        }
        return 0;
    }
    CHECK(r->addr>=0x20000000);
    u32 off=r->addr-0x20000000;
    if(HW_IS_BLOCK(r->action)) {
        struct hw_block_req *b=(struct hw_block_req *)r;
        CHECK(trial<9 && off==0 && r->width==widths[trial] && b->count==counts[trial]);
        CHECK(r->action==(r->width==1?HW_BLOCK8:r->width==2?HW_BLOCK16:HW_BLOCK32));
        CHECK(b->increment==(r->width==4) && r->value==seeds[trial]);
        CHECK(cr[0x39]==(trial==8?0xD7U:0xD1U) && initialized==1024);
        for(u32 n=0;n<4096;n++)CHECK(memory[n]==0xA5);
        initialized=0; ranges=bad=starts=ends=0;
        u32 active=0;
        for(u32 n=0;n<b->count;n++) {
            if(survives(n)) {
                if(!active)want_start[ranges]=n*r->width;
                active=1;want_end[ranges]=(n+1)*r->width-1;
                u32 v=r->value+(b->increment?n:0);
                for(u32 k=0;k<r->width;k++)memory[n*r->width+k]=v>>(k*8);
            } else {bad++;if(active){ranges++;active=0;}}
        }
        if(active)ranges++;
        trial++;return 0;
    }
    CHECK(off+r->width<=sizeof(memory));
    if(r->action==HW_READ) {
        r->value=0;
        for(u32 k=0;k<r->width;k++)r->value|=(u32)memory[off+k]<<(k*8);
    } else {
        if(r->value==0xA5A5A5A5)initialized++;
        else {CHECK(trial==9);restored++;}
        for(u32 k=0;k<r->width;k++)memory[off+k]=r->value>>(k*8);
    }
    return 0;
}
static void output(void *ctx,const char *s) {
    (void)ctx;logs++;
    if(has(s,"tag=\"CR39-before\"")){CHECK(value(s)==(before==8?0xD7U:0xD1U));before++;}
    if(has(s,"tag=\"CR39-after\"")){CHECK(value(s)==(after==8?0xD7U:0xD1U));after++;}
    if(has(s,"tag=\"match-start\"")){CHECK(starts<ranges && value(s)==want_start[starts]);starts++;}
    if(has(s,"tag=\"match-end\"")){CHECK(ends<ranges && value(s)==want_end[ends]);ends++;}
    if(has(s,"tag=\"mismatch-count\"")) {
        CHECK(value(s)==bad && starts==ranges && ends==ranges);badlogs++;
    }
}
static int file(void *ctx,const char *path,char *buf,u32 cap,u32 *n) {
    (void)ctx;(void)path;*n=0;while(script[*n])(*n)++;CHECK(*n<=cap);
    for(u32 k=0;k<*n;k++)buf[k]=script[k];
    return 0;
}
static int point(void *ctx,u32 *flag) {(void)ctx;events++;*flag=0;return 0;}
static struct hwb_backend backend={.access=access,.point=point,.line=output,.read_file=file};
void _start(void) {
    HWB_TEST("tri-burst2");
    for(fault=0;fault<3;fault++) {
        for(u32 n=0;n<4096;n++)memory[n]=(u8)(n*13+7);
        hwbf_zero(cr,sizeof(cr));cr[0x39]=0xD1;cr[0x55]=0xBD;
        ci=trial=initialized=restored=disabled=logs=events=before=after=badlogs=0;
        hwb_init(&state,&backend);
        CHECK(hwb_run(&state,&program,source,sizeof(source),"script",0,0)==HWB_OK);
        CHECK(trial==9 && badlogs==9 && before==9 && after==9);
        CHECK(restored==1024 && disabled==1 && cr[0x39]==0xD1 && cr[0x55]==0xBD);
        for(u32 n=0;n<4096;n++)CHECK(memory[n]==(u8)(n*13+7));
        CHECK(logs<524288 && logs+events<2097152);
    }
    hwb_test_write("GREEN tri-burst2: repeats/widths/lengths, moving holes, ranges, CR39/CR55/VRAM restoration\n");
    hwb_test_exit(0);
}
