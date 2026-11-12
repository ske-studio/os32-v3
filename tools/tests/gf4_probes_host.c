/* Exercise the real start composition, with existing PCI/MMIO host seams. */
#define GF4_SCANLOG_ENTRY probes_unused_start
#include "gf4_scanlog_host.c"
#undef GF4_SCANLOG_ENTRY
#include "concrete_probe_broker.h"

static int probe_same(const char *a,const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static u32 records[2], order;
static void record_probe(u32 id)
{
    assert(id < 2);
    assert(order++ == id);
    if (!id) {
        /* The existing diagnostic has not run at detect.pre. */
        assert(!scanlog_contains("[gf4] detect state"));
    } else {
        assert(scanlog_contains("[gf4] detect state"));
        assert(svc_ctx.state->head[0].enabled);
        assert(svc_ctx.state->head[0].mode.t.hd == GF_MODE_WIDTH);
    }
    ++records[id];
}
void _start(void)
{
    const struct probe_decl *decls;
    u32 count;
    assert(!gf4_get_probe_decls(&decls, &count) && count == 2);
    assert(probe_same(decls[0].name, "gf4.detect.pre"));
    assert(probe_same(decls[1].name, "gf4.mode.post"));
    assert(decls[0].slot != decls[1].slot);
    for (u32 i=0;i<count;++i) assert(!decls[i].slot->hit && !decls[i].slot->id);
    assert(!concrete_register(decls,count));
    scanlog_map(CINT_FB_PHYS,GF_MODE_VRAM_BYTES);
    scanlog_map(SCANLOG_BAR0,0x101000U);
    scanlog_init(); /* The existing display/start oracle also runs unarmed. */
    assert(!records[0] && !records[1]);
    assert(!gf4_service_stop(&svc_ctx,cint_deadline()));
    assert(!concrete_arm(record_probe,count));
    startlog_clear();
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(records[0] == 1 && records[1] == 1 && order == 2);
    /* After STOP, a restart through the service records no further hits. */
    assert(!gf4_service_stop(&svc_ctx,cint_deadline()));
    assert(!concrete_stop());
    /* S0 detect diagnostics belong to each start; the preceding phase
     * has already checked its output and probe records. */
    startlog_clear();
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(records[0] == 1 && records[1] == 1);
    scanlog_finish();
    gf2_host_exit(0);
}
