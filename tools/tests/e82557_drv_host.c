/* Real LB3 driver with persistent VFS and byte-accurate SCB model.
 * Contract/T/S are separate oracles. Record: tools/tests/e82557_drv_tdd.md */
/* Freestanding ILP32: this host has no 32-bit glibc. These helpers serve
 * the test oracle/log capture only; none are compiled into the kernel. */
#include <stdarg.h>
typedef unsigned long size_t;
#define stderr 2
static __attribute__((noreturn)) void exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static __attribute__((unused,noreturn)) void abort(void) { exit(99); }
static size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void *memset(void *dst, int v, size_t n)
{
    unsigned char *p = dst;
    while (n--) *p++ = (unsigned char)v;
    return dst;
}
static __attribute__((unused)) int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    while (n--) { if (*p != *q) return *p - *q; p++; q++; }
    return 0;
}
static __attribute__((unused)) void *memchr(const void *src, int v, size_t n)
{
    const unsigned char *p = src;
    while (n--) { if (*p == (unsigned char)v) return (void *)p; p++; }
    return (void *)0;
}
static int strncmp(const char *a, const char *b, size_t n)
{
    while (n--) { if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b; a++; b++; }
    return 0;
}
static int strcmp(const char *a, const char *b) { return strncmp(a, b, strlen(a) + 1); }
static char *strstr(const char *s, const char *part)
{
    size_t n = strlen(part);
    do { if (!strncmp(s, part, n)) return (char *)s; } while (*s++);
    return (char *)0;
}
static int atoi(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }
static void fmt_char(char *dst, size_t cap, size_t *len, char c)
{
    if (*len + 1 < cap) dst[*len] = c;
    (*len)++;
}
static int vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap)
{
    size_t len = 0;
    while (*fmt) {
        if (*fmt != '%') { fmt_char(dst, cap, &len, *fmt++); continue; }
        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            while (*s) fmt_char(dst, cap, &len, *s++);
        } else {
            unsigned int v = va_arg(ap, unsigned int), radix = *fmt == 'x' ? 16 : 10;
            char digits[16]; int n = 0;
            if (*fmt == 'd' && (int)v < 0) { fmt_char(dst, cap, &len, '-'); v = 0u - v; }
            do { unsigned int d = v % radix; digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= radix; } while (v);
            while (width-- > n) fmt_char(dst, cap, &len, '0');
            while (n) fmt_char(dst, cap, &len, digits[--n]);
        }
        fmt++;
    }
    if (cap) dst[len < cap ? len : cap - 1] = 0;
    return (int)len;
}
static int snprintf(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap; int n;
    va_start(ap, fmt); n = vsnprintf(dst, cap, fmt, ap); va_end(ap);
    return n;
}
static void fprintf(int fd, const char *fmt, ...)
{
    char buf[512]; va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(fd), "c"(buf), "d"(strlen(buf)) : "memory");
}
__asm__(".global _start\n_start:\n"
        "mov (%esp), %eax\nlea 4(%esp), %edx\npush %edx\npush %eax\ncall main\n"
        "mov %eax, %ebx\nmov $1, %eax\nint $0x80\n");
#include "../../drivers/pci_decode.c"
#define e82557_ee_read_word real_ee_read_word
#include "../../drivers/e82557_eeprom.c"
#undef e82557_ee_read_word
#include "../../fs/vfs.h"
#include "../../drivers/e82557.h"
#include "../../kernel/module.h"
#include "../../include/bootinfo.h"
_Static_assert(sizeof(u32) == 4, "ILP32 required");
static int failed;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; } } while (0)
static void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; return d; }
struct file { char data[96]; int len, present, reads, writes, synced, verified; };
static struct file files[2];
static int cfg=1, fs_calls, mkdir_fail, read_fail, write_fail, short_write, sync_fail;
static int corrupt_read, remove_fail, write_zero, bad_crc, irq_depth, trace_count;
static int io_count, resets, mask_writes, ack_writes, commands, pointers, ee_calls;
static int ee_mode, auto_calls, compat_reads;
static int idle_fail, pre_busy, post_busy, busy_reads, operations;
static int alloc_fail, bm_fail, stop_fail, cb_fail, cb_ng, cu_pre, cu_post;
static int allocated, freed, leaked, bm_writes, starts, last_start_reset;
static int rm_calls, completion_reads, registered, irq_fail, unregistered;
static u32 event_seq, last_store, last_barrier, last_complete[9];
static u32 cb_due, cb_delay=3000;
static u16 pci_command=0x0143;
static u8 memory[16384], *active_cb;
static u32 trace_bits, now, reset_at, cu_base=0x1000, ru_base=0x1000, pointer;
static u16 status=0xA000;
static u8 command, mask;
static char output[8192];
static u16 phy_regs[32][32];
static int phyvendor=1, pcr_writes, pcr_reads, pcr_reserved, pcr_drop;
static int vendor_reads[32], vendor_timeout, pcr_timeout_first, pcr_timeout_write;
static int mdi_timeout, mdi_reads, mdi_total, bmsr_reads, link_tick, an_off, latch_zero;
static u32 mdi_ctl, current_stage, clock_reads;
static int cb_pre_pending, loopback, lb_timeout, lb_corrupt, ru_starts, ru_fail;
static int swi_lost, irq0_first, irq_deliveries, runtime, inject_rus, inject_reset;
static int yield_mode, ack_repeat, status_reads, cb_completed;
static u32 ru_pointer, model_ticks;
static u8 rx_frame[1600];
#include "../../kernel/irq.h"
static irq_handler_fn handler;
static void model_assert_ring(u32 pa);
static void yield_event(void);
static void model_rx(const u8 *frame, u32 len);
static void deliver_irq(void);
static void service_tick(void);
static void check_pending(void);
static u32 raw16(u32 off) { return memory[off] | ((u32)memory[off+1]<<8); }
static u32 raw32(u32 off) { return raw16(off) | (raw16(off+2)<<16); }
static void put16(u32 off,u16 v) { memory[off]=(u8)v; memory[off+1]=(u8)(v>>8); }
static void model_time(void)
{
    if (active_cb && !cb_completed && now>=cb_due && cb_fail!=starts) {
        u32 off=(u32)(active_cb-memory);
        put16(off,cb_ng==starts?0x8000:0xa000); cb_completed=1;
        status|=0xA000;
        if ((active_cb[2]&7)==4 && loopback && !lb_timeout) {
            model_rx(active_cb+16,raw16(off+12)&0x3fff);
            if (lb_corrupt) memory[1616+16]^=1;
        }
    }
}
static u32 fake_ticks(void)
{
    if (++clock_reads>100000) exit(22);
    if (!runtime) now+=100;
    model_time(); return runtime?model_ticks:now/10000;
}
static u32 inpd(u16 port)
{
    CHECK(port==0x6010 && !irq_depth && resets==2);
    if (++mdi_total>20000) exit(23);
    if (mdi_timeout || ++mdi_reads<3) return mdi_ctl;
    u32 addr=(mdi_ctl>>21)&31,reg=(mdi_ctl>>16)&31,op=(mdi_ctl>>26)&3;
    if ((pcr_timeout_first && reg==0x17 && !pcr_writes) ||
        (pcr_timeout_write && reg==0x17 && op==1) ||
        (vendor_timeout && vendor_timeout==(int)reg && (reg!=0x17 || pcr_writes))) return mdi_ctl;
    if (op==2 && reg>=0x12) {
        CHECK(phy_regs[addr][2]==0x2000 && (phy_regs[addr][3]&0xfff0)==0x5c00);
        vendor_reads[reg]++; if (reg==0x17) pcr_reads++;
    }
    u16 value=phy_regs[addr][reg];
    if (reg==0x17 && pcr_writes) value=(value^(pcr_reserved?0x0500:0))&~(u16)pcr_drop;
    if (op==2 && reg==1) {
        bmsr_reads++; value=(latch_zero && (bmsr_reads&1))?0:0x7809;
        if (link_tick>=0 && (int)(now/10000)>=link_tick && !(bmsr_reads&1)) value|=an_off?4:0x24;
    }
    return (mdi_ctl&~0xffffu)|0x10000000u|value;
}
static struct file *file_for(const char *path)
{
    if (!strcmp(path,"/var/log/lan.try")) return &files[0];
    if (!strcmp(path,"/var/log/lan.seq")) return &files[1];
    CHECK(0); return &files[0];
}
int sysconfig_get_int(const char *path, const char *key, int def)
{ (void)path; CHECK(def==1);
  if (!strcmp(key,"E82557_PHYVENDOR")) return phyvendor;
  CHECK(!fs_calls && !io_count); CHECK(!strcmp(key,"E82557")); return cfg; }
