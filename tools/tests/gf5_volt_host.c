/* Independent fixture values from MIT nouveau c03c1a1:
 * nvkm/subdev/bios/{bit.c:36-57,gpio.c:32-118,volt.c:29-100,137-150,
 * vmap.c:29-53,93-153}; gpio/nv10.c:29-79; volt/base.c:31-78,104-182;
 * volt/gpio.c:30-97. Bytes and MMIO golden words are embedded here;
 * no external reference checkout or libc is required. */
#define GF5_HOST_UNIT 4
#include "gf5_fake.h"

/* Deliberately disjoint synthetic ROM objects, not real-card addresses. */
enum { ROM_BYTES=2048, BIT=0x40, POWER=0x80, VOLT=0x100, DCB=0x180,
       GPIO=0x200, VMAP=0x300, VMAP_HEADER=4,
       BIT_BYTES=12, BIT_ENTRY_BYTES=6, POWER_BYTES=0x24,
       LOW=0x600818, MID=0x60081c, HIGH=0x600850,
       TRACE_CAPACITY=128, REG_CAPACITY=16,
       UV_LOW=1000000, UV_MID=1100000, UV_HIGH=1200000,
       UV_BETWEEN=1050000, UV_OVER=1200001, DEADLINE=0x12345678 };
static struct gf5_host h;
static struct gf4_state state;
static struct gf5_host_event events[TRACE_CAPACITY];
static struct gf4_host_event display_events[TRACE_CAPACITY];
static struct gf3_host_reg regs[REG_CAPACITY];
static u8 rom[ROM_BYTES];
static void put(u32 at,u32 v,u32 n)
{
    assert(at+n<=sizeof(rom));
    for (u32 i=0;i<n;++i) rom[at+i]=(u8)(v>>(8*i));
}
static void gpio40(u32 index,u8 tag,u8 line,u8 zero,u8 one)
{
    /* gpio.c:103-111: v40 line[4:0], func[15:8], log[28:27]/[30:29]. */
    put(GPIO+4+index*4,line|((u32)tag<<8)|((u32)zero<<27)|((u32)one<<29),4);
}
static void volt_entry(u32 i,u8 centivolts,u8 vid)
{
    u32 header=rom[VOLT]==0x12 ? 5 : rom[VOLT+1];
    put(VOLT+header+i*2,centivolts,1);
    put(VOLT+header+i*2+1,rom[VOLT]==0x30 ? vid<<2 : vid,1);
}
static void setup(u8 version)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf5_host_init(&h,&state);
    h.events=events; h.cevents=TRACE_CAPACITY;
    h.display.events=display_events; h.display.cevents=TRACE_CAPACITY;
    h.display.base.regs=regs; h.display.base.cregs=REG_CAPACITY;
    h.ctx.deadline=DEADLINE;
    gf2_host_memset(rom,0,sizeof(rom));
    put(BIT,0x4942b8ff,4); rom[BIT+4]='T';
    rom[BIT+8]=BIT_BYTES; rom[BIT+9]=BIT_ENTRY_BYTES; rom[BIT+10]=1;
    rom[BIT+BIT_BYTES]='P'; rom[BIT+BIT_BYTES+1]=2;
    put(BIT+BIT_BYTES+2,POWER_BYTES,2); put(BIT+BIT_BYTES+4,POWER,2);
    put(POWER+0x0c,VOLT,4);
    rom[VOLT]=version;
    if (version==0x12) { rom[VOLT+1]=2; rom[VOLT+2]=4; rom[VOLT+4]=3; }
    else if (version==0x20) {
        rom[VOLT+1]=6; rom[VOLT+2]=4; rom[VOLT+3]=2; rom[VOLT+5]=3;
    } else { rom[VOLT+1]=5; rom[VOLT+2]=2; rom[VOLT+3]=4; rom[VOLT+4]=3; }
    volt_entry(0,100,0); volt_entry(1,110,1);
    volt_entry(2,110,2); volt_entry(3,120,3);
    put(0x36,DCB,2); rom[DCB]=0x30; rom[DCB+1]=12;
    put(DCB+6,0x4edcbdcb,4); put(DCB+10,GPIO,2);
    rom[GPIO]=0x40; rom[GPIO+1]=4; rom[GPIO+2]=2; rom[GPIO+3]=4;
    gpio40(0,0x04,0,2,3); gpio40(1,0x05,2,2,3);
    h.ctx.rom=(struct gf5_rom_view){.bytes=rom,.size=sizeof(rom)};
}
static u32 reg_value(u32 reg)
{
    for (u32 i=0;i<h.display.base.nregs;++i)
        if (regs[i].off==reg && !regs[i].bar) return regs[i].value;
    return 0;
}
static void seed(u32 reg,u32 value) { gf3_host_set(&h.display.base,0,reg,value); }
static void no_io(void)
{ assert(!h.display.nevents && !h.nevents); }
static void retained(void)
{
    assert(h.ctx.rom.bytes==rom && h.ctx.rom.size==sizeof(rom));
    assert(h.ctx.phase==GF5_RUN && h.ctx.accepting && !h.ctx.busy);
    assert(h.ctx.deadline==DEADLINE && !h.live_allocs && !h.nevents);
}
static void write_trace(u32 index,u32 reg,u32 value)
{
    u32 seen=0;
    for (u32 i=0;i<h.display.nevents;++i) {
        const struct gf4_host_event *e=&display_events[i];
        if (e->op!=D_WRITE) continue;
        if (seen++!=index) continue;
        assert(e->a[0]==0 && e->a[1]==reg && e->a[2]==4 && e->a[3]==value);
        return;
    }
    assert(0);
}
static void voltage_versions(void)
{
    static const u8 versions[]={0x12,0x20,0x30};
    for (u32 i=0;i<sizeof(versions);++i) {
        setup(versions[i]);
        seed(LOW,0xa5a50000); seed(MID,0x5a5a0000);
        /* Positive equal error picks the later 1.10V VID=2. */
        assert(!gf5_volt_set(&h.ctx,UV_BETWEEN));
        assert(h.display.calls[D_READ]==2 && h.display.calls[D_WRITE]==2);
        write_trace(0,LOW,0xa5a50010); write_trace(1,MID,0x5a5a0003);
        assert(reg_value(LOW)==0xa5a50010 && reg_value(MID)==0x5a5a0003);
        retained();
        gf5_host_trace_clear(&h);
        /* Exact match exits on the first 1.10V entry, VID=1. */
        assert(!gf5_volt_set(&h.ctx,UV_MID));
        write_trace(0,LOW,0xa5a50011); write_trace(1,MID,0x5a5a0002);
        gf5_host_trace_clear(&h);
        assert(gf5_volt_set(&h.ctx,UV_OVER)==GF5_ERANGE); no_io();
        /* GPIO sense is independent of output latch in this fixture. */
        seed(LOW,0x100); seed(MID,0);
        u32 uv=0;
        assert(!gf5_volt_get(&h.ctx,&uv) && uv==UV_MID);
        assert(h.display.calls[D_READ]==2 && !h.display.calls[D_WRITE]);
        retained();
    }
}
static void gpio_golden(void)
{
    /* nv10.c:29-79 golden register/mask/set/sense vectors. Explicit expected
     * words cover every line; no product register helper is reused. */
    static const struct { u8 line; u32 reg,mask,sense; } cases[]={
        {0,LOW,0x11,0x100},{1,LOW,0x110000,0x1000000},
        {2,MID,0x3,0x4},{3,MID,0x30,0x40},{4,MID,0x300,0x400},
        {5,MID,0x3000,0x4000},{6,MID,0x30000,0x40000},
        {7,MID,0x300000,0x400000},{8,MID,0x3000000,0x4000000},
        {9,MID,0x30000000,0x40000000},{10,HIGH,0x3,0x4},
        {11,HIGH,0x30,0x40},{12,HIGH,0x300,0x400},
        {13,HIGH,0x3000,0x4000}
    };
    const u8 normal[2]={0,3},inverse[2]={3,0};
    for (u32 i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        setup(0x30);
        seed(cases[i].reg,0);
        assert(!gf5_gpio_set(&h.ctx,cases[i].line,normal,true));
        write_trace(0,cases[i].reg,cases[i].mask);
        gf5_host_trace_clear(&h);
        seed(cases[i].reg,U32_MAX);
        assert(!gf5_gpio_set(&h.ctx,cases[i].line,inverse,true));
        write_trace(0,cases[i].reg,U32_MAX^cases[i].mask);
        gf5_host_trace_clear(&h);
        bool value=false;
        seed(cases[i].reg,cases[i].sense);
        assert(!gf5_gpio_get(&h.ctx,cases[i].line,normal,&value) && value);
        assert(!gf5_gpio_get(&h.ctx,cases[i].line,inverse,&value) && !value);
        seed(cases[i].reg,0);
        assert(!gf5_gpio_get(&h.ctx,cases[i].line,inverse,&value) && value);
        retained();
    }
    setup(0x30); bool value=true;
    assert(gf5_gpio_get(&h.ctx,14,normal,&value)==GF5_EINVAL && value);
    assert(gf5_gpio_set(&h.ctx,14,normal,false)==GF5_EINVAL); no_io();
}
static void gpio_tables(void)
{
    u8 line,log[2];
    setup(0x30);
    assert(!gf5_gpio_find(&h.ctx,4,&line,log) && line==0 && log[0]==2 && log[1]==3);
    no_io();
    /* Pre-3.0 GPIO table layout. */
    rom[GPIO]=0x10; rom[GPIO+1]=2; rom[GPIO+2]=1;
    put(GPIO+3,9|(9<<5)|(1<<11)|(2<<13),2);
    assert(!gf5_gpio_find(&h.ctx,9,&line,log) && line==9 && log[0]==1 && log[1]==2);
    /* DCB 2.2 GPIO pointer lives before the DCB. */
    rom[DCB]=0x22; put(DCB+4,0x4edcbdcb,4);
    rom[DCB-1]=0x13; put(DCB-0x0f,GPIO,2);
    assert(!gf5_gpio_find(&h.ctx,9,&line,log) && line==9);
    setup(0x30);
    rom[GPIO]=0x41; rom[GPIO+2]=1; rom[GPIO+3]=5;
    put(GPIO+4,13|(0x76<<8),4); rom[GPIO+8]=0x90;
    assert(!gf5_gpio_find(&h.ctx,0x76,&line,log) && line==13 && log[0]==1 && log[1]==2);
    no_io();
    line=0xaa; log[0]=0xbb; log[1]=0xcc;
    assert(gf5_gpio_find(&h.ctx,9,&line,log)==GF5_ENODEV);
    assert(line==0xaa && log[0]==0xbb && log[1]==0xcc);
    rom[GPIO+4]=14;
    assert(gf5_gpio_find(&h.ctx,0x76,&line,log)==GF5_EINVAL); no_io();
}
static void all_vid_bits(void)
{
    setup(0x20);
    /* bios/gpio.h:15-22; deliberately different GPIO lines: VID0..7
     * map to 0,1,2,9,10,11,12,13, in that bit order. */
    static const u8 tags[]={4,5,6,0x1a,0x73,0x74,0x75,0x76};
    static const u8 lines[]={0,1,2,9,10,11,12,13};
    rom[GPIO+2]=sizeof(tags); rom[VOLT+5]=0xff;
    for (u32 i=0;i<sizeof(tags);++i) gpio40(i,tags[i],lines[i],2,3);
    volt_entry(3,120,0xff);
    assert(!gf5_volt_set(&h.ctx,UV_HIGH));
    assert(h.display.calls[D_WRITE]==sizeof(tags));
    write_trace(0,LOW,0x11); write_trace(1,LOW,0x110011);
    write_trace(2,MID,3); write_trace(3,MID,0x30000003);
    write_trace(4,HIGH,3); write_trace(5,HIGH,0x33);
    write_trace(6,HIGH,0x333); write_trace(7,HIGH,0x3333);
    gf5_host_trace_clear(&h);
    seed(LOW,0x1000100); seed(MID,0x40000004); seed(HIGH,0x4444);
    u32 uv=0;
    assert(!gf5_volt_get(&h.ctx,&uv) && uv==UV_HIGH);
    assert(h.display.calls[D_READ]==sizeof(tags)); retained();
}
static void mask_and_unknown(void)
{
    setup(0x30);
    /* Missing bit 1 leaves only VID0. Applied to both sense and table VID. */
    rom[GPIO+2]=1;
    seed(LOW,0); seed(MID,4);
    u32 uv=0;
    assert(!gf5_volt_get(&h.ctx,&uv) && uv==UV_LOW);
    assert(h.display.calls[D_READ]==1);
    gf5_host_trace_clear(&h);
    assert(!gf5_volt_set(&h.ctx,UV_BETWEEN));
    assert(h.display.calls[D_WRITE]==1); write_trace(0,LOW,0x10);
    gf5_host_trace_clear(&h);
    rom[VOLT+3]=1; volt_entry(0,110,2);
    assert(!gf5_volt_get(&h.ctx,&uv) && uv==UV_MID);
    assert(h.display.calls[D_READ]==1);
    setup(0x30);
    rom[VOLT+3]=1; seed(LOW,0x100); seed(MID,4);
    uv=0xfeed;
    assert(gf5_volt_get(&h.ctx,&uv)==GF5_ESTATE && uv==0xfeed);
    retained();
    /* Inverted VID0: sense low means logical one. */
    setup(0x30); gpio40(0,4,0,3,2); seed(LOW,0); seed(MID,0);
    assert(!gf5_volt_get(&h.ctx,&uv) && uv==UV_MID);
}
static void id_selection(void)
{
    setup(0x30); seed(LOW,0x100); seed(MID,4); /* previous=1.20V */
    assert(!gf5_volt_id(&h.ctx,100,110,-1));
    /* min_id raises the request to 1.10V, not the previous 1.20V. */
    write_trace(0,LOW,0x111); write_trace(1,MID,0x6);
    assert(h.display.calls[D_WRITE]==2); retained();
    setup(0x30); seed(LOW,0x100); seed(MID,4);
    assert(!gf5_volt_id(&h.ctx,100,110,1));
    assert(h.display.calls[D_READ]==2 && !h.display.calls[D_WRITE]);
    setup(0x30); seed(LOW,0); seed(MID,0);
    assert(!gf5_volt_id(&h.ctx,110,0,1));
    assert(h.display.calls[D_WRITE]==2);
    setup(0x30); seed(LOW,0x100); seed(MID,0);
    assert(!gf5_volt_id(&h.ctx,110,120,1)); /* equality skips min_id */
    assert(!h.display.calls[D_WRITE]);
    setup(0x30);
    gf4_host_fail_at(&h.display,D_READ,1,-1);
    assert(!gf5_volt_id(&h.ctx,100,120,1)); /* prev failure still sets */
    assert(h.display.calls[D_READ]==3 && h.display.calls[D_WRITE]==2);
    assert(!h.ctx.last_rc && !h.ctx.source_rc); retained();
    setup(0x30);
    assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_ENODEV); no_io();
    setup(0x30);
    rom[BIT+BIT_BYTES+1]=1; put(POWER+0x10,VOLT,4);
    assert(!gf5_volt_id(&h.ctx,100,0,0)); /* P v1 has no vmap */
    assert(h.display.calls[D_WRITE]==2);
}
static void vmap_absent_speedo(void)
{
    static const u8 versions[]={0x10,0x20};
    for (u32 v=0;v<sizeof(versions);++v) {
        setup(0x30); put(POWER+0x20,VMAP,4);
        rom[VMAP]=versions[v]; rom[VMAP+1]=VMAP_HEADER;
        rom[VMAP+2]=versions[v]==0x10 ? 0x14 : 0x22; rom[VMAP+3]=1;
        /* All modes require speedo BEFORE the mode switch, even mode 3. */
        for (u32 mode=0;mode<4;++mode) {
            rom[VMAP+VMAP_HEADER]=mode;
            assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_ENOTSUP); no_io();
        }
        /* A valid map table with no matching entry permits id*10000. */
        assert(!gf5_volt_id(&h.ctx,100,0,0));
        assert(h.display.calls[D_WRITE]==2);
        gf5_host_trace_clear(&h);
        rom[VMAP+2]=1;
        assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_EBIOS); no_io();
        rom[VMAP]=0x99;
        assert(!gf5_volt_id(&h.ctx,100,0,0));
        assert(h.display.calls[D_WRITE]==2);
    }
}
/* A07/V0: check the real parser/selector, with literal BIOS fixtures above.
 * No helper here computes a voltage or substitutes for the product check.
 * The only permitted context changes are the diagnostic return codes. */
