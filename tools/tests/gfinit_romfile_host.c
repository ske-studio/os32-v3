/* Private-ROM diagnostic: report decoded structure and derived numbers only. */
#define _start gf_profile_entry
#include "recover_gf_host.c"
#undef _start
#include "../../kdebug/gfinit_pll.c"
#include "../../kdebug/gfinit_decode.c"
#include GF_ROM_HEADER
static struct gfinit_decode decoded;
static void number(u32 x)
{
    char b[16]; u32 n = 0;
    do { b[n++] = (char)('0' + x % 10); x /= 10; } while (x);
    while (n) { char a[2] = {b[--n], 0}; print(a); }
}
static void field(const char *name, u32 value) { print(name); print("="); number(value); print("\n"); }
static void indexed(const char *prefix, u32 i, const char *name, u32 value)
{ print(prefix); number(i); print("."); field(name, value); }
static struct gfinit_pll_vco row_vco(const u8 *r, u32 at, u32 second)
{
    u32 f = second ? GF_PLL_ENT_VCO2_MIN : GF_PLL_ENT_VCO1_MIN;
    u32 in = second ? GF_PLL_ENT_VCO2_INPUT_MIN : GF_PLL_ENT_VCO1_INPUT_MIN;
    u32 n = second ? GF_PLL_ENT_VCO2_N_MIN : GF_PLL_ENT_VCO1_N_MIN;
    return (struct gfinit_pll_vco){pll_le(r, at+f, 2)*GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at+f+2, 2)*GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at+in, 2)*GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at+in+4, 2)*GF_PLL_KHZ_PER_MHZ,
        r[at+n], r[at+n+1], r[at+n+2], r[at+n+3]};
}
static int vco_equal(const struct gfinit_pll_vco *a, const struct gfinit_pll_vco *b)
{
    return a->min_freq == b->min_freq && a->max_freq == b->max_freq &&
        a->min_input == b->min_input && a->max_input == b->max_input &&
        a->min_n == b->min_n && a->max_n == b->max_n && a->min_m == b->min_m && a->max_m == b->max_m;
}
static void vco_fields(const char *p, const struct gfinit_pll_vco *v)
{
#define V(f) print(p); field("." #f, v->f)
    V(min_freq); V(max_freq); V(min_input); V(max_input); V(min_n); V(max_n); V(min_m); V(max_m);
#undef V
}
static int solve(const char *p, const struct gfinit_pll_limits *l, u32 khz)
{
    struct gfinit_pll_solution s; struct gfinit_pll_sequence q;
    if (gfinit_pll_solve(l, khz, &s) || gfinit_pll_sequence(GF_PLL_VIDEO, l, &s, &q)) return -1;
#define S(f) print(p); field("." #f, s.f)
    S(n1); S(m1); S(n2); S(m2); S(p); S(actual); S(error);
#undef S
    print(p); field(".vco1_khz", l->ref * s.n1 / s.m1);
    print(p); field(".vco2_khz", (l->ref * s.n1 / s.m1) * s.n2 / s.m2);
    for (u32 i=0; i<q.count; i++) {
        print(p); indexed(".write", i, "reg", q.writes[i].reg);
        print(p); indexed(".write", i, "source", q.writes[i].source);
        print(p); indexed(".write", i, "keep", q.writes[i].keep);
        print(p); indexed(".write", i, "value", q.writes[i].value);
    }
    return q.count == 5 ? 0 : -1;
}
static int diagnostic(void)
{
    const u8 *r = gf_private_rom; u32 size = sizeof(gf_private_rom), table = GF_INIT_PLL_TABLE;
    struct gfinit_pll_limits l, row = {0};
    if (gfinit_pll_parse(r, size, table, GF_PLL_VIDEO, &l)) return -1;
    u32 header=r[table+GF_PLL_HDR_HEADER_LEN], len=r[table+GF_PLL_HDR_ENTRY_LEN], count=r[table+GF_PLL_HDR_COUNT];
    field("pll.table", table); field("pll.version", r[table+GF_PLL_HDR_VERSION]);
    field("pll.header", header); field("pll.entry", len); field("pll.count", count);
    u32 matches=0, selected=0;
    for (u32 i=0; i<count; i++) {
        u32 key=pll_le(r, table+header+i*len+GF_PLL_ENT_REG, 4);
        indexed("pll.row", i, "key", key);
        if (key == GF_PLL_VIDEO) { matches++; selected=i; }
    }
    if (matches > 1) return -1;
    u32 at=table+header+selected*len;
    field("video.matches", matches); field("video.row", selected); field("video.offset", at);
    field("video.key", pll_le(r, at+GF_PLL_ENT_REG, 4)); field("video.fallback", !matches);
    row.vco1=row_vco(r, at, 0); row.vco2=row_vco(r, at, 1);
    row.max_p=r[at+GF_PLL_ENT_MAX_P]; row.bias_p=r[at+GF_PLL_ENT_BIAS_P];
    row.ref=len > GF_PLL_ENT_REF_LAST ? pll_le(r, at+GF_PLL_ENT_REF, 4) : 0;
    if (!row.ref) row.ref=GF_PLL_REF;
    if (!vco_equal(&row.vco1,&l.vco1) || !vco_equal(&row.vco2,&l.vco2) ||
        row.max_p!=l.max_p || row.bias_p!=l.bias_p || row.ref!=l.ref) return -1;
    vco_fields("limits.vco1", &l.vco1); vco_fields("limits.vco2", &l.vco2);
    field("limits.ref", l.ref); field("limits.max_p", l.max_p); field("limits.bias_p", l.bias_p);
    u32 pc=0, khz=0, found=0, boundary=RECOVER_NONE;
    for (u32 i=decoded.entries[2].first; i<decoded.entries[3].first; i++) {
        const struct recover_plan_node *n=&decoded.nodes[i];
        if (n->opcode==GF_INIT_PLL2 && n->substep==GF_PLL_SUBSTEP+2*GF_PLL_SUBSTEP_STRIDE+4) {
            if (boundary!=RECOVER_NONE && n->pc!=pc) return -1;
            pc=n->pc; boundary=i;
        }
    }
    if (boundary==RECOVER_NONE) return -1;
    for (u32 i=0; i<decoded.instruction_count; i++) {
        const struct gfinit_instruction *v=&decoded.insns[i];
        if (v->pc==pc && v->opcode==GF_INIT_PLL2 && v->target==GF_PLL_VIDEO) { khz=v->value; found++; }
    }
    if (found!=1 || !khz) return -1;
    field("video.pc", pc); field("video.request_khz", khz);
    if (solve("two", &l, khz)) return -1;
    l.vco2=(struct gfinit_pll_vco){0};
    if (solve("single", &l, khz)) return -1;
    u32 writes=0;
    for (u32 i=boundary+1; i<decoded.plan.node_count; i++) {
        const struct recover_plan_node *n=&decoded.nodes[i];
        if (n->main_id<2 || n->main_id>4) continue;
        const struct recover_opcode_layout *layout=layout_for(&gf_fixture_profile, n);
        if (!layout) return -1;
        if (layout->dir_substep!=REC_WRITE) continue;
        if (!n->arg_count || decoded.args[n->arg_first].kind!=REC_IMM) return -1;
        u32 key=decoded.args[n->arg_first].value;
        if (key>=gf_fixture_profile.target_count) return -1;
        const struct recover_target *t=&gf_fixture_profile.targets[key];
        if (t->space!=REC_BAR_MMIO || t->bar || (t->offset!=GF_PLL_VIDEO && t->offset!=GF_PLL_VIDEO_COEFF &&
            t->offset!=GF_PLL_VIDEO_STAGE && t->offset!=GF_PLL_GATE && t->offset!=GF_PLL_VIDEO+4)) continue;
        indexed("later",writes,"main",n->main_id); indexed("later",writes,"pc",n->pc);
        indexed("later",writes,"substep",n->substep); indexed("later",writes,"ctx",n->ctx);
        indexed("later",writes,"head",n->head); indexed("later",writes,"skip",n->skip);
        indexed("later",writes,"reg",t->offset); writes++;
    }
    field("later.count", writes); return 0;
}
void _start(void)
{
    gf_fixture_init();
    int rc=gfinit_decode_rom(gf_private_rom, sizeof(gf_private_rom), &gf_fixture_profile, &decoded);
    field("decode.rc", rc ? 1U : 0U); field("decode.size", sizeof(gf_private_rom));
    field("decode.insns", decoded.instruction_count); field("decode.schedule", decoded.schedule_count);
    field("decode.nodes", decoded.plan.node_count); field("decode.error_pc", decoded.error_pc); field("decode.error", decoded.error);
    if (!rc) {
        field("decode.rom_crc", decoded.plan.rom_crc);
#ifdef GF_TEST_LATER
        /* Synthetic plan: retarget a write inside main 3's repeat. */
        for (u32 i=0; i<decoded.plan.node_count; i++) {
            struct recover_plan_node *n=&decoded.nodes[i];
            const struct recover_opcode_layout *l=layout_for(&gf_fixture_profile, n);
            if (n->main_id==3 && n->pc==0xe7e0 && l && l->dir_substep==REC_WRITE)
                decoded.args[n->arg_first].value=key_for(&gf_fixture_profile, GF_PLL_VIDEO, 0, 0, 0);
        }
#endif
        rc=diagnostic();
    }
    field("diagnostic.rc", rc ? 1U : 0U);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? 1 : 0));
    __builtin_unreachable();
}
