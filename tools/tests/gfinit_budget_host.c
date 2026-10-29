/* Count production C records and follow real failure cleanup through S0.
 * The inherited fixture replaces PCI/PTE/memory only; plans and recovery run. */
#define GF_C_ENTRY budget_old_entry
#include "gfinit_plan_c_host.c"
#undef GF_C_ENTRY
#include "../../kdebug/gfinit_budget.inc"

static u32 run_rows,run_bytes,pre_rows,pre_bytes,s0_rows,s0_bytes,line_used;
static u32 budget_fault,budget_tick,quiesce_seen,local_count,cleanup_p,cleanup_q;
static u32 c_bound,whole_bound,sequence_peak;
static char budget_line[RECOVER_ROW_MAX];
static bool restoring_seen,final_seen;
static u32 vram_rec,dac_rec,ar_rec,wait_rec,check_rec,cell_rec,route_rec;
static int budget_put(void *v,u32 ch)
{
    (void)v;
    CHECK(line_used+1<sizeof(budget_line));
    budget_line[line_used++]=(char)ch;budget_line[line_used]=0;
    if(core.state==REC_RESTORING) {
        if(core.in_s0)s0_bytes++;else pre_bytes++;
    } else run_bytes++;
    if(ch!='\n')return 0;
    sequence_peak=core.seq;
    if(core.state==REC_RESTORING) {
        if(core.in_s0)s0_rows++;else pre_rows++;
        if(frame_contains(budget_line,"event=RESTORING"))restoring_seen=true;
        if(frame_contains(budget_line,"phase=final"))final_seen=true;
    } else {
        run_rows++;
        if(frame_contains(budget_line," len=307200 "))vram_rec++;
        if(frame_contains(budget_line," len=768 "))dac_rec++;
        if(frame_contains(budget_line," row=") && !frame_contains(budget_line," len=768 "))ar_rec++;
        if(frame_contains(budget_line,"phase=wait "))wait_rec++;
        if(frame_contains(budget_line,"phase=check "))check_rec++;
        if(frame_contains(budget_line,"phase=cell "))cell_rec++;
        if(frame_contains(budget_line,"phase=route "))route_rec++;
    }
    line_used=0;
    return 0;
}
static const struct recover_transport budget_transport={
    .uart={exec_log_ready,budget_put,exec_log_wait},.frame_us=1042,
    .max_polls=RECOVER_CLEANUP_TRANSPORT_POLLS};
static u32 budget_clock(void *v){(void)v;return budget_tick;}
static int budget_local(void *v)
{
    (void)v;cleanup_calls++;local_count=core.local_saved_count;
    cleanup_p=cb.bytes-cb.s0_bytes;cleanup_q=cb.s0_bytes;
    CHECK(restoring_seen && !quiesce_seen);
    return gfinit_bus_local_cleanup(&device,&core);
}
static int budget_write(void *v,const struct recover_bus_access *a,u32 value)
{
    if(core.state==REC_RESTORING && core.auth_phase==REC_QUIESCE) {
        const u32 offsets[]={0x1850,0x1854,0x140};
        CHECK(quiesce_seen<3 && a->offset==offsets[quiesce_seen] && value==0);
        CHECK(!core.local_saved_count);quiesce_seen++;
    }
    if(core.state==REC_RUNNING && budget_fault==12 &&
       composed.plan.nodes[core.cursor].rule_id==REC_GF_C_RULE_FIRST+204)return -1;
    return bus_write_test(v,a,value);
}
static int budget_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    if(core.state==REC_RUNNING && budget_fault==13 && damaged_check())fault=3;
    int rc=bus_read_test(v,a,value);
    if(core.state==REC_RUNNING && damaged_check()) {
        if(budget_fault==1)*value^=check_damage_mask;
        if(budget_fault==2)return -1;
    }
    return rc;
}
static int budget_route_read(void *v,u32 *value)
{
    (void)v;
    *value=route_value;
    if(core.state==REC_RUNNING && budget_fault==3 && recover_mode_route_active(&core))*value^=2;
    return 0;
}
static int budget_route_write(void *v,u32 value)
{
    (void)v;
    if(value==0)CHECK(core.in_s0 && quiesce_seen==((budget_fault==5 || budget_fault==13)?0U:3U));
    route_value=value;route_writes++;return value==1 && budget_fault==14 ? -1 : 0;
}
static int budget_safe(void *v)
{ (void)v;CHECK(core.in_s0 && quiesce_seen==((budget_fault==5 || budget_fault==13)?0U:3U) && route_value==0);return recover_gf_screen_safe(&core); }
static int budget_cell_read(void *v,u32 *value)
{ (void)v;*value=cell_value;return 0; }
static int budget_cell_write(void *v,u32 value)
{ (void)v;cell_value=value;return 0; }
static const struct recover_screen_ops budget_screen_ops={
    .cui=ok,.safe=ok,.restore=ok,.read_cell=budget_cell_read,.write_cell=budget_cell_write,.redraw=ok};
