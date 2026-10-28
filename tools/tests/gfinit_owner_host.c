/* Actual S6 owner/request/ROM lifetime code. Platform and trial engine are
 * injected contracts; bus/decoder/plan internals have separate real tests. */
#include "../../kdebug/gfinit.c"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "gfinit_output_host.h"
#pragma GCC diagnostic pop
#define TEST_PAGE_BUDGET GF_WORK_MAX_PAGES
#define TEST_ARENA_PAGES 512U
/* Contiguous physical pages, ascending first-fit as in pgalloc. Foreign pages
 * split the arena; they are never owned or returned by the GF owner. */
static u8 arena[TEST_ARENA_PAGES * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u8 page_owner[TEST_ARENA_PAGES];
static u32 block_start[TEST_ARENA_PAGES];
static u32 live_pages, peak_pages, alloc_calls, fail_alloc, plan_nodes;
static struct recover core;
static struct recover_ops base;
static struct recover_profile s0;
static u8 image[GF_ROM_MAX];
static u32 failures, allocations, frees, reads, writes, begins, steps, ends, cleans;
static u32 expand_fail, expand_calls, detach_fail, plan_reads, boot_fail;
static u32 run_mask, run_fail, step_fail, decode_fail, probe_fail, peek_fail, verify_calls;
static char events[64]; static u32 event_count;
static void output(const char *s)
{ u32 n=0,result;while(s[n])n++;__asm__ volatile("int $0x80":"=a"(result):"0"(4),"b"(1),"c"(s),"d"(n):"memory");(void)result; }
#define CHECK(x) do { if (!(x)) { output("FAIL " #x "\n"); failures++; } } while(0)
static void zero(void *p,u32 n) { u8 *b=p;while(n--)*b++=0; }
static void event(char c) { CHECK(event_count<sizeof(events));if(event_count<sizeof(events))events[event_count++]=c; }
void *kmemset(void *p,int value,u32 n) { u8 *b=p;while(n--)*b++=value;return p; }
u32 pgalloc_alloc_phys(u32 who,int n)
{
    CHECK(who==LEDGER_OWNER_KERNEL && n>0);
    alloc_calls++;
    if(alloc_calls==fail_alloc || n<=0 || live_pages+(u32)n>TEST_PAGE_BUDGET)return 0;
    for(u32 first=0;first+(u32)n<=TEST_ARENA_PAGES;first++) {
        u32 i=0;
        while(i<(u32)n && !page_owner[first+i])i++;
        if(i!=(u32)n)continue;
        for(i=0;i<(u32)n;i++)page_owner[first+i]=1;
        block_start[first]=n;live_pages+=n;
        if(live_pages>peak_pages)peak_pages=live_pages;
        if((u32)n==GF_ROM_MAX/PAGE_SIZE)allocations++;
        return V2P(arena+first*PAGE_SIZE);
    }
    return 0;
}
int pgalloc_free_n_owner(u32 who,u32 pfn,int n)
{
    u32 first=pfn-V2P(arena)/PAGE_SIZE;
    CHECK(who==LEDGER_OWNER_KERNEL && first<TEST_ARENA_PAGES && n>0);
    if (owner && (pfn == V2P(owner->rom)/PAGE_SIZE ||
                  pfn == V2P(owner->decoded)/PAGE_SIZE || pfn == V2P(owner->compiled)/PAGE_SIZE))
        CHECK(!core.plan && !core.bound_profile && core.opaque==0 && core.ops==&base);
    if(first>=TEST_ARENA_PAGES || n<=0)return 0;
    CHECK(block_start[first]==(u32)n);
    if(block_start[first]!=(u32)n)return 0;
    for(u32 i=0;i<(u32)n;i++){CHECK(page_owner[first+i]==1);page_owner[first+i]=0;}
    block_start[first]=0;live_pages-=n;
    if((u32)n==GF_ROM_MAX/PAGE_SIZE)frees++;
    return 1;
}
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 o) { (void)b;(void)d;(void)f;(void)o;reads++;return 0; }
u16 pci_cfg_read16(u32 b,u32 d,u32 f,u32 o) { return pci_cfg_read32(b,d,f,o); }
void pci_cfg_write32(u32 b,u32 d,u32 f,u32 o,u32 v) { (void)b;(void)d;(void)f;(void)o;(void)v;writes++; }
int pci_cfg_write16(u32 b,u32 d,u32 f,u32 o,u16 v) { pci_cfg_write32(b,d,f,o,v);return 0; }
int boot_image_info(void *v) { BootImageInfo *b=v;zero(b,sizeof(*b));b->crc_valid=1;b->image_crc=42;return boot_fail?-1:0; }
static const struct serial_setup serial={.actual=9600};
const struct serial_setup *serial_get_setup(void) { return &serial; }
void gfinit_bus_init(struct gfinit_bus *b,const struct recover_profile *p,const struct gfinit_pci_ops *pci)
{ zero(b,sizeof(*b));b->profile=p;b->pci=pci;b->bars[0]=0x10000000; }
int gfinit_bus_probe(struct gfinit_bus *b) { reads++;b->ready=!probe_fail;return probe_fail?-1:0; }
int gfinit_bus_identity(void *v,struct recover_identity *id) { *id=((struct gfinit_bus *)v)->identity;return 0; }
int gfinit_bus_raw_config(void *v,u32 *command,u32 bars[3])
{ struct gfinit_bus *b=v;reads++;*command=RECOVER_COMMAND_MEM;for(u32 i=0;i<3;i++)bars[i]=b->bars[i];return 0; }
int gfinit_bus_read(void *v,const struct recover_bus_access *a,u32 *out) { (void)v;(void)a;reads++;*out=0;return 0; }
int gfinit_bus_write(void *v,const struct recover_bus_access *a,u32 value) { (void)v;(void)a;(void)value;writes++;return 0; }
int gfinit_bus_local_cleanup(struct gfinit_bus *b,struct recover *r) { (void)b;(void)r;cleans++;return 0; }
int recover_gf_screen_safe(struct recover *r) { (void)r;return 0; }
int recover_hal_wait_frame(void *v,u32 us) { (void)v;(void)us;return 0; }
i32 kpeek_call(u32 op,void *v,u32 size)
{
    KdebugPeek *q=v;CHECK(op==KDEBUG_OP_PEEK && size==sizeof(*q));reads++;
    if(peek_fail)return -1;
    u32 offset=q->addr-0x10000000-KPEEK_NV_PROM_OFF;
    CHECK(offset+q->len<=sizeof(image));
    for(u32 i=0;i<q->len;i++)q->data[i]=image[offset+i];
    return 0;
}
int gfinit_decode_rom(const u8 *rom,u32 size,const struct recover_profile *p,struct gfinit_decode *d)
{ CHECK(rom==owner->rom && size==512 && p);zero(d,sizeof(*d));return decode_fail?-1:0; }
int gfinit_resources(struct recover_range *ranges,u32 *n) { ranges[0]=(struct recover_range){0};*n=1;return 0; }
int recover_gf_profile_expand(struct recover_profile *p,struct gfinit_plan *buf)
{
    if(buf) { expand_calls++;if(expand_fail)return -1; }
    *p=(struct recover_profile){.trial_limit=6000,.opcode_limit=6000,.max_polls=65536};
    if(buf) {
        p->targets=buf->targets;p->target_count=1;
        p->rules=buf->rules;p->rule_count=1;
        p->layouts=buf->layouts;p->layout_count=1;
    }
    return 0;
}
const struct recover_plan recover_gf_size_bars_plan={.version=1,.node_count=32};
int gfinit_plan_build(struct gfinit_plan *p,struct gfinit_decode *d,struct recover_profile *profile,u32 length)
{ (void)d;CHECK(profile->rules==p->rules && profile->rule_count==1);profile->rule_count++;CHECK(length==0x100000);zero(p,sizeof(*p));p->plan.version=1;p->plan.node_count=plan_nodes;return 0; }
int gfinit_plan_build_c(struct gfinit_plan *p,struct gfinit_decode *d,struct recover_profile *profile,
                        u32 length,const u8 *rom,u32 size)
{
    CHECK(rom==owner->rom && size==owner->rom_size);
#if GF_VPLL_PROBE
    (void)p;(void)d;(void)profile;(void)length;return -1;
#else
    int rc=gfinit_plan_build(p,d,profile,length);p->mode_flags=GF_MODE_C;return rc;
#endif
}
void gfinit_plan_read(struct gfinit_plan *p,const struct recover *r,const struct recover_bus_access *a,u32 value)
{ CHECK(p==owner->compiled && r==&core && a);plan_reads++;(void)value; }
int gfinit_plan_verify(struct recover *r,void *v) { CHECK(r==&core && v==owner->compiled);verify_calls++;return 0; }
int gfinit_plan_c_vram(void *v,struct recover *r,bool write,u32 *bad,u32 *want,u32 *got)
{ (void)v;(void)r;(void)write;(void)bad;(void)want;(void)got;CHECK(false);return -1; }
int recover_budget_apply(struct recover_profile *p,u32 kind,u32 hz,u32 baud,u32 bits,const struct recover_static_work *w)
{ CHECK(kind==REC_BUDGET_SIZE_BARS || kind==REC_BUDGET_GF);CHECK(hz==PIT_HZ && baud==9600 && bits==10 && w->bytes);p->crc=1;return 0; }
u32 recover_profile_crc(const struct recover_profile *p) { (void)p;return 1; }
struct recover *recover_owner(void) { return &core; }
int recover_owner_configure(const struct recover_ops *ops,void *opaque,const struct recover_profile *s0,bool approved)
{ CHECK(!approved);
  if(!ops) {
      if(detach_fail)return -1;
      CHECK(core.state==REC_IDLE || core.state==REC_VERIFIED || core.state==REC_FAILED);
      core.ops=&base;core.opaque=0;core.profile=core.s0_profile;
      core.plan=0;core.bound_profile=0;core.buffers=(struct recover_plan_buffers){0};
      return 0;
  }
  core.ops=ops;core.opaque=opaque;core.s0_profile=s0;return 0; }
