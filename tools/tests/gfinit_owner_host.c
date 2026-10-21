/* Actual S6 owner/request/ROM lifetime code. Platform and trial engine are
 * injected contracts; bus/decoder/plan internals have separate real tests. */
#include "../../kdebug/gfinit.c"
#define TEST_PAGE_BUDGET (1024U * 1024U / PAGE_SIZE)
static u8 decode_storage[PAGE_ALIGN_UP(sizeof(struct gfinit_decode))] __attribute__((aligned(PAGE_SIZE)));
static u8 plan_storage[PAGE_ALIGN_UP(sizeof(struct gfinit_plan))] __attribute__((aligned(PAGE_SIZE)));
static u8 flow_storage[RECOVER_NODE_MAX * RECOVER_BITS_WORDS * 4] __attribute__((aligned(PAGE_SIZE)));
static u32 live_pages, peak_pages, alloc_calls, fail_alloc;
static u32 block_pages[4];
static struct recover core;
static u8 rom_storage[GF_ROM_MAX] __attribute__((aligned(PAGE_SIZE)));
static u8 image[GF_ROM_MAX];
static u32 failures, allocations, frees, reads, writes, begins, steps, ends, cleans;
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
    u32 slot;
    void *p;
    if((u32)n==sizeof(decode_storage)/PAGE_SIZE){slot=0;p=decode_storage;}
    else if((u32)n==sizeof(plan_storage)/PAGE_SIZE){slot=1;p=plan_storage;}
    else if((u32)n==sizeof(rom_storage)/PAGE_SIZE){slot=2;p=rom_storage;allocations++;}
    else {slot=3;p=flow_storage;CHECK((u32)n<=sizeof(flow_storage)/PAGE_SIZE);}
    CHECK(!block_pages[slot]);if(block_pages[slot])return 0;
    block_pages[slot]=n;live_pages+=n;if(live_pages>peak_pages)peak_pages=live_pages;
    return V2P(p);
}
int pgalloc_free_n_owner(u32 who,u32 pfn,int n)
{
    void *blocks[]={decode_storage,plan_storage,rom_storage,flow_storage};
    CHECK(who==LEDGER_OWNER_KERNEL);
    for(u32 i=0;i<4;i++)if(V2P(blocks[i])/PAGE_SIZE==pfn) {
        CHECK(block_pages[i]==(u32)n && n>0);
        if(block_pages[i]!=(u32)n || n<=0)return 0;
        live_pages-=n;block_pages[i]=0;if(i==2)frees++;return 1;
    }
    CHECK(false);return 0;
}
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 o) { (void)b;(void)d;(void)f;(void)o;reads++;return 0; }
u16 pci_cfg_read16(u32 b,u32 d,u32 f,u32 o) { return pci_cfg_read32(b,d,f,o); }
void pci_cfg_write32(u32 b,u32 d,u32 f,u32 o,u32 v) { (void)b;(void)d;(void)f;(void)o;(void)v;writes++; }
int pci_cfg_write16(u32 b,u32 d,u32 f,u32 o,u16 v) { pci_cfg_write32(b,d,f,o,v);return 0; }
int boot_image_info(void *v) { BootImageInfo *b=v;zero(b,sizeof(*b));b->crc_valid=1;b->image_crc=42;return 0; }
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
{ CHECK(rom==rom_storage && size==512 && p);zero(d,sizeof(*d));return decode_fail?-1:0; }
int gfinit_resources(struct recover_range *ranges,u32 *n) { ranges[0]=(struct recover_range){0};*n=1;return 0; }
const struct recover_profile recover_gf_template={.trial_limit=6000,.opcode_limit=6000,.max_polls=65536};
const struct recover_plan recover_gf_size_bars_plan={.version=1,.node_count=32};
int gfinit_plan_build(struct gfinit_plan *p,struct gfinit_decode *d,struct recover_profile *profile,u32 length)
{ (void)d;(void)profile;CHECK(length==0x100000);zero(p,sizeof(*p));p->plan.version=1;p->plan.node_count=1790;return 0; }
void gfinit_plan_read(struct gfinit_plan *p,const struct recover *r,const struct recover_bus_access *a,u32 value)
{ CHECK(p==owner->compiled && r==&core && a);(void)value; }
int gfinit_plan_verify(struct recover *r,void *v) { CHECK(r==&core && v==owner->compiled);verify_calls++;return 0; }
int recover_budget_apply(struct recover_profile *p,u32 kind,u32 hz,u32 baud,u32 bits,const struct recover_static_work *w)
{ CHECK(kind==REC_BUDGET_SIZE_BARS || kind==REC_BUDGET_GF);CHECK(hz==PIT_HZ && baud==9600 && bits==10 && w->bytes);p->crc=1;return 0; }
u32 recover_profile_crc(const struct recover_profile *p) { (void)p;return 1; }
struct recover *recover_owner(void) { return &core; }
int recover_owner_configure(const struct recover_ops *ops,void *opaque,const struct recover_profile *s0,bool approved)
{ CHECK(!approved);core.ops=ops;core.opaque=opaque;core.s0_profile=s0;return 0; }
int recover_bind_plan(struct recover *r,const struct recover_profile *p,const struct recover_plan *plan,struct recover_plan_buffers *b,u32 *hash)
{ CHECK(r==&core && b->valid_bits && b->flow_bits);
  CHECK(block_pages[3]==PAGE_ALIGN_UP(plan->node_count*RECOVER_BITS_WORDS*4)/PAGE_SIZE);r->profile=p;r->plan=plan;*hash=123;return 0; }
