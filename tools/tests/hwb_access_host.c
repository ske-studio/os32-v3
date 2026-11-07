#define irq_save unused_cpu_irq_save
#define irq_restore unused_cpu_irq_restore
#include "io.h"
#undef irq_save
#undef irq_restore
/* Reuse the init fixture's external stubs; execute the real dispatcher below. */
#define _start unused_init_start
#define HWB_ACCESS_REAL
#include "kdebug_init_host.c"
#undef _start
#include "hwb_fake.h"
#include "io.h"
#include "mm/paging.h"
#include "memmap.h"

/* Test addresses/data are arbitrary unregistered targets (design §7).
 * Asymmetric bytes make lane, width and zero extension independently visible. */
#define TEST_BDF HW_BDF(37, 19, 6)
#define TEST_PORT 0x1235U
#define TEST_PHYS 0x34567000UL
#define TEST_DATA 0xD3B791E5UL
#define ORIGINAL_PTE 0x56789023UL
static u32 reads, writes, seen_space, seen_addr, seen_width, seen_bdf, stored;
static u32 backend_status, abort_flag, abort_calls, pci_absent;
static u32 fake_if, pte = ORIGINAL_PTE, map_flags, map_fail, access_fail;
static u32 saves, restores, maps, unmaps, write_only, block_mode;
static u32 block_memory[2048] __attribute__((aligned(4096)));
static u32 aperture(void) { return block_mode ? (u32)(uptr)block_memory : MEM_DEVICE_APERTURE_BASE; }
static void trace(u32 space, u32 addr, u32 width, bool write, u32 value)
{
    if (space == HW_PHYS) CHECK(fake_if == 0);
    if (write) { writes++; stored = value; }
    else { reads++; CHECK(!write_only); }
    seen_space = space; seen_addr = addr; seen_width = width;
}
static unsigned int fake_inp(unsigned int p)
{ trace(HW_IO,p,1,false,0); return (u8)TEST_DATA; }
static unsigned int fake_inpw(unsigned int p)
{ trace(HW_IO,p,2,false,0); return (u16)TEST_DATA; }
static unsigned long fake_inpd(unsigned int p)
{ trace(HW_IO,p,4,false,0); return TEST_DATA; }
static void fake_outp(unsigned int p, unsigned int v)
{ trace(HW_IO,p,1,true,v); }
static void fake_outpw(unsigned int p, unsigned int v)
{ trace(HW_IO,p,2,true,v); }
static void fake_outpd(unsigned int p, unsigned long v)
{ trace(HW_IO,p,4,true,v); }
#define inp fake_inp
#define inpw fake_inpw
#define inpd fake_inpd
#define outp fake_outp
#define outpw fake_outpw
#define outpd fake_outpd
#include "kdebug/kdebug_main.c"

u32 pci_hwb_read(u32 bdf, u32 offset, u32 width, u32 *value)
{
    trace(HW_PCI,offset,width,false,0); seen_bdf = bdf;
    /* Deliberately dirty output even on failure: access owns zero-on-failure. */
    *value = pci_absent ? U32_MAX : TEST_DATA;
    return backend_status;
}
u32 pci_hwb_write(u32 bdf, u32 offset, u32 width, u32 value)
{
    trace(HW_PCI,offset,width,true,value); seen_bdf = bdf;
    return backend_status;
}
u32 exec_hwb_abort_take(void)
{ u32 v = abort_flag; abort_flag = 0; abort_calls++; return v; }