int recover_bind_plan(struct recover *r,const struct recover_profile *p,const struct recover_plan *plan,struct recover_plan_buffers *b,u32 *hash)
{ CHECK(r==&core && b->valid_bits && b->flow_bits);
  CHECK(p->rules==owner->compiled->rules && p->targets==owner->compiled->targets && p->layouts==owner->compiled->layouts);
  CHECK(p->rule_count==(plan==&owner->size_plan?1U:2U));
  CHECK(block_start[((u8 *)b->flow_bits-arena)/PAGE_SIZE]==PAGE_ALIGN_UP(plan->node_count*RECOVER_BITS_WORDS*4)/PAGE_SIZE);r->profile=p;r->plan=plan;*hash=123;return 0; }
int trial_begin(struct recover *r,const struct recover_profile *p,u32 hash,u32 budget)
{ CHECK((r->state==REC_IDLE || r->state==REC_VERIFIED) && p==r->profile && hash==123 && budget==p->trial_limit);begins++;event('B');r->token.session=++r->next_session;r->state=REC_PAUSED;r->expected_node=RECOVER_NONE;return 0; }
int trial_step(struct recover *r,const struct recover_token *token,bool approved,int (*operation)(struct recover *,void *),void *arg)
{
    CHECK(token==&r->token && approved);if(r->state!=REC_PAUSED)return -1;
    steps++;event('S');r->state=REC_RUNNING;int rc=step_fail?-1:operation(r,arg);
    if(rc){r->reason=REC_OPERATION;r->state=REC_FAILED;r->ops->local_cleanup(r->opaque);}
    else r->state=REC_PAUSED;
    return rc;
}
int trial_end(struct recover *r,const struct recover_token *token,enum recover_reason reason)
{ CHECK(token==&r->token);if(r->state!=REC_PAUSED)return -1;ends++;event('E');r->reason=reason;r->ops->local_cleanup(r->opaque);r->state=REC_VERIFIED;r->plan=0;r->cache.bar[1].valid=true;r->cache.bar[1].length=0x100000;return 0; }
int gfinit_exec_run(struct recover *r,void *arg)
{ struct gfinit_exec *e=arg;CHECK(r==&core && e->rom==owner->rom && e->rom_size==512);CHECK(e->main_id<5);run_mask|=1U<<e->main_id;e->steps=9;e->fail_pc=0;if(run_fail==e->main_id+1)return -1;writes++;return 0; }
int recover_safe_point(struct recover *r) { return r->state==REC_RUNNING?0:-1; }
int recover_plan_enter(struct recover *r,u32 node) { (void)r;(void)node;CHECK(false);return -1; }
int recover_plan_access(struct recover *r,u32 *value) { (void)r;(void)value;CHECK(false);return -1; }
int recover_plan_leave(struct recover *r,u32 edge) { (void)r;(void)edge;CHECK(false);return -1; }
int recover_bar_probe(struct recover *r,u32 bar,u32 raw,u32 mask,const struct recover_range *exclude,u32 n,const struct recover_range *allowed,u32 m)
{ (void)r;(void)bar;(void)raw;(void)mask;(void)exclude;(void)n;(void)allowed;(void)m;CHECK(false);return -1; }
static int base_cleanup(void *opaque) { (void)opaque;cleans++;return 0; }
static struct recover_ops base={.local_cleanup=base_cleanup};
static struct recover_transport transport;
static struct recover_profile s0;
static struct gfinit_request request(u32 flags,u32 main)
{ return (struct gfinit_request){.version=GF_INIT_VERSION,.size=sizeof(struct gfinit_request),.flags=flags,.session=core.token.session,.script_id=main,.max_steps=GF_INIT_SCHEDULE}; }
static int call(u32 op,u32 flags,u32 main)
{ struct gfinit_request q=request(flags,main);return gfinit_call(op,&q,sizeof(q),&base,&transport,&s0); }
static void setup(void)
{
    if(owner)gf_release_rom(owner);
    CHECK(live_pages==0);
    for(u32 i=0;i<TEST_ARENA_PAGES;i++)CHECK(page_owner[i]!=1);
    zero(page_owner,sizeof(page_owner));zero(block_start,sizeof(block_start));
    alloc_calls=fail_alloc=peak_pages=0;plan_nodes=1790;
    expand_fail=expand_calls=detach_fail=plan_reads=boot_fail=0;
    zero(&control,sizeof(control));owner=0;zero(&gf_lease,sizeof(gf_lease));gf_device.generation=1;gf_device.state=HWDEV_STOPPED_UNOWNED;zero(&core,sizeof(core));core.ops=&base;core.s0_profile=&s0;zero(image,sizeof(image));zero(&transport,sizeof(transport));
    allocations=frees=reads=writes=begins=steps=ends=cleans=run_mask=run_fail=step_fail=decode_fail=probe_fail=peek_fail=verify_calls=event_count=0;
    image[0]=0x55;image[1]=0xaa;image[2]=1;image[0x18]=0x20;
    image[0x20]='P';image[0x21]='C';image[0x22]='I';image[0x23]='R';image[0x24]=0xde;image[0x25]=0x10;image[0x26]=0x21;image[0x27]=2;
    image[0x40]=0xff;image[0x41]=0xb8;image[0x42]='B';image[0x43]='I';image[0x44]='T';
    image[0x47]=1;image[0x48]=12;image[0x49]=6;image[0x4a]=1;
    image[0x4c]='i';image[0x4d]=2;image[0x4e]=5;image[0x50]=0x60;
    image[0x62]=0x44;
    clear_output();
    u8 sum=0;for(u32 i=0;i<511;i++)sum+=image[i];image[511]=0-sum;
}
static void prepared(void)
{ CHECK(call(GF_OP_INSPECT,0,0)==0);CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)==0); }
static void mode_c_test(void)
{
    setup();prepared();
    CHECK(owner->prepare_flags==GF_ALLOW_WRITE && !owner->compiled->mode_flags);
    u32 before=begins;
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)<0 && begins==before);
    CHECK(owner->prepare_flags==GF_ALLOW_WRITE && !owner->compiled->mode_flags);
    /* Stage 2a exposes neither op13 nor an upgrade of an old prepare. */
    CHECK(call(13,GF_ALLOW_WRITE,0)<0);
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0);
    setup();
    CHECK(call(GF_OP_INSPECT,GF_MODE_C,0)<0 && !reads);
    CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE|GF_MODE_C,0)<0 && !begins);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(call(GF_OP_PREPARE,GF_MODE_C,0)<0);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|4,0)<0); /* BUS50_ONE deferred */
