/* CW foreground bridge: fake B components, real prepare and dispatcher. */
#define IO_H
#include "types.h"
#include "hwdev/gf/gf1_test.h"
#include "kdebug/gfinit_bus.h"
#include "memmap.h"
static void fail(int line)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 250 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static unsigned host_if=1, saves, restores;
unsigned irq_save(void) { unsigned v=host_if;host_if=0;saves++;return v; }
static void irq_restore(unsigned v) { CHECK(!host_if);host_if=v;restores++; }
#ifndef GF1_DISPATCH_SOURCE
#define GF1_DISPATCH_SOURCE "../../hwdev/gf/foundation_test.c"
#endif
#include GF1_DISPATCH_SOURCE
static struct gf_foundation foundation;
static struct gfinit_bus lease_bus;
static struct recover_bar_extent bars[RECOVER_BAR_COUNT];
static u32 mmio[8], page_data[MEM_PAGE_SIZE/sizeof(u32)];
static unsigned masters, starts, stops, revokes, snapshots, heads_calls, allocs, frees;
static int operation_rc, alloc_rc, write_rc, read_rc;
static u32 fault_off=~0U;
const struct gf_instmem_map_ops gf_production_instmem_map_ops={0};
int gf_master_enable(struct gf_foundation *f)
{ CHECK(f==&foundation);masters++;return operation_rc; }
int gf_foundation_start(struct gf_foundation *f)
{ CHECK(f==&foundation);starts++;return operation_rc; }
int gf_foundation_stop(struct gf_foundation *f)
{ CHECK(f==&foundation);stops++;return operation_rc; }
void gf_foundation_revoke(struct gf_foundation *f)
{ CHECK(f==&foundation);revokes++;f->revoked=true;f->running=false; }
void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *out)
{ CHECK(f==&foundation);snapshots++;*out=f->stats; }
int gf_irq_enable(struct gf_foundation *f, bool fifo, bool graph, unsigned heads)
{
    CHECK(f==&foundation && !host_if && f->running);
    CHECK(fifo==(f->wanted_fifo!=0) && graph==(f->wanted_graph!=0));
    heads_calls++;f->wanted_heads=heads;return operation_rc;
}
int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero, struct gf_inst *out)
{
    CHECK(f==&foundation && bytes==MEM_PAGE_SIZE && align==MEM_PAGE_SIZE && !zero);
    allocs++;if (alloc_rc) return alloc_rc;
    *out=(struct gf_inst){.off=0x40000,.size=MEM_PAGE_SIZE};return 0;
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *page)
{ CHECK(f==&foundation && page->off==0x40000 && page->size==MEM_PAGE_SIZE);frees++; }
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *page, u32 off, u32 v)
{
    CHECK(f==&foundation && page->off==0x40000 && off<MEM_PAGE_SIZE && !(off%4));
    CHECK(v==(0x5aa50000U^off));
    if (write_rc) return write_rc;
    page_data[off/4]=v;return 0;
}
int gf_inst_read32(struct gf_foundation *f, const struct gf_inst *page, u32 off, u32 *v)
{
    CHECK(f==&foundation && page->off==0x40000 && off<MEM_PAGE_SIZE && !(off%4));
    if (read_rc) return read_rc;
    *v=page_data[off/4]^(off==fault_off ? 1U : 0U);return 0;
}
static struct gf1_test_reply reply;
static int request(u32 op,u32 heads)
{
    struct gf1_test_request q={sizeof(q),op,heads};
    int rc=gf1_module_ops.call(&foundation,&q,&reply);
    CHECK(reply.rc==rc);return rc;
}
static void prepare_cases(void)
{
    foundation.io.opaque=&lease_bus;
    lease_bus.foundation=&foundation;lease_bus.ready=true;
    for (u32 i=0;i<RECOVER_BAR_COUNT;i++) {
        bars[i]=(struct recover_bar_extent){.valid=true,.raw=0x1000000U*(i+1),
            .base=0x1000000U*(i+1),.length=0x1000000};
        lease_bus.bars[i]=bars[i].raw;
    }
    bars[0].base=V2P(mmio);
    CHECK(gf1_module_ops.prepare(0,bars,RECOVER_BAR_COUNT)==GF1_EINVAL);
    CHECK(gf1_module_ops.prepare(&foundation,0,RECOVER_BAR_COUNT)==GF1_EINVAL);
    CHECK(gf1_module_ops.prepare(&foundation,bars,RECOVER_BAR_COUNT-1)==GF1_EINVAL);
    lease_bus.ready=false;
    CHECK(gf1_module_ops.prepare(&foundation,bars,RECOVER_BAR_COUNT)==GF1_EINVAL);
    lease_bus.ready=true;bars[2].raw++;
    CHECK(gf1_module_ops.prepare(&foundation,bars,RECOVER_BAR_COUNT)==GF1_EPCI);
    bars[2].raw--;bars[2].valid=false;
    CHECK(gf1_module_ops.prepare(&foundation,bars,RECOVER_BAR_COUNT)==GF1_EPCI);
    bars[2].valid=true;foundation.bm=true;
    CHECK(gf1_module_ops.prepare(&foundation,bars,RECOVER_BAR_COUNT)==GF1_EINVAL);
    foundation.bm=false;
    CHECK(gf1_module_ops.prepare(&foundation,bars,RECOVER_BAR_COUNT)==0);
    CHECK(foundation.io.opaque==&lease_bus && foundation.map_owner==&lease_bus);
    CHECK(foundation.bar0==(volatile u8 *)mmio && foundation.instmem_bar.base==bars[2].base);
    CHECK(foundation.map_ops==&gf_production_instmem_map_ops);
    CHECK(foundation.expected_command==RECOVER_COMMAND_STOP);
    mmio[3]=0x12345678;u32 v=0;
    CHECK(!foundation.io.read(foundation.io.opaque,0,12,4,&v) && v==mmio[3]);
    CHECK(!foundation.io.write(foundation.io.opaque,0,16,4,0x87654321) && mmio[4]==0x87654321);
    foundation.revoked=true;v=0xaabbccdd;
    CHECK(foundation.io.read(foundation.io.opaque,0,12,4,&v)!=0 && v==0xaabbccdd);
    CHECK(foundation.io.write(foundation.io.opaque,0,16,4,1)!=0 && mmio[4]==0x87654321);
    CHECK(gf1_module_ops.prepare(&foundation,bars,RECOVER_BAR_COUNT)==GF1_EINVAL);
    foundation.revoked=false;
}
void _start(void)
{
    prepare_cases();
    struct gf1_test_request q={sizeof(q),GF1T_SNAPSHOT,0};
    CHECK(gf1_module_ops.call(&foundation,&q,0)==GF1_EINVAL);
    CHECK(gf1_module_ops.call(0,&q,&reply)==GF1_EINVAL);
    CHECK(gf1_module_ops.call(&foundation,0,&reply)==GF1_EINVAL);
    q.size--;
    CHECK(gf1_module_ops.call(&foundation,&q,&reply)==GF1_EINVAL);
    CHECK(request(GF1T_REVOKE+1,0)==GF1_EINVAL);
    CHECK(request(GF1T_HEADS,4)==GF1_EINVAL);
    CHECK(request(GF1T_SNAPSHOT,1)==GF1_EINVAL);
    CHECK(!masters && !starts && !stops && !revokes && !snapshots);
    CHECK(request(GF1T_MASTER,0)==0 && masters==1);
    operation_rc=GF1_EIO;
    CHECK(request(GF1T_START,0)==GF1_EIO && starts==1);
    CHECK(request(GF1T_STOP,0)==GF1_EIO && stops==1);
    operation_rc=0;
    CHECK(request(GF1T_HEADS,1)==GF1_EINVAL && !heads_calls);
    CHECK(request(GF1T_INST_PAGE,0)==GF1_EINVAL && !allocs);
    foundation.running=true;foundation.bm=true;foundation.mapped=true;foundation.registered=true;
    foundation.expected_command=RECOVER_COMMAND_RUN;foundation.expected_pmc=0x76543210;
    foundation.stats=(struct gf_irq_stats){.entries=7,.vblank={8,9},.unknown=10};
    for (u32 i=0;i<4;i++) {
        foundation.wanted_fifo=i&1;foundation.wanted_graph=i&2;
        unsigned before=saves;
        CHECK(request(GF1T_HEADS,i)==0 && foundation.wanted_heads==i);
        CHECK(saves==before+1 && saves==restores && host_if);
    }
    CHECK(heads_calls==4);
    CHECK(reply.command==RECOVER_COMMAND_RUN && reply.pmc==0x76543210 && reply.flags==15);
    CHECK(reply.stats.entries==7 && reply.stats.vblank[0]==8 && reply.stats.vblank[1]==9 && reply.stats.unknown==10);
    CHECK(reply.bytes==0 && reply.bad_off==~0U);
    CHECK(request(GF1T_INST_PAGE,0)==0 && reply.bytes==MEM_PAGE_SIZE && reply.bad_off==~0U);
    CHECK(allocs==1 && frees==1);
    fault_off=16;
    CHECK(request(GF1T_INST_PAGE,0)==GF1_EIO && reply.bytes==16 && reply.bad_off==16 && frees==2);
    fault_off=~0U;write_rc=GF1_EMAP;
    CHECK(request(GF1T_INST_PAGE,0)==GF1_EMAP && reply.bytes==0 && frees==3);
    write_rc=0;read_rc=GF1_EIO;
    CHECK(request(GF1T_INST_PAGE,0)==GF1_EIO && reply.bytes==0 && frees==4);
    read_rc=0;alloc_rc=GF1_ENOSPC;
    CHECK(request(GF1T_INST_PAGE,0)==GF1_ENOSPC && frees==4);
    CHECK(request(GF1T_REVOKE,0)==0 && revokes==1 && foundation.revoked && !foundation.running);
    CHECK(reply.flags==23);
    unsigned before=snapshots;
    CHECK(request(GF1T_SNAPSHOT,0)==0 && snapshots==before+1 && reply.flags==23);
    CHECK(request(GF1T_MASTER,0)==GF1_EPCI && masters==1);
    CHECK(request(GF1T_START,0)==GF1_EPCI && starts==1);
    CHECK(request(GF1T_HEADS,1)==GF1_EPCI && heads_calls==4);
    CHECK(request(GF1T_INST_PAGE,0)==GF1_EPCI && allocs==5);
    CHECK(request(GF1T_STOP,0)==0 && stops==2);
    CHECK(request(GF1T_REVOKE,0)==0 && revokes==2);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
