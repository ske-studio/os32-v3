/* Pure host calculation/allow-table extraction, no hardware I/O. */
#define _start gf_old_tests
#include "recover_gf_host.c"
#undef _start
#include "../../kdebug/gfinit_pll.c"
#include "../../kdebug/gfinit_mode.c"
#include "gf_display_calc_host.inc"
#include "../../hwdev/gf_load_state.c"
static void number(u32 x)
{
    char b[16]; u32 n = 0;
    do { b[n++] = (char)('0' + x % 10); x /= 10; } while (x);
    while (n) { char a[2] = {b[--n], 0}; print(a); }
}
static void value(u32 v) { number(v); print(" "); }
static void field(const char *name, u32 v) { print(name); print("="); number(v); print("\n"); }
static struct gfinit_mode mode;
static u32 load_seen,load_stop=~0U,load_sections,load_waits,load_fault;
static bool load_silent;
static int load_section(void *saved,u32 section)
{
    (void)saved;load_sections++;
    if (!load_silent) field("section",section);
    return load_fault==1 && section==load_stop ? -1 : 0;
}
static int load_wait(void *saved,u32 ms,bool fp_only)
{
    (void)saved;load_waits++;
    if (!load_silent) {
        print("wait=");value(ms);value(fp_only);value(load_seen);print("\n");
    }
    return load_fault==2 && load_waits==load_stop ? -1 : 0;
}
static int load_row(void *saved,const struct gfinit_mode *state,u32 i)
{
    (void)saved;
    CHECK(i==load_seen++);
    if (!load_fault && i==load_stop) return -1;
    if (load_silent) return 0;
    const struct gf_mode_row *r=&gf_mode_rows[i];
    u32 v=r->bits, keep=r->keep, bits=r->bits;
    const u32 live=0xa5a55a5aU & (r->width==1 ? 255U : ~0U);
    if (r->expr==GF_C_STATE) {
        CHECK(r->source <= sizeof(*state)-r->width);
        v=0; for (u32 b=0;b<r->width;b++) v|=(u32)((const u8 *)state)[r->source+b]<<(8*b);
    } else if (r->expr==GF_C_LIVE) v=(live & r->keep)|r->bits;
    else if (r->expr==GF_C_PLL) {
        CHECK(r->source < state->sequence.count);
        const struct gfinit_pll_write *w=&state->sequence.writes[r->source];
        CHECK(r->reg==w->reg); keep=w->keep; bits=w->value; v=(live & keep)|bits;
    } else CHECK(r->expr==GF_C_LITERAL || r->expr==GF_C_PALETTE);
    print("row=");
    value(r->kind); value(r->reg);
    value(r->index); value(r->width); value(r->expr); value(r->source);
    value(keep); value(bits); value(r->verify); value(r->count); value(r->flags); value(v); print("\n");
    return 0;
}
static const struct gf_load_io load_io={load_section,load_row,load_wait};
static void dump(u32 variant)
{
    struct gfinit_pll_limits limits = {
        {100000,400000,1000,27000,1,255,1,27},
        {400000,1000000,25000,200000,4,40,1,4}, 6, 0, 27000
    };
    struct gfinit_mode_saved saved = {0};
    if (variant) saved = (struct gfinit_mode_saved){0xff,0xff,0x5a,0x12,0xfe,
        0x12345678,0x87654321,0xabcdef01};
    struct gfinit_pll_limits before = limits;
    CHECK(gfinit_mode_calc(&limits, &saved, &mode) == 0);
    CHECK(limits.vco2.max_freq == before.vco2.max_freq);
    CHECK(mode.single && mode.limits.vco2.max_freq == 0);
    print("variant="); number(variant); print("\n");
    const u32 *t = (const u32 *)&mode.timing;
    print("timing="); for (u32 i=0;i<sizeof(mode.timing)/sizeof(u32);i++) value(t[i]); print("\n");
#define F(f) field(#f, mode.f)
    F(pllsel); F(sel_clk); F(cursync); F(general); F(ramdac630); F(ramdac634);
    F(tv_setup); F(fp_control); F(ramdac8c0); F(ramdaca20); F(ramdaca24); F(ramdaca34);
    F(engine_ctrl); F(cursor_cfg); F(crtc830); F(crtc834); F(gpio_ext); F(crtc850);
    F(config); F(start); F(dacclk); F(test_control); F(misc);
#undef F
    print("cr="); for (u32 i=0;i<GF_MODE_CRTC_COUNT;i++) value(mode.cr[i]); print("\n");
    print("sr="); for (u32 i=0;i<GF_MODE_SR_COUNT;i++) value(mode.sr[i]); print("\n");
    print("gr="); for (u32 i=0;i<GF_MODE_GR_COUNT;i++) value(mode.gr[i]); print("\n");
    print("ar="); for (u32 i=0;i<GF_MODE_AR_COUNT;i++) value(mode.ar[i]); print("\n");
    print("pll="); const u32 *p=(const u32 *)&mode.pll;
    for (u32 i=0;i<sizeof(mode.pll)/sizeof(u32);i++) value(p[i]);
    print("\n");
    for (u32 i=0;i<mode.sequence.count;i++) {
        const struct gfinit_pll_write *w=&mode.sequence.writes[i];
        print("pllwrite="); value(w->reg); value(w->source); value(w->keep); value(w->value); print("\n");
    }
    for (u32 i=0;i<GF_MODE_PALETTE_SIZE;i++) {
        u8 rgb[GF_MODE_RGB]; gfinit_mode_palette(i,rgb);
        print("rgb="); value(i); for (u32 c=0;c<GF_MODE_RGB;c++) value(rgb[c]); print("\n");
    }
    load_seen=load_sections=load_waits=0;
    CHECK(!gf_load_state(&mode,0,&load_io));
    CHECK(load_seen==205 && load_sections==7 && load_waits==3);
}
void _start(void)
{
    u8 row[640];
    CHECK(gfinit_mode_row(480, row) < 0);
    CHECK(gfinit_mode_row(0, 0) < 0);
    for (u32 y = 0; y < 480; y++) {
        CHECK(gfinit_mode_row(y, row) == 0);
        for (u32 x = 0; x < 640; x++) {
            u32 expected = x / 80;
            if (x >= 8 && x < 72 && y >= 8 && y < 72)
                expected = (((x - 8) / 8 + (y - 8) / 8) % 2) ? 0 : 4;
            if (x < 8 || x >= 632 || y < 8 || y >= 472) expected = 4;
            CHECK(row[x] == expected);
        }
    }
    gf_fixture_init();
    CHECK(gf_fixture_profile.rule_count==578 && gf_fixture_profile.target_count==154);
    CHECK(GF_MODE_DCB_ENTRY==0 && GF_MODE_DCB_ANALOG==0 && GF_MODE_DCB_HEADS==3 &&
          GF_MODE_DCB_OR==0 && GF_MODE_DCB_CONNECTOR==0 && GF_MODE_CONNECTOR_VGA==0 &&
          (GF_MODE_DCB_HEADS & (1U<<GF_MODE_HEAD)));
    CHECK(REC_GF_C_CHECK_508_MASK == 0x0007ffffU);
    CHECK(REC_GF_C_CHECK_578_MASK == 0x8000ffffU);
    dump(0); dump(1);
    /* A consumer failure at any row, section or wait stops the shared walk. */
    load_silent=true;
    for (load_stop=0;load_stop<205;load_stop++) {
        load_seen=load_sections=load_waits=0;
        CHECK(gf_load_state(&mode,0,&load_io)<0 && load_seen==load_stop+1);
    }
    load_fault=1;
    for (load_stop=0;load_stop<7;load_stop++) {
        load_seen=load_sections=load_waits=0;
        CHECK(gf_load_state(&mode,0,&load_io)<0 && load_sections==load_stop+1);
    }
    load_fault=2;
    for (load_stop=1;load_stop<=3;load_stop++) {
        load_seen=load_sections=load_waits=0;
        CHECK(gf_load_state(&mode,0,&load_io)<0 && load_waits==load_stop);
    }
    /* Single-stage threshold equality and a forced two-stage test. */
    struct gfinit_pll_limits l={{10000,50350,1000,27000,1,255,1,27},
        {200000,400000,1000,200000,4,255,1,4},6,0,27000};
    struct gfinit_mode_saved s={0};
    CHECK(gfinit_mode_calc(&l,&s,&mode)==0 && mode.single);
    l.vco1.max_freq--;
    CHECK(gfinit_mode_calc(&l,&s,&mode)==0 && !mode.single && mode.pll.n2!=mode.pll.m2);
    l.ref=0;
    CHECK(gfinit_mode_calc(&l,&s,&mode)<0);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failures ? 1 : 0));
    __builtin_unreachable();
}
