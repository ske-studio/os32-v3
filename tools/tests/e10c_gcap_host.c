#include "types.h"
#include "v86_gcap.h"
#include "v86_gcap_math.h"
#include "os32_kapi_shared.h"
#define GFX_HEIGHT 400
#define GCAP_TV_CELLS (80U * 30U)
static V86Gcap storage;
static u16 tvstorage[GCAP_TV_CELLS * 2];
static V86Gcap *v86_gcap_rec, *gcap_owned;
static u16 *gcap_tv;
static const V86gIoOps *gcap_ops;
static V86gIoOps gcap_dry_ops, gcap_hw_ops;
static int gcap_keep_if;
static u32 kctx_irq_depth, kctx_exc_depth, v86_gcap_alloc_count, v86_gcap_free_count;
static u32 freed, restored, cursor, polled, exec_stop_count, allocations;
static int kill_capture, setup_fail;
static struct { int aborting; } v86_session;
static void *jump[5];
static void die(int rc) { __asm__ volatile("int $0x80"::"a"(1),"b"(rc)); for (;;) {} }
static void say(const char *s, u32 n) { __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
#define CHECK(x) do { if (!(x)) { say("FAIL " #x "\n",sizeof("FAIL " #x "\n")-1); die(1); } } while (0)
static void *kzalloc(u32 n) { (void)n; allocations++; return &storage; }
static void *kmalloc(u32 n) { (void)n; allocations++; return tvstorage; }
static void kfree(void *p) { CHECK(p == &storage || p == tvstorage); CHECK(!kctx_irq_depth && !kctx_exc_depth); freed++; }
static void kmemcpy(void *d,const void *s,u32 n) { u8 *x=d; const u8 *y=s; while(n--) *x++=*y++; }
static int v86_gui_refuse(void) { return 0; }
static int v86_is_active(void) { return 0; }
static int gfx_get_height(void) { return GFX_HEIGHT; }
void v86g_reset(V86Gcap *g) { u8 *p=(void *)g; for(u32 i=0;i<sizeof(*g);i++)p[i]=0; }
static void tv_save(u16 *p) { for(u32 i=0;i<GCAP_TV_CELLS*2;i++)p[i]=(u16)i; }
static void tv_restore(const u16 *p) { for(u32 i=0;i<GCAP_TV_CELLS*2;i++)CHECK(p[i]==(u16)i); restored++; }
static void v86_cui_display_restore(void) {}
static void console_hw_cursor_enable(void) { cursor++; }
int v86g_mode_is_31k(int l,unsigned int a) { (void)l;(void)a;return 0; }
static void (*pegc_restore_text_sync)(int);
static u32 exec_ledger_owner(void) { return 2; }
static int v86_mem_setup(u32 o) { (void)o;return setup_fail; }
static void v86_pic_set_imr(int p,int m) { (void)p;(void)m; }
static u8 code_dest[1], v86_gcap_code[1];
#define V86_TEST_CODE_ADDR code_dest
static void v86_mem_teardown(void) {}
void v86_gcap_release(void);
static void gcap_selftest(V86Gcap *g) { (void)g; }
static void gcap_rom(V86Gcap *g) {
    (void)g;
    if (kill_capture) {
        kctx_exc_depth=1;
        v86_gcap_rec=0; /* exception-time disconnect */
        v86_gcap_release();
        CHECK(freed == 0 && gcap_owned != 0);
        kctx_exc_depth=0;
        v86_gcap_release();
        __builtin_longjmp(jump,1);
    }
}
static void serial_puts_polled(const char *s) { CHECK(s && *s); polled++; }
#include "gcap_slice.inc"
#include "stop_slice.inc"
static void run(void) __attribute__((used));
static void run(void) {
    static V86Gcap out;
    CHECK(v86_gdc_capture(V86G_MODE_SELFTEST,&out) == 0);
    CHECK(restored == 0 && cursor == 0);
    CHECK(freed == 2 && v86_gcap_alloc_count == 2 && v86_gcap_free_count == 2);
    CHECK(!gcap_owned && !gcap_tv && !gcap_ops && !v86_gcap_rec);
    freed=0;
    CHECK(v86_gdc_capture(V86G_MODE_ROM,&out) == 0);
    CHECK(freed == 2 && restored == 1 && cursor == 1);
    freed=0; restored=0; cursor=0;
    storage.mode = V86G_MODE_SELFTEST;
    gcap_owned = &storage; gcap_tv = tvstorage;
    v86_gcap_alloc_count += 2; v86_session.aborting = 1;
    v86_gcap_release();
    CHECK(freed == 2 && restored == 1 && cursor == 1);
    v86_session.aborting = 0;
    freed=0; restored=0; cursor=0; kill_capture=1;
    if (!__builtin_setjmp(jump)) { v86_gdc_capture(V86G_MODE_ROM,&out); CHECK(0); }
    CHECK(freed == 2 && restored == 1 && cursor == 1);
    CHECK(!gcap_owned && !gcap_tv && !gcap_ops && !gcap_keep_if);
    v86_gcap_release(); CHECK(freed == 2);
    CHECK(v86_gcap_alloc_count == v86_gcap_free_count);
    exec_stop_mark(); CHECK(exec_stop_count == 1 && polled == 1);
    say("PASS e10c gcap kill/release and stop marker\n",sizeof("PASS e10c gcap kill/release and stop marker\n")-1);
    die(0);
}
__asm__(".globl _start\n_start: call run\n");