static unsigned int irq_save(void)
{ u32 v = fake_if; fake_if = 0; saves++; return v; }
static void irq_restore(unsigned int v)
{ CHECK(fake_if == 0); fake_if = v; restores++; }
u32 paging_pte_raw(u32 va)
{ CHECK(fake_if == 0 && (va == aperture() || (block_mode && va == aperture()+MEM_PAGE_SIZE))); return pte; }
int paging_map_phys(u32 va, u32 pa, u32 pages, u32 flags)
{
    CHECK(fake_if == 0 && (va == aperture() || (block_mode && va == aperture()+MEM_PAGE_SIZE)));
    CHECK(pa == TEST_PHYS && (pages == 1 || (block_mode && pages == 2)));
    maps++; map_flags = flags; pte = pa | flags;
    return map_fail ? -1 : 0;
}
int paging_pte_restore(u32 va, u32 raw)
{
    CHECK(fake_if == 0 && (va == aperture() || (block_mode && va == aperture()+MEM_PAGE_SIZE)));
    pte = raw; unmaps++; return 0;
}
#define KPEEK_HOST
#define KDEBUG_WINDOW_HOST
#define recover_window_hal_init unused_real_hal_init
#include "kdebug/kdebug_window.c"
#undef recover_window_hal_init
static int fake_load(void *opaque, u32 va, u32 width, u32 *value)
{
    (void)opaque; trace(HW_PHYS, va, width, false, 0);
    *value = TEST_DATA; return access_fail ? -1 : 0;
}
static int fake_store(void *opaque, u32 va, u32 width, u32 value)
{
    (void)opaque; trace(HW_PHYS, va, width, true, value);
    return access_fail ? -1 : 0;
}
static const struct recover_window_ops fake_ops = {
    window_hal_irq_save, window_hal_irq_restore, window_hal_pte, window_hal_map,
    window_hal_unmap, fake_load, fake_store
};
void recover_window_hal_init(struct recover_window *w)
{ *w = (struct recover_window){.ops=&fake_ops, .va=aperture()}; }
#include "kdebug/hwb_access.c"

