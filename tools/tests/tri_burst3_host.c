/* Indexed CRTC and byte-addressed LFB; dropped stores vary on every trial. */
#include "hwb_fake.h"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/hwb_log.c"
static const char *script = SCRIPT_DATA;
static struct hwb_state state;
static struct hwb_program program;
static char source[HWB_FILE_BYTES];
static u8 memory[4096], cr[256], sr[256];
static u32 si, opmode, ge_reads, ge_init, unlock, seen, stuck, ci, trial, fault, initialized, restored, disabled, logs, events;
static u32 starts, ends, badlogs, before, after, want_start[4096], want_end[4096], ranges, bad;
static const u32 widths[4]={4,4,4,4};
static const u32 counts[4]={1024,1024,1024,1024};
static const u32 seeds[4]={0x5A000000,0x5B000000,0x5C000000,0x5D000000};
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
    if(fault==2 && trial>=1)return 1;
    return n<2 || (n>=8+trial && n<12+trial) || n==counts[trial]-1;
}
static int access(void *ctx,struct hw_req *r) {
    (void)ctx;events++;r->status=HW_OK;
    if(r->space==HW_PCI) {r->value=r->addr==0x10?0x20000000:0x20400000;return 0;}
    if(r->space==HW_IO) {if(r->addr==0xFAC && r->value==0)disabled++;return 0;}
    CHECK(r->space==HW_PHYS);
    if(r->addr>=0x20400000) {
        u32 a=r->addr-0x20400000;
        if(a==0x2120) {CHECK(r->action==HW_READ && r->width==1);
            ge_reads++;r->value=stuck || (trial==2 && ge_reads<8)?0x80:0;return 0;}
        if(a==0x2122) {
            CHECK(r->width==2);
            if(r->action==HW_READ)r->value=opmode;
            else {if(r->value==0x100){CHECK(trial==2 && ge_reads>=8);ge_init++;}opmode=r->value;}
            return 0;
        }
        if(r->action==HW_READ) {
            r->value=(a==0x3D5 || a==0x3B5)?cr[ci]:a==0x3C5?sr[si]:0;
            return 0;
        }
        for(u32 k=0;k<r->width;k++) {
            u32 v=(r->value>>(k*8))&255;
            if(a+k==0x3D4 || a+k==0x3B4)ci=v;
            if(a+k==0x3D5 || a+k==0x3B5) {
                if(ci!=0x39 || !fault || (sr[14]&0x80))cr[ci]=v;
            }
            if(a+k==0x3C4)si=v;
            if(a+k==0x3C5) {sr[si]=si==14?(v^2):v;
                if(si==14 && (trial==3 || (stuck && trial==2)) && (sr[14]&0x80))unlock++;}
        }
        return 0;
    }
    CHECK(r->addr>=0x20000000);
    u32 off=r->addr-0x20000000;
    if(HW_IS_BLOCK(r->action)) {
        struct hw_block_req *b=(struct hw_block_req *)r;
        if(stuck && trial==2)trial++;
        CHECK(trial<4 && off==0 && r->width==widths[trial] && b->count==counts[trial]);
        CHECK(r->action==(r->width==1?HW_BLOCK8:r->width==2?HW_BLOCK16:HW_BLOCK32));
        CHECK(b->increment==(r->width==4) && r->value==seeds[trial]);
        CHECK(cr[0x39]==(fault && trial<3?0xD1U:1U) && initialized==1024);
        CHECK((cr[0x2A]&0x40)==(trial?0x40U:0U));
        CHECK((cr[0x2F]&0x10)==(trial?0x10U:0U));
        if(trial>=2 && !stuck)CHECK(ge_init==1 && opmode==0x100);
        if(trial==3)CHECK(unlock && (sr[14]&0x80));
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
        /* Inject old-mode after mode setup, before the diagnostic trials. */
        if(off==4092 && trial==0 && fault){sr[14]&=0x7F;cr[0x39]=0xD1;}
    } else {
        if(r->value==0xA5A5A5A5)initialized++;
        else {CHECK(trial==4);restored++;}
        for(u32 k=0;k<r->width;k++)memory[off+k]=r->value>>(k*8);
    }
    return 0;
}
static void output(void *ctx,const char *s) {
    (void)ctx;logs++;
    const char *tags[]={"SR0E", "CR2A", "CR2F", "CR38", "CR23", "CR39", "CR55", "GE_STATUS"};
    for(u32 n=0;n<8;n++) {
        char tag[40]="tag=\"";u32 j=5,k=0;while(tags[n][k])tag[j++]=tags[n][k++];tag[j++]='"';tag[j]=0;
        if(has(s,tag))seen++;
    }
    if(has(s,"tag=\"CR39-before\"")){CHECK(value(s)==(fault && before<(stuck?2U:3U)?0xD1U:1U));before++;}
    if(has(s,"tag=\"CR39-after\"")){CHECK(value(s)==(fault && after<(stuck?2U:3U)?0xD1U:1U));after++;}
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
    HWB_TEST("tri-burst3");
    for(fault=0;fault<4;fault++) {
        for(u32 n=0;n<4096;n++)memory[n]=(u8)(n*13+7);
        hwbf_zero(cr,sizeof(cr));hwbf_zero(sr,sizeof(sr));
        cr[0x39]=0xD1;cr[0x55]=0xBD;cr[0x23]=0xA5;cr[0x2A]=3;cr[0x2F]=5;
        cr[0x38]=0x42;cr[0x20]=0xAB;cr[0x30]=0xAC;cr[0x31]=0xAD;cr[0x32]=0xAE;cr[0x3B]=0xAF;
        sr[14]=0x24;si=ge_reads=ge_init=unlock=seen=stuck=0;opmode=0x205;stuck=fault==3;
        ci=trial=initialized=restored=disabled=logs=events=before=after=badlogs=0;
        hwb_init(&state,&backend);
        CHECK(hwb_run(&state,&program,source,sizeof(source),"script",0,0)==HWB_OK);
        CHECK(trial==4 && badlogs==4-stuck && before==4-stuck && after==4-stuck && seen==8*(4-stuck)+1);
        if(stuck)CHECK(ge_init==0 && ge_reads==10003);
        CHECK(restored==1024 && disabled==1 && cr[0x39]==0xD1 && cr[0x55]==0xBD);
        CHECK(sr[14]==0x24 && opmode==0x205 && cr[0x23]==0xA5 && cr[0x2A]==3 && cr[0x2F]==5);
        CHECK(cr[0x38]==0x42 && cr[0x20]==0xAB && cr[0x30]==0xAC && cr[0x31]==0xAD && cr[0x32]==0xAE && cr[0x3B]==0xAF);
        for(u32 n=0;n<4096;n++)CHECK(memory[n]==(u8)(n*13+7));
        CHECK(logs<524288 && logs+events<2097152);
    }
    hwb_test_write("GREEN tri-burst3: interface, GE wait/init, old-mode CR39 readback, ranges and restoration\n");
    hwb_test_exit(0);
}
