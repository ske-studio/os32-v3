/* Real engine, hwb READ, pair and aperture; fake broker/bus, libc-free i386. */
#define irq_save unused_irq_save
#define irq_restore unused_irq_restore
#include "io.h"
#undef irq_save
#undef irq_restore
#include "hwb_fake.h"
#include "debug_probe.h"
#include "kdebug/probe_wire.h"
#include "kdebug/kdebug_window.h"
#include "mm/paging.h"
#include "lib/kstring.h"
#include "drivers/char/serial.h"
#include "kprintf.h"
#define TEST_PAGE 0x12345000U
#define TEST_INDEX 0x20U
#define TEST_DATA 0x21U
#define OLD_INDEX 0xA7U
#define OLD_PTE 0x98765003U
static u32 fake_if=1, pte=OLD_PTE, stores, loads, port_stores, port_loads;
static u32 bus_step, fail_step, map_fail, unmap_fail, saved_index=OLD_INDEX, prints;
static bool reenter, no_owner, no_serial, renamed;
static const char *test_point_name;
static probe_hit_fn callback;
static u32 registry_epoch=1, attached_count, attached_ids[8];
static struct module_handle fake_owner={2,3};
static unsigned int irq_save(void) {u32 old=fake_if;fake_if=0;return old;}
static void irq_restore(unsigned int f) {fake_if=f;}
u32 paging_pte_raw(u32 va) {(void)va;CHECK(!fake_if);return pte;}
int paging_map_phys(u32 va,u32 pa,u32 n,u32 f)
{(void)va;CHECK(!fake_if && pa==TEST_PAGE && n==1);pte=pa|f;return map_fail?-1:0;}
int paging_pte_restore(u32 va,u32 raw)
{(void)va;CHECK(!fake_if);pte=raw;return unmap_fail?-1:0;}
#define KPEEK_HOST
#define KDEBUG_WINDOW_HOST
#define recover_window_hal_init unused_real_hal_init
#include "kdebug/kdebug_window.c"
#undef recover_window_hal_init
static unsigned int fake_inp(unsigned int port)
{
    port_loads++;
    return port==TEST_INDEX ? saved_index : 0xE5;
}
static unsigned int fake_inpw(unsigned int port) {(void)port;port_loads++;return 0x91E5;}
static unsigned long fake_inpd(unsigned int port) {(void)port;port_loads++;return 0xD3B791E5;}
static void fake_outp(unsigned int port,unsigned int value)
{CHECK(!fake_if && port==TEST_INDEX);port_stores++;saved_index=value;}
static void fake_outpw(unsigned int p,unsigned int v) {(void)p;(void)v;CHECK(0);}
static void fake_outpd(unsigned int p,unsigned long v) {(void)p;(void)v;CHECK(0);}
#define inp fake_inp
#define inpw fake_inpw
#define inpd fake_inpd
#define outp fake_outp
#define outpw fake_outpw
#define outpd fake_outpd
static int fake_load(void *o,u32 va,u32 width,u32 *value)
{
    (void)o;CHECK(!fake_if);loads++;
    if (reenter) {reenter=false;CHECK(callback!=0);callback(attached_ids[0]);}
    *value=va==MEM_DEVICE_APERTURE_BASE+TEST_INDEX ? saved_index :
        width==1 ? 0xE5U : width==2 ? 0x91E5U : 0xD3B791E5U;
    return ++bus_step==fail_step?-1:0;
}
static int fake_store(void *o,u32 va,u32 width,u32 value)
{
    (void)o;CHECK(!fake_if && va==MEM_DEVICE_APERTURE_BASE+TEST_INDEX && width==1);
    stores++;saved_index=value;return ++bus_step==fail_step?-1:0;
}
static const struct recover_window_ops fake_ops={window_hal_irq_save,window_hal_irq_restore,
    window_hal_pte,window_hal_map,window_hal_unmap,fake_load,fake_store};