int boot_image_info(void *out)
{ BootImageInfo *b=out; memset(b,0,sizeof(*b)); b->crc_valid=!bad_crc; b->image_crc=0x1234abcd; return 0; }
int vfs_mkdir(const char *path)
{ CHECK(!irq_depth); fs_calls++; CHECK(!strcmp(path,"/var")||!strcmp(path,"/var/log")); return mkdir_fail ? VFS_ERR_IO : VFS_ERR_EXIST; }
int vfs_read(const char *path, void *buf, u32 cap)
{
    struct file *f=file_for(path); int idx=(int)(f-files), n;
    CHECK(!irq_depth); fs_calls++; f->reads++;
    if (read_fail==idx+1) return VFS_ERR_IO;
    if (!f->present) return VFS_ERR_NOTFOUND;
    n=f->len < (int)cap ? f->len : (int)cap;
    memcpy(buf,f->data,(size_t)n);
    if (f->writes && corrupt_read==idx+1 && n) ((char *)buf)[0]^=1;
    if (f->writes && f->synced && corrupt_read!=idx+1) f->verified=1;
    return n;
}
int vfs_write(const char *path, const void *data, u32 len)
{
    struct file *f=file_for(path); int idx=(int)(f-files);
    CHECK(!irq_depth && !io_count); fs_calls++; f->writes++;
    if (write_fail==idx+1) return VFS_ERR_IO;
    CHECK(len<sizeof(f->data)); memcpy(f->data,data,len); f->data[len]=0;
    f->len=(int)len; f->present=1; f->synced=f->verified=0;
    return short_write==idx+1 ? (int)len-1 : write_zero ? 0 : (int)len;
}
int vfs_sync(void)
{ CHECK(!irq_depth); fs_calls++; if(sync_fail) return VFS_ERR_IO; files[0].synced=files[1].synced=1; return 0; }
int vfs_rm(const char *path)
{ CHECK(!irq_depth); fs_calls++; rm_calls++; CHECK(!strcmp(path,"/var/log/lan.try")); if(remove_fail) return VFS_ERR_IO; files[0].present=0; return 0; }
static void kprintf(int attr, const char *fmt, ...)
{ va_list ap; size_t n=strlen(output); (void)attr; va_start(ap,fmt); vsnprintf(output+n,sizeof(output)-n,fmt,ap); va_end(ap); }
#include "../../drivers/pci_bind_match.c"
static u32 irq_save(void) { u32 f=irq_depth?0:0x200; irq_depth++; return f; }
static void irq_restore(u32 flags) { CHECK(irq_depth>0); irq_depth--; CHECK(!!(flags&0x200)==!irq_depth); }
static void trace(u32 stage)
{ CHECK(!irq_depth && stage); trace_count++; current_stage=stage; trace_bits|=1u<<stage; }
struct kdebug kdebug;
static void guard_io(u16 port)
{
    if (++operations>25000) exit(21);
    io_count++; CHECK(port>=0x6000 && port<0x6020);
    CHECK(files[1].verified && (runtime || files[0].verified));
    if (port!=0x6008) CHECK(resets>=2 && now-reset_at>=20);
}
static void cpu_delay_us(u32 us) { now+=us; model_time(); if (!irq_depth) deliver_irq(); }
static u8 inp(u16 port)
{
    guard_io(port); CHECK(port==0x6002);
    if ((pre_busy && !commands) || (cu_pre && cb_pre_pending && starts+1==cu_pre && !command)) {
        status=0x2040; return 1;
    }
    if (command && !(post_busy && commands>=post_busy) &&
        !(cu_post && starts==cu_post) && !(ru_fail && command==1 && ru_starts>=ru_fail) && ++busy_reads>=3) {
        if(command==0x60) cu_base=pointer;
        if(command==0x06) ru_base=pointer;
        command=0;
    }
    if (command && command!=1) status=(status&0xff00)|0x40|(status&0x3c);
    else if (!command) status&=~0xc0;
    return command;
}
static u16 inpw(u16 port)
{
    guard_io(port); CHECK(port==0x6000); status_reads++;
    if (inject_rus) { inject_rus=0; model_rx(rx_frame,60); }
    return status;
}
static void outp(u16 port,u8 value)
{
    guard_io(port);
    if (port==0x6001) {
        CHECK(value==(u8)(status>>8)); ack_writes++; status&=~((u16)value<<8);
        if (ack_repeat) { status|=0x4000; ack_repeat--; }
    } else if (port==0x6003) {
        if (runtime && mask && value==0) CHECK(!(status&0xff00));
        CHECK(value==0 || value==1 || value==2); mask=value&1; mask_writes++;
        if (value&2) {
            if (!swi_lost) status|=0x0400;
            if (irq0_first) {
                u16 old=status; int a=ack_writes;
                service_tick(); CHECK(status==old && ack_writes==a);
            }
            deliver_irq();
        }
    } else {
        CHECK(port==0x6002 && irq_depth && !command);
        CHECK(value==0x60 || value==0x06 || value==0x10 || value==1);
        if (value==0x60 || value==0x06) CHECK(mask==1);
        command=value; commands++; busy_reads=0;
        if (value==1) {
            CHECK(allocated && (pci_command&4) && !ru_base && !cu_base);
            CHECK((status&0x3c)!=0x10); model_assert_ring(pointer);
            CHECK(last_barrier>last_store);
            ru_starts++; ru_pointer=pointer; status=(status&~0x3c)|0x10;
            last_start_reset=resets;
        }
        if (value==0x10) {
            cb_pre_pending=0;
            CHECK(allocated && (pci_command&4) && !cu_base && !ru_base);
            CHECK(last_barrier>last_store);
            starts++; last_start_reset=resets;
            CHECK(pointer==0x2e8000u || pointer==0x2e8040u);
            active_cb=memory+pointer-0x2e8000u;
            CHECK(!raw16(pointer-0x2e8000u));
            int cmd=active_cb[2]&7;
            CHECK(cmd==1 || cmd==2 || cmd==4);
            CHECK(active_cb[3]==(runtime?0xa0:0x80));
            CHECK(raw32((u32)(active_cb-memory)+4)==0xffffffffu);
            if (cmd==2) {
                CHECK((status&0x3c)!=0x10);
                CHECK(starts==1 || starts==4);
                CHECK(strstr(output,"link="));
                const u8 cfg_bytes[]={0x16,8,0,0,0,0x80,0x32,3,1,0,0x2e,0,0x60,0,0xf2,0x48,0,0x40,0xf2,0x80,0x3f,5};
                for(int i=0;i<22;i++) CHECK(active_cb[8+i]==(i==10 && starts==1?0x6e:cfg_bytes[i]));
                loopback=active_cb[18]==0x6e;
            } else if (cmd==1) {
                const u8 mac[]={0,0,0x4c,7,0x21,0xe};
                CHECK(starts==2 || starts==5); CHECK(!memcmp(active_cb+8,mac,6));
            } else {
                CHECK(raw32(64+8)==0xffffffffu && (raw16(64+12)&0x8000));
                if (!runtime) {
                    CHECK(starts==3 && loopback && ru_starts==1);
                    const u8 header[]={255,255,255,255,255,255,0,0,0x4c,7,0x21,0xe,0x88,0xb5};
                    CHECK(raw16(64+12)==0x803c && raw16(64+14)==0x10);
                    CHECK(!memcmp(active_cb+16,header,14));
                    CHECK(!memcmp(active_cb+30,"OS32-LB-HELLO\0",14));
                }
            }
            cb_due=now+cb_delay; cb_completed=0;
        }
    }
    check_pending();
}
static __attribute__((unused)) void outpw(u16 port,u16 value) { (void)port; (void)value; CHECK(0); }
static void outpd(u16 port,u32 value)
{
    guard_io(port);
    if (port==0x6010) {
        CHECK(!irq_depth && resets==2 && !(value&0xf0000000u));
        u32 op=(value>>26)&3,addr=(value>>21)&31,reg=(value>>16)&31;
        CHECK(op==1 || op==2); mdi_ctl=value; mdi_reads=0;
        if (op==1) {
            CHECK(!starts && phyvendor && reg==0x17);
            CHECK(phy_regs[addr][2]==0x2000 && (phy_regs[addr][3]&0xfff0)==0x5c00);
            CHECK((u16)value==(u16)(phy_regs[addr][reg]|0x0562));
            CHECK(pcr_reads==1 && !pcr_writes && !bmsr_reads);
            pcr_writes++; phy_regs[addr][reg]=(u16)value;
        }
    } else if (port==0x6008) {
        CHECK(value==(resets?2u:0u)); if (resets) CHECK(now-reset_at>=20);
        if (inject_reset) { inject_reset=0; model_rx(rx_frame,60); }
        resets++; reset_at=now; mask=0;
        status=(idle_fail || (resets>2 && (stop_fail==2 || (stop_fail==1 && resets==3))))?0xA014:0xA000;
        if (resets==1) cu_base=ru_base=0x1000;
        command=0; active_cb=NULL;
        /* PORT may post CNA but SCB cannot be accessed until reset delay ends. */
    } else { CHECK(port==0x6004 && irq_depth && !command && !pre_busy); pointer=value; pointers++; }
}
int e82557_ee_read_word(const struct e82557_ee_pins *p,int mode,int *width,u16 addr,u16 *word,int *dummy)
{
    static const u16 words[]={0,0x074c,0x0e21};
    (void)p; CHECK(!irq_depth);
    if(mode==E82557_EE_AUTO) { CHECK(ee_mode && !auto_calls); auto_calls++; *width=8; return ee_mode==3?E82557_NOWIN:E82557_OK; }
    CHECK(mode==E82557_EE_FIXED && *width==(auto_calls?8:6));
    if (addr==6 || addr==3) {
        if (addr==3) compat_reads++;
        *word=addr==6?0x4a01:0x1233; *dummy=0; return E82557_OK;
    }
    CHECK(addr<3);
    if(ee_mode && !auto_calls) { ee_calls++; *dummy=1; *word=0xffff; return E82557_OK; }
    if(ee_mode==2) { ee_calls++; *dummy=0; *word=0; return E82557_OK; }
    CHECK(!cu_base && !ru_base && !command); ee_calls++; *word=words[addr%3]; *dummy=0; return E82557_OK;
}
#include "../../include/config.h"
#define kstrncmp strncmp
#define kstrcmp strcmp
#include "../../kernel/dma_pool.h"
static void desc_event(const volatile void *p,u32 n,int write);
static void barrier_event(void) { last_barrier=++event_seq; }
#define E82557_BARRIER() barrier_event()
#define E82557_DESC_ACCESS(p,n,w) desc_event(p,n,w)
#define E82557_HOST_YIELD() yield_event()
#include "../../drivers/e82557_desc.c"
u32 pci_cfg_read32(u32 bus,u32 dev,u32 fn,u32 reg)
{ CHECK(!bus && dev==11 && !fn && reg==4); return pci_command; }
int pci_cfg_write16(u32 bus,u32 dev,u32 fn,u32 reg,u16 value)
{
    CHECK(!bus && dev==11 && !fn && reg==4 && (value&~4u)==(pci_command&~4u));
    if(value&4) { CHECK(allocated && registered && !starts); if(bm_fail) return -1; }
    else CHECK(!starts || resets>last_start_reset || runtime);
    pci_command=value; bm_writes++; return 0;
}
int dma_alloc(u32 size,u32 align,u32 limit,struct dma_buf *out)
{
    CHECK(!irq_depth && !allocated && !command && !cu_base && !ru_base);
    CHECK(size==16384 && align==16 && limit==0xffffffffu);
    if(alloc_fail) return -4;
    allocated=1; memset(memory,0xcc,sizeof(memory));
    out->pa=0x2e8000; out->va=memory; out->size=size; return 0;
}
int dma_free(const struct dma_buf *b)
{
    CHECK(allocated && b->pa==0x2e8000 && b->va==memory && !freed && !leaked && !registered);
    CHECK(resets>last_start_reset && resets>=3 && now-reset_at>=20 && !(status&0xfc));
    CHECK(mask==1 && !(status&0xff00));
    CHECK(pci_command==0x0143 || (pci_command==0x0147 && !bm_writes)); freed++; return 0;
}
int dma_mark_leaked(const struct dma_buf *b)
{ CHECK(allocated && b->pa==0x2e8000 && !freed && !leaked && mask==1 && !(pci_command&4)); leaked++; return 0; }
int irq_register(unsigned int irq,irq_handler_fn fn,void *arg,unsigned int flags)
{ CHECK(!irq_depth && allocated && !bm_writes && irq==3 && !arg && flags==IRQ_F_SHARED); if(irq_fail) return -1; handler=fn; registered=1; return 0; }
int irq_unregister(unsigned int irq,irq_handler_fn fn,void *arg)
{ CHECK(!irq_depth && irq==3 && fn==handler && !arg && registered && !freed && resets>last_start_reset && mask==1); registered=0; unregistered++; return 0; }
static int kutoa_dec(u32 value,char *buf,u32 cap) { return snprintf(buf,cap,"%u",value); }
#define tick_count fake_ticks()
#define E82557_HOST_TEST
#include "../../drivers/e82557.c"
#include "pci_table.h"
static void check_pending(void)
{
    if (!mask && registered && (status&0xff00)) CHECK(nic.state==E82557_RUNNING);
}
static void deliver_irq(void)
{
    if (!irq_depth && registered && !mask && (status&0xff00)) {
        irq_depth++; irq_deliveries++; CHECK(handler(3, NULL)==IRQ_HANDLED); irq_depth--;
    }
}
static void service_tick(void)
{
    int cmd=commands; u32 start=now;
    irq_depth++; e82557_tick(); irq_depth--;
    CHECK(commands-cmd<=1 && now-start<=300);
}
static void model_assert_ring(u32 pa)
{
    CHECK(nic.rx.ready<8);
    CHECK(pa==0x2e8000u+1616+nic.rx.dev*1552);
    CHECK(nic.rx.dev==(nic.rx.cons+nic.rx.ready)%8);
    for(u32 n=0;n<8;n++) {
        u32 i=(nic.rx.cons+n)%8, off=1616+i*1552;
        CHECK(raw32(off+4)==0x2e8000u+1616+((i+1)%8)*1552);
        CHECK(raw32(off+8)==0xffffffffu && raw16(off+14)==1536);
        if(n<nic.rx.ready) CHECK(raw16(off)&0x8000);
        else CHECK(!raw16(off) && !raw16(off+12));
        /* Completed old EL can remain only while full, never when restarted. */
        CHECK(raw16(off+2)==(n==7?0x8000u:0u));
    }
}
static void model_rx(const u8 *frame,u32 len)
{
    CHECK((status&0x3c)==0x10 && !freed && !leaked);
    CHECK(ru_pointer>=0x2e8000u+1616 && ru_pointer<0x2e8000u+14032);
    u32 off=ru_pointer-0x2e8000u;
    CHECK(!raw16(off) && !raw16(off+12));
    u32 count=len>1536?1536:len;
    memcpy(memory+off+16,frame,count);
    put16(off+12,(u16)(0xc000|count));
    put16(off,len>1536?0x8200:0xa000);
    status|=0x4000;
    if ((raw16(off+2)&0x8000) || len>1536) status=(status&~0x3c)|0x1008;
    else ru_pointer=raw32(off+4);
}
static u32 zero_event[8], el_event, el_index;
static void desc_event(const volatile void *p,u32 n,int write)
{
    const volatile u8 *b=p;
    event_seq++;
    if(b<memory || b>=memory+sizeof(memory)) return;
    CHECK(allocated && !freed && !leaked);
    u32 off=(u32)(b-memory);
    if(off>=1616 && off<14032) {
        u32 i=(off-1616)/1552, word=(off-1616)%1552;
        if(write) {
            last_store=event_seq;
            if(word==12 && !raw16(off)) zero_event[i]=event_seq;
            if(word==2 && (raw16(off)&0x8000)) {
                CHECK(!raw16(1616+i*1552) && !raw16(1616+i*1552+12));
                CHECK(zero_event[i] && last_barrier>zero_event[i]);
                if(runtime && (nic.state==E82557_RUNNING || nic.state==E82557_RX_WAIT)) {
                    if(nic.rx.ready==8) CHECK(el_event && el_index==(i+7)%8 && last_barrier>el_event);
                    el_event=event_seq; el_index=i;
                }
            } else if(word==2 && !raw16(off) && runtime &&
                      (nic.state==E82557_RUNNING || nic.state==E82557_RX_WAIT)) {
                if(nic.rx.ready<8) CHECK(el_event && el_index==nic.rx.cons && last_barrier>el_event);
                el_event=event_seq; el_index=i;
            }
        } else if(word==0) {
            if(raw16(off)&0x8000) last_complete[i]=event_seq;
        } else if(word==12 || word==16) {
            CHECK(last_complete[i] && last_barrier>last_complete[i]);
        }
        return;
    }
    if(write) {
        cb_pre_pending=1;
        if(last_complete[8]) CHECK(last_barrier>last_complete[8]);
        last_store=event_seq;
    } else if(b==active_cb) {
        completion_reads++;
        if(b[1]&0x80) last_complete[8]=event_seq;
    } else { (void)n; CHECK(last_complete[8] && last_barrier>last_complete[8]); }
}
static void yield_event(void)
{
    int mode=yield_mode; yield_mode=0;
    if(!mode) return;
    if(mode==1 || mode==2 || mode==3) {
        CHECK(nic.tx==TX_RESERVED && nic.tx_issued==0);
        CHECK(e82557_send(rx_frame,60)==OS32_ERR_AGAIN);
        if(mode==2) { status&=~0x3c; ru_fail=ru_starts+1; }
        service_tick();
        CHECK(nic.tx==TX_RESERVED && nic.state==E82557_RUNNING);
        if(mode==2) { CHECK(nic.recover_pending); ru_fail=0; command=0; }
        if(mode==3) nic.gen++; /* fault injection, must prevent CU Start */
    } else if(mode==4) {
        u8 saved[60]; memcpy(saved,memory+1616+nic.rx.cons*1552+16,60);
        recover_begin(); service_tick(); service_tick(); service_tick();
        CHECK(!memcmp(saved,memory+1616+nic.rx.cons*1552+16,60));
    }
}
static void seed(int idx,const char *data)
{ struct file *f=&files[idx]; f->present=1; f->len=(int)strlen(data); memcpy(f->data,data,(size_t)f->len+1); }
static void runtime_tests(const char *c)
{
    u8 buf[1600], mac[6]; u32 len;
    runtime=1; model_ticks=100;
    for(u32 i=0;i<sizeof(rx_frame);i++) rx_frame[i]=(u8)(i*7+3);
    CHECK(e82557_state()==E82557_RUNNING && e82557_rx_free()==8);
    e82557_get_mac(mac); const u8 expected[]={0,0,0x4c,7,0x21,0xe}; CHECK(!memcmp(mac,expected,6));
    CHECK(e82557_loopback(1)==OS32_ERR_NOSYS && e82557_loopback(0)==OS32_ERR_NOSYS);
    CHECK(e82557_stat(12)==0 && e82557_stat(0xffffffffu)==0);
    (void)e82557_irq(0, NULL); nic.rnr_pending=0;
    if(!strncmp(c,"rx",2)) {
        if(!strcmp(c,"rx_empty")) {
            CHECK(e82557_recv(buf,sizeof(buf),&len)==OS32_ERR_AGAIN && !len);
        } else if(!strncmp(c,"rx_wrap",7)) {
            int count=atoi(c+7);
            for(int round=0;round<3;round++) {
                for(int n=0;n<count;n++) model_rx(rx_frame,60);
                for(int n=0;n<count;n++) {
                    CHECK(e82557_recv(buf,sizeof(buf),&len)==0 && len==60 && !memcmp(buf,rx_frame,60));
                    service_tick();
                }
            }
            CHECK(nic.stats[7]==(u32)(3*count) && e82557_rx_free()==8);
        } else if(!strcmp(c,"rx_full")) {
            for(int n=0;n<8;n++) model_rx(rx_frame,60);
            service_tick(); CHECK(nic.rx.ready==8 && nic.rx.dev==nic.rx.cons && ru_starts==2);
            CHECK(e82557_recv(buf,59,&len)==OS32_ERR_NOSPC && nic.rx.ready==8);
            CHECK(e82557_recv(buf,sizeof(buf),&len)==0 && nic.rx.ready==7);
            service_tick(); CHECK(ru_starts==3);
            model_rx(rx_frame,60); CHECK((status&0x3c)==8);
            service_tick(); CHECK(nic.rx.ready==8 && ru_starts==3);
        } else if(!strcmp(c,"rx_p1")) {
            for(int n=0;n<7;n++) model_rx(rx_frame,60);
            rx_scan(); CHECK(nic.rx.ready==7); inject_rus=1;
            service_tick(); CHECK(nic.rx.ready==8 && ru_starts==2);
        } else if(!strcmp(c,"rx_ready")) {
            nic.rnr_pending=1; service_tick(); CHECK(!nic.rnr_pending && ru_starts==2);
        } else if(!strcmp(c,"rx_idle")) {
            status=0; service_tick(); CHECK(ru_starts==3 && !nic.rnr_pending);
        } else if(!strcmp(c,"rx_copy_tick")) {
            model_rx(rx_frame,60); yield_mode=4;
            CHECK(e82557_recv(buf,sizeof(buf),&len)==0 && len==60 && !memcmp(buf,rx_frame,60));
            CHECK(nic.state==E82557_RUNNING && nic.rx.ready==0);
        } else {
            u32 length=!strcmp(c,"rx_short")?13:!strcmp(c,"rx_long")?1519:!strcmp(c,"rx_overrun")?1600:60;
            u32 off=ru_pointer-0x2e8000u;
            model_rx(rx_frame,length);
            if(!strcmp(c,"rx_bad")) put16(off,0x8000);
            rx_scan();
            if(length>1536) { CHECK(nic.rnr_pending); status&=~0xff00; }
            CHECK(e82557_recv(buf,sizeof(buf),&len)==OS32_ERR_AGAIN && !len);
            CHECK(nic.stats[8]==1 && nic.rx.ready==0);
        }
    } else if(!strncmp(c,"irq_",4)) {
        for(int i=0;i<5;i++) nic.stats[i]=0;
        nic.rnr_pending=0;
        u16 event=0;
        if(!strcmp(c,"irq_cx")) event=0x8000;
        if(!strcmp(c,"irq_cna")) event=0x2000;
        if(!strcmp(c,"irq_fr")) event=0x4000;
        if(!strcmp(c,"irq_rnr")) event=0x1000;
        if(!strcmp(c,"irq_swi")) event=0x0400;
        if(!strcmp(c,"irq_mdi")) event=0x0800;
        if(!strcmp(c,"irq_all")) event=0xfc00;
        if(!strcmp(c,"irq_dead")) event=0xff00;
        if(!strcmp(c,"irq_again")) { event=0x8000; ack_repeat=1; }
        if(!strcmp(c,"irq_limit")) { event=0x8000; ack_repeat=2; }
        if(!strcmp(c,"irq_starting")) { event=0xfc00; nic.state=E82557_STARTING; }
        if(!strcmp(c,"irq_stopping")) { event=0xfc00; nic.state=E82557_STOPPING; }
        if(!strcmp(c,"irq_recover")) { event=0xfc00; nic.state=E82557_RECOVER; }
        if(!strcmp(c,"irq_failed")) { event=0xfc00; nic.state=E82557_FAILED; }
        status=event; int before=io_count,a=ack_writes,r=status_reads;
        irq_depth++; int rc=e82557_irq(0, NULL); irq_depth--;
        if(nic.state==E82557_STARTING || nic.state==E82557_STOPPING) { service_tick(); CHECK(rc==IRQ_NONE && io_count==before); }
        else if(event==0xff00) { CHECK(rc==IRQ_NONE && nic.stats[4]==1 && ack_writes==a); CHECK(!nic.stats[0]&&!nic.stats[1]&&!nic.stats[2]&&!nic.stats[3]); }
        else {
            CHECK(rc==(event?IRQ_HANDLED:IRQ_NONE));
            CHECK(status_reads-r<=2);
            CHECK(nic.stats[0]==!!(event&0xa000)); CHECK(nic.stats[2]==!!(event&0x1000));
            CHECK(nic.stats[3]==!!(event&0x400)); CHECK(nic.stats[4]==!!(event&0x800));
            if(!strcmp(c,"irq_again") || !strcmp(c,"irq_limit")) CHECK(nic.stats[1]==1 && ack_writes-a==2);
            else CHECK(nic.stats[1]==!!(event&0x4000));
            if(!strcmp(c,"irq_limit")) CHECK(status==0x4000); else CHECK(!status);
            CHECK(nic.rnr_pending==(nic.state==E82557_RUNNING && !!(event&0x1000)));
        }
    } else if(!strncmp(c,"tx_",3) || !strncmp(c,"recover_",8)) {
        if(!strcmp(c,"tx_reserved")) yield_mode=1;
        if(!strcmp(c,"tx_pending")) yield_mode=2;
        if(!strcmp(c,"tx_generation")) yield_mode=3;
        int initial=starts;
        if(!strcmp(c,"tx_pre")) cu_pre=starts+1;
        if(!strcmp(c,"tx_post")) cu_post=starts+1;
        int rc=e82557_send(rx_frame,60);
        if(cu_pre || cu_post) {
            CHECK(rc==OS32_ERR_AGAIN && nic.recover_pending && nic.tx==TX_IDLE);
            CHECK(e82557_send(rx_frame,60)==OS32_ERR_AGAIN);
            service_tick(); CHECK(nic.state==E82557_RECOVER && !nic.recover_pending);
            return;
        }
        if(!strcmp(c,"tx_generation")) { CHECK(rc==OS32_ERR_AGAIN && starts==initial); return; }
        CHECK(rc==0 && nic.tx==TX_ISSUED && starts==initial+1);
        if(!strcmp(c,"tx_done") || !strcmp(c,"tx_bad") || !strcmp(c,"tx_reserved")) {
            put16(64,!strcmp(c,"tx_bad")?0x8000:0xa000); service_tick();
            CHECK(nic.tx==TX_IDLE && nic.state==E82557_RUNNING);
            CHECK(nic.stats[5]==(strcmp(c,"tx_bad")!=0) && nic.stats[6]==!strcmp(c,"tx_bad"));
            CHECK(!nic.stats[9]); yield_mode=1;
            CHECK(e82557_send(rx_frame,60)==0 && nic.tx==TX_ISSUED);
        } else if(!strcmp(c,"tx_pending")) {
            service_tick(); CHECK(nic.state==E82557_RECOVER && !nic.recover_pending);
        } else {
            cb_fail=starts;
            if(!strcmp(c,"recover_full")) for(int n=0;n<8;n++) model_rx(rx_frame,60);
            if(!strcmp(c,"recover_final")) inject_reset=1;
            model_ticks=150; service_tick(); CHECK(nic.state==E82557_RUNNING);
            model_ticks=151; service_tick(); CHECK(nic.state==E82557_RECOVER && nic.recover_step==1);
            CHECK(e82557_send(rx_frame,60)==OS32_ERR_AGAIN && e82557_recv(buf,sizeof(buf),&len)==OS32_ERR_AGAIN);
            if(!strcmp(c,"recover_stopfail")) stop_fail=2;
            if(!strcmp(c,"recover_r3fail")) ru_fail=ru_starts+1;
            service_tick();
            if(stop_fail==2) { CHECK(nic.state==E82557_RECOVER); service_tick(); CHECK(nic.state==E82557_FAILED && leaked==1); CHECK(e82557_recv(buf,sizeof(buf),&len)==OS32_ERR_NOSYS); return; }
            CHECK(nic.recover_step==2);
            if(!strcmp(c,"recover_final")) CHECK(nic.rx.ready==1 && (raw16(1616+1552)&0x8000));
            service_tick(); CHECK(nic.recover_step==3 && nic.tx==TX_IDLE && nic.stats[6]==1);
            status|=0xa000; /* Pending before R3 must be drained before M=0. */
            service_tick();
            if(!strcmp(c,"recover_full")) {
                CHECK(nic.state==E82557_RX_WAIT && nic.rx.ready==8);
                CHECK(e82557_send(rx_frame,60)==OS32_ERR_AGAIN);
                CHECK(e82557_recv(buf,sizeof(buf),&len)==0 && len==60 && nic.rx.ready==7);
                service_tick(); CHECK(nic.state==E82557_RUNNING && nic.stats[10]==1);
            } else if(ru_fail) {
                for(int i=0;i<10 && nic.state!=E82557_FAILED;i++) service_tick();
                CHECK(nic.state==E82557_FAILED && leaked==1 && nic.stop.reset_seq-nic.reset_begin==3);
            } else CHECK(nic.state==E82557_RUNNING && nic.stats[10]==1);
        }
    } else if(!strcmp(c,"ru_timeout")) {
        status=0; ru_fail=ru_starts+1; service_tick();
        CHECK(nic.state==E82557_RECOVER && nic.stats[11]);
    } else if(!strcmp(c,"recovery_limit")) {
        for(int i=0;i<11;i++) {
            recover_begin();
            if(i<10) { service_tick(); service_tick(); service_tick(); CHECK(nic.state==E82557_RUNNING); }
        }
        CHECK(nic.state==E82557_FAILED && nic.stats[9]==11 && leaked==1);
    } else if(!strcmp(c,"api_invalid")) {
        CHECK(e82557_send(NULL,60)==OS32_ERR_INVAL && e82557_send(buf,13)==OS32_ERR_INVAL && e82557_send(buf,1519)==OS32_ERR_INVAL);
        CHECK(e82557_recv(NULL,60,&len)==OS32_ERR_INVAL && e82557_recv(buf,60,NULL)==OS32_ERR_INVAL);
    }
}
int main(int argc,char **argv)
{
    const char *c=argc>1?argv[1]:"pass"; int rc,skip=0;
    struct pci_dev dev={0}; dev.vendor=0x8086; dev.device=0x1229;
    dev.class=PCI_CLASS_NETWORK; dev.subclass=PCI_SUB_NET_ETHERNET;
    dev.dev=11; dev.bar[1]=0x6001; dev.command=1; dev.irq_line=3;
    kdebug.ops.nic_trace=trace;
    memset(phy_regs,0xff,sizeof(phy_regs));
    const u16 defaults[]={0x3000,0x782d,0x2000,0x5c01};
    memcpy(phy_regs[1],defaults,sizeof(defaults));
    phy_regs[1][0x17]=0x9014; phy_regs[1][0x19]=0x81;
    e82557_tick(); CHECK(!io_count); /* NP21/W: no probe, no I/O */
    if(!strcmp(c,"nsc_rev")) phy_regs[1][3]=0x5c0f;
    if(!strcmp(c,"nsc_id1")) phy_regs[1][2]=0x2001;
    if(!strcmp(c,"nsc_id2")) phy_regs[1][3]=0x5c11;
    if(!strcmp(c,"nsc_same")) phy_regs[1][0x17]|=0x0562;
    if(!strcmp(c,"nsc_off")) phyvendor=0;
    if(!strcmp(c,"nsc_reserved")) pcr_reserved=1;
    if(!strncmp(c,"nsc_drop",8)) pcr_drop=1<<atoi(c+8);
    if(!strcmp(c,"nsc_timeout_pcr")) vendor_timeout=0x17;
    if(!strcmp(c,"nsc_timeout_par")) vendor_timeout=0x19;
    if(!strcmp(c,"nsc_timeout_read")) pcr_timeout_first=1;
    if(!strcmp(c,"nsc_timeout_write")) pcr_timeout_write=1;
    if(!strcmp(c,"phy_latch")) latch_zero=1;
    if(!strcmp(c,"phy_late")) link_tick=50;
    if(!strcmp(c,"phy_down")) link_tick=-1;
    if(!strcmp(c,"phy_off")) { an_off=1; phy_regs[1][0]=0x2100; }
    if(!strcmp(c,"phy_none")) phy_regs[1][2]=0xffff;
    if(!strcmp(c,"phy_timeout")) mdi_timeout=1;
    if(!strcmp(c,"kill")) { cfg=0; skip=1; }
    else if(!strcmp(c,"mkdir")) { mkdir_fail=1; skip=1; }
    else if(!strcmp(c,"same")) { seed(0,"1234abcd 7\n"); skip=1; }
    else if(!strcmp(c,"other")) { seed(0,"deadbeef 7\n"); seed(1,"7\n"); }
    else if(!strcmp(c,"seq_missing_old")) seed(0,"deadbeef 7\n");
    else if(!strcmp(c,"seq_ahead")) { seed(0,"deadbeef 7\n"); seed(1,"12\n"); }
    else if(!strcmp(c,"monotonic")) seed(1,"41\n");
    else if(!strcmp(c,"try_read")) { read_fail=1; skip=1; }
    else if(!strcmp(c,"seq_read")) { read_fail=2; skip=1; }
    else if(!strcmp(c,"try_write")) { write_fail=1; skip=1; }
    else if(!strcmp(c,"seq_write")) { write_fail=2; skip=1; }
    else if(!strcmp(c,"try_short")) { short_write=1; skip=1; }
    else if(!strcmp(c,"seq_short")) { short_write=2; skip=1; }
    else if(!strcmp(c,"try_back")) { corrupt_read=1; skip=1; }
    else if(!strcmp(c,"seq_back")) { corrupt_read=2; skip=1; }
    else if(!strcmp(c,"sync")) { sync_fail=1; skip=1; }
    else if(!strcmp(c,"write_zero")) write_zero=1;
    else if(!strcmp(c,"remove")) remove_fail=1;
    else if(!strcmp(c,"crc_missing")) { bad_crc=1; skip=1; }
    else if(!strcmp(c,"bad_bar")) { dev.bar[1]=0; skip=1; }
    else if(!strcmp(c,"io_disabled")) { dev.command=0; skip=1; }
    else if(!strcmp(c,"reset_fail")) idle_fail=1;
    else if(!strcmp(c,"ee_fallback")) ee_mode=1;
    else if(!strcmp(c,"ee_badmac")) ee_mode=2;
    else if(!strcmp(c,"ee_detect_fail")) ee_mode=3;
    else if(!strcmp(c,"pre_wait")) pre_busy=1;
    else if(!strcmp(c,"post_wait")) post_busy=1;
    else if(!strcmp(c,"ru_post_wait")) post_busy=2;
    else if(!strcmp(c,"no_trace")) kdebug.ops.nic_trace=NULL;
    else if(!strncmp(c,"bad_try",7)) {
        const char *bad[]={"", "1234abcd", "1234abcd 1", "1234abcd 1\nX", "1234abcd -1\n", "1234abcd 4294967296\n", "gggggggg 1\n", "1234abcd 0\n", "1234abcd 1\n\n"};
        seed(0,bad[atoi(c+7)]); skip=1;
    } else if(!strncmp(c,"bad_seq",7)) {
        const char *bad[]={"", "1", "-1\n", "4294967296\n", "1\nx", "1\n\n", "4294967295\n"};
        seed(1,bad[atoi(c+7)]); skip=1;
    }

    if(!strcmp(c,"dma_fail")) alloc_fail=1;
    if(!strcmp(c,"bm_fail")) bm_fail=1;
    if(!strcmp(c,"bm_present") || !strcmp(c,"bm_present_ng")) pci_command=0x0147;
    if(!strcmp(c,"bm_present_ng")) cb_ng=1;
    if(!strcmp(c,"cb_edge")) cb_delay=20000;
    if(!strcmp(c,"cb_late")) cb_delay=21000;
    if(!strcmp(c,"stop_retry")) stop_fail=1;
    if(!strcmp(c,"stop_fail")) stop_fail=2;
    if(!strcmp(c,"probe_irq_fail")) irq_fail=1;
    if(!strncmp(c,"cb_timeout",10)) cb_fail=atoi(c+10);
    if(!strncmp(c,"cb_ng",5)) cb_ng=atoi(c+5);
    if(!strncmp(c,"cu_pre",6)) cu_pre=atoi(c+6);
    if(!strncmp(c,"cu_post",7)) cu_post=atoi(c+7);
    if(!strcmp(c,"probe_ring_fail")) ru_fail=1;
    if(!strcmp(c,"probe_ru_fail")) ru_fail=2;
    if(!strcmp(c,"lb_timeout")) lb_timeout=1;
    if(!strcmp(c,"lb_corrupt")) lb_corrupt=1;
    if(!strcmp(c,"swi_lost")) swi_lost=1;
    if(!strcmp(c,"swi_irq0")) irq0_first=1;
    u16 initial_command=pci_command;
    int expect_fail=skip || idle_fail || pre_busy || post_busy || ee_mode>=2 || alloc_fail || bm_fail ||
        cb_fail || cb_ng || cu_pre || cu_post || cb_delay>20000 || stop_fail==2 || irq_fail || ru_fail ||
        lb_timeout || lb_corrupt || mdi_timeout || vendor_timeout || pcr_timeout_first || pcr_timeout_write ||
        !strcmp(c,"phy_none");
    if(!strcmp(c,"table")) {
        struct pci_bind_info info;
        rc=pci_bind_one(pci_drivers,PCI_DRIVER_COUNT,&dev,&info);
        CHECK(info.result==PCI_BIND_BOUND && info.reason==PCI_BIND_OK);
    } else rc=e82557_driver.probe(&dev);
    CHECK(!irq_depth);
    CHECK(rc==(idle_fail || stop_fail==2?PCI_PROBE_QUARANTINE:expect_fail?PCI_PROBE_DECLINE:PCI_PROBE_OK));
    if(skip) { CHECK(!io_count && !ee_calls && !strstr(output,"LB3=PASS")); }
    else {
        u32 limit=70000;
        if(!strcmp(c,"phy_down")) limit=3100000;
        if(!strcmp(c,"phy_late")) limit=600000;
        if(!strcmp(c,"cb_edge")) limit=110000;
        if(!strcmp(c,"lb_timeout")) limit=80000;
        CHECK(now>=40 && now<limit);
        if(!expect_fail) {
            CHECK(nic.state==E82557_RUNNING && starts==5 && ru_starts==2 && registered && !freed && !leaked && !mask);
            CHECK(compat_reads==1 && strstr(output,"compat=1233"));
            CHECK(strstr(output,"lb=ok len=60 cmp=ok rst2=ok cfg2=ok ia2=ok ru=ok"));
            CHECK(swi_lost ? strstr(output,"LB3=FAIL(swi:irq)")!=NULL : strstr(output,"LB3=PASS")!=NULL);
            CHECK(swi_lost ? nic.stats[3]==0 : nic.stats[3]==1 && irq_deliveries>0);
            CHECK((trace_bits&(1u<<E82557_TRACE_TX))==0 && (trace_bits&(1u<<E82557_TRACE_STAT))==0);
            if(kdebug.ops.nic_trace) CHECK(trace_bits&(1u<<E82557_TRACE_IA2));
            if(phy_regs[1][2]!=0x2000 || (phy_regs[1][3]&0xfff0)!=0x5c00) {
                CHECK(!pcr_reads && !pcr_writes && strstr(output,"pcr=skip") && strstr(output,"par=-"));
            } else {
                CHECK(pcr_reads==(pcr_writes?2:1));
                CHECK(pcr_writes==(phyvendor && strcmp(c,"nsc_same")!=0));
                CHECK(vendor_reads[0x19]==1);
                CHECK(strstr(output,pcr_drop?"pcr=ng":phyvendor?(!strcmp(c,"nsc_same")?"pcr=9576->9576":"pcr=9014->9576"):"pcr=off"));
            }
            CHECK(bmsr_reads>=2 && !(bmsr_reads&1));
            if(latch_zero) CHECK(phy_regs[1][1]==0x782d && strstr(output,"link=up"));
            if(link_tick<0) CHECK(now>=3000000 && strstr(output,"link=down"));
            if(link_tick>0) CHECK(now>=500000 && strstr(output,"link=up"));
            if(an_off) CHECK(strstr(output,"link=up an=no"));
        } else {
            CHECK(strstr(output,"LB3=FAIL") && !strstr(output,"LB3=PASS"));
            if(cb_ng==3) CHECK(strstr(output,"FAIL(lb:txng)"));
            if(cb_fail==3) CHECK(strstr(output,"FAIL(lb:timeout)"));
            if(cu_pre==3) CHECK(strstr(output,"FAIL(lb:pre)"));
            if(cu_post==3) CHECK(strstr(output,"FAIL(lb:post)"));
            if(pre_busy) CHECK(strstr(output,"base=timeout scb=2040"));
            if(post_busy) CHECK(strstr(output,"base=timeout scb=0040"));
            if(lb_timeout) CHECK(strstr(output,"FAIL(lb:timeout)"));
            if(lb_corrupt) CHECK(strstr(output,"FAIL(cmp:ng)"));
            if(allocated) CHECK(stop_fail==2?leaked==1 && !freed:freed==1 && !leaked);
            if(freed && !irq_fail) CHECK(unregistered==1);
            if(freed) {
                CHECK(pci_command==initial_command);
                if(initial_command&4) CHECK(bm_writes==0);
            }
            if(pcr_timeout_first || pcr_timeout_write || vendor_timeout || mdi_timeout)
                CHECK(strstr(output,"pcr=- mdi=timeout") && strstr(output,"par=-"));
        }
        for(int i=0x12;i<32;i++) if(i!=0x17 && i!=0x19) CHECK(!vendor_reads[i]);
    }
    if(!strcmp(c,"same")) CHECK(files[0].present && !rm_calls && !files[0].writes && !files[1].writes);
    if(!cfg) CHECK(!fs_calls);
    if(read_fail) CHECK(!files[0].writes && !files[1].writes);
    if(!skip) {
        CHECK(files[0].present==remove_fail);
        if(!strcmp(c,"monotonic")) CHECK(!strcmp(files[1].data,"42\n"));
        else if(!strcmp(c,"other") || !strcmp(c,"seq_missing_old")) CHECK(!strcmp(files[1].data,"8\n"));
        else if(!strcmp(c,"seq_ahead")) CHECK(!strcmp(files[1].data,"13\n"));
        else CHECK(!strcmp(files[1].data,"1\n"));
    }
    if(rc==PCI_PROBE_OK && !swi_lost) runtime_tests(c);
    if(!strcmp(c,"pass")) fprintf(1,"%s",output);
    return !!failed;
}
