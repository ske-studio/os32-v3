#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
/* Production lowering/bind/access with synthetic device values. Every C
 * section runs through its real access/control operation, without barriers. */
#define GF_S5_ENTRY c_old_bus_entry
#include "gfinit_bus_host.c"
#undef GF_S5_ENTRY
#include "../../kdebug/gfinit_plan.c"
#include "../../kdebug/gfinit_mode.c"
#include "gf_display_calc_host.inc"
#include "../../hwdev/gf_load_state.c"
#include "../../kdebug/gfinit_plan_c.c"
#include "../../kdebug/gfinit_verify.inc"
#include "../../kdebug/recover_profile.c"
static struct gfinit_plan composed;
static u8 frame[GF_MODE_VRAM_BYTES];
static u32 frame_writes, frame_reads, drains, frame_fail_at, frame_abort_at, frame_points;
static bool vram_active, color_active;
static u32 color_count, color_fail_at, color_row, color_events[850][3];
static int color_operation(void *v,struct recover *r,u32 row,u32 value)
{
    (void)v;color_active=true;color_row=row;
    int rc=gfinit_plan_c_color(&device,r,row,value);
    color_active=false;return rc;
}
static int color_memory(bool write,u32 va,u32 width,u32 *value)
{
    u32 pa=mapped_pa+va-MEM_DEVICE_APERTURE_BASE, off=pa-(rawbars[0]&~15U);
    CHECK(width==1 && !irq_state && color_count<850);
    color_events[color_count][0]=write;color_events[color_count][1]=off;color_events[color_count][2]=*value;
    return color_count++==color_fail_at ? -1 : 0;
}
/* Independent pixel oracle: frame > 8x8 checker > eight vertical bands. */
static u32 frame_pixel(u32 off)
{
    u32 x=off%640,y=off/640;
    if(x<8 || x>=632 || y<8 || y>=472)return 4;
    if(x<72 && y<72)return (((x-8)/8+(y-8)/8)&1)?0:4;
    return x/80;
}
static int frame_load(void *v,u32 va,u32 width,u32 *value)
{
    if(color_active)return color_memory(false,va,width,value);
    u32 pa=mapped_pa+va-MEM_DEVICE_APERTURE_BASE,base=rawbars[1]&~15U;
    if(pa>=base && pa-base<sizeof(frame)) {
        u32 off=pa-base;
        CHECK(!irq_state && width==4 && mapped_flags==(PTE_PRESENT|PTE_PCD));
        CHECK(frame_writes==sizeof(frame) && drains==1 && off==frame_reads);
        *value=0;
        for(u32 i=0;i<4;i++)*value|=(u32)frame[off+i]<<(8*i);
        frame_reads+=4;
        return off==frame_fail_at?-1:0;
    }
    if(vram_active && pa==(rawbars[0]&~15U)+GF_PLAN_PFB_AMOUNT) {
        CHECK(!irq_state && width==4 && frame_writes==sizeof(frame) && !frame_reads);
        drains++;*value=0x10000000;return 0;
    }
    return memory_read(v,va,width,value);
}
static int frame_store(void *v,u32 va,u32 width,u32 value)
{
    if(color_active)return color_memory(true,va,width,&value);
    u32 pa=mapped_pa+va-MEM_DEVICE_APERTURE_BASE,base=rawbars[1]&~15U;
    if(pa>=base && pa-base<sizeof(frame)) {
        u32 off=pa-base;
        CHECK(!irq_state && width==4 && mapped_flags==(PTE_PRESENT|PTE_PCD|PTE_RW));
        CHECK(off==frame_writes && !frame_reads);
        for(u32 i=0;i<4;i++) {
            frame[off+i]=(u8)(value>>(8*i));
            CHECK(frame[off+i]==frame_pixel(off+i));
        }
        frame_writes+=4;
        return off==frame_fail_at?-1:0;
    }
    return memory_write(v,va,width,value);
}
static int frame_point(void *v,u32 sid)
{
    (void)v;(void)sid;
    if(vram_active) {
        CHECK(irq_state==1 && pte==0x876540a5 && !device.window.owned && !window_busy);
        frame_points++;
        if(frame_writes==frame_abort_at || frame_reads==frame_abort_at)return 1;
    }
    return 0;
}
static int frame_operation(void *v,struct recover *r,bool write,u32 *bad,u32 *want,u32 *got)
{
    (void)v;vram_active=true;
    int rc=gfinit_plan_c_vram(&device,r,write,bad,want,got);
    vram_active=false;
    return rc;
}
/* Same reservation callbacks as S0; forbid whole-screen operations in RUN. */
static u32 route_value,route_writes,route_reads,route_fault,route_mode_reads,route_mode_writes,route_noise;
static u32 cell_value,cell_calls,cell_writes,cell_fault,cell_runs,cell_trace[8];
static int c_route_read(void *v,u32 *value)
{ (void)v;route_reads++;if(recover_mode_route_active(&core))route_mode_reads++;*value=(route_value | route_noise) ^ (route_fault==1 ? 2U : 0U);return route_fault==3 ? -1 : 0; }
static int c_route_write(void *v,u32 value)
{
    (void)v;
    if(value!=0 && (value!=1 || !recover_mode_route_active(&core)))return -1;
    route_writes++;if(value==1)route_mode_writes++;route_value=value;return route_fault==2 ? -1 : 0;
}
static int cell_read(void *v,u32 *value)
{
    (void)v;CHECK(cell_calls<8);cell_trace[cell_calls++]=0;
    *value=cell_value;
    if((cell_fault==1 && cell_calls==3) || (cell_fault==3 && cell_calls==5))*value^=1;
    return cell_fault==4 && cell_calls==1 ? -1 : 0;
}
static int cell_write(void *v,u32 value)
{
    (void)v;CHECK(cell_calls<8);cell_trace[cell_calls++]=value;
    cell_writes++;cell_value=value;
    return cell_fault==2 && cell_writes==1 ? -1 : 0;
}
static const struct recover_screen_ops cell_ops={.read_cell=cell_read,.write_cell=cell_write};
static int c_cell(void *v,struct recover *r,u32 *actual)
{
    (void)v;CHECK(r->state==REC_RUNNING && (route_value&3)==1);
    cell_runs++;
    int rc=recover_cell_98(r,&cell_ops,0);*actual=!rc;return rc;
}
static void field(const char *s,u32 v){output(s);output("=");number(v);output("\n");}
static void val(u32 v){number(v);output(" ");}
/* Record reads as well as the existing write/delay trace with logging on. */
static int prefix_put(void *v,u32 ch){(void)v;(void)ch;return 0;}
static const struct recover_transport prefix_transport={
    .uart={exec_log_ready,prefix_put,exec_log_wait},.frame_us=1,.max_polls=1048576};
