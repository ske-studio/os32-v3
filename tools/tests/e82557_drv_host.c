/* Real LB2 driver with persistent VFS and byte-accurate SCB model.
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
static int ee_mode, auto_calls;
static int idle_fail, pre_busy, post_busy, busy_reads, operations;
static int alloc_fail, bm_fail, stop_fail, cb_fail, cb_ng, cu_pre, cu_post;
static int allocated, freed, leaked, bm_writes, starts, last_start_reset;
static int rm_calls, completion_reads, published, acquired, saw_complete;
static u32 event_seq, last_store, last_barrier, last_complete;
static u32 cb_due, cb_delay=3000;
static u16 pci_command = 0x0143;
static u8 memory[16384];
static u8 *active_cb;
static u32 trace_bits;
static u32 now, reset_at, cu_base=0x1000, ru_base=0x1000, pointer;
static u16 status=0xA000;
static u8 command, mask;
static char output[8192];
static struct file *file_for(const char *path)
{
    if (!strcmp(path,"/var/log/lan.try")) return &files[0];
    if (!strcmp(path,"/var/log/lan.seq")) return &files[1];
    CHECK(0); return &files[0];
}
int sysconfig_get_int(const char *path, const char *key, int def)
{ CHECK(!fs_calls && !io_count); CHECK(!strcmp(key,"E82557")); (void)path; (void)def; return cfg; }
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
static u32 irq_save(void) { CHECK(!irq_depth); irq_depth++; return 0x200; }
static void irq_restore(u32 flags) { CHECK(irq_depth==1 && flags==0x200); irq_depth--; }
static void trace(u32 stage) { CHECK(!irq_depth); CHECK(stage!=0); trace_count++; trace_bits |= 1u << stage; }
struct kdebug kdebug;
static void guard_io(u16 port)
{
    if (++operations>1000) { fprintf(stderr,"FAIL unbounded SCB wait\n"); exit(21); }
    io_count++; CHECK(port>=0x6000 && port<0x6020);
    CHECK(files[0].verified && files[1].verified);
    if (port!=0x6008) CHECK(resets>=2 && now-reset_at>=20);
}
static void cpu_delay_us(u32 us)
{
    now+=us;
    if (active_cb && now >= cb_due && cb_fail != starts) {
        active_cb[0]=0; active_cb[1]=(u8)(cb_ng == starts ? 0x80 : 0xa0);
    }
}
static u8 inp(u16 port)
{
    guard_io(port); CHECK(port==0x6002);
    if ((pre_busy && !commands) || (cu_pre && allocated && published && !command && commands == cu_pre + 1)) {
        status=0x2040; return 1;
    }
    if (command && !(post_busy && commands>=post_busy) &&
        !(cu_post && starts==cu_post) && ++busy_reads>=3) {
        if(command==0x60) cu_base=pointer;
        if(command==0x06) ru_base=pointer;
        command=0;
    }
    if (command) status=0x2040;
    return command;
}
static u16 inpw(u16 port) { guard_io(port); CHECK(port==0x6000); return status; }
static void outp(u16 port, u8 value)
{
    guard_io(port);
    if(port==0x6001) { CHECK(value==(u8)(status>>8)); ack_writes++; status&=(u16)~((u16)value<<8); }
    else if(port==0x6003) { mask=value; mask_writes++; CHECK(value==1); }
    else { CHECK(port==0x6002 && irq_depth); CHECK(!command); CHECK(mask==1);
        command=value; commands++; busy_reads=0; CHECK(value==0x60 || value==0x06 || value==0x10);
        if (value==0x10) {
            CHECK(allocated && (pci_command & 4) && !cu_base && !ru_base);
            CHECK(published && last_barrier > last_store); published=0;
            if (starts) CHECK(saw_complete && acquired);
            starts++; last_start_reset=resets;
            CHECK(pointer==0x2e8000u+(starts==3?64u:0u));
            CHECK(pointer>=0x2e8000u && pointer<0x2e8000u+sizeof(memory));
            active_cb=memory+pointer-0x2e8000u+cu_base;
            CHECK(active_cb[0]==0 && active_cb[1]==0);
            CHECK(active_cb[2]==(starts==1?2:starts==2?1:4));
            CHECK(active_cb[3]==0x80); /* EL only, no S/I/flexible mode */
            CHECK(active_cb[4]==0xff && active_cb[5]==0xff && active_cb[6]==0xff && active_cb[7]==0xff);
            if (starts==1) {
                const u8 cfg_bytes[]={0x16,8,0,0,0,0x80,0x32,3,1,0,0x2e,0,0x60,0,0xf2,0x48,0,0x40,0xf2,0x80,0x3f,5};
                CHECK(!memcmp(active_cb+8,cfg_bytes,22));
            } else if (starts==2) {
                const u8 mac[]={0,0,0x4c,7,0x21,0x0e};
                CHECK(!memcmp(active_cb+8,mac,6));
            } else {
                const u8 header[]={0xff,0xff,0xff,0xff,0xff,0xff,0,0,0x4c,7,0x21,0x0e,0x88,0xb5};
                CHECK(active_cb[8]==0xff && active_cb[9]==0xff && active_cb[10]==0xff && active_cb[11]==0xff);
                CHECK(active_cb[12]==60 && active_cb[13]==0x80 && active_cb[14]==0x10 && active_cb[15]==0);
                CHECK(!memcmp(active_cb+16,header,14));
                CHECK(!memcmp(active_cb+30,"OS32-LB-HELLO\0\0\0",16));
                CHECK(!memcmp(active_cb+46,header+6,6));
                for (int j=52;j<76;j++) CHECK(active_cb[j]==0);
            }
            saw_complete=acquired=0;
            cb_due=now+cb_delay; /* several initial CUS=Idle reads are not proof */
            status=0;
        } }
}
static __attribute__((unused)) void outpw(u16 port, u16 value)
{
    guard_io(port); CHECK(port==0x6000); CHECK(value==0xA000); ack_writes++; status&=(u16)~value;
}
static void outpd(u16 port, u32 value)
{
    guard_io(port);
    if(port==0x6008) { CHECK(value==(resets?2u:0u)); if(resets) CHECK(now-reset_at>=20);
        resets++; reset_at=now; mask=0;
        status=(idle_fail || (resets>2 && (stop_fail==2 || (stop_fail==1 && resets==3)))) ? 0xA014 : 0xA000;
        if(resets==1) cu_base=ru_base=0x1000;
        command=0; active_cb=NULL; }
    else { CHECK(port==0x6004 && irq_depth); CHECK(!command && !pre_busy); pointer=value; pointers++; }
}
int e82557_ee_read_word(const struct e82557_ee_pins *p, int mode, int *width, u16 addr, u16 *word, int *dummy)
{
    static const u16 words[]={0,0x074c,0x0e21};
    (void)p; CHECK(!irq_depth);
    if (mode==E82557_EE_AUTO) {
        CHECK(ee_mode && auto_calls==0); auto_calls++; *width=8;
        return ee_mode==3 ? E82557_NOWIN : E82557_OK;
    }
    CHECK(mode==E82557_EE_FIXED && *width==(auto_calls?8:6) && addr<3);
    if(ee_mode && !auto_calls) { ee_calls++; *dummy=1; *word=0xffff; return E82557_OK; }
    if(ee_mode==2) { ee_calls++; *dummy=0; *word=0; return E82557_OK; }
    CHECK(cu_base==0 && ru_base==0 && command==0); ee_calls++; *word=words[addr%3]; *dummy=0; return E82557_OK;
}
#include "../../include/config.h"
#define kstrncmp strncmp
#include "../../kernel/dma_pool.h"
static void barrier_event(void)
{
    last_barrier=++event_seq; published=1;
    if (saw_complete) { CHECK(last_barrier>last_complete); acquired=1; }
}
static void desc_event(const volatile void *p, u32 n, int write)
{
    const volatile u8 *b=p;
    event_seq++;
    if (b < memory || b >= memory+sizeof(memory)) return;
    CHECK(allocated && !freed && !leaked);
    if (write) { if (starts && saw_complete) CHECK(acquired); last_store=event_seq; published=0; }
    else if (b==active_cb) {
        completion_reads++;
        if (b[1]&0x80) { last_complete=event_seq; saw_complete=1; }
    } else {
        CHECK(n==2 && saw_complete && acquired && last_barrier>last_complete);
    }
}
#define E82557_BARRIER() barrier_event()
#define E82557_DESC_ACCESS(p, n, w) desc_event(p, n, w)
#include "../../drivers/e82557_desc.c"
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{ CHECK(!bus && dev==11 && !fn && reg==4); return pci_command; }
int pci_cfg_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value)
{
    CHECK(!bus && dev==11 && !fn && reg==4);
    CHECK((value&~4u)==(pci_command&~4u));
    if (value&4) { CHECK(allocated && !starts); if(bm_fail) return -1; }
    else CHECK(!starts || resets>last_start_reset);
    pci_command=value; bm_writes++; return 0;
}
int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)
{
    CHECK(!irq_depth && !allocated && !command && !cu_base && !ru_base);
    CHECK(size==16384 && align==16 && limit==0xffffffffu);
    if (alloc_fail) return -4;
    allocated=1; memset(memory,0xcc,sizeof(memory));
    out->pa=0x2e8000; out->va=memory; out->size=size; return 0;
}
int dma_free(const struct dma_buf *b)
{
    CHECK(allocated && b->pa==0x2e8000 && b->va==memory && !freed && !leaked);
    CHECK(resets>last_start_reset && resets>=3 && now-reset_at>=20 && !(status&0xfc));
    CHECK(mask==1 && !(status&0xff00));
    CHECK(pci_command==0x0143 || (pci_command==0x0147 && bm_writes==0));
    freed++; return 0;
}
int dma_mark_leaked(const struct dma_buf *b)
{
    CHECK(allocated && b->pa==0x2e8000 && !freed && !leaked);
    CHECK(resets==4 && mask==1 && !(status&0xff00) && !(pci_command&4));
    leaked++; return 0;
}
static int kutoa_dec(u32 value, char *buf, u32 cap) { return snprintf(buf,cap,"%u",value); }
#define E82557_HOST_TEST
#include "../../drivers/e82557.c"
#include "pci_table.h"
static void seed(int idx, const char *data)
{ struct file *f=&files[idx]; f->present=1; f->len=(int)strlen(data); memcpy(f->data,data,(size_t)f->len+1); }
int main(int argc, char **argv)
{
    struct pci_dev dev; const char *c=argc>1?argv[1]:"pass"; int rc, skip=0;
    memset(&dev,0,sizeof(dev)); dev.vendor=0x8086; dev.device=0x1229;
    dev.class=PCI_CLASS_NETWORK; dev.subclass=PCI_SUB_NET_ETHERNET;
    dev.dev=11; dev.bar[1]=0x6001; dev.command=1; dev.irq_line=3;
    kdebug.ops.nic_trace=trace;
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
    if(!strcmp(c,"cb_late")) { cb_delay=21000; cb_fail=1; }
    if(!strcmp(c,"stop_retry")) stop_fail=1;
    if(!strcmp(c,"stop_fail") || !strcmp(c,"stop_bm_present")) stop_fail=2;
    if(!strcmp(c,"stop_bm_present")) pci_command=0x0147;
    if(!strncmp(c,"cb_timeout",10)) cb_fail=atoi(c+10);
    if(!strncmp(c,"cb_ng",5)) cb_ng=atoi(c+5);
    if(!strncmp(c,"cu_pre",6)) cu_pre=atoi(c+6);
    if(!strncmp(c,"cu_post",7)) cu_post=atoi(c+7);
    if(!strcmp(c,"table")) {
        struct pci_bind_info info;
        rc=pci_bind_one(pci_drivers, PCI_DRIVER_COUNT, &dev, &info);
        CHECK(info.result==PCI_BIND_DECLINED && info.reason==PCI_BIND_DECLINED_UNSPECIFIED);
        CHECK(io_count && starts==3);
    } else rc=e82557_driver.probe(&dev);
    CHECK(!irq_depth);
    CHECK(rc==((idle_fail || stop_fail==2)?PCI_PROBE_QUARANTINE:PCI_PROBE_DECLINE));
    if(skip) { CHECK(!io_count && !ee_calls); CHECK(!strstr(output,"LB2=PASS")); }
    else {
        CHECK(resets==(allocated ? (stop_fail ? 4 : 3) : 2));
        CHECK(mask==1);
        CHECK(mask_writes==(allocated?2:1) && ack_writes==(allocated?2:1));
        CHECK(now>=40 && now<70000);
        if(idle_fail) { CHECK(pci_bind_reason_get()==PCI_BIND_RESET_FAILED); CHECK(!commands && !ee_calls); }
        else if(pre_busy) { CHECK(!pointers && !commands && !ee_calls); CHECK(strstr(output,"base=timeout scb=2040")); CHECK(now==140); }
        else if(post_busy) { CHECK(pointers==post_busy && commands==post_busy && !ee_calls); CHECK(strstr(output,"base=timeout scb=2040")); CHECK(now==(post_busy==1?140u:160u)); }
        else if(ee_mode>=2) {
            CHECK(auto_calls==1 && ee_calls==(ee_mode==2?4:1));
            CHECK(strstr(output,"LB2=FAIL(ee:mac)")); CHECK(!files[0].present);
        } else {
            CHECK(cu_base==0 && ru_base==0 && !command); CHECK(ee_calls==(ee_mode?4:3));
            if (alloc_fail || bm_fail || cb_fail || cb_ng || cu_pre || cu_post || stop_fail==2)
                CHECK(strstr(output,"LB2=FAIL") && !strstr(output,"LB2=PASS"));
            else CHECK(strstr(output,"LB2=PASS") && strstr(output,"cfg=ok ia=ok tx=ok(60) stop=ok"));
            if (alloc_fail) CHECK(!allocated && !starts && !bm_writes && strstr(output,"FAIL(dma:alloc)"));
            else {
                CHECK(allocated && (stop_fail==2 ? leaked==1 && !freed : freed==1 && !leaked));
                CHECK(bm_writes==(!strcmp(c,"bm_present") || !strcmp(c,"bm_present_ng") || bm_fail ? 0 : !strcmp(c,"stop_bm_present") ? 1 : 2));
                if (bm_fail) CHECK(!starts && strstr(output,"FAIL(dma:bm)"));
                else {
                    int fail_at=cb_fail?cb_fail:cb_ng?cb_ng:cu_pre?cu_pre:cu_post?cu_post:3;
                    CHECK(starts==fail_at-(cu_pre?1:0));
                    CHECK(commands==2+starts && pointers==commands);
                    if (cb_fail) CHECK(strstr(output,":timeout)") && completion_reads>=21);
                    if (cb_ng) CHECK(strstr(output,":ng)"));
                    if (cu_pre) CHECK(strstr(output,":pre)"));
                    if (cu_post) CHECK(strstr(output,":post)"));
                    if (stop_fail==2) CHECK(strstr(output,"FAIL(stop:idle)") && strstr(output,"QUARANTINE"));
                }
            }
            if (!alloc_fail && !bm_fail && !cb_fail && !cb_ng && !cu_pre && !cu_post && kdebug.ops.nic_trace)
                CHECK((trace_bits & ((1u<<E82557_TRACE_DMA)|(1u<<E82557_TRACE_CFG)|(1u<<E82557_TRACE_IA)|(1u<<E82557_TRACE_TX)|(1u<<E82557_TRACE_STOP))) ==
                      ((1u<<E82557_TRACE_DMA)|(1u<<E82557_TRACE_CFG)|(1u<<E82557_TRACE_IA)|(1u<<E82557_TRACE_TX)|(1u<<E82557_TRACE_STOP)));
            CHECK(!strcmp(c,"no_trace") ? trace_count==0 : trace_count>0);
            if(!strcmp(c,"monotonic")) CHECK(!strcmp(files[1].data,"42\n"));
            else if(!strcmp(c,"other") || !strcmp(c,"seq_missing_old")) CHECK(!strcmp(files[1].data,"8\n"));
            else if(!strcmp(c,"seq_ahead")) CHECK(!strcmp(files[1].data,"13\n"));
            else CHECK(!strcmp(files[1].data,"1\n"));
            CHECK(files[0].present==remove_fail);
        }
    }
    if(idle_fail || pre_busy || post_busy || !strcmp(c,"bad_bar") || !strcmp(c,"io_disabled"))
        CHECK(!files[0].present);
    if(!strcmp(c,"same")) {
        CHECK(files[0].present && !strcmp(files[0].data,"1234abcd 7\n"));
        CHECK(!rm_calls && !files[0].writes && !files[1].writes);
    }
    if(!cfg) CHECK(!fs_calls);
    if(read_fail) CHECK(!files[0].writes && !files[1].writes);
    return !!failed;
}
