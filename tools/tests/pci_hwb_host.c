#include "hwb_fake.h"
#define IO_H
static u32 cf8, initial_if, irq_on, mech, io_count, data_count, data_port, data_width, data_write, selected;
static u8 config[256];
static unsigned int irq_save(void) { u32 old = irq_on; irq_on = 0; return old; }
static void irq_restore(unsigned int f) { irq_on = f; }
static u32 read_data(unsigned int p, u32 w) {
    u32 v = 0; CHECK(!irq_on); data_count++; data_port=p; data_width=w; data_write=0; selected=cf8;
    for (u32 i=0;i<w;i++) v |= (u32)config[(cf8 & 252u) + p - 0xcfcu + i] << (8*i);
    return v;
}
static void write_data(unsigned int p, u32 w, u32 v) {
    CHECK(!irq_on); data_count++; data_port=p; data_width=w; data_write=1; selected=cf8;
    for (u32 i=0;i<w;i++) { u32 o=(cf8 & 252u)+p-0xcfcu+i; u8 b=v >> (8*i); if (o==6 || o==7) config[o] &= ~b; else config[o]=b; }
}
static unsigned int inp(unsigned int p) { io_count++; return read_data(p,1); }
static unsigned int inpw(unsigned int p) { io_count++; return read_data(p,2); }
static unsigned long inpd(unsigned int p) { io_count++; CHECK(!irq_on); return p==0xcf8u ? (mech || cf8 == 0x12345678u ? cf8 : 0) : read_data(p,4); }
static void outp(unsigned int p,unsigned int v) { io_count++; write_data(p,1,v); }
static inline void outpw(unsigned int p,unsigned int v) { io_count++; write_data(p,2,v); }
static void outpd(unsigned int p,unsigned long v) { io_count++; CHECK(!irq_on); if (p==0xcf8u) cf8=v; else write_data(p,4,v); }
#include "drivers/pci/pci_hwb.c"
static void reset(void) { cf8=0x12345678; initial_if=irq_on; mech=1; io_count=data_count=0; }
static void restored(void) { CHECK(cf8==0x12345678); CHECK(irq_on==initial_if); }
static void tests(void) {
    u32 v, bdf=HW_BDF(0x23, 0x12, 5); u32 expected=0x80000000u | (0x23u<<16) | (0x12u<<11) | (5u<<8) | 4u;
    for (u32 f=0;f<2;f++) { irq_on=f; reset(); config[4]=0x12; config[5]=0x34; config[6]=0xf9; config[7]=0xf9;
        HWB_TEST("read-lane"); CHECK(pci_hwb_read(bdf,5,1,&v)==HW_OK); CHECK(v==0x34 && data_count==1 && data_port==0xcfdu && data_width==1 && !data_write && selected==expected); restored();
        reset(); CHECK(pci_hwb_read(bdf,6,2,&v)==HW_OK); CHECK(v==0xf9f9 && data_count==1 && data_port==0xcfeu && data_width==2); restored();
        HWB_TEST("write-lane"); reset(); CHECK(pci_hwb_write(bdf,5,1,0x56)==HW_OK); CHECK(data_count==1 && data_write && data_port==0xcfdu && data_width==1 && config[4]==0x12 && config[5]==0x56); restored();
        HWB_TEST("w1c"); reset(); CHECK(pci_hwb_write(bdf,4,2,0xabcd)==HW_OK); CHECK(data_count==1 && data_write && data_width==2 && data_port==0xcfcu && config[6]==0xf9 && config[7]==0xf9); restored();
        HWB_TEST("dword"); reset(); CHECK(pci_hwb_read(bdf,4,4,&v)==HW_OK); CHECK(v==0xf9f9abcd && data_width==4); restored();
        reset(); CHECK(pci_hwb_write(bdf,8,4,0x87654321)==HW_OK); CHECK(data_width==4 && config[8]==0x21 && config[11]==0x87); restored();
        HWB_TEST("absent"); reset(); mech=0; v=99; CHECK(pci_hwb_read(bdf,0,4,&v)==HW_NOSYS && v==0 && data_count==0); restored(); reset(); mech=0; CHECK(pci_hwb_write(bdf,0,4,1)==HW_NOSYS && data_count==0); restored();
    }
    HWB_TEST("invalid"); reset(); v=3; CHECK(pci_hwb_read(0x10000,0,1,&v)==HW_ARG && v==0); CHECK(pci_hwb_read(0,255,2,&v)==HW_ARG); CHECK(pci_hwb_read(0,1,2,&v)==HW_ARG); CHECK(pci_hwb_read(0,256,1,&v)==HW_ARG); CHECK(pci_hwb_read(0,0,3,&v)==HW_ARG); CHECK(pci_hwb_read(0,0,1,0)==HW_ARG); CHECK(pci_hwb_write(0,0,1,256)==HW_ARG); CHECK(pci_hwb_write(0,0,2,65536)==HW_ARG); CHECK(io_count==0);
    HWB_TEST("all-ones"); reset(); config[252]=config[253]=config[254]=config[255]=255; CHECK(pci_hwb_read(bdf,255,1,&v)==HW_OK && v==255); CHECK(pci_hwb_read(bdf,254,2,&v)==HW_OK && v==65535); CHECK(pci_hwb_read(bdf,252,4,&v)==HW_OK && v==0xffffffffu);
}
void _start(void) { tests(); hwb_test_write("PASS pci-hwb\n"); hwb_test_exit(0); }