static void check_request(u8 id,u8 minimum,i32 expected)
{
    struct gf5 before;
    gf2_host_memcpy(&before,&h.ctx,sizeof(before));
    h.ctx.last_rc=GF5_EIO; h.ctx.source_rc=-1;
    assert(gf5_volt_check_id(&h.ctx,id,minimum)==expected);
    assert(h.ctx.last_rc==expected && h.ctx.source_rc==expected);
    before.last_rc=expected; before.source_rc=expected;
    assert(!gf2_host_memcmp(&before,&h.ctx,sizeof(before)));
    struct gf5_host_activity activity=gf5_host_activity(&h);
    assert(!activity.reads && !activity.writes && !activity.pauses && !activity.resumes);
    no_io(); retained();
}
static void check_classification(void)
{
    static const u8 versions[]={0x12,0x20,0x30};
    for (u32 i=0;i<sizeof(versions);++i) {
        setup(versions[i]);
        check_request(100,0,0);
        check_request(100,110,0);
        check_request(105,120,0); /* round up, with an available minimum */
        check_request(121,0,GF5_ERANGE);
        check_request(100,121,GF5_ERANGE); /* minimum must also be feasible */
        check_request(0,0,GF5_ENODEV); /* no mapped voltage request */
    }
    setup(0x30); h.ctx.rom.bytes=0;
    assert(gf5_volt_check_id(&h.ctx,100,0)==GF5_ENOTSUP); no_io();
    setup(0x30); rom[BIT+BIT_BYTES]='X'; /* decoded BIT, no P entry */
    check_request(0,0,GF5_ENODEV);
    setup(0x30); put(POWER+0x0c,0,4);
    check_request(0,0,GF5_ENODEV);
    check_request(100,0,GF5_ENODEV);
    /* Missing DCB/GPIO pins reduce the VID mask, as volt/gpio.c:30-97
     * does; an unmapped id=0 still stops before parsing the wiring. */
    setup(0x30); put(0x36,0,2);
    check_request(100,0,0); check_request(0,0,GF5_ENODEV);
    setup(0x30); put(DCB+10,0,2);
    check_request(100,0,0); check_request(0,0,GF5_ENODEV);
    setup(0x30); rom[GPIO+2]=0;
    check_request(100,0,0); check_request(0,0,GF5_ENODEV);
    setup(0x30); rom[VOLT]=0x40;
    check_request(0,0,GF5_ENODEV);
    check_request(100,0,GF5_ENOTSUP);
    setup(0x30); rom[BIT+BIT_BYTES+1]=3;
    check_request(100,0,GF5_ENOTSUP);
    setup(0x30); rom[DCB]=0x42;
    check_request(100,0,GF5_ENOTSUP);
    setup(0x30); rom[GPIO]=0x42;
    check_request(100,0,GF5_ENOTSUP);
    setup(0x30); rom[VOLT+1]=4;
    check_request(0,0,GF5_ENODEV);
    check_request(100,0,GF5_EBIOS);
    setup(0x30); rom[VOLT+3]=0;
    check_request(100,0,GF5_ERANGE);
    setup(0x30); rom[DCB+6]=0;
    check_request(100,0,GF5_EBIOS);
    setup(0x30); rom[GPIO+3]=3;
    check_request(100,0,GF5_EBIOS);
    setup(0x30); rom[GPIO+2]=1; /* known missing VID1 is optional */
    check_request(110,0,0);
    setup(0x30); gpio40(1,5,14,2,3); /* invalid wiring is not absent */
    check_request(100,0,GF5_EINVAL);
    setup(0x30); rom[BIT+BIT_BYTES+1]=1; put(POWER+0x10,VOLT,4);
    check_request(100,0,0); /* P v1 has no vmap */
    setup(0x30); put(BIT+BIT_BYTES+2,0x10,2);
    check_request(100,0,0); /* short P v2 predates vmap */
    assert(gf5_volt_check_id(0,100,0)==GF5_EINVAL);
}
static void check_vmap(void)
{
    static const u8 versions[]={0x10,0x20};
    for (u32 v=0;v<sizeof(versions);++v) {
        setup(0x30); put(POWER+0x20,VMAP,4);
        rom[VMAP]=versions[v]; rom[VMAP+1]=VMAP_HEADER;
        rom[VMAP+2]=versions[v]==0x10 ? 0x14 : 0x22; rom[VMAP+3]=1;
        for (u32 mode=0;mode<4;++mode) {
            rom[VMAP+VMAP_HEADER]=mode;
            check_request(0,0,GF5_ENOTSUP);
            check_request(100,0,GF5_ENOTSUP); /* even zero floor needs speedo */
        }
        rom[VMAP+3]=2;
        check_request(100,1,GF5_ENOTSUP); /* nonzero floor needs speedo too */
        check_request(100,110,0); /* both indices outside map: direct units */
        rom[VMAP+3]=0;
        check_request(100,0,0); /* no real zero entry: no extra floor */
        rom[VMAP]=0x99;
        check_request(100,110,0);
        check_request(0,0,GF5_ENODEV);
        rom[VMAP]=versions[v]; rom[VMAP+2]=1;
        check_request(100,110,GF5_EBIOS);
        rom[VMAP+2]=versions[v]==0x10 ? 0x14 : 0x22;
        rom[VMAP+3]=0xff; /* declared entries run past ROM */
        check_request(100,110,GF5_EBIOS);
    }
}
/* volt/base.c:32-47,156-182,195-223,312-317; bios/vmap.c:29-53.
 * Positive controls retain usable VID tables beside each absent-map case. */