int trial_begin(struct recover *r,const struct recover_profile *p,u32 hash,u32 budget)
{ CHECK((r->state==REC_IDLE || r->state==REC_VERIFIED) && p==r->profile && hash==123 && budget==p->trial_limit);begins++;event('B');r->token.session++;r->state=REC_PAUSED;r->expected_node=RECOVER_NONE;return 0; }
int trial_step(struct recover *r,const struct recover_token *token,bool approved,int (*operation)(struct recover *,void *),void *arg)
{
    CHECK(token==&r->token && approved);if(r->state!=REC_PAUSED)return -1;
    steps++;event('S');r->state=REC_RUNNING;int rc=step_fail?-1:operation(r,arg);
    if(rc){r->reason=REC_OPERATION;r->state=REC_FAILED;r->ops->local_cleanup(r->opaque);}
    else r->state=REC_PAUSED;
    return rc;
}
int trial_end(struct recover *r,const struct recover_token *token,enum recover_reason reason)
{ CHECK(token==&r->token);if(r->state!=REC_PAUSED)return -1;ends++;event('E');r->reason=reason;r->ops->local_cleanup(r->opaque);r->state=REC_VERIFIED;r->cache.bar[1].valid=true;r->cache.bar[1].length=0x100000;return 0; }
int gfinit_exec_run(struct recover *r,void *arg)
{ struct gfinit_exec *e=arg;CHECK(r==&core && e->rom==rom_storage && e->rom_size==512);CHECK(e->main_id<5);run_mask|=1U<<e->main_id;e->steps=9;e->fail_pc=0;if(run_fail==e->main_id+1)return -1;writes++;return 0; }
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
    CHECK(live_pages==0);zero(block_pages,sizeof(block_pages));alloc_calls=fail_alloc=peak_pages=0;
    zero(&control,sizeof(control));owner=0;zero(&core,sizeof(core));core.ops=&base;core.s0_profile=&s0;zero(image,sizeof(image));zero(&transport,sizeof(transport));
    allocations=frees=reads=writes=begins=steps=ends=cleans=run_mask=run_fail=step_fail=decode_fail=probe_fail=peek_fail=verify_calls=event_count=0;
    image[0]=0x55;image[1]=0xaa;image[2]=1;image[0x18]=0x20;
    image[0x20]='P';image[0x21]='C';image[0x22]='I';image[0x23]='R';image[0x24]=0xde;image[0x25]=0x10;image[0x26]=0x21;image[0x27]=2;
    u8 sum=0;for(u32 i=0;i<511;i++)sum+=image[i];image[511]=0-sum;
}
static void prepared(void)
{ CHECK(call(GF_OP_INSPECT,0,0)==0);CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)==0); }
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
    setup();prepared();core.state=REC_FAILED;gfinit_recover_end();CHECK(frees==1 && !owner->rom);
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
static void number(u32 n)
{ char buf[16];u32 i=0;do{buf[i++]='0'+n%10;n/=10;}while(n);while(i){char c[2]={buf[--i],0};output(c);} }
static void allocation_test(void)
{
    for(u32 fail=1;fail<=5;fail++) {
        setup();fail_alloc=fail;
        int rc=call(GF_OP_INSPECT,0,0);
        if(!rc)rc=call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0);
        if(!rc)rc=call(GF_OP_PREPARE,GF_ALLOW_WRITE,0);
        CHECK(rc<0 && live_pages==0 && !owner->inspected);
    }
    setup();prepared();CHECK(peak_pages<=TEST_PAGE_BUDGET);
    output("bytes control/decode/plan/ROM/flow pages_peak: ");
    number(sizeof(control));output("/");number(sizeof(struct gfinit_decode));output("/");
    number(sizeof(struct gfinit_plan));output("/");number(GF_ROM_MAX);output("/");
    number(owner->flow_bytes);output(" pages=");number(peak_pages);output("\n");
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && live_pages==0);
    setup();probe_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0 && live_pages==0);
}
void _start(void)
{ lifecycle();failures_test();allocation_test();output(failures?"GF owner FAIL\n":"GF owner PASS\n");__asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0));__builtin_unreachable(); }
