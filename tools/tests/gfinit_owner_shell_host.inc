/* Real shell and KDEBUG=0 gateway; only the loaded GF dispatch is injected. */
#define SHELL_H
#define str_eq(a,b) equal(a,b)
#include "../../kernel/kdebug_call.c"
struct kdebug kdebug;
static u32 cli_calls, cli_op;
static struct gfinit_request cli_request;
static struct gfinit_prepare_request cli_payload;
static int cli_result, cli_live;
static i32 cli_dispatch(u32 op, void *buf, u32 size)
{
    CHECK(size==40 || size==sizeof(struct gfinit_prepare_request));
    if(size==sizeof(cli_payload))cli_payload=*(struct gfinit_prepare_request *)buf;
    cli_calls++;cli_op=op;cli_request=*(struct gfinit_request *)buf;
    if (cli_live) return gfinit_call(op,buf,size,&base,&transport,&s0);
    ((struct gfinit_request *)buf)->status=99; /* rc, never status, is authoritative */
    return cli_result;
}
static const char *cli_file;
static u32 cli_file_pos, cli_file_closes;
static int cli_file_fail;
static int cli_open(const char *path, int flags)
{ CHECK(equal(path,"/verify.txt") && flags==0);cli_file_pos=0;return cli_file?3:-1; }
static int cli_read(int fd, void *out, u32 size)
{ CHECK(fd==3 && size==1);if(cli_file_fail)return -1;if(!cli_file[cli_file_pos])return 0;*(char *)out=cli_file[cli_file_pos++];return 1; }
static int cli_close(int fd) { CHECK(fd==3);cli_file_closes++;return 0; }
static struct {
    u32 version;
    void (*kprintf)(u8,const char *,...);
    i32 (*kdebug_call)(u32,void *,u32);
    int (*sys_open)(const char *,int);
    int (*sys_read)(int,void *,u32);
    int (*sys_close)(int);
} api={78,kprintf,kdebug_call,cli_open,cli_read,cli_close}, *g_api=&api;
#include "../../userland/shell/cmd_gfinit.c"
static void cli_verify_test(void)
{
    /* No GeForce/init: rejection must echo this request, not the prior policy. */
    for(u32 mode=0;mode<3;mode++) {
        setup();core.verify_record=mode!=0;
        struct gfinit_request q=request(GF_ALLOW_WRITE|GF_MODE_C|
            (mode==0?GF_VERIFY_RECORD:mode==1?GF_VERIFY_REQUIRED:0),0);
        CHECK(gfinit_call(GF_OP_C_ONLY,&q,sizeof(q),&base,&transport,&s0)<0);
        CHECK(equal(serial_output,mode==0?"GF C verify=record result=NG\n":
                    "GF C verify=required result=NG\n"));
        CHECK(!reads && !writes && !begins);
    }
    char *args[]={"gfinit","prepare","5","--mode-c","--allow-write","--verify","required","--no-verify-file","/verify.txt"};
    setup();kdebug.ops.dbg_call=cli_dispatch;cli_live=0;cli_result=0;api.version=78;api.kdebug_call=kdebug_call;
    cli_file=" # comment\nMMIO 680a24 # write-only\r\nCR 1c\nSR 01";cli_file_fail=0;cli_file_closes=0;
    CHECK(!shell_cmd_gfinit(9,args));
    CHECK(cli_request.flags==(GF_ALLOW_WRITE|GF_MODE_C|GF_VERIFY_REQUIRED|GF_NO_VERIFY_FILE));
    CHECK(cli_payload.count==3 && cli_file_closes==1);
    CHECK(cli_payload.regs[0].kind==GF_VERIFY_MMIO && cli_payload.regs[0].reg==0x680a24);
    CHECK(cli_payload.regs[1].kind==GF_VERIFY_CR && cli_payload.regs[1].reg==0x1c);
    CHECK(cli_payload.regs[2].kind==GF_VERIFY_SR && cli_payload.regs[2].reg==1);
    CHECK(!shell_cmd_gfinit(7,args) && cli_request.size==40 && !(cli_request.flags&GF_NO_VERIFY_FILE));
    const char *bad[]={"CR 100", "MMIO fffffffff", "AR 1", "MMIO nope", "CR 1c extra", "SR"};
    const char *rejected="gfinit: rejected rc=-1 invalid no-verify file (MMIO/CR/SR hex, max 64 entries)\n";
    for(u32 i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        cli_file=bad[i];u32 before=cli_calls;clear_output();
        CHECK(shell_cmd_gfinit(9,args)==2 && cli_calls==before);
        CHECK(equal(serial_output,rejected));
    }
    u32 before=cli_calls, closed=cli_file_closes;
    cli_file_fail=1;clear_output();
    CHECK(shell_cmd_gfinit(9,args)==2 && cli_calls==before && cli_file_closes==closed+1);
    CHECK(equal(serial_output,rejected));cli_file_fail=0;
    cli_file=0;clear_output();
    CHECK(shell_cmd_gfinit(9,args)==2 && cli_calls==before && cli_file_closes==closed+1);
    CHECK(equal(serial_output,rejected));
    char *again[]={"gfinit","c-only","--allow-write","--verify","record"};
    CHECK(!shell_cmd_gfinit(5,again) && cli_op==GF_OP_C_ONLY && !cli_request.session);
    CHECK(cli_request.flags==(GF_ALLOW_WRITE|GF_MODE_C|GF_VERIFY_RECORD));
    again[4]="required";
    CHECK(!shell_cmd_gfinit(5,again) && cli_request.flags==(GF_ALLOW_WRITE|GF_MODE_C|GF_VERIFY_REQUIRED));
    CHECK(!shell_cmd_gfinit(3,again) && cli_request.flags==(GF_ALLOW_WRITE|GF_MODE_C));
    again[2]="--unknown";CHECK(shell_cmd_gfinit(5,again)==2);
    output("CLI verify required/file parsing, comments, hex and errors PASS\n");
}
static void cli_test(void)
{
    char *inspect[]={"gfinit","inspect","5","--allow-write","extra"};
    char *abort[]={"gfinit","abort","5","--allow-write","extra"};
    setup();kdebug.ops.dbg_call=cli_dispatch;cli_live=1;core.token.session=5;
    CHECK(shell_cmd_gfinit(3,inspect)==0);
    CHECK(contains(serial_output,"GF inspect sid=5 rc=0 rom_len=512 crc=E0A29354"));
    CHECK(contains(serial_output,"match=1\nGF inspect-end sid=5 rc=0\n"));
    clear_output();CHECK(shell_cmd_gfinit(3,inspect)==0);
    CHECK(equal(serial_output,"GF inspect-end sid=5 rc=0\n"));
    CHECK(shell_cmd_gfinit(4,abort)==0 && !writes && !gf_lease.active && !live_pages);
    cli_live=0;cli_result=0;
    inspect[2]="4294967295";
    CHECK(shell_cmd_gfinit(3,inspect)==0 && cli_op==5);
    CHECK(cli_request.version==1 && cli_request.size==40 && cli_request.session==~0U);
    CHECK(cli_request.flags==0 && cli_request.script_id==0 && cli_request.max_steps==4096);
    CHECK(!cli_request.status && !cli_request.fail_pc && !cli_request.fail_reg && !cli_request.steps);
    CHECK(shell_cmd_gfinit(4,abort)==0 && cli_op==10 && cli_request.flags==1);
    CHECK(cli_request.session==5 && cli_request.script_id==0 && cli_request.max_steps==4096);
    CHECK(!cli_request.status && !cli_request.fail_pc && !cli_request.fail_reg && !cli_request.steps);
    u32 before=cli_calls;
    CHECK(shell_cmd_gfinit(4,inspect)==2 && shell_cmd_gfinit(3,abort)==2);
    for (int n=1;n<=5;n++) {
        if(n!=3)CHECK(shell_cmd_gfinit(n,inspect)==2);
        if(n!=4)CHECK(shell_cmd_gfinit(n,abort)==2);
    }
    const char *bad[]={"", "-1", "+1", "4294967296", "1x", " 1"};
    for(u32 i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        inspect[2]=(char *)bad[i];abort[2]=(char *)bad[i];
        CHECK(shell_cmd_gfinit(3,inspect)==2 && shell_cmd_gfinit(4,abort)==2);
    }
    inspect[2]=abort[2]="0";abort[3]="--unknown";
    CHECK(shell_cmd_gfinit(4,abort)==2);
    const char *commands[]={"size-bars","prepare","run-one","verify","unknown"};
    for(u32 i=0;i<sizeof(commands)/sizeof(*commands);i++) {
        inspect[1]=(char *)commands[i];CHECK(shell_cmd_gfinit(3,inspect)==2);
    }
    CHECK(cli_calls==before);inspect[1]="inspect";abort[3]="--allow-write";
    cli_result=-1;clear_output();CHECK(shell_cmd_gfinit(3,inspect)==1);
    CHECK(equal(serial_output,"GF inspect-end sid=0 rc=-1\n"));
    kdebug.ops.dbg_call=0;before=cli_calls;clear_output();
    CHECK(shell_cmd_gfinit(3,inspect)==1 && cli_calls==before);
    CHECK(equal(serial_output,"GF inspect-end sid=0 rc=-10\n"));
    clear_output();CHECK(shell_cmd_gfinit(4,abort)==1 && cli_calls==before);
    CHECK(equal(serial_output,"gfinit: rejected rc=-10\n"));
    api.version=77;clear_output();CHECK(shell_cmd_gfinit(3,inspect)==1);
    CHECK(equal(serial_output,"gfinit: rejected rc=-10 kdebug unavailable\n"));
    api.version=78;api.kdebug_call=0;clear_output();
    CHECK(shell_cmd_gfinit(3,inspect)==1 && cli_calls==before);
    CHECK(equal(serial_output,"gfinit: rejected rc=-10 kdebug unavailable\n"));api.kdebug_call=kdebug_call;
    output("S7a CLI / KDEBUG=0 NOSYS / inspect two lines via console serial PASS checks complete\n");
}
static void cli_write_test(void)
{
    const char *names[]={"size-bars","prepare","run-one","verify","abort","modeset"};
    const u32 ops[]={6,7,8,9,10,13};
    const char *bad[]={"", "-1", "+1", "4294967296", "1x", " 1"};
    cli_live=0;api.version=78;api.kdebug_call=kdebug_call;
    for(u32 i=0;i<sizeof(ops)/sizeof(*ops);i++) {
        int argc=i==2 ? 5 : 4;
        char *args[]={"gfinit",(char *)names[i],"4294967295","--allow-write","--allow-write","extra"};
        if(i==2)args[3]="0";
        kdebug.ops.dbg_call=cli_dispatch;cli_result=0;
        u32 before=cli_calls;
        clear_output();
        CHECK(shell_cmd_gfinit(argc,args)==0 && cli_calls==before+1 && cli_op==ops[i]);
        CHECK(!serial_length); /* No automatic next stage or added success line. */
        CHECK(cli_request.version==1 && cli_request.size==40 && cli_request.flags==1);
        CHECK(cli_request.session==~0U && cli_request.script_id==0 && cli_request.max_steps==4096);
        CHECK(!cli_request.status && !cli_request.fail_pc && !cli_request.fail_reg && !cli_request.steps);
        before=cli_calls;
        for(int n=1;n<=6;n++)if(n!=argc)CHECK(shell_cmd_gfinit(n,args)==2);
        args[argc-1]="--unknown";CHECK(shell_cmd_gfinit(argc,args)==2);
        args[argc-1]="--allow-write";
        for(u32 j=0;j<sizeof(bad)/sizeof(*bad);j++) {
            args[2]=(char *)bad[j];CHECK(shell_cmd_gfinit(argc,args)==2);
        }
        args[2]="5";
        if(i==2) {
            const char *bad_main[]={"5","4294967295","", "-1", "+1", "4294967296", "1x", " 1"};
            for(u32 j=0;j<sizeof(bad_main)/sizeof(*bad_main);j++) {
                args[3]=(char *)bad_main[j];CHECK(shell_cmd_gfinit(argc,args)==2);
            }
            CHECK(cli_calls==before);
            for(u32 main=0;main<5;main++) {
                char number[]={(char)('0'+main),0};args[3]=number;
                CHECK(shell_cmd_gfinit(argc,args)==0 && cli_request.script_id==main);
                CHECK(cli_calls==++before && cli_op==8);
            }
            args[3]="0";
        }
        CHECK(cli_calls==before);
        cli_result=-1;clear_output();
        CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before+1);
        CHECK(equal(serial_output,"gfinit: rejected rc=-1\n"));
        kdebug.ops.dbg_call=0;before=cli_calls;clear_output();
        CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before);
        CHECK(equal(serial_output,"gfinit: rejected rc=-10\n"));
        api.version=77;clear_output();CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before);
        CHECK(equal(serial_output,"gfinit: rejected rc=-10 kdebug unavailable\n"));
        api.version=78;api.kdebug_call=0;clear_output();
        CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before);
        CHECK(equal(serial_output,"gfinit: rejected rc=-10 kdebug unavailable\n"));api.kdebug_call=kdebug_call;
    }
    char *mode[]={"gfinit","prepare","5","--mode-c","--allow-write","extra"};
    cli_result=0;kdebug.ops.dbg_call=cli_dispatch;
    CHECK(shell_cmd_gfinit(5,mode)==0 && cli_op==GF_OP_PREPARE && cli_request.flags==(GF_ALLOW_WRITE|GF_MODE_C));
    mode[1]="modeset";CHECK(shell_cmd_gfinit(5,mode)==2);
    mode[1]="prepare";mode[3]="--unknown";CHECK(shell_cmd_gfinit(5,mode)==2);
    mode[3]="--mode-c";mode[4]="--unknown";CHECK(shell_cmd_gfinit(5,mode)==2);
    output("S7c single-op CLI / main range / write gate / NOSYS PASS checks complete\n");
}
