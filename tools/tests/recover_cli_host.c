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
    puts("recover CLI: status/now/session bounds/rejection PASS");
    return 0;
}
