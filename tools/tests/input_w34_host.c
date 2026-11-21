#include "input_policy.h"
#include "in2_int.h"
#include "kbd.h"
#include "os32_kapi_shared.h"

static u8 wire[IKM_HEADER_SIZE + IKM_MAX_ROWS * IKM_ROW_SIZE];
static u8 output[sizeof(wire)];
static u32 rows;
volatile int kbd_gui_mode;
static int force_busy, busy_calls, late_busy;
u32 in2_time_ms(void) { return 1; }
int legacy_active(void) { return 0; }
int legacy_stop_filter(int sc, int br, int mods) { (void)sc; (void)br; (void)mods; return 0; }
void legacy_cooked_push(u16 value) { (void)value; }
void legacy_raw_push(int sc, int br, int mods) { (void)sc; (void)br; (void)mods; }
void input_emit_in2(const InputEvent *e) { (void)e; }
void input_state_release(u8 src, u16 gen) { (void)src; (void)gen; }
static int pressed(void) { return force_busy || (late_busy && ++busy_calls == 2) || input_policy_pressed(); }
static void put16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static void put32(u8 *p, u32 v) { put16(p, v); put16(p + 2, v >> 16); }
static void begin(void)
{
    rows = 0;
    wire[0]='I'; wire[1]='K'; wire[2]='M'; wire[3]='1';
    put16(wire+4, 2); put16(wire+6, 16); put32(wire+8, 0); put32(wire+12, 0);
}
static void add(u8 kind, u8 profile, u8 ctx, u16 index, u16 selector, u32 value, u32 aux)
{
    u8 *p = wire + 16 + 16 * rows++;
    p[0]=kind; p[1]=profile; p[2]=ctx; p[3]=0;
    put16(p+4,index); put16(p+6,selector); put32(p+8,value); put32(p+12,aux);
    put32(wire+8,rows);
}
static int set(u32 owner, u32 gen) { return ikm_policy_set(wire,16+16*rows,owner,gen); }
static int symbol(u8 src, u16 gen, u16 code, u16 mods, u16 locks, u16 expected)
{
    KmSource source;
    KmEvalResult result;
    KmState state = {mods, locks};
    return km_source_lookup(src,gen,&source) || source.map_generation != ikm_policy_generation() ||
        km_eval(source.map,code,&state,&result) || result.sym != expected;
}
static int capture(u8 src, u16 gen, u16 code, u8 down)
{
    KmKey key = { .src=src, .generation=gen, .keycode=code, .down=down };
    return input_capture_key(&key);
}
#define REQUIRE(x) do { if (!(x)) return __LINE__ % 120 + 1; } while (0)
#ifndef MAC_POLICY_TEST
static int test(void)
{
    u16 native_gen, usb_gen;
    u8 action=99, byte=99;
    u32 needed, n, revision;
    int native = pc98_input_keymap_register(&native_gen);
    int usb = input_src_register(INPUT_KIND_KBD,INPUT_ORIGIN_USB,&usb_gen);
    REQUIRE(native > 0 && usb > 0 && !km_source_bind(usb,usb_gen,2));
    REQUIRE(!ikm_policy_init(IKM_PLATFORM_PC98,&pc98_jp_backend,&hid_jp_backend,pressed));
    REQUIRE(ikm_policy_generation()==1);
    REQUIRE(!ikm_policy_get(output,sizeof(output),&needed) && needed==16);
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'a'));
    begin(); add(2,0,2,4,1,'b',0); REQUIRE(!set(0,0));
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'b'));
    /* Every set starts from defaults, while a parent's first save stays fixed. */
    begin(); add(2,0,2,4,1,'c',0); REQUIRE(!set(1,100));
    begin(); add(2,0,2,4,1,'d',0); REQUIRE(!set(1,100));
    revision=ikm_policy_generation();
    REQUIRE(!ikm_policy_get(output,sizeof(output),&needed));
    REQUIRE(needed==32 && output[24]=='d');
    REQUIRE(set(2,200)==OS32_ERR_BUSY && ikm_policy_reset(2,200)==OS32_ERR_BUSY);
    REQUIRE(set(1,101)==OS32_ERR_BUSY && ikm_policy_reset(0,0)==OS32_ERR_BUSY);
    ikm_policy_owner_exit(2,200); ikm_policy_owner_exit(1,99);
    REQUIRE(ikm_policy_generation()==revision && !symbol(usb,usb_gen,4,0,0,'d'));
    REQUIRE(!ikm_policy_get(output,sizeof(output),&needed));
    REQUIRE(needed==32 && output[24]=='d' && output[25]==0 && output[26]==0 && output[27]==0);
    REQUIRE(!ikm_policy_reset(1,100) && !symbol(usb,usb_gen,4,0,0,'a'));
    ikm_policy_owner_exit(1,100);
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'b') && ikm_policy_generation()==revision+2);
    REQUIRE(!ikm_policy_reset(1,100));
    ikm_policy_owner_exit(1,100);
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'b'));
    /* Both the early and final BUSY guards must leave active/save untouched. */
    revision=ikm_policy_generation();
    force_busy=1; REQUIRE(set(1,100)==OS32_ERR_BUSY && ikm_policy_reset(1,100)==OS32_ERR_BUSY); force_busy=0;
    busy_calls=0; late_busy=1; REQUIRE(set(1,100)==OS32_ERR_BUSY); late_busy=0;
    REQUIRE(ikm_policy_generation()==revision);
    REQUIRE(!capture(usb,usb_gen,4,1)); REQUIRE(set(1,100)==OS32_ERR_BUSY);
    REQUIRE(!capture(usb,usb_gen,4,0));
    REQUIRE(!capture(usb,usb_gen,0x53,1)); REQUIRE(ikm_policy_reset(0,0)==OS32_ERR_BUSY);
    REQUIRE(!capture(usb,usb_gen,0x53,0));
    REQUIRE(!capture(native,native_gen,KEY_CAPS,1)); REQUIRE(!input_policy_pressed());
    REQUIRE(!ikm_policy_reset(0,0)); REQUIRE(!capture(native,native_gen,KEY_CAPS,0));
    REQUIRE(!capture(native,native_gen,KEY_KANA,1)); REQUIRE(!input_policy_pressed());
    REQUIRE(!ikm_policy_reset(0,0)); REQUIRE(!capture(native,native_gen,KEY_KANA,0));
    /* Specific key NONE wins over EXT, and absence falls back to EXT. */
    begin(); add(6,1,2,0x60,0,3,0); add(8,1,2,0x60,1,0,0);
    REQUIRE(!set(0,0)); REQUIRE(!ikm_policy_action(1,2,1,0x60,0x60,0,&action) && action==0);
    begin(); add(6,1,2,0x60,0,3,0); REQUIRE(!set(0,0));
    REQUIRE(!ikm_policy_action(1,2,1,0x60,0x60,0,&action) && action==3);
    REQUIRE(!ikm_policy_action(1,1,1,0x60,0x60,0,&action) && action==0); /* whole EXT profile replaced */
    REQUIRE(ikm_policy_compat(1,0x37,&byte)==OS32_ERR_NOTFOUND);
    REQUIRE(!ikm_policy_compat(2,0x37,&byte) && byte==3);
    REQUIRE(!ikm_policy_reset(0,0));
    REQUIRE(!ikm_policy_action(1,1,1,0x60,0x60,0,&action) && action==4);
    REQUIRE(!ikm_policy_action(1,2,2,0x90,0x72,0,&action) && action==1);
    /* Unsorted input, EXACT special payload, bank inheritance and flags. */
    begin(); add(7,0,1,0x1d,0,1,0); add(3,0,1,0x1d,9,'Z',0);
    add(3,0,1,0x1d,0,'q',0); add(3,0,1,0x1d,2,0xf500,0x8001);
    add(2,0,2,4,1,'w',0); add(1,0,2,0,0,256,0);
    add(5,1,1,1,0,5,0); add(5,1,0,1,0,6,0);
    REQUIRE(!set(0,0));
    REQUIRE(!symbol(native,native_gen,0x1d,0,0,'q'));
    REQUIRE(!symbol(native,native_gen,0x1d,KM_MOD_MODE,0,'q'));
    REQUIRE(!symbol(native,native_gen,0x1d,KM_MOD_MODE,KM_LOCK_CAPS,'Z'));
    REQUIRE(!symbol(native,native_gen,0x1d,KM_MOD_CTRL,0,0xf500));
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'w'));
    REQUIRE(!ikm_policy_term(1,1,1,&byte) && byte==5);
    REQUIRE(!ikm_policy_term(1,2,1,&byte) && byte==6);
    REQUIRE(!ikm_policy_term(2,1,1,&byte) && byte==3);
    /* get returns a canonical, complete snapshot relative to OS defaults,
     * never a patch against the previous active table. */
    for (unsigned i=0;i<sizeof(output);++i) output[i]=0xa5;
    REQUIRE(ikm_policy_get(output,16+16*rows-1,&needed)==OS32_ERR_FULL);
    for (unsigned i=0;i<sizeof(output);++i) REQUIRE(output[i]==0xa5);
    REQUIRE(needed==16+16*rows && !ikm_policy_get(output,sizeof(output),&needed));
    REQUIRE(!ikm_wire_header(output,needed,&n) && n==rows);
    for (unsigned i=0;i<n;++i) { IkmRow r; REQUIRE(!ikm_wire_row(output,needed,i,&r) && !r.flags); }
    REQUIRE(output[4]==2 && output[5]==0 && output[6]==16 && output[7]==0);
    REQUIRE(!ikm_policy_reset(0,0));
    REQUIRE(!ikm_policy_set(output,needed,0,0));
    REQUIRE(!symbol(native,native_gen,0x1d,KM_MOD_MODE,KM_LOCK_CAPS,'Z'));
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'w'));
    /* Failed compilation leaves active, generation, and saved source intact. */
    REQUIRE(!set(1,100)); revision=ikm_policy_generation();
    begin(); add(2,0,2,4,1,'x',0); add(2,0,2,4,1,'y',0);
    REQUIRE(set(1,100)==OS32_ERR_INVAL && ikm_policy_generation()==revision);
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'w'));
    ikm_policy_owner_exit(1,100); REQUIRE(!symbol(usb,usb_gen,4,0,0,'w'));
    begin(); add(5,1,1,1,0,7,0); add(5,1,1,2,0,7,0); REQUIRE(set(0,0)==OS32_ERR_INVAL);
    /* Maximum count, mask-distinct actions and indexed boundary lookups. */
    begin();
    for (unsigned i=8192;i>0;--i) {
        unsigned k=i-1, slot=(k/256)%16;
        add(8,1,k/4096,k%256,2,k%19,(slot&7)|((slot&8)<<1));
    }
    REQUIRE(!set(0,0));
    for (unsigned k=0;k<8192;++k) {
        unsigned slot=(k/256)%16;
        REQUIRE(!ikm_policy_action(1,k/4096,2,k%256,0xff,(slot&7)|((slot&8)<<1),&action) && action==k%19);
    }
    REQUIRE(!ikm_policy_get(output,sizeof(output),&needed) && needed==sizeof(output));
    REQUIRE(!ikm_policy_set(output,needed,0,0));
    REQUIRE(!ikm_policy_reset(0,0));
    REQUIRE(!symbol(usb,usb_gen,4,0,0,'a'));
    /* Mixed maximum: all maps/cells/banks/flags and 128 EXT entries together
     * with thousands of mask-distinct actions. Reverse insertion exercises
     * changing delta widths; get/set preserves all native cell payloads. */
    begin();
    for (unsigned map=1;map<=2;++map) {
        add(1,0,map,0,0,map==1?128:256,0); add(1,0,map,1,0,0,0);
        for (unsigned key=0;key<(map==1?128:256);++key)
            for (unsigned col=0;col<5;++col) add(2,0,map,key,col,0x2000+key*5+col,0);
    }
    for (unsigned key=0;key<128;++key) {
        for (unsigned col=0;col<16;++col)
            add(3,0,1,key,col,col%2?0xf500:0x400+key*16+col,col%2?0x8001:0);
        add(7,0,1,key,0,key%4,0);
        add(6,2,4,key,0,key%19,0);
    }
    while (rows<8192) {
        unsigned key=rows-4228, slot=(key/256)%16;
        add(8,2,key/4096,key%256,2,key%19,(slot&7)|((slot&8)<<1));
    }
    /* Shuffle all rows with an invertible permutation of their wire slots. */
    for (unsigned i=0;i<8192;++i)
        for (unsigned b=0;b<16;++b) output[16+i*16+b]=wire[16+((i*4051)&8191)*16+b];
    for (unsigned i=16;i<sizeof(wire);++i) wire[i]=output[i];
    REQUIRE(!set(0,0));
    REQUIRE(!ikm_policy_get(output,sizeof(output),&needed));
    REQUIRE(!ikm_policy_set(output,needed,0,0));
    REQUIRE(!symbol(usb,usb_gen,255,0,0,0x2000+255*5+1));
    REQUIRE(!symbol(native,native_gen,127,0,0,0x400+127*16));
    return 0;
}
#else
static int test(void)
{
    u16 gen;
    u8 action, value;
    u32 needed;
    REQUIRE(!km_backend_register(&hid_jp_backend));
    int usb=input_src_register(INPUT_KIND_KBD,INPUT_ORIGIN_USB,&gen);
    REQUIRE(usb>0 && !km_source_bind(usb,gen,2));
    REQUIRE(!ikm_policy_init(IKM_PLATFORM_MAC,0,&hid_jp_backend,pressed));
    REQUIRE(!ikm_policy_term(3,1,2,&value) && value==0x15);
    REQUIRE(!ikm_policy_term(3,1,3,&value) && value==4);
    REQUIRE(!ikm_policy_term(4,1,5,&value) && value==0x0c);
    REQUIRE(ikm_policy_term(1,1,1,&value)==OS32_ERR_INVAL);
    REQUIRE(!ikm_policy_action(3,2,2,0x37,0x31,KM_MOD_COMMAND,&action) && action==3);
    REQUIRE(!ikm_policy_action(3,1,2,0x0e,0x24,KM_MOD_COMMAND,&action) && action==9);
    REQUIRE(!ikm_policy_action(4,4,2,0x1d,0x29,KM_MOD_COMMAND,&action) && action==13);
    REQUIRE(!ikm_policy_action(4,4,2,0x1d,0x29,KM_MOD_COMMAND|KM_MOD_SHIFT,&action) && action==14);
    REQUIRE(!ikm_policy_action(3,2,2,0x90,0x72,KM_MOD_SHIFT,&action) && action==1);
    begin(); add(8,4,4,0x1d,2,13,KM_MOD_COMMAND);
    add(8,4,4,0x1d,2,14,KM_MOD_COMMAND|KM_MOD_SHIFT);
    add(2,0,2,4,1,'q',0); REQUIRE(!set(1,1));
    REQUIRE(!symbol(usb,gen,4,0,0,'q'));
    REQUIRE(!ikm_policy_action(4,4,2,0x1d,0x29,KM_MOD_COMMAND|KM_MOD_SHIFT,&action) && action==14);
    REQUIRE(!ikm_policy_get(output,sizeof(output),&needed) && needed==64);
    begin(); add(2,0,1,0,1,'r',0); REQUIRE(set(1,1)==OS32_ERR_INVAL);
    REQUIRE(!capture(usb,gen,4,1)); REQUIRE(ikm_policy_reset(1,1)==OS32_ERR_BUSY);
    REQUIRE(!capture(usb,gen,4,0)); ikm_policy_owner_exit(1,1);
    REQUIRE(!symbol(usb,gen,4,0,0,'a'));
    return 0;
}
#endif
void _start(void)
{
    int rc=test();
#ifdef PPC_TEST
    register int call __asm__("r0")=1;
    register int status __asm__("r3")=rc;
    __asm__ volatile("sc" : : "r"(call), "r"(status) : "memory");
#else
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
#endif
    __builtin_unreachable();
}
