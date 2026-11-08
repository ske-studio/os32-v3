#include "userland/shell/cmd_tri.c"
static KernelAPI api;
KernelAPI *g_api=&api;
static u32 called;
static struct tri_request last;
int strcmp(const char *a,const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return (u8)*a-(u8)*b; }
static void print(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
static i32 dispatch(u32 op, void *buf, u32 size)
{
    ++called;
    last=*(struct tri_request *)buf;
    if (op!=TRI_OP_COMMAND || size!=sizeof(last) || last.size!=size ||
        last.version!=TRI_COMMAND_VERSION) return -1;
    return 0;
}
#define CHECK(c) do { if (!(c)) return 1; } while (0)
static int test(void)
{
    api.version=TRI_KAPI_MIN;api.kdebug_call=dispatch;api.kprintf=print;
    char *show[]={"tri","gap"}, *set[]={"tri","gap","0"};
    char *sweep[]={"tri","gap","sweep","0","600","50"};
    CHECK(!shell_cmd_tri(2,show) && last.test==TRI_TEST_GAP);
    CHECK(!shell_cmd_tri(3,set) && last.test==TRI_TEST_GAP_SET && !last.arg);
    set[2]="4294967295";
    CHECK(!shell_cmd_tri(3,set) && last.arg==0xffffffffu);
    CHECK(!shell_cmd_tri(6,sweep) && last.test==TRI_TEST_GAP_SWEEP &&
        !last.arg && last.end_ns==600 && last.step_ns==50);
    const char *invalid[]={"-1","4294967296","50x",""};
    for(u32 i=0;i<4;++i) {
        set[2]=(char *)invalid[i];
        CHECK(shell_cmd_tri(3,set)==SH_STATUS_USAGE && called==4);
    }
    sweep[5]="0";CHECK(shell_cmd_tri(6,sweep)==SH_STATUS_USAGE && called==4);
    sweep[5]="50";sweep[3]="601";
    CHECK(shell_cmd_tri(6,sweep)==SH_STATUS_USAGE && called==4);
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory");
    __builtin_unreachable();
}
