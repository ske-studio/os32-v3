/* D:2919-2975 full SPR table, no hardware memory probe. */
#include "trident_device_fake.h"
static struct trident_fake f;
static struct trident_device d;
static u8 memory[TRI_FAKE_REAL_BYTES];
static struct trident_fake_event trace[16];
static int test(void) {
    static const u32 kib[16] = {1024,512,6144,1024,8192,1024,10240,2048,12288,1024,14336,1024,16384,1024,20480,4096};
    if (trident_fake_reset(&f,memory,sizeof(memory),trace,16)) return 1;
    trident_fake_bind(&f,&d);
    d.armed=d.pinned=true;
    for (u32 mono=0;mono<2;++mono) {
        d.cur_vga_base=mono?TRI_CR_MONO:TRI_CR_COLOR;
        f.regs[TRI_MISC_WRITE]=mono?0:1;
        u8 *cr=mono?f.cr_mono:f.cr_color;
        for (u32 i=0;i<256;++i) {
            cr[0x1F]=i; cr[0xC1]=0;
            if (trident_capacity_plan(&d) || d.declared_bytes!=kib[i&15]*1024u || d.spr!=i) return 2;
        }
        static const u8 ext[]={0,1,0x10,0x11};
        cr[0x1F]=14;
        for(u32 i=0;i<4;++i) {
            cr[0xC1]=ext[i];
            if(trident_capacity_plan(&d) || d.declared_bytes!=(20480u+i*4096u)*1024u) return 3;
        }
        cr[0x1F]=0;
        if(trident_capacity_plan(&d) || !d.declared_unknown) return 4;
    }
    if(f.reads[2] || f.writes[2]) return 5;
    return 0;
}
void _start(void) {
    int rc=test(); const char *s=rc?"RED vram line failure\n":"GREEN vram\n";
    u32 n=0;while(s[n])++n;
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory");
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc?1:0):"memory");
    __builtin_unreachable();
}
