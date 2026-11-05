/* Real prepare callbacks over RAM. GCC kernel-address instrumentation reports
 * actual volatile accesses (including overreads later masked to a byte), without
 * replacing the MMIO expressions or linking libc/libasan. Only accesses within
 * this BAR0 backing and between begin/end are recorded. */
#ifndef GF_FOUNDATION_WIDTH_HOST_H
#define GF_FOUNDATION_WIDTH_HOST_H
#define WIDTH_CRTC (RECOVER_PRMCIO+RECOVER_CRTC)
static u32 width_ram[(GF_MODE_DAC_DATA+sizeof(u32))/sizeof(u32)+1U];
static bool width_active;
static struct { uptr address; u32 width; bool write; } width_events[4];
static u32 width_count;
__attribute__((no_sanitize_address))
static void width_record(uptr address, u32 width, bool write)
{
    if (!width_active || address<(uptr)width_ram ||
        address>=(uptr)width_ram+sizeof(width_ram)) return;
    if (width_count<4) {
        width_events[width_count].address=address;
        width_events[width_count].width=width;
        width_events[width_count].write=write;
    }
    width_count++;
}
#define WIDTH_HOOK(n) \
    __attribute__((no_sanitize_address)) \
    void __asan_load##n##_noabort(uptr a) { width_record(a,n,false); } \
    __attribute__((no_sanitize_address)) \
    void __asan_store##n##_noabort(uptr a) { width_record(a,n,true); }
WIDTH_HOOK(1)
WIDTH_HOOK(2)
WIDTH_HOOK(4)
WIDTH_HOOK(8)
WIDTH_HOOK(16)
__attribute__((no_sanitize_address))
void __asan_loadN_noabort(uptr a, u32 n) { width_record(a,n,false); }
__attribute__((no_sanitize_address))
void __asan_storeN_noabort(uptr a, u32 n) { width_record(a,n,true); }
static void width_begin(void)
{
    width_count=0;width_active=true;
}
static void width_end(u32 off, u32 width, bool write, u32 count)
{
    width_active=false;
    CHECK(width_count==count);
    if (count) CHECK(width_events[0].address==(uptr)width_ram+off &&
                     width_events[0].width==width && width_events[0].write==write);
}
static void foundation_width_cases(struct gf_foundation *f)
{
    volatile u8 *saved=f->bar0;
    u8 *bytes=(u8 *)width_ram;
    f->bar0=(volatile u8 *)width_ram;
    const u32 offsets[]={WIDTH_CRTC,WIDTH_CRTC+1U,RECOVER_PRMVIO+KPEEK_VGA_SRX,
                         RECOVER_PRMVIO+KPEEK_VGA_SR,RECOVER_PRMCIO+RECOVER_ATTR,
                         GF_MODE_DAC_DATA};
    for (u32 i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        u32 off=offsets[i],value=~0U;
        for (u32 j=0;j<4;j++) bytes[off+j]=(u8)(0x81U+j);
        width_begin();
        CHECK(!f->io.read(f->io.opaque,0,off,1,&value));
        width_end(off,1,false,1);
        CHECK(value==0x81U);
        width_begin();
        CHECK(!f->io.write(f->io.opaque,0,off,1,0xabcdef5aU));
        width_end(off,1,true,1);
        CHECK(bytes[off]==0x5a);
        for (u32 j=1;j<4;j++) CHECK(bytes[off+j]==(u8)(0x81U+j));
    }
    /* Index and data are separate BAR0 byte offsets, never DWORD indices. */
    for (u32 off=WIDTH_CRTC;off<=WIDTH_CRTC+1U;off++) {
        width_begin();
        CHECK(!f->io.write(f->io.opaque,0,off,1,off==WIDTH_CRTC ? 0x44 : 0xa7));
        width_end(off,1,true,1);
    }
    CHECK(bytes[WIDTH_CRTC]==0x44 && bytes[WIDTH_CRTC+1U]==0xa7);
    for (u32 width=2;width<=4;width+=2) {
        u32 value=~0U,off=16;
        width_ram[off/4]=0x9abcdef0U;width_ram[off/4+1]=0x76543210U;
        width_begin();
        CHECK(!f->io.read(f->io.opaque,0,off,width,&value));
        width_end(off,width,false,1);
        CHECK(value==(width==2 ? 0xdef0U : 0x9abcdef0U));
        width_begin();
        CHECK(!f->io.write(f->io.opaque,0,off,width,0x12345678U));
        width_end(off,width,true,1);
        CHECK(width_ram[off/4]==(width==2 ? 0x9abc5678U : 0x12345678U));
        CHECK(width_ram[off/4+1]==0x76543210U);
    }
    f->revoked=true;
    for (u32 width=1;width<=4;width*=2) {
        u32 value=0xaabbccddU;
        width_begin();
        CHECK(f->io.read(f->io.opaque,0,16,width,&value)==-1);
        width_end(16,width,false,0);
        CHECK(value==0xaabbccddU);
        width_begin();
        CHECK(f->io.write(f->io.opaque,0,16,width,0)==-1);
        width_end(16,width,true,0);
        CHECK(width_ram[4]==0x12345678U);
    }
    f->revoked=false;f->bar0=saved;
}
#endif
