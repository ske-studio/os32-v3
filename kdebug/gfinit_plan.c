/* S6 PREPARE/RUN/VRAM composition. Addresses, values and comparisons are fixed
 * before arm; READ groups use only profile-registered pure expressions. */
#include "gfinit_plan.h"
#define GF_PLAN_ARRAY(a) (sizeof(a) / sizeof((a)[0]))
#define GF_PLAN_OPCODE 0x200U
#define GF_PLAN_EXPR_SAVE 0x200U
#define GF_PLAN_EXPR_CHECK 0x201U
#define GF_PLAN_EXPR_BRANCH 0x202U
#define GF_PLAN_EXPR_EQUAL 0x203U
#define GF_PLAN_ALIAS_MAX 61U
#define GF_PLAN_PFB_AMOUNT 0x10020cU
#define GF_PLAN_PFB_MASK 0xff000000U
#define GF_PLAN_PFB_MIN 0x01000000U
static const u32 branch_kinds[] = {REC_ARG_READ, REC_IMM, REC_IMM};
static const u32 equal_kinds[] = {REC_ARG_READ, REC_IMM};
/* MIT nouveau ramnv44.c:30 and envytools nv10_pfb.xml MEM_AMOUNT.
 * Non-power-of-two encodings remain unknown, not a claim of absent memory. */
static u32 capacity(u32 raw)
{
    u32 bytes=raw & GF_PLAN_PFB_MASK;
    return bytes>=GF_PLAN_PFB_MIN && !(bytes & (bytes-1)) ? bytes : 0;
}
static int alias_branch(const u32 *args,u32 argc,u32 *value)
{
    if(argc!=3)return -1;
    u32 bytes=capacity(args[0]);
    if(bytes>args[1])bytes=args[1];
    *value=bytes>=4 && args[2]<=bytes-4;
    return 0;
}
static int alias_equal(const u32 *args,u32 argc,u32 *value)
{
    if(argc!=2)return -1;
    *value=args[0]==args[1];
    return 0;
}
static u32 alias_signature(u32 offset) { return offset ^ 0x6d32a591U; }
static u32 alias_offsets(u32 length,u32 *offsets)
{
    u32 count=1;offsets[0]=0;
    for(u32 bit=2;bit<32;bit++) {
        u32 boundary=1U<<bit;
        if(boundary>length)break;
        if(boundary>4 && offsets[count-1]!=boundary-4)offsets[count++]=boundary-4;
        if(boundary<length)offsets[count++]=boundary;
    }
    return count;
}
static const u32 save_kinds[] = {REC_ARG_READ, REC_IMM};
static const u32 save_layout[] = {REC_IMM, REC_ARG_READ, REC_IMM};
static const u32 literal_layout[] = {REC_IMM, REC_IMM};
static const u32 check_kinds[] = {
    REC_ARG_READ, REC_IMM, REC_ARG_READ, REC_IMM,
    REC_ARG_READ, REC_IMM, REC_ARG_READ, REC_IMM,
    REC_ARG_READ, REC_IMM, REC_ARG_READ, REC_IMM,
    REC_ARG_READ, REC_IMM, REC_ARG_READ, REC_IMM
};
/* Both pattern passes write and check every word. Alias points each add
 * two branches, one signature write and one equality comparison. */
#define GF_PLAN_ARGS_PER_WORD (GF_PLAN_ARRAY(save_layout) + GF_PLAN_ARRAY(literal_layout) + \
    2U * GF_PLAN_ARRAY(check_kinds) / GF_PLAN_CHECK_WORDS)
#define GF_PLAN_ARGS_PER_ALIAS (2U * GF_PLAN_ARRAY(branch_kinds) + \
    GF_PLAN_ARRAY(literal_layout) + GF_PLAN_ARRAY(equal_kinds))
