/* Loader loan/pin tests use actual body and HAL facade. No device operations
 * run here; real regs/device assembly is test_trident_lifecycle's oracle. */
#include "drivers/video/trident/trident_module.c"
#include "drivers/video/trident/trident_body.c"
#include "lib/kstring.h"
#include "mm/kmalloc.h"
#define CHECK(x) do { if (!(x)) finish(__LINE__); } while (0)
static void finish(int line)
{
    if (line) {
        char b[20]="RED module "; u32 n=11;
        b[n++]=(line/100)%10+'0'; b[n++]=(line/10)%10+'0'; b[n++]=line%10+'0'; b[n++]='\n';
        __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(b),"d"(n):"memory");
    }
    __asm__ volatile("int $0x80"::"a"(1),"b"(!!line):"memory");
    for (;;) {}
}
int kstrcmp(const char *a,const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return (u8)*a-(u8)*b; }
static struct module_record record;
static struct trident_resources resources;
static struct bootinfo bi;
static u32 ops[4], mode, pins, prepares, aborts, rollbacks;
static u8 client[TRI_DISPLAY_BYTES], regs[TRI_REG_MAPPED_BYTES], lfb[TRI_DISPLAY_BYTES];
GfxCounters gfx_counters;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
const u32 trident_kexports[TRIDENT_IMPORT_COUNT]={0};
const struct module_symbol_contract trident_import_contracts[TRIDENT_IMPORT_COUNT]={{0}};
const struct module_symbol_contract trident_export_contracts[3]={{0}};
const struct bootinfo *bootinfo_get(void) { return &bi; }
int appslot_gfx_owner(void) { return 42; }
int gfx_arm_emergency(int (*cb)(void *), void *opaque, u32 owner)
{ CHECK(pinned && cb && opaque && owner==42); return mode==8 ? -78 : 0; }
const struct trident_resources *trident_resources_get(void) { return mode==1 ? 0 : &resources; }
int trident_resources_rollback(bool after) { CHECK(!after); ++rollbacks; return 0; }
int module_load(const struct module_request *q, struct module_handle *h)
{
    CHECK(q->policy.module_id==MODULE_ID_TRIDENT && q->policy.ops_version==1);
    CHECK(q->lifecycle.start==start && q->lifecycle.detach==detach);
    CHECK(q->path[0]=='/');
    if (mode==2) return -72;
    *h=(struct module_handle){.generation=1};
    return 0;
}
int module_ref_acquire(struct module_handle h, u32 owner, enum module_ref_kind kind, u32 token)
{ (void)h;(void)owner;(void)token; CHECK(kind==MODULE_REF_REGISTRATION); ++pins; return mode!=5; }
int module_ref_release(struct module_handle h, enum module_ref_kind kind)
{ (void)h; CHECK(kind==MODULE_REF_REGISTRATION && pinned); --pins; return 1; }
const u32 *module_exports(struct module_handle h) { (void)h; return ops; }
int module_start(struct module_handle h)
{
    (void)h;
    CHECK(!trident_context && !pinned);
    if (mode==3) ops[3]=0;
    if (mode==4) trident_backend_ops.osri_show=(void *)1;
    if (mode==10) trident_backend_ops.prepare=0;
    void **required[]={(void **)&trident_backend_ops.probe,(void **)&trident_backend_ops.init,
        (void **)&trident_backend_ops.query,(void **)&trident_backend_ops.shutdown,
        (void **)&trident_backend_ops.present_rect,(void **)&trident_backend_ops.set_palette,
        (void **)&trident_backend_ops.enter,(void **)&trident_backend_ops.leave,
        (void **)&trident_backend_ops.prepare};
    void **optional[]={(void **)&trident_backend_ops.fill_rect,(void **)&trident_backend_ops.blit,
        (void **)&trident_backend_ops.set_mode,(void **)&trident_backend_ops.dpms,
        (void **)&trident_backend_ops.cursor_image,(void **)&trident_backend_ops.cursor_move,
        (void **)&trident_backend_ops.cursor_show,(void **)&trident_backend_ops.vsync,
        (void **)&trident_backend_ops.page_flip,(void **)&trident_backend_ops.gamma,
        (void **)&trident_backend_ops.osri_show};
    if (mode>=11 && mode<20) *required[mode-11]=(void *)1;
    if (mode>=20 && mode<31) *optional[mode-20]=(void *)1;
    if (mode==31) trident_backend_ops.name=(void *)1;
    if (mode==32) trident_backend_ops.modes=(void *)1;
    if (mode==33) trident_backend_ops.mode_count=2;
    if (mode==34) trident_backend_ops.bb_size=1;
    if (mode==35) ops[0]=2;
    if (mode==36) ops[2]=0;
    return start(&record,ops[1],ops);
}
int module_abort(struct module_handle h)
{ (void)h; ++aborts; return detach(&record) ? 0 : MODULE_ISOLATED; }
int trident_backend_prepare(void); /* actual HAL prepare below is linked */
void kprintf(u8 attr, const char *fmt, ...) { (void)attr;(void)fmt; }
int serial_gate_active(void) { return 0; }
void serial_puts(const char *p) { (void)p; }
void serial_puts_polled(const char *p) { (void)p; }
static PaletteEntry colors[16];
const PaletteEntry *palette_get_all(void) { return colors; }
void palette_shadow_set(int i,u8 r,u8 g,u8 b) { colors[i]=(PaletteEntry){r,g,b}; }
void *kmalloc(u32 bytes) { (void)bytes; return 0; }
void kfree(void *p) { (void)p; }
int __wrap_trident_backend_prepare(void)
{
    ++prepares;
    CHECK(pinned && pins==1);
    CHECK(gfx_backend_trident.probe==missing_probe);
    if (mode==6) return -76;
    return trident_backend_prepare();
}
extern char __executable_start[], _etext[], _end[];
void _start(void)
{
    u32 read_rc;
    __asm__ volatile("int $0x80":"=a"(read_rc):"0"(3),"b"(0),"c"(&mode),"d"(4):"memory");
    CHECK(read_rc==4);
    resources=(struct trident_resources){.ready=true,.regs=regs,.lfb=lfb,.client=client,
        .client_bytes=sizeof(client),.mapped_bytes=sizeof(lfb),
        .decode_bytes=TRI_BAR0_DECODE_BYTES,.regs_bytes=sizeof(regs),
        .pci={.bus=TRI_PCI_BUS,.slot=TRI_PCI_SLOT,.function=TRI_PCI_FUNCTION,
            .vendor=TRI_PCI_VENDOR,.device=TRI_PCI_DEVICE,.command=TRI_PCI_COMMAND,
            .bar0=TRI_BAR0_PHYS,.bar1=TRI_BAR1_PHYS}};
    record.text=(u32)(uptr)__executable_start;
    record.text_size=(u32)(_etext-__executable_start);
    record.ro=(u32)(uptr)_etext; record.ro_size=(u32)(_end-_etext);
    ops[0]=1; ops[1]=(u32)(uptr)trident_module_init;
    ops[2]=(u32)(uptr)__wrap_trident_backend_prepare; ops[3]=(u32)(uptr)trident_restore98;
    TridentModuleContext loan=context;
    GfxBackend out={0};
    CHECK(trident_module_init(0,&loan)==TRI_E_INVAL);
    CHECK(trident_module_init(&out,0)==TRI_E_INVAL);
    void **fields[]={(void **)&loan.backend,(void **)&loan.counters,(void **)&loan.resources,
        (void **)&loan.arm,(void **)&loan.height,(void **)&loan.flip,(void **)&loan.page,
        (void **)&loan.display_owner};
    loan.resources=&resources;
    for (u32 i=0;i<sizeof(fields)/sizeof(fields[0]);++i) {
        void *saved=*fields[i]; *fields[i]=0;
        CHECK(trident_module_init(&out,&loan)==TRI_E_INVAL);
        *fields[i]=saved;
    }
    CHECK(!trident_context);
    int rc=trident_module_load();
    if (mode==1 || mode==2 || mode==3 || mode==4 || mode==5 || mode==6 || mode==10 || mode>=11) {
        CHECK(rc!=0 && gfx_backend_trident.probe==missing_probe);
        CHECK(prepares==(mode==6));
        if (pinned) CHECK(!trident_module_abort());
        finish(0);
    }
    CHECK(!rc && pinned && pins==1 && prepares==1 && !armed);
    CHECK(trident_context->backend==&candidate);
    CHECK(gfx_backend_trident.bb_base==client && gfx_backend_trident.bb_size==sizeof(client));
    CHECK(!gfx_backend_trident.fill_rect && !gfx_backend_trident.osri_show);
    CHECK(trident_module_load()==MODULE_BUSY);
    if (mode==7 || mode==8) {
        rc=context.arm(trident_restore98,&resources,42);
        if (mode==7) {
            CHECK(!rc && armed && trident_module_abort()==TRI_E_BUSY);
            CHECK(!aborts && pinned && pins==1 && !rollbacks);
        } else {
            CHECK(rc==-78 && !armed && !trident_module_abort());
            CHECK(!pinned && rollbacks==1);
        }
    } else {
        CHECK(!trident_module_abort() && !pinned && pins==0 && rollbacks==1);
        CHECK(gfx_backend_trident.probe==missing_probe);
    }
    finish(0);
}
