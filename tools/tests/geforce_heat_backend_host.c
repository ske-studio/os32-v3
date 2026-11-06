/* Exercise the actual module backend: thermal stop differs from permanent
 * device failure, and a cold explicit request must precede a new start. */
#define GF_F0S_HOST
#define run baseline_run
#define _start baseline_start
#include "backend_geforce_host.c"
#undef _start
#undef run
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) finish(1); } while (0)
static int map_rc, identity_rc, read_rc, restart_rc;
static int force_rc;
static u32 forced;
int gf_foundation_stop(struct gf_foundation *f) { CHECK(f); ++forced; return force_rc; }
static int fail_stop(void) { return -77; }
static u32 reads, requests, restarts;
static i32 temperature=79999;
int gf_production_identity(struct gf_production *p) { CHECK(p); return identity_rc; }
int gf_production_mapped(struct gf_production *p) { CHECK(p); return map_rc; }
i32 gf5_therm_read(struct gf5 *g,struct gf5_thermal *out)
{
    CHECK(g && out); reads++;
    *out=(struct gf5_thermal){.milli_c=temperature,.calibrated=true};
    return read_rc;
}
static void request(i32 reason,i32 milli_c,i32 source_rc)
{
    CHECK(reason==-1 && milli_c==120000 && source_rc==-2);
    test_heat.stop_state.state=GF5_STOP_REQUESTED; requests++;
}
i32 gf5_therm_monitor_restart(struct gf5 *g)
{
    CHECK(g && !active && !routed);
    restarts++;
    return restart_rc;
}
void _start(void)
{
    struct video_device d={0};
    static struct gf_production owner;
    static struct gf2_chan ch;
    static struct gf_foundation f;
    static struct gf5 g;
    ch.f=&f; g.ch=&ch; g.fan.threshold[1]=85; g.fan.hyst[1]=5;
    test_platform.opaque=&owner;
    test_heat.request=request;
    module_backend=geforce_backend_ops;
    geforce_backend_bind(&d,identify,0,0,0);
    geforce_backend_feature5_bind(&g);
    CHECK(test_heat.stop && test_heat.clear && test_heat.backend_active);
    module_backend.bb_base=client;
    CHECK(module_backend.probe());
    module_backend.init();
    CHECK(active && routed && !failed);
    CHECK(test_heat.backend_active());
    geforce_heat_stop_request(-1,120000,-2);
    CHECK(requests==1 && (test_heat.stop_state.state!=GF5_STOP_NORMAL));
    CHECK(!test_heat.stop() && !active && !routed && !failed);
    CHECK(!test_heat.backend_active());
    CHECK(events[n-2]==5 && events[n-1]==6);
    int left=n;
    startlog_clear();
    CHECK(!test_heat.stop());
    CHECK(n==left+1 && events[n-1]==5); /* No second display_restore_98. */
    u32 before=n;
    /* A new ordinary app probes then initializes; both must stay closed. */
    CHECK(!module_backend.probe());
    module_backend.init();
    CHECK(n==(int)before && !active && !routed);
    map_rc=-1;
    CHECK(test_heat.clear()==GF5_ESTATE && !reads);
    map_rc=0; identity_rc=-1;
    CHECK(test_heat.clear()==GF5_ESTATE && !reads);
    identity_rc=0; read_rc=GF5_EIO;
    CHECK(test_heat.clear()==GF5_EIO && (test_heat.stop_state.state!=GF5_STOP_NORMAL));
    read_rc=0; temperature=80001;
    CHECK(test_heat.clear()==GF5_EBUSY && (test_heat.stop_state.state!=GF5_STOP_NORMAL));
    CHECK(!restarts);
    temperature=80000;
    restart_rc=GF5_EIO;
    CHECK(test_heat.clear()==GF5_EIO && (test_heat.stop_state.state!=GF5_STOP_NORMAL) && restarts==1);
    restart_rc=0;
    CHECK(!test_heat.clear() && n==(int)before && !active && !routed);
    CHECK(restarts==2);
    /* The resident owner clears only after this callback succeeds. */
    test_heat.stop_state.state=GF5_STOP_NORMAL;
    CHECK(module_backend.probe());
    module_backend.enter();
    CHECK(active && routed && !failed);
    geforce_backend_stop_bind(fail_stop);
    startlog_clear();
    force_rc=-78;
    CHECK(test_heat.stop()==-78 && forced==1 && !active && !routed);
    startlog_clear();
    force_rc=0;
    CHECK(!test_heat.stop() && forced==2 && failed);
    finish(0);
}
