/* Diagnostic adapter for the production runner. Recover owns approvals,
 * recording, cancellation and S0; the runner owns script control and waits. */
#include "gfinit_exec.h"
static int runner_point(void *opaque)
{
    struct gfinit_exec *e = opaque;
    return recover_safe_point(e->recover);
}
static int runner_clock(void *opaque, u32 *now)
{
    struct gfinit_exec *e = opaque;
    int rc = e->ops->clock_us(e->opaque,now);
    if (!rc && *now - e->init.started_us >= GF_INIT_EXEC_US)
        e->recover->reason = REC_TIMEOUT;
    return rc;
}
static int runner_wait(void *opaque, u32 us)
{
    struct gfinit_exec *e = opaque;
    return e->ops->wait_us(e->opaque,us);
}
static int runner_read(void *opaque, u32 bar, u32 off, u32 width, u32 *value)
{
    struct gfinit_exec *e = opaque;
    return recover_gf_runner_access(e->recover,e->init.fail_pc,bar,off,width,false,value);
}
static int runner_write(void *opaque, u32 bar, u32 off, u32 width, u32 value)
{
    struct gfinit_exec *e = opaque;
    int rc = recover_gf_runner_access(e->recover,e->init.fail_pc,bar,off,width,true,&value);
#if GF_VPLL_PROBE
    if (!rc && e->init.main_id==RECOVER_VPLL_MAIN && e->init.fail_pc==RECOVER_VPLL_PC) {
        u32 actual;
        rc=runner_read(e,bar,off,width,&actual);
        if (!rc && actual!=value) {
            (void)trial_end(e->recover,&e->recover->token,REC_VERIFY);
            return -1;
        }
    }
#endif
    return rc;
}
static int runner_trace(void *opaque, struct gf_init_context *ctx, bool begin, int rc)
{
    struct gfinit_exec *e = opaque;
    struct recover *r = e->recover;
    if (r->state != REC_RUNNING) return -1; /* cancellation already emitted S0 */
    struct recover_plan_node n = {.main_id=ctx->main_id, .pc=ctx->fail_pc,
        .opcode=ctx->rom[ctx->fail_pc], .substep=e->substep};
    if (begin) e->executing = !ctx->skip || n.opcode == GF_INIT_RESET ||
        n.opcode == GF_INIT_COMPUTE_MEM || n.opcode == GF_INIT_REPEAT ||
        n.opcode == GF_INIT_END_REPEAT || n.opcode == GF_INIT_NOT ||
        n.opcode == GF_INIT_DONE || n.opcode == GF_INIT_RESUME;
    int log = recover_gf_pre(r,&n,begin ? "BEGIN" : "END",e->executing,rc);
    if (!begin) e->substep++;
    return log;
}
static int runner_decode_safe(void *opaque)
{
    struct gfinit_exec *e=opaque;
    const u32 offsets[]={RECOVER_CFG50,RECOVER_CFG54,RECOVER_PBUS1850,RECOVER_PBUS1854};
    for(u32 i=0;i<4;i++) {
        u32 value;
        if(runner_read(e,i<2?RECOVER_NONE:0,offsets[i],4,&value))return -1;
        if(value) { (void)trial_end(e->recover,&e->recover->token,REC_CONTRACT);return -1; }
    }
    return 0;
}
static int runner_begin(struct recover *r,struct gfinit_exec *e,u32 main)
{
    e->recover=r; e->substep=0;
    e->init=(struct gf_init_context){.rom=e->rom,.rom_size=e->rom_size,
        .main_id=main,.fail_pc=RECOVER_NONE,.io={.opaque=e,.read=runner_read,.write=runner_write,
            .timing={runner_clock,runner_wait},.point=runner_point,.trace=runner_trace}};
    return e->ops->clock_us(e->opaque,&e->init.started_us) ||
        recover_plan_enter(r,r->expected_node) ? -1 : 0;
}
int gfinit_exec_prepare(struct recover *r,void *arg)
{
    struct gfinit_exec *e=arg;
    if(!e || !e->ops || !e->ops->clock_us || !e->ops->wait_us || !r->plan ||
        r->state!=REC_RUNNING || r->expected_node>=r->plan->node_count ||
        r->plan->nodes[r->expected_node].opcode!=GF_INIT_A2 || runner_begin(r,e,RECOVER_NONE))return -1;
    if(gf_init_a2(&e->init,runner_decode_safe) || r->state!=REC_RUNNING)return -1;
    return recover_plan_leave(r,0);
}
int gfinit_exec_restore(struct gfinit_exec *e)
{
    return e && e->recover ? gf_init_restore(&e->init) : 0;
}
int gfinit_exec_run(struct recover *r, void *arg)
{
    struct gfinit_exec *e = arg;
    if (!e || !e->ops || !e->ops->clock_us || !e->ops->wait_us || !e->program ||
        !e->rom || !r->enabled || !r->plan || r->plan->unresolved_count ||
        e->rom_size != r->plan->rom_size || e->main_id >= GF_INIT_MAINS ||
        r->state != REC_RUNNING || r->entered || r->expected_node >= r->plan->node_count) return -1;
    const struct recover_plan_node *n = &r->plan->nodes[r->expected_node];
    if (n->phase != REC_RUN || n->opcode != GF_INIT_RUNNER || n->main_id != e->main_id) return -1;
    if (runner_begin(r,e,e->main_id)) return -1;
    int rc = gf_init_run_one(&e->init,e->program);
    e->steps = e->init.steps; e->fail_pc = e->init.fail_pc;
    if (rc || r->state != REC_RUNNING) return -1;
    return recover_plan_leave(r,0);
}
