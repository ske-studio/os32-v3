/* Real recover checker with the GF table; synthetic layouts only (S2 owns ROM
 * layouts). No physical I/O. ILP32 qemu runner. */
#include "../../kdebug/recover.c"
#include "../../kdebug/recover_gf.c"
#include "gfinit_fixture.h"
#include "recover_gf_inventory.h"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static unsigned failures;
static void print(const char *s)
{
    unsigned n = 0; while (s[n]) n++;
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { print("FAIL " #x "\n"); failures++; } } while (0)
static struct gfinit_plan gf_fixture_buffer;
static struct recover_profile gf_fixture_profile;
static void gf_fixture_init(void)
{
    CHECK(!recover_gf_template.rules && !recover_gf_template.rule_count &&
          !recover_gf_template.targets && !recover_gf_template.target_count &&
          !recover_gf_template.layouts && !recover_gf_template.layout_count);
    u8 *bytes = (u8 *)&gf_fixture_buffer;
    for (u32 i = 0; i < sizeof(gf_fixture_buffer); i++) bytes[i] = 0xa5;
    CHECK(recover_gf_profile_expand(&gf_fixture_profile, &gf_fixture_buffer) == 0);
    CHECK(gf_fixture_profile.rules == gf_fixture_buffer.rules &&
          gf_fixture_profile.targets == gf_fixture_buffer.targets &&
          gf_fixture_profile.layouts == gf_fixture_buffer.layouts);
    /* Only the occupied parts of the three arrays may be changed. */
    for (u32 i = 0; i < sizeof(gf_fixture_buffer); i++) {
        u8 *at = bytes + i;
        if ((at >= (u8 *)gf_fixture_buffer.rules &&
             at < (u8 *)(gf_fixture_buffer.rules + gf_fixture_profile.rule_count)) ||
            (at >= (u8 *)gf_fixture_buffer.targets &&
             at < (u8 *)(gf_fixture_buffer.targets + gf_fixture_profile.target_count)) ||
            (at >= (u8 *)gf_fixture_buffer.layouts &&
             at < (u8 *)(gf_fixture_buffer.layouts + gf_fixture_profile.layout_count))) continue;
        CHECK(*at == 0xa5);
    }
    struct recover_profile reset = gf_fixture_profile;
    CHECK(recover_gf_profile_expand(&reset, 0) == 0);
    CHECK(!reset.rules && !reset.rule_count && !reset.targets && !reset.target_count &&
          !reset.layouts && !reset.layout_count && reset.exprs == gf_fixture_profile.exprs);
    u32 crc = recover_profile_crc(&gf_fixture_profile);
    gf_fixture_buffer.rules[0].write_mask ^= 1;
    gf_fixture_buffer.layouts[gf_fixture_profile.layout_count-1].substep ^= 1;
    gf_fixture_buffer.targets[gf_fixture_profile.target_count-1].offset ^= 1;
    gf_fixture_profile.rule_count++;
    CHECK(recover_gf_profile_expand(&gf_fixture_profile, &gf_fixture_buffer) == 0);
    CHECK(recover_profile_crc(&gf_fixture_profile) == crc);
}
static struct recover core;
static struct recover_profile profile;
static u32 flow[RECOVER_NODE_MAX * RECOVER_BITS_WORDS], valid[RECOVER_BITS_WORDS];
static struct recover_plan_buffers buffers = {flow, valid};
static u32 values[RECOVER_TARGET_MAX], indices[RECOVER_TARGET_MAX];
static u32 n1_completed, writes, cleanup_calls, data_writes, inject_key, inject_when, n1key;
static struct recover_identity ident = {.boot=1, .image_version=1, .kdebug_version=1,
    .bdf=13, .device=0x022110de, .bar0=0x24000000};
static u32 rawcommand;
static u32 rawbars[] = {0x24000000, 0x30000008, 0x25000000};
static u32 tick(void *v) { (void)v; return 0; }
static int identity(void *v, struct recover_identity *id) { (void)v; *id=ident; return 0; }
static int ok(void *v) { (void)v; return 0; }
static int raw(void *v, u32 *command, u32 bar[3])
{ (void)v; *command=rawcommand; for (u32 i=0;i<3;i++) bar[i]=rawbars[i]; return 0; }
static int read_fake(void *v, const struct recover_bus_access *a, u32 *value)
{
    (void)v;
    *value=a->dir_substep==REC_SELECT ? indices[a->key] : values[a->key];
    return 0;
}
static int write_fake(void *v, const struct recover_bus_access *a, u32 value)
{
    (void)v; writes++;
    if(a->space==REC_PCI_CFG && a->offset==4) rawcommand=value;
    if(a->space==REC_PCI_CFG && a->offset>=0x10 && a->offset<=0x18) {
        u32 bar=(a->offset-0x10)/4;
        rawbars[bar]=value;
        if(value==~0U) {
            values[a->key]=0xff000000U | (bar==1 ? 8 : 0);
            return 0;
        }
    }
    if (a->dir_substep==REC_SELECT) indices[a->key]=value;
    else {
        const struct recover_target *t=&profile.targets[a->key];
        values[a->key]=a->dir_substep==REC_DATA && t->space==REC_VGA_INDEXED &&
            t->port==RECOVER_CRTC && t->index==RECOVER_CR1F ?
            (value==RECOVER_CR1F_UNLOCK?RECOVER_CR1F_UNLOCKED:0):value;
    }
    if (a->key==n1key && a->dir_substep==REC_DATA) {
        data_writes++;
        if (inject_when==REC_AFTER) values[inject_key]=1;
    }
    /* cfg and PBUS are separate reads, but writes affect the same device. */
    if (a->key==2) values[0]=value;
    if (a->key==3) values[1]=value;
    return 0;
}
static int route_read_fake(void *v,u32 *out) { (void)v; *out=0; return 0; }
static int route_write_fake(void *v,u32 value) { (void)v; (void)value; return 0; }
static int abort_fake(void *v,u32 session) { (void)v; (void)session; return 0; }
static int cleanup_fake(void *v) { (void)v; cleanup_calls++; return 0; }
static int event_fake(void *v,enum recover_state s,u32 session)
{ (void)v; (void)s; (void)session; return 0; }
static const struct recover_ops ops = {tick,identity,ok,raw,read_fake,write_fake,
    route_read_fake,route_write_fake,ok,ok,abort_fake,cleanup_fake,event_fake,0,0,0,0};
static const u32 four[] = {REC_IMM,REC_ARG_READ,REC_IMM,REC_IMM};
static const u32 two[] = {REC_IMM,REC_IMM};
static const struct recover_opcode_layout layouts[] = {
    {256,0,0,0,RECOVER_NONE,0},
    {256,REC_WRITE,2,two,REC_WRITE,0},
    {257,REC_SELECT,2,two,REC_SELECT,0},
    {257,REC_DATA,2,two,REC_DATA,0},
};
static struct recover_plan_node nodes[8];
static struct recover_plan_arg args[16];
static struct recover_plan_entry entries[5];
static struct recover_plan_dep links[2];
static struct recover_plan plan;
static u32 hash;
static void setup(void)
{
    rawcommand=0x102; rawbars[0]=0x24000000; rawbars[1]=0x30000008; rawbars[2]=0x25000000;
    profile=gf_fixture_profile;
    profile.trial_limit=1000; profile.opcode_limit=1000; profile.restore_limit=1000;
    profile.max_steps=4096; profile.max_polls=65536; profile.restore_max_polls=65536;
    profile.layouts=layouts; profile.layout_count=ARRAY_SIZE(layouts);
    profile.crc=recover_profile_crc(&profile);
    recover_init(&core,&ops,0,true,false);
    core.cache=(struct recover_bar_measurement){.identity=ident,.measurement_session=1};
    for(u32 i=0;i<3;i++) core.cache.bar[i]=(struct recover_bar_extent){
        rawbars[i],rawbars[i]&~15U,0x1000000,1};
    for(u32 i=0;i<RECOVER_TARGET_MAX;i++) values[i]=indices[i]=0;
    for(u32 i=0;i<3;i++) values[7+i]=rawbars[i];
    n1_completed=writes=cleanup_calls=data_writes=0; inject_key=RECOVER_NONE; inject_when=0;
}
