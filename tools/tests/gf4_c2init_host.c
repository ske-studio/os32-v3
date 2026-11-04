/* C2-service init, A1 UNIT14: real service/V0 and real links/exec tables.
 * Reuse C1's libc-free real F1/F2/F3 integration, not its test entry point.
 * Link like gf4_c2svc_host.c, additionally D/links.c and C4's
 * gf_display_data.c; do not link another copy of the TUs included by C1.
 * Use kernel include flags, -m32 -nostdlib -static -ffunction-sections
 * -fdata-sections -Wl,--gc-sections. A2/PM owns registration.
 * Oracle: GF_FEAT4_DESIGN final C-group / links-exec binding order.
 * nouveau c03c1a1 dispnv04/disp.c:247-294 creates/connects display objects;
 * module loans and initial publication are OS32 contracts, not HW values.
 * No registers, numeric HW expectations, lifecycle stubs or refs/ reads.
 */
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"

#ifndef GF4_SERVICE_SOURCE
#define GF4_SERVICE_SOURCE "../../hwdev/gf/display/service.c"
#endif
#include GF4_SERVICE_SOURCE

const GeforceModuleContext *geforce_context;
static GeforceModuleContext init_module;
static struct gf_production init_other;
static struct gf4_ctx init_before;
static struct gf4_state init_state_before;
static const struct gf4_test_ops *init_slot;
static const struct gf4_restore_ops *init_restore_slot;

static void init_unchanged(const struct gf4_ctx *c)
{
    assert(!gf2_host_memcmp(c,&init_before,sizeof(init_before)));
    assert(!gf2_host_memcmp(init_module.display_state,&init_state_before,
                           sizeof(init_state_before)));
    assert(!cint_host.nevents && !cint_slot && !hook_active);
    assert(!init_slot && !init_restore_slot);
}

static void test_initial_binding(void)
{
    cint_reset();
    cint_prod.display.active=cint_prod.display.engaged=false;
    /* C4 alone owns schema validation and the kernel-owned initial image. */
    assert(!gf4_data_bind(&init_module.display_data,&init_module.display_state));
    init_module.display_out=&init_slot;
    init_module.display_restore_out=&init_restore_slot;
    init_state_before=*init_module.display_state;
    const struct gf3_service_owner *owner=gf4_service_owner_table();
    assert(owner && owner->owner && owner->start && owner->stop && owner->recover);
    struct gf4_ctx *c=owner->owner;
    init_before=*c;
    /* Required C3 order: F3 bind (cint_reset), owner_bind, then F4 init.
     * This must work before init gives the ctx its production loans. */
    assert(!gf3_service_owner_bind(cint_service,owner));
    assert(cint_service->owner==owner && !c->legacy);
    assert(gf4_service_init(&cint_prod,&cint_prod.display)==GF4_EINVAL);
    init_unchanged(c); /* absent module Context */
    geforce_context=&init_module;
    assert(gf4_service_init(NULL,&cint_prod.display)==GF4_EINVAL);
    assert(gf4_service_init(&cint_prod,NULL)==GF4_EINVAL);
    assert(gf4_service_init(&cint_prod,&init_other.display)==GF4_EINVAL);
    init_unchanged(c);
    const struct gf4_data_ops *data=init_module.display_data;
    struct gf4_state *state=init_module.display_state;
    init_module.display_data=NULL;
    assert(gf4_service_init(&cint_prod,&cint_prod.display)==GF4_EINVAL);
    init_module.display_data=data;
    init_module.display_state=NULL;
    assert(gf4_service_init(&cint_prod,&cint_prod.display)==GF4_EINVAL);
    init_module.display_state=state;
    init_unchanged(c);

    struct gf4_links *links=gf4_links_table();
    assert(links && gf4_exec_table());
    /* Missing mandatory callback is rejected before the table owner moves.
     * Real table storage is temporarily edited; no dependency is stubbed. */
    i32 (*admission)(void *,bool)=links->admission;
    void *old_owner=links->owner;
    links->admission=NULL;
    assert(gf4_service_init(&cint_prod,&cint_prod.display)==GF4_EINVAL);
    assert(links->owner==old_owner && !cint_prod.display.display4);
    links->admission=admission;
    init_unchanged(c);

    assert(!gf4_service_init(&cint_prod,&cint_prod.display));
    assert(owner==gf4_service_owner_table() && owner->owner==c);
    assert(c->ch==&cint_prod.chan && c->accel==&cint_prod.accel);
    assert(c->data==data && c->state==state);
    assert(c->links==links && c->exec==gf4_exec_table() && links->owner==c);
    assert(c->legacy==&cint_prod.display && cint_prod.display.display4==c);
    assert(!c->ready && !c->accepting && c->reusable && c->next_mode_id==1U);
    assert(!c->epoch && !c->routes && !c->nroutes && !c->diagnostic_saved);
    init_before=*c;
    init_unchanged(c); /* init neither starts hardware nor publishes slots */
}

static void test_rebinding_preserves_loans(void)
{
    struct gf4_ctx *c=gf4_service_owner_table()->owner;
    /* Mark resident state with legal contract values, not GPU expectations.
     * Same binding must preserve *all* bytes, including node addresses and
     * held snapshot. No registry entry is fabricated or released here. */
    c->ready=c->accepting=true;
    c->saved_closed=true;
    c->diagnostic_saved=&c->hold_saved;
    c->hold_saved.version=GF4_SAVED_VERSION;
    c->hold_saved.size=sizeof(c->hold_saved);
    c->hold_saved.head[GF4_HEADS-1U].blank=true;
    c->scan[0][0].value.generation=U32_MAX;
    c->next_mode_id=GF4_MODES;
    c->state->nmodes=GF4_MODES;
    init_before=*c; init_state_before=*c->state;
    assert(!gf4_service_init(&cint_prod,&cint_prod.display));
    init_unchanged(c);
    assert(gf4_service_init(&init_other,&init_other.display)==GF4_ESTATE);
    assert(!init_other.display.display4);
    init_unchanged(c);
    const struct gf4_data_ops *data=init_module.display_data;
    init_module.display_data=&gf4_host_data_ops;
    assert(gf4_service_init(&cint_prod,&cint_prod.display)==GF4_ESTATE);
    init_module.display_data=data;
    init_unchanged(c);
    init_module.display_state=&init_state_before;
    assert(gf4_service_init(&cint_prod,&cint_prod.display)==GF4_ESTATE);
    init_module.display_state=c->state;
    init_unchanged(c);
}

void _start(void)
{
    test_initial_binding();
    test_rebinding_preserves_loans();
    gf2_host_exit(0);
}