#if GF_VPLL_PROBE
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)<0 && !live_pages);
#else
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)==0);
    CHECK(owner->prepare_flags==(GF_ALLOW_WRITE|GF_MODE_C));
    CHECK(owner->compiled->mode_flags==GF_MODE_C && core.state==REC_PAUSED);
    before=begins;
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)==0 && begins==before);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)<0 && begins==before);
    CHECK(owner->prepare_flags==(GF_ALLOW_WRITE|GF_MODE_C));
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE|GF_MODE_C,main)<0);
        CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0 && core.state==REC_PAUSED);
    }
    CHECK(owner->next_main==5);
    CHECK(call(GF_OP_VERIFY,GF_ALLOW_WRITE,0)<0 && !verify_calls && core.state==REC_PAUSED);
    CHECK(call(13,GF_ALLOW_WRITE,0)<0 && core.state==REC_PAUSED);
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && !live_pages && !owner->prepare_flags);
#endif
    output("MODE_C flags / cache / old prepare refusal / PAUSED PASS\n");
}
static void lifecycle(void)
{
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0 && writes==0 && reads>0 && owner->inspected);
    CHECK(core.ops==&base && core.opaque==0 && core.s0_profile==&s0);
    CHECK(call(GF_OP_SIZE_BARS,0,0)<0 && begins==0);
    struct gfinit_request q=request(GF_ALLOW_WRITE,0);q.session++;
    CHECK(gfinit_call(GF_OP_SIZE_BARS,&q,sizeof(q),&base,&transport,&s0)<0 && begins==0);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(core.ops==&owner->ops && core.opaque==&owner->bus && core.s0_profile==&s0);
    CHECK(begins==1 && steps==1 && ends==1 && cleans==1 && events[0]=='B' && events[1]=='S' && events[2]=='E');
    CHECK(allocations==1 && frees==0);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)==0 && core.state==REC_PAUSED && begins==2 && steps==2);
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,1)<0 && writes==0);
    for(u32 main=0;main<5;main++) {
        CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0);
        u32 before=steps;CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0 && steps==before);
    }
    CHECK(run_mask==31 && call(GF_OP_VERIFY,GF_ALLOW_WRITE,0)==0);
    CHECK(verify_calls==1 && core.state==REC_VERIFIED && frees==1 && !owner->rom && !owner->inspected);
}
static void failures_test(void)
{
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && frees==1 && !writes && !begins && !steps && !ends);
    setup();prepared();gfinit_recover_end();CHECK(frees==0 && owner->rom);
    core.state=REC_VERIFIED;gfinit_recover_end();CHECK(frees==1 && !owner->rom);
    gfinit_recover_end();CHECK(frees==1);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)<0 && begins==2 && live_pages==0);
    CHECK(core.ops==&base && !core.opaque && !core.plan);
    setup();prepared();core.state=REC_FAILED;core.failed=true;core.cold_latched=true;
    u32 sid=core.token.session;
    gfinit_recover_end();CHECK(frees==1 && !owner->rom && !gf_lease.active);
    CHECK(core.ops==&base && !core.opaque && !core.plan && core.failed && core.cold_latched);
    gfinit_recover_end();
    CHECK(call(GF_OP_INSPECT,0,0)<0 && core.token.session==sid && core.state==REC_FAILED);
    CHECK(core.failed && core.cold_latched && frees==1);
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);step_fail=1;
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0 && core.state==REC_FAILED && frees==1 && cleans==1);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0 && frees==1);
    setup();peek_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0 && frees==1 && !writes);
    setup();decode_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0 && frees==1 && !writes);
    setup();prepared();CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && core.reason==REC_ABORT && frees==1 && cleans==2);
    setup();prepared();run_fail=1;CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && core.state==REC_FAILED && frees==1 && cleans==2);
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && frees==1);
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(call(GF_OP_INSPECT,0,0)==0 && owner->inspected && owner->rom && frees==1 && allocations==2);
}
static void ownership_test(void)
{
    const enum recover_state inactive[]={REC_IDLE,REC_VERIFIED};
    for(u32 i=0;i<sizeof(inactive)/sizeof(*inactive);i++){
        setup();CHECK(call(GF_OP_INSPECT,0,0)==0 && owner->rom);
        core.state=inactive[i];gf_device.generation++;
        CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0);
        CHECK(!live_pages && !owner->inspected && !owner->rom && !gf_lease.active);
        CHECK(!writes && !begins && !steps && !ends && frees==1);
    }
    const enum hwdev_owner_state busy[] = {
        HWDEV_UNCLASSIFIED, HWDEV_ROOT_IDE, HWDEV_LAN_SERIALFS, HWDEV_DISPLAY
    };
    for (u32 i=0;i<sizeof(busy)/sizeof(*busy);i++) {
        setup();gf_device.state=busy[i];
        CHECK(call(GF_OP_INSPECT,0,0)<0 && !reads && !writes && !live_pages && !gf_lease.active);
    }
    setup();prepared();
    CHECK(gf_lease.active);
    u32 before=steps;
    owner->generation--;
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && steps==before && !writes);
    owner->generation++;
    gf_device.generation++;
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && steps==before && !writes);
    gf_device.generation--;
    gf_lease.session++;
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && steps==before && !writes);
    gf_lease.session--;
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && !gf_lease.active && !live_pages);
    struct hwdev_lease old=gf_lease;
    CHECK(call(GF_OP_INSPECT,0,0)==0 && gf_lease.generation>old.generation);
    CHECK(!hwdev_lease_matches(&gf_lease,&gf_device,old.generation,core.token.session));
}
static void number(u32 n)
{ char buf[16];u32 i=0;do{buf[i++]='0'+n%10;n/=10;}while(n);while(i){char c[2]={buf[--i],0};output(c);} }
static void allocation_test(void)
{
    for(u32 fail=1;fail<=5;fail++) {
        setup();plan_nodes=RECOVER_NODE_MAX;fail_alloc=fail;
        int rc=call(GF_OP_INSPECT,0,0);
        if(!rc)rc=call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0);
        if(!rc)rc=call(GF_OP_PREPARE,GF_ALLOW_WRITE,0);
        CHECK(rc<0 && live_pages==0 && !owner->inspected && writes==0);
        CHECK(alloc_calls==fail);
    }
    setup();plan_nodes=RECOVER_NODE_MAX;prepared();
    CHECK(peak_pages==TEST_PAGE_BUDGET && writes==0);
    output("bytes control/decode/plan/ROM/flow pages_peak: ");
    number(sizeof(control));output("/");number(sizeof(struct gfinit_decode));output("/");
    number(sizeof(struct gfinit_plan));output("/");number(GF_ROM_MAX);output("/");
    number(owner->flow_bytes);output(" pages=");number(peak_pages);output("\n");
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && live_pages==0);
    setup();probe_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0 && live_pages==0);
    setup();plan_nodes=RECOVER_NODE_MAX+1U;
    CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)<0 && live_pages==0 && writes==0);
    CHECK(alloc_calls==4 && begins==1); /* refused before oversized flow */
    /* Enough free pages in total, but each hole is smaller than the requested
     * block. Exercise every allocation point, including flow replacement. */
    for(u32 fail=1;fail<=5;fail++) {
        setup();plan_nodes=RECOVER_NODE_MAX;
        int rc=0;
        if(fail>3)rc=call(GF_OP_INSPECT,0,0);
        if(fail>4)rc=call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0);
        CHECK(rc==0);
        /* INSPECT now allocates compiled, decode, then ROM. Leave exactly
         * the preceding blocks contiguous, fragment the requested block. */
        u32 keep=0;
        if(fail==2 || fail==3)keep=PAGE_ALIGN_UP(sizeof(struct gfinit_plan))/PAGE_SIZE;
        if(fail==3)keep+=PAGE_ALIGN_UP(sizeof(struct gfinit_decode))/PAGE_SIZE;
        u32 first=0;
        while(first<TEST_ARENA_PAGES && page_owner[first])first++;
        for(u32 i=first+keep;i<TEST_ARENA_PAGES;i++)
            if(!page_owner[i] && (i-first-keep)%2==0)page_owner[i]=2;
        u32 available=0;for(u32 i=0;i<TEST_ARENA_PAGES;i++)available+=page_owner[i]==0;
        CHECK(available>=128);
        if(fail<=3)rc=call(GF_OP_INSPECT,0,0);
        else if(fail==4)rc=call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0);
        else rc=call(GF_OP_PREPARE,GF_ALLOW_WRITE,0);
        CHECK(rc<0 && alloc_calls==fail && writes==0 && live_pages==0);
        for(u32 i=0;i<TEST_ARENA_PAGES;i++)CHECK(page_owner[i]!=1);
    }
}
static void expansion_lifetime_test(void)
{
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(owner->compiled && expand_calls==1);
    u32 inspect_pages=PAGE_ALIGN_UP(sizeof(struct gfinit_plan))/PAGE_SIZE +
        PAGE_ALIGN_UP(sizeof(struct gfinit_decode))/PAGE_SIZE + GF_ROM_MAX/PAGE_SIZE;
    CHECK(live_pages==inspect_pages);
    struct gfinit_plan *backing=owner->compiled;
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0 && expand_calls==2);
    CHECK(owner->compiled==backing);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)==0 && expand_calls==3);
    struct recover_bus_access access={0};u32 value;
    CHECK(gf_read(&owner->bus,&access,&value)==0 && plan_reads==1);
    /* B4: retain a previous RUN plan and enter SIZE_BARS without end release. */
    core.state=REC_VERIFIED;
    CHECK(gf_begin(&core,owner,&transport,true,GF_ALLOW_WRITE)==0 && expand_calls==4);
    core.plan=&owner->size_plan;
    CHECK(gf_read(&owner->bus,&access,&value)==0 && plan_reads==1);
    CHECK(owner->compiled==backing && owner->profile.rule_count==1);
    /* A refused detach must leave all backing, profile and lease intact. */
    u32 held=live_pages, calls=alloc_calls;
    detach_fail=1;
    CHECK(gf_release_rom(owner)<0 && live_pages==held && gf_lease.active);
    CHECK(owner->compiled==backing && owner->profile.rules==backing->rules);
    CHECK(gf_inspect(&core,&base,&transport,&s0)<0 && alloc_calls==calls);
    CHECK(live_pages==held && owner->compiled==backing && gf_lease.active);
    detach_fail=0;CHECK(gf_release_rom(owner)==0 && !live_pages && !gf_lease.active);
    CHECK(!owner->compiled && !owner->profile.rules && !owner->profile.rule_count);
    setup();expand_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(!live_pages && !gf_lease.active && !owner->inspected && !reads);
    setup();boot_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(!live_pages && !gf_lease.active && !owner->inspected);
    setup();probe_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(!live_pages && !gf_lease.active && !owner->inspected);
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);expand_fail=1;
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0 && !live_pages && !gf_lease.active);
}
static void inspection_test(void)
{
    setup();core.token.session=5;
    CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(contains(serial_output,"GF inspect sid=5 rc=0 rom_len=512 crc=E0A29354"));
    CHECK(contains(serial_output," chip=44 expect=44 match=1\n"));
    CHECK(!writes && !begins && core.token.session==5 && core.seq==0);
    clear_output();CHECK(call(GF_OP_INSPECT,0,0)==0 && !serial_length);
    /* A request with different flags is not the cached response. */
    CHECK(call(GF_OP_INSPECT,GF_ALLOW_WRITE,0)==0 && serial_length);
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && !writes && !gf_lease.active && !live_pages);
    setup();image[0x62]=0x43;image[511]++;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && !writes && !live_pages);
    CHECK(contains(serial_output," chip=43 expect=44 match=0\n"));
    setup();image[0x4c]='x';
    CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(contains(serial_output," chip=unknown expect=44 match=unknown\n"));
    setup();peek_fail=1;
    CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(equal(serial_output,"GF inspect sid=0 rc=-1 rom_len=unknown crc=unknown chip=unknown expect=44 match=unknown\n"));
    setup();decode_fail=1;
    CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(contains(serial_output,"GF inspect sid=0 rc=-1 rom_len=512 crc=E0A29354"));
    CHECK(contains(serial_output," chip=44 expect=44 match=1\n"));
    setup();image[0x50]=0xff;image[0x51]=0xff;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"chip=unknown"));
    setup();image[0x4e]=2;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"chip=unknown"));
    setup();image[0x49]=5;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"chip=unknown"));
    setup();image[511]++;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"match=1"));
    setup();rshell_active=0;CHECK(call(GF_OP_INSPECT,0,0)==0 && !serial_length);
}
/* Real shell and KDEBUG=0 gateway; only the loaded GF dispatch is injected. */
#define SHELL_H
#define str_eq(a,b) equal(a,b)
#include "../../kernel/kdebug_call.c"
struct kdebug kdebug;
static u32 cli_calls, cli_op;
static struct gfinit_request cli_request;
static int cli_result, cli_live;
static i32 cli_dispatch(u32 op, void *buf, u32 size)
{
    CHECK(size==40);
    cli_calls++;cli_op=op;cli_request=*(struct gfinit_request *)buf;
    if (cli_live) return gfinit_call(op,buf,size,&base,&transport,&s0);
    ((struct gfinit_request *)buf)->status=99; /* rc, never status, is authoritative */
    return cli_result;
}
static struct {
    u32 version;
    void (*kprintf)(u8,const char *,...);
    i32 (*kdebug_call)(u32,void *,u32);
} api={78,kprintf,kdebug_call}, *g_api=&api;
#include "../../userland/shell/cmd_gfinit.c"
static void cli_test(void)
{
    char *inspect[]={"gfinit","inspect","5","--allow-write","extra"};
    char *abort[]={"gfinit","abort","5","--allow-write","extra"};
    setup();kdebug.ops.dbg_call=cli_dispatch;cli_live=1;core.token.session=5;
    CHECK(shell_cmd_gfinit(3,inspect)==0);
    CHECK(contains(serial_output,"GF inspect sid=5 rc=0 rom_len=512 crc=E0A29354"));
    CHECK(contains(serial_output,"match=1\nGF inspect-end sid=5 rc=0\n"));
    clear_output();CHECK(shell_cmd_gfinit(3,inspect)==0);
    CHECK(equal(serial_output,"GF inspect-end sid=5 rc=0\n"));
    CHECK(shell_cmd_gfinit(4,abort)==0 && !writes && !gf_lease.active && !live_pages);
    cli_live=0;cli_result=0;
    inspect[2]="4294967295";
    CHECK(shell_cmd_gfinit(3,inspect)==0 && cli_op==5);
    CHECK(cli_request.version==1 && cli_request.size==40 && cli_request.session==~0U);
    CHECK(cli_request.flags==0 && cli_request.script_id==0 && cli_request.max_steps==4096);
    CHECK(!cli_request.status && !cli_request.fail_pc && !cli_request.fail_reg && !cli_request.steps);
    CHECK(shell_cmd_gfinit(4,abort)==0 && cli_op==10 && cli_request.flags==1);
    CHECK(cli_request.session==5 && cli_request.script_id==0 && cli_request.max_steps==4096);
    CHECK(!cli_request.status && !cli_request.fail_pc && !cli_request.fail_reg && !cli_request.steps);
    u32 before=cli_calls;
    CHECK(shell_cmd_gfinit(4,inspect)==2 && shell_cmd_gfinit(3,abort)==2);
    for (int n=1;n<=5;n++) {
        if(n!=3)CHECK(shell_cmd_gfinit(n,inspect)==2);
        if(n!=4)CHECK(shell_cmd_gfinit(n,abort)==2);
    }
    const char *bad[]={"", "-1", "+1", "4294967296", "1x", " 1"};
    for(u32 i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        inspect[2]=(char *)bad[i];abort[2]=(char *)bad[i];
        CHECK(shell_cmd_gfinit(3,inspect)==2 && shell_cmd_gfinit(4,abort)==2);
    }
    inspect[2]=abort[2]="0";abort[3]="--unknown";
    CHECK(shell_cmd_gfinit(4,abort)==2);
    const char *commands[]={"size-bars","prepare","run-one","verify","unknown"};
    for(u32 i=0;i<sizeof(commands)/sizeof(*commands);i++) {
        inspect[1]=(char *)commands[i];CHECK(shell_cmd_gfinit(3,inspect)==2);
    }
    CHECK(cli_calls==before);inspect[1]="inspect";abort[3]="--allow-write";
    cli_result=-1;clear_output();CHECK(shell_cmd_gfinit(3,inspect)==1);
    CHECK(equal(serial_output,"GF inspect-end sid=0 rc=-1\n"));
    kdebug.ops.dbg_call=0;before=cli_calls;clear_output();
    CHECK(shell_cmd_gfinit(3,inspect)==1 && cli_calls==before);
    CHECK(equal(serial_output,"GF inspect-end sid=0 rc=-10\n"));
    clear_output();CHECK(shell_cmd_gfinit(4,abort)==1 && cli_calls==before);
    CHECK(equal(serial_output,"gfinit: rejected rc=-10\n"));
    api.version=77;clear_output();CHECK(shell_cmd_gfinit(3,inspect)==1);
    CHECK(equal(serial_output,"gfinit: kdebug unavailable\n"));
    api.version=78;api.kdebug_call=0;clear_output();
    CHECK(shell_cmd_gfinit(3,inspect)==1 && cli_calls==before);
    CHECK(equal(serial_output,"gfinit: kdebug unavailable\n"));api.kdebug_call=kdebug_call;
    output("S7a CLI / KDEBUG=0 NOSYS / inspect two lines via console serial PASS checks complete\n");
}
static void cli_write_test(void)
{
    const char *names[]={"size-bars","prepare","run-one","verify","abort"};
    const u32 ops[]={6,7,8,9,10};
    const char *bad[]={"", "-1", "+1", "4294967296", "1x", " 1"};
    cli_live=0;api.version=78;api.kdebug_call=kdebug_call;
    for(u32 i=0;i<sizeof(ops)/sizeof(*ops);i++) {
        int argc=i==2 ? 5 : 4;
        char *args[]={"gfinit",(char *)names[i],"4294967295","--allow-write","--allow-write","extra"};
        if(i==2)args[3]="0";
        kdebug.ops.dbg_call=cli_dispatch;cli_result=0;
        u32 before=cli_calls;
        clear_output();
        CHECK(shell_cmd_gfinit(argc,args)==0 && cli_calls==before+1 && cli_op==ops[i]);
        CHECK(!serial_length); /* No automatic next stage or added success line. */
        CHECK(cli_request.version==1 && cli_request.size==40 && cli_request.flags==1);
        CHECK(cli_request.session==~0U && cli_request.script_id==0 && cli_request.max_steps==4096);
        CHECK(!cli_request.status && !cli_request.fail_pc && !cli_request.fail_reg && !cli_request.steps);
        before=cli_calls;
        for(int n=1;n<=6;n++)if(n!=argc)CHECK(shell_cmd_gfinit(n,args)==2);
        args[argc-1]="--unknown";CHECK(shell_cmd_gfinit(argc,args)==2);
        args[argc-1]="--allow-write";
        for(u32 j=0;j<sizeof(bad)/sizeof(*bad);j++) {
            args[2]=(char *)bad[j];CHECK(shell_cmd_gfinit(argc,args)==2);
        }
        args[2]="5";
        if(i==2) {
            const char *bad_main[]={"5","4294967295","", "-1", "+1", "4294967296", "1x", " 1"};
            for(u32 j=0;j<sizeof(bad_main)/sizeof(*bad_main);j++) {
                args[3]=(char *)bad_main[j];CHECK(shell_cmd_gfinit(argc,args)==2);
            }
            CHECK(cli_calls==before);
            for(u32 main=0;main<5;main++) {
                char number[]={(char)('0'+main),0};args[3]=number;
                CHECK(shell_cmd_gfinit(argc,args)==0 && cli_request.script_id==main);
                CHECK(cli_calls==++before && cli_op==8);
            }
            args[3]="0";
        }
        CHECK(cli_calls==before);
        cli_result=-1;clear_output();
        CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before+1);
        CHECK(equal(serial_output,"gfinit: rejected rc=-1\n"));
        kdebug.ops.dbg_call=0;before=cli_calls;clear_output();
        CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before);
        CHECK(equal(serial_output,"gfinit: rejected rc=-10\n"));
        api.version=77;clear_output();CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before);
        CHECK(equal(serial_output,"gfinit: kdebug unavailable\n"));
        api.version=78;api.kdebug_call=0;
        CHECK(shell_cmd_gfinit(argc,args)==1 && cli_calls==before);api.kdebug_call=kdebug_call;
    }
    output("S7c single-op CLI / main range / write gate / NOSYS PASS checks complete\n");
}
static void vpll_budget_test(void)
{
    struct gfinit_owner g = {0}; struct recover r = {0};
    struct recover_transport t = {0};
    struct recover_plan p = {.node_count=100, .arg_count=200};
    u32 rows=128+100*12+200*4;
#if GF_VPLL_PROBE
    CHECK(RECOVER_VPLL_ROWS==20U);
    rows += 20U;
#endif
    u32 bytes=rows*(RECOVER_ROW_MAX+1U);
    u32 seconds=(bytes*RECOVER_UART_FRAME_BITS+serial.actual-1)/serial.actual;
    CHECK(gf_budget(&g,&r,&t,REC_BUDGET_GF,&p)==0);
    CHECK(r.log_limit==(seconds*2+RECOVER_BUDGET_MARGIN_S)*PIT_HZ);
    CHECK(t.max_polls==bytes*2+1024);
    p.node_count=~0U; CHECK(gf_budget(&g,&r,&t,REC_BUDGET_GF,&p)!=0);
    output("VPLL budget rows and overflow PASS\n");
}
void _start(void)
{ vpll_budget_test();mode_c_test();cli_test();cli_write_test();inspection_test();lifecycle();failures_test();allocation_test();ownership_test();expansion_lifetime_test();output(failures?"GF owner FAIL\n":"GF owner PASS\n");__asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0));__builtin_unreachable(); }