void recover_window_hal_init(struct recover_window *w)
{*w=(struct recover_window){.ops=&fake_ops,.va=MEM_DEVICE_APERTURE_BASE};}
u32 pci_hwb_read(u32 a,u32 b,u32 c,u32 *v) {(void)a;(void)b;(void)c;(void)v;CHECK(0);return 0;}
u32 pci_hwb_write(u32 a,u32 b,u32 c,u32 v) {(void)a;(void)b;(void)c;(void)v;CHECK(0);return 0;}
u32 exec_hwb_abort_take(void) {CHECK(0);return 0;}
void *kmemset(void *p,int v,u32 n) {u8 *b=p;while(n--)*b++=v;return p;}
char *kstrncpy(char *d,const char *s,u32 n)
{u32 i=0;if(n){while(i+1<n && s[i]){d[i]=s[i];i++;}d[i]=0;}return d;}
int kstrcmp(const char *a,const char *b) {while(*a && *a==*b){a++;b++;}return *a-*b;}
void kprintf(u8 attr,const char *fmt,...) {(void)attr;(void)fmt;CHECK(fake_if);prints++;}
const struct serial_setup *serial_get_setup(void)
{static const struct serial_setup s={.actual=9600};return no_serial?0:&s;}
int serial_gate_active(void) {return 0;}
i32 debug_probe_control(u32 op,void *arg,u32 size)
{
    if(op==PROBE_LIST){
        CHECK(size==sizeof(struct probe_list));struct probe_list *l=arg;
        kmemset(l,0,sizeof(*l));l->count=9;l->epoch=registry_epoch;
        for(u32 i=0;i<l->count;i++){
            kstrncpy(l->points[i].name,"point.0",PROBE_NAME_SIZE);l->points[i].name[6]+=(char)i;
            if(renamed)l->points[i].name[0]='x';
            if (!i && test_point_name) kstrncpy(l->points[i].name,test_point_name,PROBE_NAME_SIZE);
            l->points[i].id=i;l->points[i].enabled=callback!=0;
        }
        return 0;
    }
    if(op==PROBE_KDEBUG_OWNER){CHECK(size==sizeof(fake_owner));if(no_owner)return PROBE_ERR_ARG;*(struct module_handle *)arg=fake_owner;return 0;}
    if(op==PROBE_DETACH){CHECK(size==sizeof(fake_owner));callback=0;attached_count=0;return 0;}
    CHECK(op==PROBE_ATTACH && size==sizeof(struct probe_attach) && !callback);
    struct probe_attach *a=arg;CHECK(a->owner.slot==fake_owner.slot && a->owner.generation==fake_owner.generation);
    callback=a->callback;attached_count=a->count;
    for(u32 i=0;i<a->count;i++)attached_ids[i]=a->ids[i];
    return 0;
}
#include "kdebug/hwb_access.c"
#include "kdebug/probe_index.c"
#include "kdebug/probe.c"
static struct probe_wire wire(u32 action)
{return (struct probe_wire){.version=PROBE_VERSION,.size=PROBE_WIRE_BYTES,.action=action};}
static struct probe_wire command(u32 action)
{struct probe_wire w=wire(action);CHECK(!probe_call(&w,sizeof(w)));return w;}
static struct probe_wire add(u32 point,u32 kind,u32 width,u32 addr,u32 data,u32 index)
{
    struct probe_wire w=wire(PROBE_CMD_ADD);kstrncpy(w.name,"point.0",sizeof(w.name));w.name[6]+=(char)point;
    w.kind=kind;w.width=width;w.address=addr;w.data_address=data;w.index=index;
    CHECK(!probe_call(&w,sizeof(w)));return w;
}
static void reset(void)
{
    command(PROBE_CMD_STOP);CHECK(command(PROBE_CMD_RESET).status==PROBE_OK);
    stores=loads=port_stores=port_loads=bus_step=fail_step=map_fail=unmap_fail=prints=0;
    fake_if=1;pte=OLD_PTE;saved_index=OLD_INDEX;window_failed=false;no_owner=no_serial=reenter=renamed=false;
}
static void hit(u32 id) {CHECK(callback!=0);callback(id);}
static void test_capacity(void)
{
    HWB_TEST("capacity");reset();
    for(u32 i=0;i<8;i++)for(u32 j=0;j<16;j++)
        CHECK(add(i,j%2?PROBE_IO:PROBE_MMIO,1U<<(j%3),j%2?0x100:TEST_PAGE,0,0).status==PROBE_OK);
    CHECK(add(0,PROBE_IO,1,0x100,0,0).status==PROBE_LIMIT);
    CHECK(add(8,PROBE_IO,1,0x100,0,0).status==PROBE_LIMIT);
    CHECK(command(PROBE_CMD_ARM).status==PROBE_OK && attached_count==8);
    CHECK(command(PROBE_CMD_DRAIN).status==PROBE_BUSY);
    CHECK(command(PROBE_CMD_RESET).status==PROBE_BUSY);
    CHECK(add(0,PROBE_IO,1,0x100,0,0).status==PROBE_BUSY);
    for(u32 i=0;i<8;i++)hit(i);
    command(PROBE_CMD_STOP);CHECK(!callback && !stores && !port_stores && loads==64 && port_loads==64);
    for(u32 i=0;i<128;i++){
        struct probe_wire w=command(PROBE_CMD_DRAIN);
        CHECK(w.hit==i/16+1 && w.id==i/16 && w.item==i%16 && w.status==PROBE_OK);
        CHECK(w.value==(w.width==1?0xE5U:w.width==2?0x91E5U:0xD3B791E5U));
        CHECK(w.more==(i!=127) && w.captured==8);
    }
    CHECK(!command(PROBE_CMD_DRAIN).hit);
}
static void test_validation(void)
{
    HWB_TEST("width");reset();
    CHECK(add(0,PROBE_IO,3,0x100,0,0).status==PROBE_ARG);
    CHECK(!setting_count && !loads && !port_loads);
    HWB_TEST("boundary");
    CHECK(add(0,PROBE_IO,2,0xFFFF,0,0).status==PROBE_ARG);
    CHECK(add(0,PROBE_MMIO,4,0xFFFFFFFE,0,0).status==PROBE_ARG);
    CHECK(add(0,PROBE_IO,2,0x101,0,0).status==PROBE_ARG);
    CHECK(add(0,PROBE_IDXMMIO,1,TEST_PAGE+4095,TEST_PAGE+4096,1).status==PROBE_ARG);
    CHECK(add(0,PROBE_IDXIO,2,TEST_INDEX,TEST_DATA,1).status==PROBE_ARG);
    CHECK(add(0,PROBE_IDXIO,1,TEST_INDEX,TEST_DATA,256).status==PROBE_ARG);
    CHECK(add(0,PROBE_IDXIO,1,TEST_INDEX,65536,1).status==PROBE_ARG);
    HWB_TEST("data-destination");
    CHECK(add(0,PROBE_IDXMMIO,1,TEST_PAGE+TEST_INDEX,TEST_PAGE+TEST_INDEX,1).status==PROBE_ARG);
    CHECK(add(0,PROBE_IO,1,65535,0,0).status==PROBE_OK);
    CHECK(add(0,PROBE_MMIO,1,0xFFFFFFFF,0,0).status==PROBE_OK);
    u32 old_count=settings[0].count;
    CHECK(add(0,PROBE_MMIO,4,3,0,0).status==PROBE_ARG && settings[0].count==old_count);
    struct probe_wire w=wire(PROBE_CMD_ARM);w.reserved[3]=1;
    CHECK(probe_call(&w,sizeof(w))==OS32_ERR_INVAL);
    w=wire(PROBE_CMD_ARM);w.flags=2;CHECK(probe_call(&w,sizeof(w))==OS32_ERR_INVAL);
    w=wire(PROBE_CMD_STATUS);w.size--;CHECK(probe_call(&w,sizeof(w))==OS32_ERR_INVAL);
    CHECK(probe_call(0,128)==OS32_ERR_INVAL);
}
static void test_limits(void)
{
    HWB_TEST("overflow");reset();CHECK(!add(0,PROBE_MMIO,1,TEST_PAGE,0,0).status);
    CHECK(!command(PROBE_CMD_ARM).status);
    for(u32 i=0;i<64;i++)hit(0);
    hit(0);CHECK(!callback && !armed && loads==64 && used==64);
    struct probe_wire w=command(PROBE_CMD_STATUS);
    CHECK(w.status==PROBE_OVERFLOW && w.captured==64 && w.dropped==1 && w.overflow==1);
    for(u32 i=0;i<64;i++)CHECK(command(PROBE_CMD_DRAIN).hit==i+1);
    CHECK(command(PROBE_CMD_STATUS).status==PROBE_OVERFLOW);
    HWB_TEST("reentry");reset();add(0,PROBE_MMIO,1,TEST_PAGE,0,0);command(PROBE_CMD_ARM);
    reenter=true;hit(0);CHECK(captured==1 && dropped==1 && loads==1);
    hits=U32_MAX;hit(0);CHECK(!callback && error==PROBE_LIMIT && captured==1);
}
static void unregister_fake(void) {callback=0;attached_count=0;registry_epoch++;}
static void test_epoch(void)
{
    HWB_TEST("epoch");reset();add(0,PROBE_IO,1,0x100,0,0);
    unregister_fake();CHECK(command(PROBE_CMD_ARM).status==PROBE_ARG && !setting_count && !callback);
    CHECK(!add(0,PROBE_IO,1,0x100,0,0).status);CHECK(!command(PROBE_CMD_ARM).status);
    hit(0);unregister_fake();CHECK(command(PROBE_CMD_STATUS).status==PROBE_ARG && !armed && !setting_count);
    renamed=true;
    struct probe_wire w=command(PROBE_CMD_DRAIN);CHECK(w.hit==1 && !kstrcmp(w.name,"point.0"));
    renamed=false;
    add(0,PROBE_IO,1,0x100,0,0);unregister_fake();
    CHECK(!add(1,PROBE_IO,1,0x100,0,0).status && setting_count==1 && settings[0].id==1);
    unregister_fake();w=command(PROBE_CMD_DRAIN);CHECK(!setting_count && !w.hit);
    HWB_TEST("stop");reset();add(0,PROBE_IO,1,0x100,0,0);command(PROBE_CMD_ARM);
    command(PROBE_CMD_STOP);CHECK(!callback);probe_hit(0);CHECK(!captured);
}
static void test_pair(void)
{
    HWB_TEST("pair");reset();
    struct probe_item p={PROBE_IDXIO,1,TEST_INDEX,TEST_DATA,0x1F};u32 v;i32 restore;
    CHECK(!probe_index_read(&p,&v,&restore) && !restore && v==0xE5);
    CHECK(port_stores==2 && port_loads==2 && fake_if && saved_index==OLD_INDEX);
    p=(struct probe_item){PROBE_IDXMMIO,1,TEST_PAGE+TEST_INDEX,TEST_PAGE+TEST_DATA,0x1F};
    for(u32 initial_if=0;initial_if<=1;initial_if++)for(u32 f=0;f<=4;f++){
        HWB_TEST("pair-restore");reset();fail_step=f;fake_if=initial_if;
        i32 rc=probe_index_read(&p,&v,&restore);
        CHECK(rc==(f && f<4?PROBE_IO_ERROR:PROBE_OK));
        CHECK(restore==(f==4?PROBE_IO_ERROR:PROBE_OK));
        CHECK(stores==(f==1?0U:2U) && saved_index==OLD_INDEX);
        HWB_TEST("pair-if");CHECK(fake_if==initial_if && pte==OLD_PTE && !window_busy);
    }
    HWB_TEST("pair-window");reset();map_fail=1;
    CHECK(probe_index_read(&p,&v,&restore)==PROBE_WINDOW && !stores && fake_if && pte==OLD_PTE);
    reset();window_busy=true;
    CHECK(probe_index_read(&p,&v,&restore)==PROBE_WINDOW && !stores && fake_if);window_busy=false;
    reset();unmap_fail=1;CHECK(probe_index_read(&p,&v,&restore)==PROBE_WINDOW && fake_if && pte==OLD_PTE);
    HWB_TEST("failure-stop");reset();
    add(0,PROBE_IDXMMIO,1,p.address,p.data_address,p.index);
    add(0,PROBE_MMIO,1,TEST_PAGE,0,0);add(1,PROBE_IO,1,0x100,0,0);
    command(PROBE_CMD_ARM);fail_step=4;hit(0);u32 before=loads;hit(0);CHECK(loads==before);
    CHECK(!command(PROBE_CMD_LIST).flags);
    hit(1);command(PROBE_CMD_STOP);
    struct probe_wire w=command(PROBE_CMD_DRAIN);CHECK(w.restore_rc==PROBE_IO_ERROR);
    w=command(PROBE_CMD_DRAIN);CHECK(w.status==PROBE_SKIP && !w.value);
    w=command(PROBE_CMD_DRAIN);CHECK(!w.status && w.id==1);
    reset();add(0,PROBE_MMIO,1,TEST_PAGE,0,0);add(0,PROBE_IO,1,0x100,0,0);
    command(PROBE_CMD_ARM);map_fail=1;hit(0);CHECK(!loads && !port_loads);
    command(PROBE_CMD_STOP);w=command(PROBE_CMD_DRAIN);CHECK(w.status==PROBE_WINDOW);
    w=command(PROBE_CMD_DRAIN);CHECK(w.status==PROBE_SKIP);
}
static void test_serial_owner(void)
{
    HWB_TEST("serial-owner");reset();add(0,PROBE_IO,1,0x100,0,0);no_owner=true;
    CHECK(command(PROBE_CMD_ARM).status==PROBE_ARG && !callback);no_owner=false;
    struct probe_wire w=wire(PROBE_CMD_ARM);w.flags=PROBE_SERIAL;no_serial=true;
    CHECK(!probe_call(&w,sizeof(w)) && w.status==PROBE_NOSYS && !callback);
    no_serial=false;w=wire(PROBE_CMD_ARM);w.flags=PROBE_SERIAL;
    CHECK(!probe_call(&w,sizeof(w)) && !w.status);hit(0);CHECK(prints==1 && captured==1);
}
void _start(void)
{
    test_capacity();test_validation();test_limits();test_epoch();test_pair();test_serial_owner();
    hwb_test_write("GREEN probe: capacity, validation, limits, epoch, pairs, failures, serial\n");hwb_test_exit(0);
}