static void empty_and_fallback(void)
{
    static const u8 versions[]={0x12,0x20,0x30};
    for (u32 i=0;i<sizeof(versions);++i) {
        setup(versions[i]);
        rom[VOLT+(versions[i]==0x30 ? 3 : 2)]=0;
        u32 uv=0xfeed;
        assert(gf5_volt_get(&h.ctx,&uv)==GF5_ESTATE && uv==0xfeed);
        gf5_host_trace_clear(&h);
        assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_ENODEV); no_io();
        check_request(0,0,GF5_ENODEV);
        check_request(100,0,GF5_ERANGE);
        assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_ERANGE); no_io();
        assert(gf5_volt_id(&h.ctx,100,0,0)==GF5_ERANGE);
        assert(!h.display.calls[D_WRITE]);
        setup(versions[i]);
        assert(!gf5_volt_id(&h.ctx,100,0,0));
        assert(h.display.calls[D_WRITE]==2);
    }
    /* Zero requests stop at map, even if get/set would reject the table. */
    setup(0x30); rom[VOLT+1]=4;
    assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_ENODEV); no_io();
    setup(0x30); rom[VOLT]=0x99;
    assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_ENODEV); no_io();
    /* Missing BIT/P and unknown P versions all use id-unit fallback. */
    for (u32 kind=0;kind<3;++kind) {
        setup(0x30);
        if (kind==0) rom[BIT]=0;
        if (kind==1) rom[BIT+BIT_BYTES]='X';
        if (kind==2) rom[BIT+BIT_BYTES+1]=3;
        check_request(0,0,GF5_ENODEV);
        assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_ENODEV); no_io();
        /* A real request still reaches the independent voltage parser. */
        i32 expected=kind==1 ? GF5_ENODEV : GF5_ENOTSUP;
        check_request(100,0,expected);
        assert(gf5_volt_id(&h.ctx,100,0,0)==expected); no_io();
    }
    setup(0x30); put(POWER+0x20,VMAP,4); rom[VMAP]=0x99;
    /* An unknown vmap version ignores even unusable header/entry lengths. */
    rom[VMAP+1]=0; rom[VMAP+2]=0; rom[VMAP+3]=0xff;
    check_request(0,0,GF5_ENODEV); check_request(100,110,0);
    assert(gf5_volt_id(&h.ctx,0,0,0)==GF5_ENODEV); no_io();
    assert(!gf5_volt_id(&h.ctx,100,110,0));
    assert(h.display.calls[D_WRITE]==2);
}
static void malformed(void)
{
    setup(0x30); h.ctx.rom.bytes=0;
    u32 uv=0xfeed;
    assert(gf5_volt_get(&h.ctx,&uv)==GF5_ENOTSUP && uv==0xfeed); no_io();
    setup(0x30); put(POWER+0x0c,0,4);
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_ENODEV); no_io();
    setup(0x30); rom[VOLT]=0x40;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_ENOTSUP); no_io();
    setup(0x30); rom[VOLT+1]=4;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); rom[VOLT+2]=1;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); h.ctx.rom.size=VOLT+5+7;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); put(POWER+0x0c,U32_MAX,4);
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); rom[BIT+9]=5;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); put(BIT+BIT_BYTES+2,0x0f,2);
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); rom[DCB+6]=0;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); rom[GPIO+3]=3;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
    setup(0x30); h.ctx.rom.size=GPIO+4+7;
    assert(gf5_volt_set(&h.ctx,UV_LOW)==GF5_EBIOS); no_io();
}
static void io_failure(void)
{
    for (u32 nth=1;nth<=2;++nth) {
        setup(0x30); seed(LOW,0x100); seed(MID,4);
        gf4_host_fail_at(&h.display,D_READ,nth,-1);
        u32 uv=0xfeed;
        assert(gf5_volt_get(&h.ctx,&uv)==GF5_EIO && uv==0xfeed);
        assert(h.display.calls[D_READ]==nth && !h.display.calls[D_WRITE]);
        assert(h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==-1); retained();
        setup(0x30);
        gf4_host_fail_at(&h.display,D_WRITE,nth,-1);
        assert(gf5_volt_set(&h.ctx,UV_HIGH)==GF5_EIO);
        assert(h.display.calls[D_WRITE]==nth && h.display.calls[D_READ]==nth);
        assert(h.ctx.source_rc==-1); retained();
        /* Earlier writes remain; neither rollback nor later GPIO writes. */
        assert(reg_value(LOW)==(nth==1 ? 0 : 0x11));
        assert(reg_value(MID)==0);
        setup(0x30);
        gf4_host_fail_at(&h.display,D_READ,nth,-1);
        assert(gf5_volt_set(&h.ctx,UV_HIGH)==GF5_EIO);
        assert(h.display.calls[D_READ]==nth && h.display.calls[D_WRITE]==nth-1);
        assert(h.ctx.source_rc==-1); retained();
    }
    setup(0x30);
    gf4_host_write_applied_error(&h.display,1,-1);
    assert(gf5_volt_set(&h.ctx,UV_HIGH)==GF5_EIO);
    assert(reg_value(LOW)==0x11 && reg_value(MID)==0);
    assert(h.display.calls[D_WRITE]==1 && h.display.calls[D_READ]==1); retained();
}
void _start(void)
{
    voltage_versions(); gpio_golden(); gpio_tables(); all_vid_bits(); mask_and_unknown();
    id_selection(); vmap_absent_speedo(); malformed(); io_failure();
    check_classification(); check_vmap(); empty_and_fallback();
    gf2_host_exit(0);
}
