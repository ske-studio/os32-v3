/* C2 stop ordering and retained ownership; linked with display/quiesce.c. */
#include "../../hwdev/gf/internal.h"
#include "../../hwdev/gf/accel_service.h"
#include "gf_command_runtime_host.h"

static struct gf_display_context display;
static struct gf_foundation foundation;
static struct gf2_chan channel;
static struct gf3_service service;
static int events[32], count, close_rc, fini_rc, stop_rc, foundation_rc;
static int restore_rc, write_rc, release_rc, clock_rc;
static bool bound, stop_off;
enum { CLOSE=1, CLOCK, FINI, STOP, INVALIDATE, FOUNDATION, RESTORE, WRITE, RELEASE };
static void event(int e) { assert(count<32); events[count++]=e; }
static struct gf2_chan *get(struct gf_display_context *c, struct gf_foundation **f)
{ assert(c==&display); *f=&foundation; return &channel; }
static int foundation_stop(struct gf_display_context *c)
{ event(FOUNDATION); if (foundation_rc) c->last_rc=foundation_rc; return foundation_rc; }
static const struct gf_display_owner owner={.foundation_stop=foundation_stop,.command_get=get};
struct gf3_service *gf3_service_get(struct gf2_chan *ch)
{ assert(ch==&channel); return bound ? &service : 0; }
i32 gf3_service_close(struct gf3_service *s)
{ assert(s==&service); event(CLOSE); return close_rc; }
i32 gf3_service_fini(struct gf3_service *s,u32 dl)
{ assert(s==&service && dl==123+GF3_TIMEOUT_US); event(FINI); return fini_rc; }
void gf3_service_invalidate(struct gf3_service *s)
{ assert(s==&service && channel.state==GF2_OFF); event(INVALIDATE); }
i32 gf3_service_release(struct gf3_service *s)
{ assert(s==&service && channel.state==GF2_OFF); event(RELEASE); return release_rc; }
int gf2_stop(struct gf2_chan *ch)
{ assert(ch==&channel); event(STOP); if (stop_off) ch->state=GF2_OFF; return stop_rc; }
static int clock_fn(void *p,u32 *now)
{ (void)p; event(CLOCK); *now=123; return clock_rc; }
static int restore(void *p,struct gf_init_context *ctx)
{ (void)p; (void)ctx; event(RESTORE); return restore_rc; }
static int write_fn(void *p,u32 bar,u32 off,u32 width,u32 val)
{ (void)p; (void)bar; (void)off; (void)width; (void)val; event(WRITE); return write_rc; }
static void reset(void)
{
    display=(struct gf_display_context){.owner=&owner,.engaged=true,.active=true,.reusable=true,
        .io={.write=write_fn,.restore=restore,.timing={.clock_us=clock_fn}}};
    foundation=(struct gf_foundation){0}; channel=(struct gf2_chan){.state=GF2_RUN};
    count=close_rc=fini_rc=stop_rc=foundation_rc=restore_rc=write_rc=release_rc=clock_rc=0;
    bound=stop_off=true;
}
static void expect(const int *a,int n)
{ assert(count==n); for (int i=0;i<n;i++) assert(events[i]==a[i]); }
void _start(void)
{
    static const int ok[]={CLOSE,CLOCK,FINI,STOP,INVALIDATE,FOUNDATION,RESTORE,WRITE,WRITE,WRITE,RELEASE};
    static const int closed[]={CLOSE};
    static const int retained[]={CLOSE,CLOCK,FINI,STOP};
    static const int revoked[]={CLOSE,STOP,INVALIDATE,FOUNDATION,RESTORE,WRITE,WRITE,WRITE};
    static const int retry[]={CLOSE,RELEASE};
    reset(); assert(!gf_display_stop(&display)); expect(ok,11);
    reset(); close_rc=-301; assert(gf_display_stop(&display)==-1 && display.last_rc==-301); expect(closed,1);
    reset(); fini_rc=-302; assert(gf_display_stop(&display)==-1 && display.last_rc==-302); expect(ok,11);
    reset(); stop_rc=-206; assert(gf_display_stop(&display)==-1 && display.last_rc==-206); expect(ok,11);
    reset(); stop_rc=-210; stop_off=false; assert(gf_display_stop(&display)==-1); expect(retained,4);
    reset(); foundation.revoked=true; close_rc=-203; assert(gf_display_stop(&display)==-1 && display.last_rc==-203); expect(revoked,8);
    reset(); restore_rc=-9; assert(gf_display_stop(&display)==-1 && display.last_rc==-9 && events[count-1]==WRITE);
    reset(); release_rc=-10; assert(gf_display_stop(&display)==-1 && display.last_rc==-10);
    release_rc=0; count=0; assert(!gf_display_stop(&display)); expect(retry,2);
    reset(); clock_rc=-1; assert(gf_display_stop(&display)==-1 && display.last_rc==GF2_EIO && events[2]==STOP);
    gf2_host_exit(0);
}