static int budget_screen(void *v)
{
    (void)v;CHECK(core.in_s0 && !route_value);
    return recover_screen_98(&core,&budget_screen_ops,0);
}
static int budget_step(struct recover *r,void *v)
{
    (void)v;
    while(r->expected_node!=RECOVER_NONE) {
        u32 id=r->expected_node,value=0,edge=0;
        const struct recover_plan_node *n=&composed.plan.nodes[id];
        const struct recover_opcode_layout *l=r->node_layout[id];
        if(budget_fault==4 && n->substep==GF_C_VRAM_COMPARE)frame[sizeof(frame)-1]^=1;
        if(budget_fault==5 && n->substep==GF_C_VRAM_COMPARE)frame_fail_at=sizeof(frame)-4;
        if(budget_fault==6 && (l->flags & REC_LAYOUT_MODE_CHECK) &&
           composed.plan.args[n->arg_first+REC_CHECK_SLOT].value==95)budget_tick=core.budget;
        if(budget_fault==10 && (l->flags & REC_LAYOUT_MODE_CHECK) &&
           composed.plan.args[n->arg_first+REC_CHECK_SLOT].value==95)core.tx_ticks=core.log_limit;
        if(budget_fault==11 && n->substep==GF_C_WAIT && frame_reads==sizeof(frame))wait_fail=1;
        if(recover_safe_point(r) || recover_plan_enter(r,id))return -1;
        int rc=0;
        if(l->flags & (REC_LAYOUT_MODE_CHECK|REC_LAYOUT_MODE_WAIT|REC_LAYOUT_MODE_ROUTE))
            rc=gfinit_plan_c_control(&composed,r);
        else if(l->flags & REC_LAYOUT_CHECK_EXPR)rc=recover_plan_check(r,&edge);
        else if(l->dir_substep!=RECOVER_NONE || (l->flags & (REC_LAYOUT_MODE_VRAM|REC_LAYOUT_MODE_COLOR)))
            rc=recover_plan_access(r,&value);
        if(rc || recover_plan_leave(r,edge))return -1;
    }
    return 0;
}
static int budget_noop(struct recover *r,void *v){(void)r;(void)v;return 0;}
static void budget_setup(u32 fault_kind,bool fp_on)
{
    c_build();c_arm();profile.expected_device=RECOVER_GF_DEVICE;budget_fault=fault_kind;budget_tick=0;
    values[key_for(&profile,0x680848,0,0,0)]=fp_on?0x10000000:0x20000022;
    executor_bus.ticks=budget_clock;executor_bus.read=budget_read;executor_bus.write=budget_write;
    executor_bus.local_cleanup=budget_local;executor_bus.route_read=budget_route_read;
    executor_bus.route_write=budget_route_write;executor_bus.screen_safe=budget_safe;
    executor_bus.screen_restore_verify=budget_screen;
    core.route_write_approved=true;core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
    core.transport=&budget_transport;exec_log_busy=false;
    run_rows=run_bytes=pre_rows=pre_bytes=s0_rows=s0_bytes=line_used=0;
    quiesce_seen=local_count=cleanup_p=cleanup_q=sequence_peak=0;restoring_seen=final_seen=false;
    vram_rec=dac_rec=ar_rec=wait_rec=check_rec=cell_rec=route_rec=0;
    check_damage_slot=RECOVER_NONE;check_damage_mask=1;
    /* Last target CHECK: maximal C prefix before a target-read fault. */
    if(fault_kind==1 || fault_kind==2)check_damage_slot=composed.c_check_count-3;
    if(fault_kind==13)check_damage_slot=composed.c_check_count-4; /* last MMIO: PBUS1854 */
    if(fault_kind==7)cell_fault=1; /* test mismatch after the whole mode */
    if(fault_kind==8)check_damage_slot=1;
    if(fault_kind==8)budget_fault=1;
    CHECK(!gfinit_c_rows(&profile,&composed.plan,composed.c_node,&c_bound));
    CHECK(!gfinit_log_rows(&profile,&composed.plan,composed.c_node,&whole_bound));
    core.log_limit=(2*((whole_bound*(RECOVER_ROW_MAX+1U)*10U+9599U)/9600U)+3U)*PIT_HZ;
    core.token.profile_crc=profile.crc=recover_profile_crc(&profile);
    core.token.snapshot_crc=snapshot_crc(&core);
}
static void budget_cases(void)
{
    /* 4a's formula is preserved for small, maximum and overflow inputs. */
    const u32 counts[]={0,1,100,2048};
    for(u32 i=0;i<4;i++) {
        struct recover_plan p={.node_count=counts[i],.arg_count=counts[i]*3};u32 rows=0;
        CHECK(!gfinit_log_rows(&profile,&p,RECOVER_NONE,&rows));
        CHECK(rows==128+counts[i]*12+counts[i]*3*4);
    }
    struct recover_plan overflow={.node_count=~0U};u32 rows=0;
    CHECK(gfinit_log_rows(&profile,&overflow,RECOVER_NONE,&rows)<0);
    for(u32 fault_kind=0;fault_kind<15;fault_kind++) {
        budget_setup(fault_kind,fault_kind!=9);
        u32 crc=profile.crc,hash=core.token.plan_hash,saved=core.local_saved_count;
        int rc=trial_step(&core,&core.token,true,budget_step,0);
        bool bad=fault_kind!=0 && fault_kind!=9;
        CHECK((rc!=0)==bad);
        if(!bad) {
            CHECK(core.state==REC_PAUSED && !core.failed && route_value==1);
            /* PAUSED wall time is not charged to the trial. */
            u32 spent=core.spent;budget_tick+=core.budget*2;
            core.transport=0;
            CHECK(!trial_step(&core,&core.token,true,budget_noop,0));
            core.transport=&budget_transport;
            CHECK(core.spent==spent);
            CHECK(!trial_end(&core,&core.token,REC_ABORT));
        }
        CHECK(core.s0_verified && route_value==0 && restoring_seen && final_seen);
        CHECK(cleanup_calls==1 && quiesce_seen==((fault_kind==5 || fault_kind==13)?0U:3U) && !core.plan);
        CHECK(core.state==(bad?REC_FAILED:REC_VERIFIED));
        static const enum recover_reason reasons[]={REC_ABORT,REC_VERIFY,REC_BUS,REC_VERIFY,
            REC_VERIFY,REC_BUS,REC_TIMEOUT,REC_BUS,REC_VERIFY,REC_ABORT,REC_LOG,REC_BUS,REC_BUS,REC_BUS,REC_BUS};
        CHECK(core.reason==reasons[fault_kind]);
        CHECK(!core.lost_lines && run_rows<=c_bound && run_bytes<=c_bound*(RECOVER_ROW_MAX+1U));
        /* P: 24 local + 6 quiesce + 4 zero-guard pairs; 188 B/pair,
         * RESTORING 178 B. Q: 14 pairs + final/discard/diag=3122 B. */
        CHECK(local_count==7 && saved==7 && cleanup_p==34*188+178);
        CHECK(cb.s0_bytes==4096 && 14*188+222+91+177==3122);
        CHECK(cb.seconds==23 && cb.ticks==2300 && cb.s0_ticks==1000);
        CHECK(cb.polls==32324 && cb.s0_polls==16384 && core.restore_polls<cb.polls);
        CHECK(core.uart_bytes==pre_bytes+s0_bytes && core.uart_bytes<cb.bytes);
        struct rec_cleanup_budget maximum;
        CHECK(!cleanup_budget_compute(&core,&profile,true,&maximum));
        CHECK(maximum.polls<=budget_transport.max_polls);
        if(!fault_kind) {
            CHECK(vram_rec==4 && dac_rec==2 && ar_rec==48 && wait_rec==6);
            CHECK(check_rec==66 && cell_rec==4 && route_rec==2);
            CHECK(run_rows==2298 && c_bound==2298);
            CHECK(128+composed.plan.node_count*12+composed.plan.arg_count*4==32960);
            output("BUDGET VRAM=4 DAC=2 AR=48 WAIT=6 CHECK=66 CELL=4 ROUTE=2 PASS\n");
        }
        CHECK(pre_bytes<=cleanup_p && s0_bytes<=cleanup_q && cleanup_q==4096);
        CHECK(profile.crc==crc && core.token.profile_crc==crc && core.token.plan_hash==hash);
        output("BUDGET case=");val(fault_kind);output("c_rows=");val(c_bound);output("actual=");val(run_rows);
        output("bytes=");val(run_bytes);output("whole_rows=");val(whole_bound);
        output("local_before=");val(saved);output("local_cleanup=");val(local_count);
        output("P=");val(cleanup_p);output("S=");val(cleanup_q);output("seconds=");val(cb.seconds);
        output("pre_rows=");val(pre_rows);output("pre_bytes=");val(pre_bytes);
        output("s0_rows=");val(s0_rows);output("s0_bytes=");val(s0_bytes);output("\n");
    }
    output("BUDGET legacy formula, C records, failures local/quiesce/route/S0, CRC and PAUSED PASS\n");
}
void _start(void)
{
    gf_fixture_init();budget_cases();
    output(failures?"BUDGET FAIL\n":"BUDGET PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");__builtin_unreachable();
}