static struct hw_req request(u32 action, u32 space, u32 width)
{
    return (struct hw_req){.version=HW_VERSION, .action=action, .space=space,
        .width=width, .bdf=space == HW_PCI ? TEST_BDF : 0,
        .addr=space == HW_PCI ? 0 : space == HW_IO ? TEST_PORT : TEST_PHYS};
}
static void reset_trace(void)
{
    reads=writes=abort_calls=maps=unmaps=saves=restores=0;
    backend_status=map_fail=access_fail=write_only=pci_absent=0;
}
static void accepted(struct hw_req *r, u32 status)
{ CHECK(kdebug_ops.dbg_call(HW_OP,r,sizeof(*r)) == 0); CHECK(r->status == status); }
static void arg(struct hw_req r)
{
    reset_trace();
    u32 value = r.value;
    accepted(&r, HW_ARG);
    CHECK(reads == 0 && writes == 0 && abort_calls == 0 && maps == 0 && saves == 0);
    CHECK(r.value == (r.action == HW_READ ? 0 : value));
}
static void wire(void)
{
    HWB_TEST("wire");
    struct hw_req r = request(HW_READ,HW_IO,1), before;
    r.version++;
    before = r;
    CHECK(hwb_access(&r,sizeof(r)) == OS32_ERR_INVAL);
    for (u32 i=0; i<sizeof(r); i++) CHECK(((u8 *)&r)[i] == ((u8 *)&before)[i]);
    r.version=HW_VERSION;
    before=r;
    const u32 bad_sizes[] = {0, HW_REQ_BYTES-1, HW_REQ_BYTES+1, U32_MAX};
    for (u32 j=0; j<sizeof(bad_sizes)/sizeof(bad_sizes[0]); j++) {
        CHECK(hwb_access(&r,bad_sizes[j]) == OS32_ERR_INVAL);
        for (u32 i=0; i<sizeof(r); i++) CHECK(((u8 *)&r)[i] == ((u8 *)&before)[i]);
    }
    CHECK(hwb_access(0,HW_REQ_BYTES) == OS32_ERR_INVAL);
    CHECK(reads == 0 && writes == 0);
}
static void arguments(void)
{
    HWB_TEST("arguments");
    struct hw_req r;
    for (u32 s=HW_PCI; s<=HW_PHYS; s++) {
        r=request(HW_READ,s,0); arg(r);
        r.width=3; arg(r); r.width=8; arg(r);
        r=request(HW_READ,s,1); r.value=1; arg(r);
        r=request(HW_READ,s,1); r.status=1; arg(r);
        r=request(HW_WRITE,s,1); r.value=U8_MAX+1U; arg(r);
        r=request(HW_WRITE,s,2); r.value=U16_MAX+1U; arg(r);
        r=request(HW_READ,s,2); r.addr=U32_MAX; arg(r);
    }
    r=request(HW_READ,HW_PCI,1); r.bdf=HW_BDF_MASK+1U; arg(r);
    r=request(HW_READ,HW_PCI,1); r.addr=HW_PCI_CONFIG_BYTES; arg(r);
    r=request(HW_READ,HW_PCI,2); r.addr=1; arg(r);
    r=request(HW_READ,HW_PCI,4); r.addr=HW_PCI_CONFIG_BYTES-2; arg(r);
    r=request(HW_READ,HW_IO,1); r.addr=U16_MAX+1U; arg(r);
    r=request(HW_READ,HW_IO,2); r.addr=U16_MAX; arg(r);
    r=request(HW_READ,HW_IO,4); r.addr=U16_MAX-2; arg(r);
    r=request(HW_READ,HW_PHYS,2); r.addr=TEST_PHYS+1; arg(r);
    for (u32 s=HW_IO; s<=HW_PHYS; s++) {
        r=request(HW_READ,s,1); r.bdf=1; arg(r);
    }
    r=request(0,HW_IO,1); arg(r);
    r=request(HW_READ,0,1); arg(r);
    r=request(HW_READ,HW_PHYS+1,1); arg(r);
    /* Every unused POINT field is independently rejected. */
    for (u32 i=2; i<=7; i++) {
        r=(struct hw_req){.version=HW_VERSION,.action=HW_POINT};
        ((u32 *)&r)[i]=1; arg(r);
    }
}
static void accesses(void)
{
    /* Independent expected truncations of TEST_DATA, design §3 wire table. */
    const u32 widths[]={1,2,4}, values[]={0xE5U,0x91E5U,TEST_DATA};
    owner.state=REC_PAUSED;
    for (u32 s=HW_PCI; s<=HW_PHYS; s++) for (u32 i=0; i<3; i++) {
        for (u32 initial_if=0; initial_if<=1; initial_if++) {
            HWB_TEST("paused"); reset_trace(); fake_if=initial_if;
            struct hw_req r=request(HW_READ,s,widths[i]);
            /* PCI offsets cover byte lane 3, halfword lane 2 and DWORD. */
            if (s==HW_PCI) r.addr=i==0 ? 3 : i==1 ? 2 : HW_PCI_CONFIG_BYTES-4;
            accepted(&r,HW_OK);
            CHECK(owner.state == REC_PAUSED && reads == 1 && writes == 0);
            CHECK(r.value == values[i] && seen_width == widths[i] && seen_space == s);
            if (s==HW_PCI) CHECK(seen_bdf == TEST_BDF && seen_addr == r.addr);
            if (s==HW_IO) CHECK(seen_addr == TEST_PORT && saves == 0);
            HWB_TEST("window");
            CHECK(pte == ORIGINAL_PTE && fake_if == initial_if);
            if (s==HW_PHYS) CHECK(maps==1 && unmaps==1 && saves==1 && restores==1 &&
                map_flags == (PTE_PRESENT|PTE_PCD));
            reset_trace(); write_only=1;
            r=request(HW_WRITE,s,widths[i]); r.value=values[i];
            if (s==HW_PCI) r.addr=i==0 ? 3 : i==1 ? 2 : HW_PCI_CONFIG_BYTES-4;
            HWB_TEST("write-only"); accepted(&r,HW_OK);
            CHECK(reads == 0);
            HWB_TEST("phys-write");
            CHECK(writes == 1 && stored == values[i] && r.value == values[i]);
            CHECK(seen_width == widths[i] && seen_space == s);
            if (s==HW_PCI) CHECK(seen_bdf==TEST_BDF && seen_addr==r.addr);
            if (s==HW_IO) CHECK(seen_addr==TEST_PORT && saves==0);
            HWB_TEST("window");
            CHECK(pte == ORIGINAL_PTE && fake_if == initial_if);
            if (s==HW_PHYS) CHECK(unmaps==1 && restores==1 &&
                map_flags == (PTE_PRESENT|PTE_PCD|PTE_RW));
        }
    }
    HWB_TEST("boundaries");
    const u32 all_ones[]={U8_MAX,U16_MAX,U32_MAX};
    for (u32 i=0; i<3; i++) {
        reset_trace(); pci_absent=1;
        struct hw_req edge=request(HW_READ,HW_PCI,widths[i]);
        edge.addr=HW_PCI_CONFIG_BYTES-widths[i];
        accepted(&edge,HW_OK);
        CHECK(edge.value==all_ones[i] && reads==1 && writes==0);
        reset_trace();
        edge=request(HW_READ,HW_IO,widths[i]);
        edge.addr=U16_MAX-(widths[i]-1U);
        accepted(&edge,HW_OK);
        CHECK(edge.value==values[i] && seen_addr==edge.addr && reads==1);
        reset_trace(); write_only=1;
        edge.action=HW_WRITE;
        accepted(&edge,HW_OK);
        CHECK(writes==1 && reads==0 && seen_addr==edge.addr);
    }
    HWB_TEST("paused"); reset_trace(); abort_flag=1;
    struct hw_req r={.version=HW_VERSION,.action=HW_POINT};
    accepted(&r,HW_OK); CHECK(r.value==1 && abort_calls==1);
    r.value=0; accepted(&r,HW_OK); CHECK(r.value==0 && abort_calls==2);
    CHECK(reads==0 && writes==0 && saves==0 && owner.state==REC_PAUSED);
}
static void failures(void)
{
    HWB_TEST("failures");
    for (u32 status=HW_NOSYS; status<=HW_IO_ERROR; status++) {
        reset_trace(); backend_status=status;
        struct hw_req r=request(HW_READ,HW_PCI,1);
        accepted(&r,status); CHECK(r.value==0 && reads==1 && writes==0);
        reset_trace(); backend_status=status;
        r=request(HW_WRITE,HW_PCI,1); r.value=1;
        accepted(&r,status); CHECK(r.value==1 && reads==0 && writes==1);
    }
    for (u32 initial_if=0; initial_if<=1; initial_if++) for (u32 action=HW_READ; action<=HW_WRITE; action++) {
        reset_trace(); window_busy=true; fake_if=initial_if;
        struct hw_req busy=request(action,HW_PHYS,4);
        accepted(&busy,HW_WINDOW);
        CHECK(maps==0 && reads==0 && writes==0 && pte==ORIGINAL_PTE);
        CHECK(fake_if==initial_if && saves==1 && restores==1 && window_busy);
        window_busy=false;
        reset_trace(); map_fail=1; fake_if=initial_if;
        struct hw_req r=request(action,HW_PHYS,4);
        accepted(&r,HW_WINDOW);
        CHECK(r.value==0 && reads==0 && writes==0 && unmaps==1 && restores==1);
        CHECK(pte==ORIGINAL_PTE && fake_if==initial_if && !window_busy);
        reset_trace(); access_fail=1;
        r=request(action,HW_PHYS,4);
        accepted(&r,HW_IO_ERROR);
        CHECK(r.value==0 && reads+writes==1 && unmaps==1 && restores==1);
        CHECK(pte==ORIGINAL_PTE && fake_if==initial_if && !window_busy);
    }
}
static void blocks(void)
{
    HWB_TEST("block32"); block_mode=1;
    for (u32 inc=0; inc<2; inc++) for (u32 cross=0; cross<2; cross++) {
        for (u32 n=0; n<2048; n++) block_memory[n]=0xBAD0BAD0;
        reset_trace(); fake_if=inc;
        struct hw_block_req b={.req=request(HW_BLOCK32,HW_PHYS,4),
                               .count=1024,.increment=inc};
        u32 start=cross ? 1023U : 0;
        b.req.addr+=start*4; b.req.value=0xFFFFFFFE;
        CHECK(kdebug_ops.dbg_call(HW_OP,&b,sizeof(b))==0 && b.req.status==HW_OK);
        CHECK(b.req.value==0xFFFFFFFE && maps==1 && unmaps==cross+1);
        CHECK(reads==0 && writes==0 && abort_calls==0 && saves==1 && restores==1);
        CHECK(fake_if==inc && pte==ORIGINAL_PTE && (map_flags & PTE_PCD) && (map_flags & PTE_RW));
        for (u32 n=0; n<2048; n++)
            CHECK(block_memory[n]==(n>=start && n<start+1024 ?
                  0xFFFFFFFEU+(inc ? n-start : 0) : 0xBAD0BAD0U));
    }
    for (u32 width=1; width<=2; width++) for (u32 inc=0; inc<2; inc++) {
        HWB_TEST("block-narrow");
        for (u32 n=0; n<sizeof(block_memory); n++) ((u8 *)block_memory)[n]=0xA5;
        reset_trace(); fake_if=inc;
        struct hw_block_req b={.req=request(width==1 ? HW_BLOCK8 : HW_BLOCK16,HW_PHYS,width),
                               .count=HW_BLOCK_BYTES/width,.increment=inc};
        u32 start=4096-width;
        b.req.addr+=start; b.req.value=width==1 ? 0xFE : 0xFFFE;
        CHECK(hwb_access(&b,sizeof(b))==0 && b.req.status==HW_OK);
        CHECK(maps==1 && unmaps==2 && !reads && !writes && !abort_calls);
        CHECK(fake_if==inc && pte==ORIGINAL_PTE && saves==1 && restores==1);
        for (u32 n=0; n<sizeof(block_memory); n++) {
            u32 v=b.req.value+(inc ? (n-start)/width : 0);
            CHECK(((u8 *)block_memory)[n]==(n>=start && n<start+HW_BLOCK_BYTES ?
                  (u8)(v >> (((n-start)%width)*8)) : 0xA5));
        }
        struct hw_block_req goodn=b;
        for (u32 bad=0; bad<(width==2 ? 8U : 7U); bad++) {
            b=goodn; b.req.status=0;
            if(bad==0)b.count++;
            if(bad==1)b.count=0;
            if(bad==2)b.req.value=width==1 ? 0x100 : 0x10000;
            if(bad==3){b.req.addr=0xFFFFFFFE;b.count=4;}
            if(bad==4)b.req.width=4;
            if(bad==5)b.increment=2;
            if(bad==6){b.req.space=HW_IO;}
            if(bad==7)b.req.addr++;
            reset_trace();
            CHECK(hwb_access(&b,sizeof(b))==0 && b.req.status==HW_ARG && !maps && !saves);
        }
    }
    HWB_TEST("block32");
    struct hw_block_req good={.req=request(HW_BLOCK32,HW_PHYS,4),.count=1};
    for (u32 which=0; which<9; which++) {
        struct hw_block_req b=good;
        if(which==0)b.count=0;
        if(which==1)b.count=1025;
        if(which==2)b.increment=2;
        if(which==3)b.req.width=2;
        if(which==4)b.req.space=HW_IO;
        if(which==5)b.req.addr++;
        if(which==6){b.req.addr=0xFFFFFFFC;b.count=2;}
        if(which==7)b.req.bdf=1;
        if(which==8)b.req.status=1;
        reset_trace();
        CHECK(hwb_access(&b,sizeof(b))==0 && b.req.status==HW_ARG);
        CHECK(maps==0 && saves==0 && !reads && !writes);
    }
    reset_trace();
    CHECK(hwb_access(&good,HW_REQ_BYTES)==OS32_ERR_INVAL);
    good.req.action=HW_WRITE;
    CHECK(hwb_access(&good,sizeof(good))==OS32_ERR_INVAL);
    good.req.action=HW_BLOCK32;
    for(u32 initial_if=0;initial_if<2;initial_if++) {
        reset_trace(); fake_if=initial_if; map_fail=1;
        CHECK(hwb_access(&good,sizeof(good))==0 && good.req.status==HW_WINDOW);
        CHECK(unmaps==1 && fake_if==initial_if && pte==ORIGINAL_PTE);
        good.req.status=0;
        reset_trace(); window_busy=true;
        CHECK(hwb_access(&good,sizeof(good))==0 && good.req.status==HW_WINDOW);
        CHECK(!maps && fake_if==initial_if); window_busy=false; good.req.status=0;
    }
    block_mode=0;
}
void _start(void)
{
    reset_trace(); wire(); arguments(); accesses(); blocks(); failures();
    hwb_test_write("GREEN wire/arguments/paused/PCI lanes/IO widths/window/failures/write-only\n");
    hwb_test_exit(0);
}