static int prefix_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=bus_read_test(v,a,value);
    if(!rc)trace_add(actual,&actual_count,REC_READ,a->key,*value);
    return rc;
}
static void c_build(void)
{
    bus_setup();fixture();
    CHECK(recover_gf_profile_expand(&profile,&composed)==0);
    CHECK(gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded)==0);
    CHECK(gfinit_plan_build_c(&composed,&decoded,&profile,0x1000000,rom,sizeof(rom))==0);
    /* Synthetic clock stays at zero. These fixture limits let trial_step
     * exercise the existing cleanup; production C budgeting remains 2c. */
    profile.trial_limit=profile.restore_limit=1000;
    profile.max_steps=4096;profile.restore_max_polls=65536;profile.restore_log_bytes=65536;
    decoded.plan=composed.plan;
    CHECK(contract_bind()==0);
}
static void c_arm(void)
{
    for(u32 i=0;i<profile.count;i++)core.saved[i]=rawbars[i];
    core.saved_count=profile.count;
    composed.plan.profile_crc=profile.crc;exec_arm();core.plan=&composed.plan;
    core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
    core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
    executor_bus.read=bus_read_test;executor_bus.write=bus_write_test;
    for(u32 i=0;i<profile.target_count;i++)values[i]=0;
    CHECK(!gfinit_exec_prepare(&core,&execution));
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        execution.main_id=main;CHECK(gfinit_exec_run(&core,&execution)==0);
    }
    CHECK(core.expected_node==composed.c_node);
    executor_bus.mode_wait=exec_delay;executor_bus.mode_vram=frame_operation;executor_bus.mode_color=color_operation;executor_bus.abort_requested=frame_point;
    memory_ops.load=frame_load;memory_ops.store=frame_store;
    executor_bus.route_read=c_route_read;executor_bus.route_write=c_route_write;executor_bus.mode_cell=c_cell;
    route_value=route_writes=route_reads=route_fault=route_mode_reads=route_mode_writes=route_noise=0;
    cell_value=0x1234;cell_calls=cell_writes=cell_fault=cell_runs=0;
    frame_writes=frame_reads=drains=frame_points=0;
    frame_fail_at=frame_abort_at=color_fail_at=RECOVER_NONE;vram_active=color_active=false;color_count=0;
}
static bool frame_contains(const char *s,const char *part)
{
    for(u32 i=0;s[i];i++) {
        u32 j=0;while(part[j] && s[i+j]==part[j])j++;
        if(!part[j])return true;
    }
    return false;
}
#include "../../kdebug/gf_stage_state.c"
#pragma GCC diagnostic pop
