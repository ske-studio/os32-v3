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
static u32 mode_calls, mode_fail;
static int production_selected;
int gf_production_selected(void) { return production_selected; }
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
    int rc=gfinit_plan_build(p,d,profile,length);p->mode_flags=GF_MODE_C;
    p->c_node=plan_nodes-1;p->plan.nodes=d->nodes;p->plan.args=d->args;
    d->nodes[p->c_node]=(struct recover_plan_node){.opcode=GF_C_OPCODE,.substep=GF_C_SECTION};
    p->layouts[0]=(struct recover_opcode_layout){.opcode=GF_C_OPCODE,.substep=GF_C_SECTION,.dir_substep=RECOVER_NONE};
    return rc;
#endif
}
int gfinit_plan_baseline(struct gfinit_plan *p,struct recover_profile *profile,
                         struct gf_stage_state *s,const u8 *rom,u32 size)
{ (void)p;(void)profile;(void)s;(void)rom;(void)size;return 0; }
int gf_stage_capture(struct recover *r,void *arg)
{ CHECK(r==&core && owner->next_main==5);struct gf_stage_state *s=arg;
#if !GF_VPLL_PROBE
  s->initialized=true;
#else
  (void)s;
#endif
  return 0; }
static u32 reentry_fault;
int gf_stage_entry(struct recover *r) { CHECK(r==&core);return reentry_fault?-1:0; }
int gfinit_plan_rebuild_c(struct gfinit_plan *p,struct gfinit_decode *d,
                          struct recover_profile *profile,const struct gf_stage_state *s)
{ (void)s;int rc=gfinit_plan_build_c(p,d,profile,0x100000,owner->rom,owner->rom_size);
  p->c_node=0;d->nodes[0]=(struct recover_plan_node){.opcode=GF_C_OPCODE,.substep=GF_C_CHECK};
  p->layouts[0].substep=GF_C_CHECK;p->layouts[0].flags=REC_LAYOUT_MODE_CHECK;return rc; }
void gfinit_plan_read(struct gfinit_plan *p,const struct recover *r,const struct recover_bus_access *a,u32 value)
{ CHECK(p==owner->compiled && r==&core && a);plan_reads++;(void)value; }
int gfinit_plan_verify(struct recover *r,void *v) { CHECK(r==&core && v==owner->compiled);verify_calls++;return 0; }
int gfinit_plan_c_vram(void *v,struct recover *r,bool write,u32 *bad,u32 *want,u32 *got)
{ (void)v;(void)r;(void)write;(void)bad;(void)want;(void)got;CHECK(false);return -1; }
int gfinit_plan_c_color(void *v,struct recover *r,u32 row,u32 value)
{ (void)v;(void)r;(void)row;(void)value;CHECK(false);return -1; }
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
  CHECK(block_start[((u8 *)b->flow_bits-arena)/PAGE_SIZE]==PAGE_ALIGN_UP(plan->node_count*RECOVER_BITS_WORDS*4)/PAGE_SIZE);if(r->c_only)r->node_layout[0]=&owner->compiled->layouts[0];r->profile=p;r->plan=plan;*hash=123;return 0; }
int trial_begin(struct recover *r,const struct recover_profile *p,u32 hash,u32 budget)
{ CHECK((r->state==REC_IDLE || r->state==REC_VERIFIED || gf_stage_reentry(r)) && p==r->profile && hash==123 && budget==p->trial_limit);begins++;event('B');r->failed=false;r->cold_latched=p->cold_required;r->s0_verified=false;r->token.session=++r->next_session;r->state=REC_PAUSED;r->expected_node=r->c_only?0:RECOVER_NONE;return 0; }
int trial_step(struct recover *r,const struct recover_token *token,bool approved,int (*operation)(struct recover *,void *),void *arg)
{
    CHECK(token==&r->token && approved);if(r->state!=REC_PAUSED)return -1;
    steps++;event('S');r->state=REC_RUNNING;int rc=step_fail?-1:operation(r,arg);
    if(rc){stage.initialized=false;r->reason=REC_OPERATION;r->state=REC_FAILED;r->ops->local_cleanup(r->opaque);}
    else r->state=REC_PAUSED;
    return rc;
}
int trial_end(struct recover *r,const struct recover_token *token,enum recover_reason reason)
{ CHECK(token==&r->token);if(r->state!=REC_PAUSED)return -1;ends++;event('E');r->reason=reason;r->ops->local_cleanup(r->opaque);r->state=REC_VERIFIED;r->s0_verified=true;if(stage.initialized)stage.s0_ok=true;r->plan=0;r->cache.bar[1].valid=true;r->cache.bar[1].length=0x100000;return 0; }
int gfinit_exec_prepare(struct recover *r,void *arg)
{ struct gfinit_exec *e=arg;CHECK(r==&core && e->rom==owner->rom);return 0; }
int gfinit_exec_restore(struct gfinit_exec *e) { (void)e;return 0; }
int gfinit_exec_run(struct recover *r,void *arg)
{ struct gfinit_exec *e=arg;CHECK(r==&core && e->rom==owner->rom && e->rom_size==512);CHECK(e->main_id<5);run_mask|=1U<<e->main_id;e->steps=9;e->fail_pc=0;if(run_fail==e->main_id+1)return -1;writes++;return 0; }
int gfinit_plan_c_control(struct gfinit_plan *p,struct recover *r)
{ CHECK(p==owner->compiled && r==&core);mode_calls++;writes++;return mode_fail?-1:0; }
int recover_plan_check(struct recover *r,u32 *edge)
{ (void)r;(void)edge;CHECK(false);return -1; }
int recover_safe_point(struct recover *r) { return r->state==REC_RUNNING?0:-1; }
int recover_plan_enter(struct recover *r,u32 node) { CHECK(r==&core && node==owner->compiled->c_node);return 0; }
int recover_plan_access(struct recover *r,u32 *value) { (void)r;(void)value;CHECK(false);return -1; }
int recover_plan_leave(struct recover *r,u32 edge) { CHECK(r==&core && !edge);r->expected_node=RECOVER_NONE;return 0; }
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
    mode_calls=mode_fail=reentry_fault=0;stage=(struct gf_stage_state){0};
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