#define GF_PLAN_RULES_PER_WORD 2U
#define GF_PLAN_ADDED_EXPRS (GF_PLAN_EXPR_EQUAL - GF_PLAN_EXPR_SAVE + 1U)
static int save_pattern(const u32 *args, u32 argc, u32 *value)
{
    if (argc != 2) return -1;
    *value = args[1];
    return 0;
}
static int check_words(const u32 *args, u32 argc, u32 *value)
{
    if (argc != 2 * GF_PLAN_CHECK_WORDS) return -1;
    *value = 1;
    for (u32 i = 0; i < argc; i += 2) if (args[i] != args[i+1]) *value = 0;
    return 0;
}
static u32 pattern(u32 pass, u32 word)
{
    return ((pass ^ word) & 1U) ? 0xaa55aa55U : 0x55aa55aaU;
}
static const struct recover_rule *plan_rule(const struct recover_profile *p, u32 id)
{
    for (u32 i = 0; i < p->rule_count; i++) if (p->rules[i].id == id) return &p->rules[i];
    return 0;
}
int gfinit_plan_build(struct gfinit_plan *out, struct gfinit_decode *d,
                      struct recover_profile *p, u32 bar1_length)
{
    const struct recover_plan *prep = &recover_gf_prepare_plan;
    u32 offsets[GF_PLAN_ALIAS_MAX], keys[GF_PLAN_ALIAS_MAX];
    u32 points=alias_offsets(bar1_length,offsets);
    const u32 extra_nodes = 2 * GF_PLAN_VRAM_WORDS + 2 * GF_PLAN_VRAM_WORDS / GF_PLAN_CHECK_WORDS + 4*points;
    if (!out || !d || !p || (bar1_length & (bar1_length-1)) || bar1_length < GF_PLAN_VRAM_WORDS * 4 ||
        d->plan.unresolved_count || !d->plan.node_count ||
        d->plan.node_count + prep->node_count + extra_nodes > RECOVER_NODE_MAX ||
        d->plan.arg_count + prep->arg_count + GF_PLAN_ARGS_PER_WORD * GF_PLAN_VRAM_WORDS + GF_PLAN_ARGS_PER_ALIAS*points > GF_INIT_ARGS ||
        d->plan.entry_count + 2 > RECOVER_ENTRY_MAX ||
        p->target_count + GF_PLAN_VRAM_WORDS + points > GF_PLAN_TARGETS ||
        p->rule_count + GF_PLAN_RULES_PER_WORD * GF_PLAN_VRAM_WORDS + points > GF_PLAN_RULES ||
        p->layout_count + GF_PLAN_SUB_COUNT > GF_PLAN_LAYOUTS || p->expr_count + GF_PLAN_ADDED_EXPRS > GF_PLAN_EXPRS) return -1;
    out->pfb_key=RECOVER_NONE;
    for(u32 i=0;i<p->target_count;i++)if(p->targets[i].space==REC_BAR_MMIO &&
        p->targets[i].bar==0 && p->targets[i].offset==GF_PLAN_PFB_AMOUNT && p->targets[i].width==4)
        out->pfb_key=i;
    if(out->pfb_key==RECOVER_NONE)return -1;
    out->bar1_length=bar1_length;out->capacity_bytes=out->tested_bytes=out->capacity_known=out->full_capacity=0;
    out->plan = d->plan; out->plan.entries = out->entries; out->plan.deps = out->deps;
    out->plan.dep_count = 0; out->saved_count = 0; out->vram_first = p->target_count;
    for (u32 i = 0; i < p->target_count; i++) out->targets[i] = p->targets[i];
    for (u32 i = 0; i < p->rule_count; i++) out->rules[i] = p->rules[i];
    for (u32 i = 0; i < p->layout_count; i++) out->layouts[i] = p->layouts[i];
    for (u32 i = 0; i < p->expr_count; i++) out->exprs[i] = p->exprs[i];
    p->targets = out->targets; p->rules = out->rules; p->layouts = out->layouts; p->exprs = out->exprs;
    for (u32 i = 0; i < GF_PLAN_VRAM_WORDS; i++)
        out->targets[p->target_count++] = (struct recover_target){.space=REC_BAR_VRAM,
            .bar=1,.offset=i*4,.width=4,.index=RECOVER_NONE,.access=REC_COLD,.read_safe=1};
    out->exprs[p->expr_count++] = (struct recover_expr){GF_PLAN_EXPR_SAVE,2,save_kinds,save_pattern};
    out->exprs[p->expr_count++] = (struct recover_expr){GF_PLAN_EXPR_CHECK,2*GF_PLAN_CHECK_WORDS,check_kinds,check_words};
    out->layouts[p->layout_count++] = (struct recover_opcode_layout){GF_PLAN_OPCODE,GF_PLAN_SUB_SAVE,3,save_layout,REC_WRITE,0};
    out->layouts[p->layout_count++] = (struct recover_opcode_layout){GF_PLAN_OPCODE,GF_PLAN_SUB_LITERAL,2,literal_layout,REC_WRITE,0};
    out->layouts[p->layout_count++] = (struct recover_opcode_layout){GF_PLAN_OPCODE,GF_PLAN_SUB_CHECK,2*GF_PLAN_CHECK_WORDS,check_kinds,RECOVER_NONE,REC_LAYOUT_CHECK_EXPR};
    out->exprs[p->expr_count++] = (struct recover_expr){GF_PLAN_EXPR_BRANCH,3,branch_kinds,alias_branch};
    out->exprs[p->expr_count++] = (struct recover_expr){GF_PLAN_EXPR_EQUAL,2,equal_kinds,alias_equal};
    out->layouts[p->layout_count++] = (struct recover_opcode_layout){GF_PLAN_OPCODE,GF_PLAN_SUB_ALIAS_WRITE,2,literal_layout,REC_WRITE,0};
    out->layouts[p->layout_count++] = (struct recover_opcode_layout){GF_PLAN_OPCODE,GF_PLAN_SUB_BRANCH,3,branch_kinds,RECOVER_NONE,REC_LAYOUT_CHECK_EXPR};
    out->layouts[p->layout_count++] = (struct recover_opcode_layout){GF_PLAN_OPCODE,GF_PLAN_SUB_EQUAL,2,equal_kinds,RECOVER_NONE,REC_LAYOUT_CHECK_EXPR};
    for(u32 i=0;i<points;i++) {
        if(offsets[i]<GF_PLAN_VRAM_WORDS*4)keys[i]=out->vram_first+offsets[i]/4;
        else {
            keys[i]=p->target_count;
            out->targets[p->target_count++]=(struct recover_target){.space=REC_BAR_VRAM,
                .bar=1,.offset=offsets[i],.width=4,.index=RECOVER_NONE,.access=REC_COLD,.read_safe=1};
        }
    }
    u32 nn=prep->node_count, na=prep->arg_count;
    for (u32 i=d->plan.node_count; i>0; i--) {
        struct recover_plan_node n=d->nodes[i-1]; n.arg_first+=na;
        for (u32 j=0;j<2;j++) if(n.next[j]!=RECOVER_NONE)n.next[j]+=nn;
        d->nodes[i-1+nn]=n;
    }
    for(u32 i=d->plan.arg_count;i>0;i--)d->args[i-1+na]=d->args[i-1];
    for(u32 i=0;i<nn;i++)d->nodes[i]=prep->nodes[i];
    for(u32 i=0;i<na;i++)d->args[i]=prep->args[i];
    d->nodes[nn-1].next[0]=d->nodes[nn-1].next[1]=nn;
    for(u32 i=0;i<d->plan.capture_count;i++) {
        d->captures[i].producer_node+=nn; d->captures[i].consumer_node+=nn;
    }
    out->entries[0]=prep->entries[0];
    for(u32 i=0;i<d->plan.entry_count;i++) {
        out->entries[i+1]=d->entries[i]; out->entries[i+1].first+=nn;
    }
    out->plan.node_count+=nn; out->plan.arg_count+=na; out->plan.entry_count++;
    for(u32 i=nn;i<out->plan.node_count;i++) {
        const struct recover_rule *w=plan_rule(p,d->nodes[i].rule_id);
        if(!w)continue;
        for(u32 j=0;j<w->requires_count;j++) for(u32 k=0;k<nn;k++)
            if(prep->nodes[k].rule_id==w->requires[j]) {
                if(out->plan.dep_count==RECOVER_DEP_MAX)return -1;
                out->deps[out->plan.dep_count++]=(struct recover_plan_dep){i,w->requires[j],k};
            }
    }
    out->vram_node=out->plan.node_count;
    out->entries[out->plan.entry_count++]=(struct recover_plan_entry){REC_VRAM_TEST,RECOVER_NONE,out->vram_node};
    for(u32 i=nn;i<out->vram_node;i++)for(u32 j=0;j<2;j++)
        if(d->nodes[i].next[j]==RECOVER_NONE)d->nodes[i].next[j]=out->vram_node;
    u32 rule_id=0;
    for(u32 i=0;i<p->rule_count;i++)if(p->rules[i].id>=rule_id)rule_id=p->rules[i].id+1;
    for(u32 pass=0;pass<2;pass++) {
        for(u32 i=0;i<GF_PLAN_VRAM_WORDS;i++) {
            u32 key=out->vram_first+i, id=out->plan.node_count++, a=out->plan.arg_count;
            u32 expr=pass?RECOVER_NONE:GF_PLAN_EXPR_SAVE;
            out->rules[p->rule_count++]=(struct recover_rule){.id=rule_id,.key=key,.phase=REC_VRAM_TEST,
                .main_id=RECOVER_NONE,.pc=RECOVER_NONE,.substep=GF_PLAN_SUB_SAVE+pass,.write_mask=~0U,
                .value={.mode=pass?REC_LITERAL:REC_EXPR,.mask=~0U,.bits=pattern(pass,i),.max=~0U,.expr_id=expr}};
            d->nodes[id]=(struct recover_plan_node){.phase=REC_VRAM_TEST,.main_id=RECOVER_NONE,.pc=RECOVER_NONE,
                .opcode=GF_PLAN_OPCODE,.substep=GF_PLAN_SUB_SAVE+pass,.rule_id=rule_id++,.expr_id=expr,.arg_first=a,
                .arg_count=pass?2:3,.next={id+1,id+1}};
            d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_IMM,key};
            if(!pass)d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_ARG_READ,key};
            d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_IMM,pattern(pass,i)};
        }
        for(u32 i=0;i<GF_PLAN_VRAM_WORDS;i+=GF_PLAN_CHECK_WORDS) {
            u32 id=out->plan.node_count++, a=out->plan.arg_count;
            d->nodes[id]=(struct recover_plan_node){.phase=REC_VRAM_TEST,.main_id=RECOVER_NONE,.pc=RECOVER_NONE,
                .opcode=GF_PLAN_OPCODE,.substep=GF_PLAN_SUB_CHECK,.rule_id=RECOVER_NONE,.expr_id=GF_PLAN_EXPR_CHECK,
                .arg_first=a,.arg_count=2*GF_PLAN_CHECK_WORDS,.next={id+1,id+1}};
            for(u32 j=0;j<GF_PLAN_CHECK_WORDS;j++) {
                d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_ARG_READ,out->vram_first+i+j};
                d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_IMM,pattern(pass,i+j)};
            }
        }
    }
    /* All selected signatures are written before any signature comparison. */
    for(u32 pass=0;pass<2;pass++)for(u32 i=0;i<points;i++) {
        u32 id=out->plan.node_count++, a=out->plan.arg_count;
        d->nodes[id]=(struct recover_plan_node){.phase=REC_VRAM_TEST,.main_id=RECOVER_NONE,.pc=RECOVER_NONE,
            .opcode=GF_PLAN_OPCODE,.substep=GF_PLAN_SUB_BRANCH,.rule_id=RECOVER_NONE,.expr_id=GF_PLAN_EXPR_BRANCH,
            .arg_first=a,.arg_count=3,.next={id+2,id+1}};
        d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_ARG_READ,out->pfb_key};
        d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_IMM,bar1_length};
        d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_IMM,offsets[i]};
        id=out->plan.node_count++;a=out->plan.arg_count;
        if(!pass)out->rules[p->rule_count++]=(struct recover_rule){.id=rule_id,.key=keys[i],.phase=REC_VRAM_TEST,
            .main_id=RECOVER_NONE,.pc=RECOVER_NONE,.substep=GF_PLAN_SUB_ALIAS_WRITE,.write_mask=~0U,
            .value={.mode=REC_LITERAL,.mask=~0U,.bits=alias_signature(offsets[i]),.max=~0U,.expr_id=RECOVER_NONE}};
        d->nodes[id]=(struct recover_plan_node){.phase=REC_VRAM_TEST,.main_id=RECOVER_NONE,.pc=RECOVER_NONE,
            .opcode=GF_PLAN_OPCODE,.substep=pass?GF_PLAN_SUB_EQUAL:GF_PLAN_SUB_ALIAS_WRITE,.rule_id=pass?RECOVER_NONE:rule_id++,
            .expr_id=pass?GF_PLAN_EXPR_EQUAL:RECOVER_NONE,.arg_first=a,.arg_count=2,.next={id+1,id+1}};
        d->args[out->plan.arg_count++]=(struct recover_plan_arg){pass?REC_ARG_READ:REC_IMM,keys[i]};
        d->args[out->plan.arg_count++]=(struct recover_plan_arg){REC_IMM,alias_signature(offsets[i])};
    }
    u32 last=out->plan.node_count-1;
    d->nodes[last-1].next[0]=RECOVER_NONE;
    d->nodes[last].next[0]=d->nodes[last].next[1]=RECOVER_NONE;
    p->vram_reserved=(struct recover_range){0,bar1_length};
    p->crc=recover_profile_crc(p); out->plan.profile_crc=p->crc;
    return 0;
}
void gfinit_plan_read(struct gfinit_plan *out, const struct recover *r,
                      const struct recover_bus_access *a, u32 value)
{
    if(a->key==out->pfb_key && r->state==REC_RUNNING) {
        out->capacity_bytes=capacity(value);out->capacity_known=out->capacity_bytes!=0;
        out->tested_bytes=out->capacity_bytes<out->bar1_length?out->capacity_bytes:out->bar1_length;
        out->full_capacity=out->capacity_known && out->tested_bytes==out->capacity_bytes;
    }
    if(r->state!=REC_RUNNING || !r->entered || r->cursor<out->vram_node ||
        r->cursor>=out->vram_node+GF_PLAN_VRAM_WORDS || a->key<out->vram_first ||
        a->key>=out->vram_first+GF_PLAN_VRAM_WORDS)return;
    u32 word=a->key-out->vram_first;
    if(word==out->saved_count) { out->saved_vram[word]=value;out->saved_count++; }
}
int gfinit_plan_verify(struct recover *r, void *arg)
{
    struct gfinit_plan *out=arg;
    if(r->plan!=&out->plan || r->expected_node!=out->vram_node)return -1;
    while(r->expected_node!=RECOVER_NONE) {
        u32 id=r->expected_node, value=0, edge=0;
        if(recover_safe_point(r) || recover_plan_enter(r,id))return -1;
        const struct recover_plan_node *n=&out->plan.nodes[id];
        if(n->substep==GF_PLAN_SUB_CHECK || n->substep==GF_PLAN_SUB_BRANCH || n->substep==GF_PLAN_SUB_EQUAL) {
            if(recover_plan_check(r,&value))return -1;
            if(n->substep==GF_PLAN_SUB_BRANCH)edge=value;
            else if(!value) { (void)trial_end(r,&r->token,REC_VERIFY); return -1; }
        } else if(recover_plan_access(r,&value))return -1;
        if(recover_plan_leave(r,edge))return -1;
    }
    if(out->saved_count!=GF_PLAN_VRAM_WORDS)return -1;
    return recover_gf_result(r,out->saved_count,true,out->capacity_known,out->full_capacity,
                             out->capacity_bytes,out->tested_bytes);
}
