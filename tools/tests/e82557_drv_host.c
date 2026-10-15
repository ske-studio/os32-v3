/* Real LB1 driver with persistent VFS and byte-accurate SCB model.
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
static int idle_fail, pre_busy, post_busy, busy_reads, reason, operations;
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
{ CHECK(!irq_depth); fs_calls++; CHECK(!strcmp(path,"/var/log/lan.try")); if(remove_fail) return VFS_ERR_IO; files[0].present=0; return 0; }
static void kprintf(int attr, const char *fmt, ...)
{ va_list ap; size_t n=strlen(output); (void)attr; va_start(ap,fmt); vsnprintf(output+n,sizeof(output)-n,fmt,ap); va_end(ap); }
void pci_bind_set_reason(u8 r) { reason=r; }
static u32 irq_save(void) { CHECK(!irq_depth); irq_depth++; return 0x200; }
static void irq_restore(u32 flags) { CHECK(irq_depth==1 && flags==0x200); irq_depth--; }
static void trace(u32 stage) { CHECK(!irq_depth); CHECK(stage!=0); trace_count++; }
struct kdebug kdebug;
static void guard_io(u16 port)
{
    if (++operations>1000) { fprintf(stderr,"FAIL unbounded SCB wait\n"); exit(21); }
    io_count++; CHECK(port>=0x6000 && port<0x6020);
    CHECK(files[0].verified && files[1].verified);
    if (port!=0x6008) CHECK(resets==2 && now-reset_at>=20);
}
static void cpu_delay_us(u32 us) { now+=us; }
static u8 inp(u16 port)
{
    guard_io(port); CHECK(port==0x6002);
    if (pre_busy && !commands) return 1;
    if (command && !(post_busy && commands>=post_busy) && ++busy_reads>=3) {
        if(command==0x60) cu_base=pointer;
        if(command==0x06) ru_base=pointer;
        command=0;
    }
    return command;
}
static u16 inpw(u16 port) { guard_io(port); CHECK(port==0x6000); return status; }
static void outp(u16 port, u8 value)
{
    guard_io(port);
    if(port==0x6001) { CHECK(value==0xA0); ack_writes++; status&=(u16)~((u16)value<<8); }
    else if(port==0x6003) { mask=value; mask_writes++; CHECK(value==1); }
    else { CHECK(port==0x6002 && irq_depth); CHECK(!command); CHECK(mask==1);
        command=value; commands++; busy_reads=0; CHECK(value==0x60 || value==0x06); }
}
static __attribute__((unused)) void outpw(u16 port, u16 value)
{
    guard_io(port); CHECK(port==0x6000); CHECK(value==0xA000); ack_writes++; status&=(u16)~value;
}
static void outpd(u16 port, u32 value)
{
    guard_io(port);
    if(port==0x6008) { CHECK(value==(resets?2u:0u)); if(resets) CHECK(now-reset_at>=20);
        resets++; reset_at=now; status=idle_fail ? 0xA014 : 0xA000; cu_base=ru_base=0x1000; }
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
static int kutoa_dec(u32 value, char *buf, u32 cap) { return snprintf(buf,cap,"%u",value); }
#define E82557_HOST_TEST
#include "../../drivers/e82557.c"
static void seed(int idx, const char *data)
{ struct file *f=&files[idx]; f->present=1; f->len=(int)strlen(data); memcpy(f->data,data,(size_t)f->len+1); }
int main(int argc, char **argv)
{
    struct pci_dev dev; const char *c=argc>1?argv[1]:"pass"; int rc, skip=0;
    memset(&dev,0,sizeof(dev)); dev.vendor=0x8086; dev.device=0x1229;
    dev.class=PCI_CLASS_NETWORK; dev.subclass=PCI_SUB_NET_ETHERNET;
    dev.bar[1]=0x6001; dev.command=1; dev.irq_line=3;
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
    rc=e82557_driver.probe(&dev);
    CHECK(!irq_depth);
    CHECK(rc==(idle_fail?PCI_PROBE_QUARANTINE:PCI_PROBE_DECLINE));
    if(skip) { CHECK(!io_count && !ee_calls); CHECK(!strstr(output,"LB1=PASS")); }
    else {
        CHECK(resets==2 && mask_writes==1 && ack_writes==1);
        CHECK(now>=40 && now<=240);
        if(idle_fail) { CHECK(reason==PCI_BIND_RESET_FAILED); CHECK(!commands && !ee_calls); }
        else if(pre_busy) { CHECK(!pointers && !commands && !ee_calls); CHECK(strstr(output,"base=timeout")); CHECK(now==140); }
        else if(post_busy) { CHECK(pointers==post_busy && commands==post_busy && !ee_calls); CHECK(strstr(output,"base=timeout")); CHECK(now==(post_busy==1?140u:160u)); }
        else if(ee_mode>=2) {
            CHECK(auto_calls==1 && ee_calls==(ee_mode==2?4:1));
            CHECK(strstr(output,"LB1=FAIL(ee:mac)")); CHECK(!files[0].present);
        } else {
            CHECK(cu_base==0 && ru_base==0 && !command); CHECK(commands==2 && pointers==2 && ee_calls==(ee_mode?4:3));
            CHECK(strstr(output,"LB1=PASS") && strstr(output,"mac=00:00:4c:07:21:0e"));
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
    if(!cfg) CHECK(!fs_calls);
    if(read_fail) CHECK(!files[0].writes && !files[1].writes);
    return !!failed;
}
