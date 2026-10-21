/* T2: real primitives, synthetic profiles. No real ROM/opcodes are enabled. */
struct window_fake { u32 pte, irq, maps, unmaps, loads, stores, fail, addr; };
static u32 win_irq_save(void *v) { struct window_fake *w=v; u32 old=w->irq; w->irq=0; return old; }
static void win_irq_restore(void *v,u32 flags) { ((struct window_fake *)v)->irq=flags; }
static u32 win_pte(void *v,u32 va) { (void)va; return ((struct window_fake *)v)->pte; }
static int win_map(void *v,u32 va,u32 pa)
{ struct window_fake *w=v; (void)va; w->maps++; w->pte=pa|3; return w->fail==1; }
static int win_unmap(void *v,u32 va,u32 raw)
{ struct window_fake *w=v; (void)va; w->unmaps++; w->pte=raw; return w->fail==2; }
static int win_load(void *v,u32 va,u32 width,u32 *value)
{ struct window_fake *w=v; w->loads++; w->addr=va; *value=width; return w->fail==3; }
static int win_store(void *v,u32 va,u32 width,u32 value)
{ struct window_fake *w=v; w->stores++; w->addr=va; CHECK(value==width); return w->fail==4; }
static const struct recover_window_ops win_ops = {
    win_irq_save,win_irq_restore,win_pte,win_map,win_unmap,win_load,win_store
};
static u32 hal_pte,hal_flags,hal_va,hal_pa,hal_pages,hal_restores;
u32 paging_pte_raw(u32 va) { hal_va=va; return hal_pte; }
int paging_map_phys(u32 va,u32 pa,u32 pages,u32 flags)
{ hal_va=va; hal_pa=pa; hal_pages=pages; hal_flags=flags; hal_pte=pa|flags; return 0; }
int paging_pte_restore(u32 va,u32 raw)
{ hal_va=va; hal_pte=raw; hal_restores++; return 0; }
static void windows(void)
{
    struct recover_window hw;
    recover_window_hal_init(&hw);
    struct recover_window_ops mixed=*hw.ops;
    struct window_fake data={0};
    mixed.load=win_load; mixed.store=win_store;
    hw.ops=&mixed; hw.opaque=&data;
    hal_pte=0x765430a5; hal_restores=0; recover_test_irq_restore(1);
    u32 datum=4;
    CHECK(!recover_window_access(&hw,0x10000ffc,4,false,&datum));
    CHECK(hal_va==MEM_DEVICE_APERTURE_BASE && hal_pa==0x10000000 && hal_pages==1);
    CHECK(hal_flags==(PTE_PRESENT|PTE_RW|PTE_PCD) && !(hal_flags&PTE_USER));
    CHECK(hal_pte==0x765430a5 && hal_restores==1 && recover_test_irq_save()==1);
    recover_test_irq_restore(1);
    for (u32 irq=0;irq<2;irq++) for (u32 fault=0;fault<5;fault++)
    for (u32 write=0;write<2;write++) for (u32 width=1;width<=4;width*=2) {
        struct window_fake wf={.irq=irq,.pte=0x123456a5,.fail=fault};
        struct recover_window w={.ops=&win_ops,.opaque=&wf,.va=MEM_DEVICE_APERTURE_BASE};
        u32 value=width;
        int rc=recover_window_access(&w,0x10000ffc,width,write,&value);
        bool bad=fault==1 || fault==2 || (fault==3 && !write) || (fault==4 && write);
        CHECK((rc<0)==bad);
        CHECK(wf.pte==0x123456a5 && wf.irq==irq && !w.owned);
        CHECK(wf.maps==1 && wf.unmaps==1);
        CHECK(wf.loads+wf.stores==(fault==1 ? 0U:1U));
        CHECK(recover_window_cleanup(&w)==rc && wf.unmaps==1);
        if (!bad) CHECK(wf.addr==MEM_DEVICE_APERTURE_BASE+0xffc);
    }
}
struct text_fake { u32 cell, saved, calls, fail_at, corrupt_at, writes, redraws; bool gui; };
static int text_hit(struct text_fake *t) { return ++t->calls==t->fail_at; }
static int text_cui(void *v) { return ((struct text_fake *)v)->gui; }
static int text_safe(void *v) { return text_hit(v); }
static int text_restore(void *v) { return text_hit(v); }
static int text_read(void *v,u32 *value)
{ struct text_fake *t=v; int rc=text_hit(t); *value=t->cell; if(t->calls==t->corrupt_at) *value^=1; return rc; }
static int text_write(void *v,u32 value)
{ struct text_fake *t=v; t->writes++; int rc=text_hit(t); t->cell=value; return rc; }
static int text_redraw(void *v) { struct text_fake *t=v; t->redraws++; return text_hit(t); }
static const struct recover_screen_ops text_ops={text_cui,text_safe,text_restore,text_read,text_write,text_redraw};
static void texts(void)
{
    /* 8 callbacks: decode, restore, save, test W/R, restore W/R, redraw. */
    for (u32 fault=0;fault<=8;fault++) {
        setup(false); r.profile=&profile; r.state=REC_RESTORING; r.in_s0=true;
        struct text_fake t={.cell=0x1234,.saved=0x1234,.fail_at=fault};
        int rc=recover_screen_98(&r,&text_ops,&t);
        CHECK((rc==0)==(fault==0));
        CHECK(t.cell==t.saved);
        if(fault<=3 && fault) CHECK(!t.writes);
        else CHECK(t.writes==2);
    }
    for(u32 corrupt=5;corrupt<=7;corrupt+=2) {
        setup(false); r.profile=&profile; r.state=REC_RESTORING; r.in_s0=true;
        struct text_fake t={.cell=0x42,.corrupt_at=corrupt};
        CHECK(recover_screen_98(&r,&text_ops,&t)<0 && t.cell==0x42);
    }
    setup(false); r.profile=&profile; r.state=REC_RESTORING; r.in_s0=true;
    struct text_fake t={.gui=true};
    CHECK(recover_screen_98(&r,&text_ops,&t)<0 && !t.calls && !t.writes);
}
static struct pit_setup pit_fake;
static u32 pit_count_fake,pit_latched,pit_step,pit_reads,pit_elapsed,pit_irq,pit_byte;
const struct pit_setup *pit_get_setup(void) { return &pit_fake; }
u32 recover_test_irq_save(void) { u32 old=pit_irq; pit_irq=0; return old; }
void recover_test_irq_restore(u32 flags) { pit_irq=flags; }
void recover_test_outp(u32 port,u32 value)
{
    CHECK(!pit_irq && port==PIT_MODE && value==PIT_LATCH_TIMER0);
    if(pit_step) {
        pit_count_fake=pit_count_fake>pit_step ? pit_count_fake-pit_step:pit_count_fake+pit_fake.reload-pit_step;
        pit_elapsed+=pit_step;
    }
    pit_latched=pit_count_fake; pit_byte=0;
}
u32 recover_test_inp(u32 port)
{
    CHECK(!pit_irq && port==PIT_CNTR0); pit_reads++;
    return pit_byte++ ? pit_latched>>8 : pit_latched&255;
}
static void frame_clock(void)
{
    const u32 clocks[]={SYSCLK_1997,SYSCLK_2458};
    for(u32 i=0;i<2;i++) for(u32 irq=0;irq<2;irq++) {
        pit_fake=(struct pit_setup){.clk_hz=clocks[i],.hz=PIT_HZ,.reload=clocks[i]/PIT_HZ,
            .period_us=10000,.mode=PIT_MODE_TIMER0,.valid=1};
        pit_count_fake=3; pit_step=1; pit_reads=pit_elapsed=0; pit_irq=irq;
        CHECK(!recover_hal_wait_frame(0,1042));
        u32 needed=(1042*(clocks[i]/100)+9999)/10000;
        CHECK(pit_elapsed==needed+1 && pit_irq==irq && pit_reads==2*(needed+1));
        /* Physical basis: 2081 counts at 1.9968MHz or 2561 at 2.4576MHz.
         * These integer inequalities bound quantization to <1us. They do not
         * claim an actual Ra266 elapsed-time measurement or bound IRQ/SMI. */
        CHECK((needed-1)*10000 < 1042*(clocks[i]/100));
        CHECK(needed*10000 >= 1042*(clocks[i]/100));
        CHECK(1000000/clocks[i]+(1000000%clocks[i]!=0)==1);
    }
    pit_step=0; pit_reads=0;
    CHECK(recover_hal_wait_frame(0,1042)<0 && pit_reads==2*(RECOVER_WAIT_SAMPLES+1));
    pit_fake.valid=0; pit_reads=0;
    CHECK(recover_hal_wait_frame(0,1042)<0 && !pit_reads);
    pit_fake.valid=1;
    CHECK(recover_hal_wait_frame(0,0)<0 && recover_hal_wait_frame(0,10000)<0);
}
struct uart_fake {u32 calls, waits, puts, busy, frame; bool error;};
static int uart_ready(void *v) { struct uart_fake *u=v; u->calls++; return u->error ? -1 : u->calls>u->busy; }
static int uart_put(void *v,u32 byte) { struct uart_fake *u=v; CHECK(byte==65); u->puts++; return 0; }
static int uart_wait(void *v,u32 us) { struct uart_fake *u=v; u->waits++; u->frame=us; f.tick+=us; return 0; }
static const struct recover_uart_ops uart_ops={uart_ready,uart_put,uart_wait};
static int event_uart(struct fake *f, enum recover_state state)
{
    struct uart_fake u={0};
    int rc=recover_uart_byte(f->core,&uart_ops,&u,65,1,1024);
    f->event_bytes[state]+=u.puts;
    return rc;
}
static void uart_events(void)
{
    setup(false); profile.restore_log_bytes=16; profile.crc=recover_profile_crc(&profile);
    f.uart_event=true; begin();
    CHECK(r.state==REC_PAUSED && !r.failed);
    CHECK(f.event_bytes[REC_SNAPSHOTTING]==1 && f.event_bytes[REC_ARMED]==1 &&
          f.event_bytes[REC_RUNNING]==1 && f.event_bytes[REC_PAUSED]==1);
    CHECK(r.spent==4 && r.uart_polls==4 && !r.in_call);
    CHECK(recover_safe_point(&r)<0);
    CHECK(!trial_end(&r,&r.token,REC_NORMAL));
    CHECK(f.event_bytes[REC_RESTORING]==1 && f.event_bytes[REC_VERIFIED]==1);
    /* Each call-state event must also charge its transmission time. */
    for(u32 s=REC_SNAPSHOTTING;s<=REC_PAUSED;s++) {
        setup(false); begin(); r.state=s; r.in_call=true; r.in_opcode=false;
        r.spent=r.budget-1; r.call_tick=f.tick;
        CHECK(event_uart(&f,s)<0 && r.failed && r.reason==REC_TIMEOUT);
    }
}
static void budgets(void)
{
    static const u32 bytes[]={4096,8192,16384}, sec[]={10,18,33}, polls[]={16384,32768,65536};
    for(u32 kind=0;kind<3;kind++) {
        setup(false);
        struct recover_static_work work={bytes[kind],2000+kind*2000,1024,4096,2000,1024};
        CHECK(!recover_budget_apply(&profile,kind,1000000,9600,10,&work));
        CHECK(profile.restore_limit==sec[kind]*1000000 && profile.restore_max_polls==polls[kind]);
        CHECK(profile.s0_reserve_ticks==(kind ? 10000000U:0U));
        CHECK(profile.s0_reserve_polls==(kind ? 16384U:0U));
        r.profile=&profile; r.state=REC_RESTORING; r.in_s0=true;
        struct uart_fake u={0};
        for(u32 i=0;i<bytes[kind];i++) CHECK(!recover_uart_byte(&r,&uart_ops,&u,65,1042,polls[kind]));
        CHECK(r.uart_polls==bytes[kind] && r.restore_polls==bytes[kind]);
        CHECK(u.puts==bytes[kind] && u.waits==bytes[kind] && u.frame==1042);
        CHECK(f.tick<profile.restore_limit);
        if(kind) {
            r.in_s0=false; f.tick=profile.restore_limit-profile.s0_reserve_ticks;
            CHECK(recover_restore_point(&r)<0 && r.failed && r.reason==REC_TIMEOUT);
            r.in_s0=true; CHECK(!recover_restore_point(&r) && r.failed);
            r.in_s0=false; r.restore_polls=0; r.uart_polls=0;
            r.uart_bytes=profile.restore_log_bytes-profile.s0_reserve_bytes;
            f.tick=0; r.failed=false;
            u32 before=u.puts;
            CHECK(recover_uart_byte(&r,&uart_ops,&u,65,1042,polls[kind])<0);
            CHECK(r.failed && r.reason==REC_LOG && u.puts==before);
            r.in_s0=true;
            CHECK(!recover_uart_byte(&r,&uart_ops,&u,65,1042,polls[kind]) && r.failed);
        }
        work.bytes++;
        CHECK(recover_budget_apply(&profile,kind,1000000,9600,10,&work)<0);
    }
    setup(false);
    struct recover_static_work work={4096,2000,1024,4096,2000,1024};
    CHECK(recover_budget_apply(&profile,0,0,9600,10,&work)<0);
    CHECK(recover_budget_apply(&profile,0,RECOVER_HALF_TICKS,9600,10,&work)<0);
    CHECK(recover_budget_apply(&profile,0,100,4800,10,&work)<0);
    CHECK(!recover_budget_apply(&profile,0,100,9600,10,&work) && profile.restore_limit==1000);
    r.profile=&profile; r.state=REC_RESTORING; r.in_s0=true;
    struct uart_fake u={.busy=2};
    /* µs fake clock here, independent of the 100Hz conversion assertion. */
    profile.restore_limit=10000000;
    CHECK(!recover_uart_byte(&r,&uart_ops,&u,65,1042,10));
    CHECK(r.uart_polls==3 && u.waits==3 && u.puts==1 && r.restore_polls==3);
    u.busy=~0U; f.tick=0; r.restore_tick=0;
    CHECK(recover_uart_byte(&r,&uart_ops,&u,65,1042,5)<0 && r.failed && r.uart_polls==5);
    CHECK(r.reason==REC_LOG);
    /* Independent from trial's remaining budget, wrap safe, no re-extension. */
    setup(false); begin(); f.tick=~0U-10; r.spent=r.budget-1;
    CHECK(!trial_end(&r,&r.token,REC_NORMAL) && r.restore_tick==~0U-10);
    u32 entry=r.restore_tick; CHECK(!trial_end(&r,&r.token,REC_NORMAL) && r.restore_tick==entry);
}
static void delays(void)
{
    setup(false); profile.restore_limit=10000000;
    r.profile=&profile; r.state=REC_RESTORING; r.in_s0=true;
    struct uart_fake u={0};
    CHECK(!recover_delay_ms(&r,3,uart_wait,&u));
    CHECK(u.waits==3 && u.frame==1000 && f.tick==3000 && r.restore_polls==3);
    profile.restore_limit=4500;
    CHECK(recover_delay_ms(&r,10,uart_wait,&u)<0 && u.waits==5 && r.failed);
    CHECK(r.reason==REC_TIMEOUT);
}
static void gf_profiles(void)
{
    for (u32 cold=0;cold<2;cold++) for(u32 size=0;size<2;size++) {
        setup(false);
        CHECK(!recover_gf_profile(&profile,cold,size ? REC_BUDGET_SIZE_BARS:REC_BUDGET_GF,100));
        profile.layouts=layouts; profile.layout_count=1;
        profile.crc=recover_profile_crc(&profile);
        CHECK(!recover_profile_ok(&profile));
        f.command_bus=true; f.command=0x0106; f.id.bdf=RECOVER_GF_BDF;
        f.id.device=RECOVER_GF_DEVICE;
        /* Explicitly measured synthetic BAR extents, no actual hardware. */
        bar_cache();
        if (size) { nodes[0].phase=REC_SIZE_BARS; entries[0].phase=REC_SIZE_BARS; }
        CHECK(!bound_begin(&r,&profile,0,100));
        CHECK(r.saved[0]==0x0106 && f.command==(size ? 0x0106U:0x0102U));
        if (size) {
            for(u32 i=0;i<3;i++) r.measurement.bar[i]=(struct recover_bar_extent){
                .raw=i ? 0:f.bar, .base=i ? 0:f.bar, .length=0x1000000U, .valid=1};
            f.command=RECOVER_COMMAND_OVERRIDE; /* local SIZE_BARS cleanup result */
        }
        f.n=0;
        CHECK(!trial_end(&r,&r.token,REC_NORMAL));
        CHECK(r.command_override && r.command_saved==0x0106 && r.command_actual==0x0102);
        CHECK(!r.s1_verified && r.s0_verified && f.command==0x0102);
        for(u32 i=0;i<f.n;i++) if(f.trace[i].op=='w' && f.trace[i].key==GF_CMD)
            CHECK(f.trace[i].value==0x0102);
    }
    setup(false); CHECK(!recover_gf_profile(&profile,false,REC_BUDGET_GF,100));
    profile.layouts=layouts; profile.layout_count=1; profile.crc=recover_profile_crc(&profile);
    f.command_bus=true; f.command=0x0106; bar_cache();
    CHECK(bound_begin(&r,&profile,0,100)<0 && !writes()); /* wrong BDF/ID */
}
static void a1_failed_config(void)
{
    for(u32 bad=0;bad<2;bad++) {
        setup(false); CHECK(!recover_gf_profile(&profile,false,REC_BUDGET_SIZE_BARS,100));
        profile.layouts=layouts; profile.layout_count=1; profile.crc=recover_profile_crc(&profile);
        f.command_bus=true; f.command=0x0106; f.id.bdf=RECOVER_GF_BDF; f.id.device=RECOVER_GF_DEVICE;
        nodes[0].phase=REC_SIZE_BARS; entries[0].phase=REC_SIZE_BARS;
        CHECK(!bound_begin(&r,&profile,0,100));
        for(u32 i=0;i<3;i++) r.measurement.bar[i]=(struct recover_bar_extent){
            .raw=i ? 0:f.bar,.base=i ? 0:f.bar,.length=0x1000000U,.valid=1};
        f.command=bad ? 0x0102:0; if(bad) f.bar+=0x1000;
        f.values[GF_CFG50]=f.values[GF_CFG54]=0;
        r.gpu_written=true; f.n=0;
        CHECK(trial_end(&r,&r.token,REC_NORMAL)<0 && r.state==REC_FAILED && r.s0_verified);
        CHECK(r.reason==REC_CONFIG);
        CHECK(!count('w',GF_OUT) && !count('w',GF_LEGACY) && !count('w',GF_IRQ));
    }
}
static void size_tail_setup(u32 max_polls)
{
    setup(false); CHECK(!recover_gf_profile(&profile,false,REC_BUDGET_SIZE_BARS,100));
    profile.restore_limit=33; profile.s0_reserve_ticks=10;
    profile.s0_reserve_polls=0; profile.restore_max_polls=max_polls;
    profile.layouts=layouts; profile.layout_count=1; profile.crc=recover_profile_crc(&profile);
    f.command_bus=true; f.command=0x0106; f.id.bdf=RECOVER_GF_BDF; f.id.device=RECOVER_GF_DEVICE;
    nodes[0].phase=REC_SIZE_BARS; entries[0].phase=REC_SIZE_BARS;
    CHECK(!bound_begin(&r,&profile,0,100));
    for(u32 i=0;i<3;i++) r.measurement.bar[i]=(struct recover_bar_extent){
        .raw=i ? 0:f.bar,.base=i ? 0:f.bar,.length=0x1000000U,.valid=1};
    f.command=RECOVER_COMMAND_OVERRIDE;
}
static void size_tail_deadline(void)
{
    size_tail_setup(65536);
    CHECK(!trial_end(&r,&r.token,REC_NORMAL));
    u32 calls=f.config_tail_calls, polls=f.config_polls;
    /* This fixed GF fixture uses 29 polls before the final raw callback. */
    CHECK(polls==30);
    for(u32 delta=0;delta<3;delta++) {
        size_tail_setup(65536);
        f.config_target=calls; f.config_ticks=22+delta;
        int rc=trial_end(&r,&r.token,REC_NORMAL);
        CHECK(f.config_tail_calls==calls && f.config_polls==polls && r.s0_verified);
        if(!delta) CHECK(!rc && r.state==REC_VERIFIED && r.cache.bar[0].valid);
        else CHECK(rc<0 && r.state==REC_FAILED && r.reason==REC_TIMEOUT && !r.cache.bar[0].valid);
    }
    /* The tail raw callback consumes exactly one restoration poll. */
    size_tail_setup(polls);
    CHECK(trial_end(&r,&r.token,REC_NORMAL)<0 && r.reason==REC_TIMEOUT);
}
static void indexed_order(void)
{
    for(u32 bad=0;bad<2;bad++) {
        synthetic(1);
        targets[TK]=(struct recover_target){.space=REC_VGA_INDEXED,.port=RECOVER_SEQ,
            .index=1,.width=1,.access=REC_RW,.read_safe=1};
        for(u32 i=5;i<7;i++) rules[i]=(struct recover_rule){.id=i,.key=TK,.phase=REC_RESTORE,
            .main_id=RECOVER_NONE,.pc=RECOVER_NONE,.substep=i==5 ? REC_DATA:REC_SELECT,
            .write_mask=0xff,.verify_mask=0xff,.value={.mode=REC_RANGE,.max=255,.expr_id=RECOVER_NONE},
            .flags=REC_LOCAL};
        profile.rule_count=7; bar_cache(); synthetic_begin();
        r.state=REC_RESTORING; r.auth_phase=REC_RESTORE; r.selected=TK; f.selected_value=1;
        r.restore_tick=f.tick; f.n=0;
        struct recover_local_step steps[]={
            {REC_LOCAL_DATA,TK,REC_DATA,1,0xa5},
            {REC_LOCAL_LOCK,TK,REC_DATA,1,0x5a},
            {REC_LOCAL_INDEX,TK,REC_SELECT,1,0}};
        if(bad) { steps[0].role=REC_LOCAL_LOCK; steps[1].role=REC_LOCAL_DATA; }
        CHECK((recover_indexed_cleanup(&r,steps,3,0)==0)==!bad);
        if(bad) CHECK(!writes());
        else {
            u32 n=0;
            for(u32 j=0;j<f.n;j++) if(f.trace[j].op=='w' || f.trace[j].op=='i') {
                CHECK(f.trace[j].value==steps[n].value); n++;
            }
            CHECK(n==3 && r.selected==RECOVER_NONE);
        }
    }
}
static void every_restore_callback(void)
{
    setup(false); begin(); f.route=0xff; r.route_write_approved=true;
    f.bus_calls=0; CHECK(!trial_end(&r,&r.token,REC_NORMAL));
    u32 total=f.bus_calls;
    for(u32 point=1;point<=total;point++) {
        setup(false); begin(); f.route=0xff; r.route_write_approved=true;
        f.bus_calls=0; f.fail_at=point;
        CHECK(trial_end(&r,&r.token,REC_NORMAL)<0 && r.state==REC_FAILED);
        CHECK(f.bus_calls>=point && f.bus_calls<total+10);
        u32 calls=f.bus_calls;
        CHECK(trial_end(&r,&r.token,REC_NORMAL)<0 && f.bus_calls==calls);
    }
}
static void t2_tests(void) { uart_events(); size_tail_deadline(); windows(); texts(); frame_clock(); budgets(); delays(); gf_profiles(); indexed_order(); every_restore_callback(); a1_failed_config(); }
