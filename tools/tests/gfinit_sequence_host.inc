/* A2 -> main0..4 with the real begin/step boundaries, not exec_arm().
 * Init instruction bytes match S1; support tables and hardware are synthetic. */
static int sequence_prepare(struct recover *r,void *arg)
{
    (void)arg;
    while(r->expected_node<recover_gf_prepare_plan.node_count) {
        u32 id=r->expected_node,value;
        if(recover_plan_enter(r,id) || recover_plan_access(r,&value) ||
           recover_plan_leave(r,0))return -1;
    }
    return 0;
}
static bool sequence_gpio_change;
static u32 sequence_gpio_reads,sequence_gpio_value;
static void sequence_gpio_sample(const struct recover_bus_access *a)
{
    /* A deterministic status change outside the instruction's write mask.
     * The real failing REC log omits read values, so this models a possible
     * hardware difference; it does not claim those real samples were measured. */
    if(core.entered && core.plan->nodes[core.cursor].pc==0xdebd && a->offset==0x60081c) {
        if(sequence_gpio_change && !sequence_gpio_reads)values[a->key]^=0x80000000U;
        sequence_gpio_reads++;
    }
}
static int sequence_bus_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=prefix_read(v,a,value);
    if(!rc)sequence_gpio_sample(a);
    return rc;
}
static int sequence_bus_write(void *v,const struct recover_bus_access *a,u32 value)
{
    if(core.state==REC_RUNNING && core.entered && core.plan->nodes[core.cursor].pc==0xdebd)
        sequence_gpio_value=value;
    return bus_write_test(v,a,value);
}
static void sequence_run(bool mode_c,bool record,bool changing,bool bad_keep)
{
    bus_setup();fixture();
    CHECK(!recover_gf_profile_expand(&profile,&composed));
    profile.expected_bdf=RECOVER_GF_BDF;profile.expected_device=RECOVER_GF_DEVICE;
    CHECK(!gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded));
    CHECK(!(mode_c ? gfinit_plan_build_c(&composed,&decoded,&profile,0x1000000,rom,sizeof(rom)) :
                    gfinit_plan_build(&composed,&decoded,&profile,0x1000000)));
    if(mode_c)CHECK(!gfinit_verify_policy(&composed,&profile,record?GF_VERIFY_RECORD:GF_VERIFY_REQUIRED,0,0));
    if(bad_keep) for(u32 i=0;i<composed.c_node;i++) {
        const struct recover_plan_node *n=&decoded.nodes[i];
        if(n->pc==0xdebd)decoded.args[n->arg_first+2].value&=~0x80000000U;
    }
    stage_fixture=(struct gf_stage_state){0};
    CHECK(!gfinit_plan_baseline(&composed,&profile,&stage_fixture,rom,sizeof(rom)));
    profile.trial_limit=profile.opcode_limit=profile.restore_limit=1000;
    profile.max_steps=4096;profile.max_polls=65536;
    profile.restore_max_polls=65536;profile.restore_log_bytes=65536;
    profile.crc=recover_profile_crc(&profile);composed.plan.profile_crc=profile.crc;
    executor_bus=ops;executor_bus.read=sequence_bus_read;executor_bus.write=sequence_bus_write;
    core.ops=&executor_bus;core.transport=&prefix_transport;
    core.cache.identity=ident;core.gf_stage=&stage_fixture;core.route_write_approved=true;core.verify_record=record;
    sequence_gpio_change=changing;sequence_gpio_reads=0;
    clock_value=clock_jump=wait_total=clock_fail=wait_fail=bus_fail=actual_count=expected_count=0;
    execution=(struct gfinit_exec){.rom=rom,.rom_size=sizeof(rom),.ops=&executor_ops};
    values[key_for(&profile,0x60081c,0,0,0)]=0xa5a50f55;
    CHECK(!recover_bind_plan(&core,&profile,&composed.plan,&buffers,&hash));
    int rc=trial_begin(&core,&profile,hash,profile.trial_limit);
    CHECK(!rc);if(rc)return;
    CHECK(!trial_step(&core,&core.token,true,sequence_prepare,0));
    CHECK(core.state==REC_PAUSED && core.expected_node==composed.entries[1].first);
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        execution.main_id=main;
        rc=trial_step(&core,&core.token,true,gfinit_exec_run,&execution);
        if(bad_keep && !main) {
            CHECK(rc && execution.fail_pc==0xdebd && core.reason==REC_CONTRACT);
            return;
        }
        if(rc) {output("sequence failure pc/reason ");number(execution.fail_pc);output("/");number(core.reason);output("\n");}
        CHECK(!rc);if(rc)return;
        CHECK(core.state==REC_PAUSED);
    }
    CHECK(!trial_step(&core,&core.token,true,gf_stage_capture,&stage_fixture));
    CHECK(stage_fixture.initialized && core.expected_node==(mode_c?composed.c_node:composed.vram_node));
    CHECK(sequence_gpio_reads==2);
    CHECK(sequence_gpio_value==(((0xa5a50f55U ^ (changing?0x80000000U:0)) & 0xfffeefefU) | 0x11000U));
}
static void sequence_cases(void)
{
    u32 before=failures;
    for(u32 changing=0;changing<2;changing++) {
        sequence_run(true,false,changing,false);
        sequence_run(true,true,changing,false);
        sequence_run(false,false,changing,false);
    }
    sequence_run(true,false,false,true); /* Incorrect preservation is still rejected. */
    if(failures==before)output("A2 -> main0..4 -> baseline required/record/A4 stable/changing GPIO PASS\n");
}
#ifdef HWDEV_SEQUENCE_IMAGE
#include "../../drivers/serial.h"
#include "../../hwdev/owner.h"
#include "hwdev_trace_symbols.h"
static struct gfinit_owner *sequence_bundle;
static u8 sequence_flow[RECOVER_NODE_MAX*RECOVER_BITS_WORDS*sizeof(u32)] __attribute__((aligned(PAGE_SIZE)));
static struct recover *sequence_recover_owner(void) {return &core;}
static int sequence_recover_owner_configure(const struct recover_ops *ops,void *opaque,
                                            const struct recover_profile *s0,bool approved)
{
    CHECK(ops && s0);
    core.ops=ops;core.opaque=opaque;core.s0_profile=s0;core.route_write_approved=approved;
    return 0;
}
static void *sequence_kmemset(void *p,int c,u32 n)
{for(u32 i=0;i<n;i++)((u8 *)p)[i]=c;return p;}
static u32 sequence_pgalloc_alloc_phys(u32 who,int n)
{(void)who;CHECK(n>0 && (u32)n*PAGE_SIZE<=sizeof(sequence_flow));return V2P(sequence_flow);}
static int sequence_pgalloc_free_n_owner(u32 who,u32 pfn,int n)
{(void)who;(void)pfn;(void)n;return 1;}
static const struct serial_setup *sequence_serial_get_setup(void)
{static const struct serial_setup s={.actual=9600};return &s;}
static int sequence_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    active=a;profile=sequence_bundle->profile;
    int rc=((__typeof__(&gfinit_bus_read))(uptr)hwdev_image_symbol(SEQUENCE_GF_READ))(v,a,value);
    if(!rc)sequence_gpio_sample(a);
    return rc;
}
static int sequence_write(void *v,const struct recover_bus_access *a,u32 value)
{
    active=a;profile=sequence_bundle->profile;
    if(core.state==REC_RUNNING && core.entered && core.plan->nodes[core.cursor].pc==0xdebd)
        sequence_gpio_value=value;
    return ((__typeof__(&gfinit_bus_write))(uptr)hwdev_image_symbol(SEQUENCE_GF_WRITE))(v,a,value);
}
static void sequence_owner_run(bool mode_c,bool record,bool changing)
{
    bus_setup();fixture();
    struct gfinit_owner *g=(void *)(uptr)hwdev_image_symbol(SEQUENCE_CONTROL);
    /* Each variant starts as a fresh inspected owner after A1. */
    sequence_bundle=g;sequence_kmemset(g,0,sizeof(*g));
    sequence_kmemset((void *)(uptr)hwdev_image_symbol(SEQUENCE_STAGE),0,sizeof(struct gf_stage_state));
    sequence_gpio_change=changing;sequence_gpio_reads=0;
    clock_value=clock_jump=wait_total=clock_fail=wait_fail=bus_fail=actual_count=expected_count=0;
    *(struct gfinit_owner **)(uptr)hwdev_image_symbol(SEQUENCE_OWNER)=g;
    struct hwdev_lease *lease=(void *)(uptr)hwdev_image_symbol(SEQUENCE_GF_LEASE);
    *lease=(struct hwdev_lease){1,1,core.token.session,true};
    g->generation=1;g->inspected=true;g->rom=rom;g->rom_size=sizeof(rom);
    g->decoded=&decoded;g->compiled=&composed;g->profile=profile;g->bus=device;
    g->bus.profile=&g->profile;g->s0=&gf_fixture_profile;g->ops=ops;
    g->ops.read=sequence_read;g->ops.write=sequence_write;
    core.cache.identity=ident;
    struct recover_transport transport=prefix_transport;
    core.transport=&transport;
    values[key_for(&profile,0x60081c,0,0,0)]=0xa5a50f55;
    typedef int (*call_type)(u32,void *,u32,const struct recover_ops *,struct recover_transport *,const struct recover_profile *);
    call_type call=(call_type)(uptr)hwdev_image_symbol(SEQUENCE_GFINIT_CALL);
    u32 flags=GF_ALLOW_WRITE | (mode_c ? GF_MODE_C | (record?GF_VERIFY_RECORD:GF_VERIFY_REQUIRED) : 0U);
    struct gfinit_request q={.version=GF_INIT_VERSION,.size=sizeof(q),.flags=flags,
        .max_steps=GF_INIT_SCHEDULE,.session=core.token.session};
    int rc=call(GF_OP_PREPARE,&q,sizeof(q),&ops,&transport,&gf_fixture_profile);
    CHECK(!rc);if(rc){output("owner prepare status ");number(q.status);output("\n");return;}
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        q=(struct gfinit_request){.version=GF_INIT_VERSION,.size=sizeof(q),.flags=GF_ALLOW_WRITE,
            .max_steps=GF_INIT_SCHEDULE,.session=core.token.session,.script_id=main};
        rc=call(GF_OP_RUN_ONE,&q,sizeof(q),&ops,&transport,&gf_fixture_profile);
        if(rc){output("owner failure pc/reason ");number(q.fail_pc);output("/");number(core.reason);output("\n");}
        CHECK(!rc);if(rc)return;
    }
    CHECK(g->next_main==GF_INIT_MAINS && core.gf_stage && core.gf_stage->initialized);
    CHECK(sequence_gpio_reads==2);
    CHECK(sequence_gpio_value==(((0xa5a50f55U ^ (changing?0x80000000U:0)) & 0xfffeefefU) | 0x11000U));
}
static void sequence_owner_cases(void)
{
    u32 before=failures;
    for(u32 changing=0;changing<2;changing++) {
        sequence_owner_run(true,false,changing);
        sequence_owner_run(true,true,changing);
        sequence_owner_run(false,false,changing);
    }
    if(failures==before)output("KMOD gfinit_call A2 -> main0..4 -> baseline required/record/A4 stable/changing GPIO PASS\n");
}
#endif
