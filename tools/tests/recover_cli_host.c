#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define SHELL_H
#define ATTR_RED 1
#define ATTR_WHITE 2
#define str_eq(a,b) (strcmp(a,b) == 0)
#include "../../kdebug/recover_wire.h"
static char output[512];
static u32 called, observed_op, observed_session;
static int result;
static KdebugRecoverVirtual observed_virtual;
static void print(int attr, const char *fmt, ...)
{
    (void)attr;
    va_list ap; va_start(ap,fmt);
    vsnprintf(output,sizeof(output),fmt,ap); va_end(ap);
}
static int call(u32 op,void *buf,u32 size)
{
    KdebugRecover *r=buf;
    assert(size == 40);
    called++; observed_op=op; observed_session=r->session;
    if (op==KDEBUG_OP_RECOVER_VIRTUAL) {
        observed_virtual=*(KdebugRecoverVirtual *)buf;
        return result;
    }
    *r=(KdebugRecover){.session=7,.state=6,.verified=1,.seq=8};
    return result;
}
static struct api {u32 version; void (*kprintf)(int,const char *,...);
    int (*kdebug_call)(u32,void *,u32);} api={78,print,call}, *g_api=&api;
#include "../../userland/shell/cmd_recover.c"
int main(void)
{
    char *status[]={"recover","status"};
    char *now[]={"recover","now","4294967295"};
    assert(!shell_cmd_recover(2,status));
    assert(called==1 && observed_op==KDEBUG_OP_RECOVER_STATUS);
    assert(strstr(output,"sid=7 state=6") && strstr(output,"verified=1"));
    assert(!shell_cmd_recover(3,now));
    assert(called==2 && observed_op==KDEBUG_OP_RECOVER_NOW && observed_session==UINT32_MAX);
    const char *bad[]={"4294967296","-1","1x","","+1"};
    for (u32 i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        now[2]=(char *)bad[i]; assert(shell_cmd_recover(3,now)==2);
    }
    assert(called==2);
    result=-1; assert(shell_cmd_recover(2,status)==1);
    assert(!strstr(output,"verified="));
    api.version=77; assert(shell_cmd_recover(2,status)==1 && called==3);
    api.version=78;
    result=KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED;
    char *v[]={"recover","virtual","begin","normal","extra"};
    const char *cases[]={"normal","fault","trial-expire","poll-stop","txbusy-stop"};
    const u32 ids[]={RECOVER_VIRTUAL_NORMAL,RECOVER_VIRTUAL_FAULT,
        RECOVER_VIRTUAL_TRIAL_EXPIRE,RECOVER_VIRTUAL_POLL_STOP,RECOVER_VIRTUAL_TXBUSY_STOP};
    for (u32 i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        v[3]=(char *)cases[i];
        assert(shell_cmd_recover(4,v)==1);
        assert(observed_op==KDEBUG_OP_RECOVER_VIRTUAL);
        assert(observed_virtual.version==RECOVER_VIRTUAL_VERSION);
        assert(observed_virtual.action==RECOVER_VIRTUAL_BEGIN && observed_virtual.case_id==ids[i]);
        assert(observed_virtual.session==0 && strstr(output,"rejected rc=-2"));
        for (u32 j=0;j<6;j++) assert(observed_virtual.reserved[j]==0);
    }
    v[3]="unknown";
    u32 before=called;
    assert(shell_cmd_recover(4,v)==2 && called==before);
    assert(shell_cmd_recover(3,v)==2 && called==before);
    assert(shell_cmd_recover(5,v)==2 && called==before);
    for (u32 action=RECOVER_VIRTUAL_STEP;action<=RECOVER_VIRTUAL_END;action++) {
        v[2]=action==RECOVER_VIRTUAL_STEP ? "step":"end";
        v[3]="4294967295";
        assert(shell_cmd_recover(4,v)==1 && observed_virtual.session==UINT32_MAX);
        assert(observed_virtual.action==action && observed_virtual.case_id==0);
        v[3]="0";
        assert(shell_cmd_recover(4,v)==1 && observed_virtual.session==0);
        for (u32 i=0;i<sizeof(bad)/sizeof(*bad);i++) {
            v[3]=(char *)bad[i]; before=called;
            assert(shell_cmd_recover(4,v)==2 && called==before);
        }
        before=called;
        assert(shell_cmd_recover(3,v)==2 && called==before);
        assert(shell_cmd_recover(5,v)==2 && called==before);
    }
    v[2]="unknown"; v[3]="1"; before=called;
    assert(shell_cmd_recover(4,v)==2 && called==before);
    v[2]="step"; result=-10; /* existing KDEBUG=0 NOSYS */
    assert(shell_cmd_recover(4,v)==1 && strstr(output,"rejected rc=-10"));
    char *route[]={"recover","route-test","0","--approve","extra"};
    before=called; result=0;
    assert(shell_cmd_recover(3,route)==2 && called==before);
    assert(shell_cmd_recover(5,route)==2 && called==before);
    route[3]="approve";
    assert(shell_cmd_recover(4,route)==2 && called==before);
    route[3]="--approve"; route[2]="1";
    assert(shell_cmd_recover(4,route)==2 && called==before);
    route[2]="0";
    assert(shell_cmd_recover(4,route)==0 && called==before+1);
    assert(observed_op==KDEBUG_OP_RECOVER_ROUTE_TEST && observed_session==0);
    assert(strstr(output,"verified=1"));
    result=-10;
    assert(shell_cmd_recover(4,route)==1 && strstr(output,"rejected rc=-10"));
    puts("recover virtual CLI: cases/actions/session/arity/rejection PASS");
    puts("recover CLI: status/now/session bounds/rejection PASS");
    return 0;
}
