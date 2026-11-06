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
static int map_rc, identity_rc, read_rc;
static u32 reads, requests;
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
    test_heat.latched=true; requests++;
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
    CHECK(test_heat.stop && test_heat.clear);
    module_backend.bb_base=client;
    CHECK(module_backend.probe());
    module_backend.init();
    CHECK(active && routed && !failed);
    geforce_heat_stop_request(-1,120000,-2);
    CHECK(requests==1 && test_heat.latched);
    CHECK(!test_heat.stop() && !active && !routed && !failed);
    CHECK(events[n-2]==5 && events[n-1]==6);
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
    CHECK(test_heat.clear()==GF5_EIO && test_heat.latched);
    read_rc=0; temperature=80001;
    CHECK(test_heat.clear()==GF5_EBUSY && test_heat.latched);
    temperature=80000;
    CHECK(!test_heat.clear() && n==(int)before && !active && !routed);
    /* The resident owner clears only after this callback succeeds. */
    test_heat.latched=false;
    CHECK(module_backend.probe());
    module_backend.enter();
    CHECK(active && routed && !failed);
    finish(0);
}
