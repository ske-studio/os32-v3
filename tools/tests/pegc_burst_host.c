/* Actual interpreter; model dropped packed and scalar stores independently. */
#include "hwb_fake.h"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/hwb_log.c"
static const char *script = SCRIPT_DATA;
static struct hwb_state state;
static struct hwb_program program;
static char source[HWB_FILE_BYTES];
static u8 memory[4096];
static u32 width, count, variant, seed, fault, writing, stored, trials, blocks;
static u32 pending, pending_addr, bad, reports, restored, expected_bad;
static int has(const char *s,const char *p) {
    for(;*s;s++) {u32 n=0;while(p[n]&&p[n]==s[n])n++;if(!p[n])return 1;}return 0;
}
static u32 value(const char *s) {
    while(*s && !(s[0]=='v' && s[1]=='a' && s[2]=='l' && s[3]=='='))s++;
    CHECK(*s);s+=6;u32 v=0;
    while(*s && *s!=' ') {v=(v<<4)|(*s<='9'?*s-'0':*s-'A'+10);s++;}return v;
}
static int survives(u32 n) { return !fault || variant==2 || n%3==0; }
static void store(u32 off,u32 w,u32 data) {
    for(u32 k=0;k<w;k++)memory[off+k]=(u8)(data>>(k*8));
}
static int access(void *ctx,struct hw_req *r) {
    (void)ctx;r->status=HW_OK;
    if(r->space==HW_IO) {CHECK(r->action==HW_READ && r->addr==0x43B);r->value=fault==2?4:0;return 0;}
    CHECK(r->space==HW_PHYS && r->addr>=0xF00000 && r->addr+r->width<=0xF01000);
    u32 off=r->addr-0xF00000;
    if(pending) { CHECK(r->action==HW_READ && off==pending_addr && r->width==width); pending=0; }
    if(HW_IS_BLOCK(r->action)) {
        struct hw_block_req *b=(struct hw_block_req *)r;
        CHECK(writing && variant==0 && off==0 && r->width==width && b->count==count);
        CHECK(b->increment==(width==4) && r->value==seed);
        for(u32 n=0;n<count;n++) if(survives(n))store(n*width,width,seed+(width==4?n:0));
        blocks++;stored=count;writing=0;return 0;
    }
    if(r->action==HW_READ) {
        r->value=0;for(u32 k=0;k<r->width;k++)r->value|=(u32)memory[off+k]<<(k*8);
    } else {
        CHECK(r->action==HW_WRITE);
        if(writing) {
            CHECK(variant!=0 && off==stored*width && r->width==width);
            CHECK(r->value==((seed+(width==4?stored:0)) & (width==4?(u32)-1:width==2?65535:255)));
            if(survives(stored))store(off,width,r->value);
            if(variant==2) {pending=1;pending_addr=off;}
            if(++stored==count)writing=0;
        } else {
            if(trials==9 && reports==9) { CHECK(r->width==1);restored++; }
            store(off,r->width,r->value);
        }
    }
    return 0;
}
static void output(void *ctx,const char *s) {
    (void)ctx;
    if(has(s,"rc=SYNTAX") || has(s,"rc=ARG")) {hwb_test_write(s);hwb_test_write("\n");}
    if(has(s,"tag=\"variant\"")) { CHECK(!writing && !pending);variant=value(s);CHECK(variant==trials/3); }
    if(has(s,"tag=\"width\"")) {width=value(s);CHECK(width==(1U<<(trials%3)));}
    if(has(s,"tag=\"count\"")) {count=value(s);CHECK(count==4096/width);}
    if(has(s,"tag=\"B-seed\"")) {
        seed=value(s);writing=1;stored=0;expected_bad=0;
        for(u32 i=0;i<count;i++)if(!survives(i))expected_bad++;
        trials++;
    }
    if(has(s,"tag=\"restore-mismatch-count\"")) CHECK(value(s)==0);
    if(has(s,"tag=\"mismatch-count\"")) {CHECK(!writing && !pending && stored==count);CHECK(value(s)==expected_bad);reports++;}
    if(has(s,"tag=\"mismatch-offset\"")) {CHECK(fault && variant!=2);CHECK(value(s)<4096);bad++;}
}
static int file(void *ctx,const char *path,char *buf,u32 cap,u32 *n) {
    (void)ctx;(void)path;*n=0;while(script[*n])(*n)++;CHECK(*n<=cap);
    for(u32 k=0;k<*n;k++)buf[k]=script[k];
    return 0;
}
static int point(void *ctx,u32 *flag) {(void)ctx;*flag=0;return 0;}
static struct hwb_backend backend={.access=access,.point=point,.line=output,.read_file=file};
void _start(void) {
    HWB_TEST("pegc-burst");
    u32 line=0, len=0;while(script[len])++len;
    if(hwb_parse(&program,script,len,&line)!=HWB_OK) {
        char n[12];u32 k=0;do {n[k++]=(char)('0'+line%10);line/=10;}while(line);
        while(k) {char x[2]={n[--k],0};hwb_test_write(x);}hwb_test_write(" parse line\n");
        hwb_test_exit(1);
    }
    for(fault=0;fault<3;fault++) {
        for(u32 n=0;n<4096;n++)memory[n]=(u8)(n*13+7);
        width=count=variant=seed=writing=stored=trials=blocks=pending=bad=reports=restored=0;
        hwb_init(&state,&backend);
        CHECK(hwb_run(&state,&program,source,sizeof(source),"script",0,0)==HWB_OK);
        if(fault==2) {CHECK(!trials && !stored && !restored);continue;}
        CHECK(trials==9 && reports==9 && blocks==3 && restored==4096);
        CHECK(bad==(fault?48U:0U));
        for(u32 n=0;n<4096;n++)CHECK(memory[n]==(u8)(n*13+7));
    }
    hwb_test_write("GREEN PEGC nine trials, widths, drop reports, read pacing and restore\n");hwb_test_exit(0);
}
