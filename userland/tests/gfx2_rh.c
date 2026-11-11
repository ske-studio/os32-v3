/* HAL v2 real-hardware acceptance. Run from CUI. */
#include "os32api.h"
#define ATTR 7
#define WAIT_MS 1000u
static int exercise(KernelAPI *api, u32 a, GFX2_AdapterInfo *info);
static int fail(KernelAPI *api, const char *stage, int rc);
int main(int argc, char **argv, KernelAPI *api)
{
    int result = 1, n; (void)argc; (void)argv;
    api->gfx_init(); n = api->gfx2_adapter_count();
    for (int i = 0; i < n; i++) {
        GFX2_AdapterInfo info;
        if (api->gfx2_adapter_info(i, &info)) { fail(api, "adapter", i); break; }
        if (!(info.flags & GFX2_AF_ACTIVE)) continue;
        if (info.backend_kind != OS32_SURFACE_GEFORCE && info.backend_kind != OS32_SURFACE_TRIDENT) {
            api->kprintf(ATTR, "gfx2_rh SKIP backend=%u\n", info.backend_kind); result = 0;
        } else result = exercise(api, i, &info);
        break;
    }
    api->gfx_shutdown();
    api->kprintf(ATTR, "gfx2_rh %s rc=%d\n", result ? "FAIL" : "PASS", result);
    return result;
}
static int fail(KernelAPI *api, const char *stage, int rc)
{
    api->kprintf(ATTR, "gfx2_rh stage=%s rc=%d\n", stage, rc); return 1;
}
static const u8 mono[8] = {0xf0,0x81,0x3c,0x01,0x80,0x55,0xaa,0x0f};
static u32 color[64];
static u32 ref_pat(u32 x, u32 y, u32 kind, u32 fg, u32 bg)
{
    u32 px=x&7, py=y&7;
    return kind == 2 ? color[py*8+px] : (mono[py] & (0x80>>px)) ? fg : bg;
}
static u32 ref_rop3(u32 p, u32 s, u32 d, u32 rop)
{
    u32 out=0;
    for (u32 bit=0; bit<32; bit++) {
        u32 i=(((p>>bit)&1)<<2)|(((s>>bit)&1)<<1)|((d>>bit)&1);
        out |= ((rop>>i)&1)<<bit;
    }
    return out;
}
static u32 initial(u32 x, u32 y, u32 fmt, int src)
{
    if (fmt == GFX2_FMT_INDEX8) return (src ? (x*29 ^ y*53) : (x*7+y*13)) & 255;
    return (src ? (x*0x1111+y*0x0123) : (x*0x0841 ^ y*0x1003)) & 65535;
}
static void put(u8 *p, u32 i, u32 cpp, u32 v)
{ p[i*cpp]=v; if (cpp==2) p[i*cpp+1]=v>>8; }
static u32 get(const u8 *p, u32 i, u32 cpp)
{ return p[i*cpp] | (cpp==2 ? (u32)p[i*cpp+1]<<8 : 0); }
static u8 buf[256*64], data[256*64], source[64*16*2];
static int submit(KernelAPI *api, u32 a, u32 gen, GFX2_Op *ops, u32 n, u32 *fence)
{
    u32 done=0; int rc=api->gfx2_submit(a,ops,n,gen,&done,fence);
    return rc ? rc : done==n ? 0 : 1;
}
static int transfer(KernelAPI *api,u32 a,u32 gen,u32 sid,u32 w,u32 h,u32 cpp,u8 *p,int read)
{
    u32 fence; GFX2_Op op={0};
    op.op=read?GFX2_OP_READBACK:GFX2_OP_HOSTDATA;
    if (read) { op.src=sid; op.sw=w; op.sh=h; }
    else { op.dst=sid; op.dw=w; op.dh=h; }
    op.pitch=w*cpp; op.data=(u32)p;
    return submit(api,a,gen,&op,1,&fence);
}
#define CALL(stage, expr) do { rc=(expr); if (rc) { fail(api,stage,rc); goto out; } } while (0)
#define REQUIRE(stage, cond) do { if (!(cond)) { fail(api,stage,1); goto out; } } while (0)
static int exercise(KernelAPI *api, u32 a, GFX2_AdapterInfo *info)
{
    int rc=0, result=1, gf=info->backend_kind==OS32_SURFACE_GEFORCE;
    u32 gen=0, oldgen, ids[4]={0}, token=0, fence=0, done, how;
    GFX2_Caps caps; MemStat before={0}, after;
    REQUIRE("mem-before",api->mem_stat(0,&before,sizeof(before))==(int)sizeof(before));
    CALL("gen",api->gfx2_adapter_gen(a,&gen));
    CALL("caps",api->gfx2_caps(a,&caps,sizeof(caps)));
    api->kprintf(ATTR,"gfx2_rh caps hw=%x sw=%x exact=%x vram=%u sync=%u profile=%s\n",caps.caps2d_hw,caps.caps2d_sw,caps.exact,caps.vram_bytes,caps.sync_mode,caps.sync_profile);
    REQUIRE("caps-common",caps.caps2d_sw==0x7fe && caps.exact==0x6f6 && caps.fmt_scanout==0x0e && caps.max_cmds==64);
    REQUIRE("caps-backend",caps.caps2d_hw==(gf?0x7e:0) && caps.fence_hw==(u32)gf && caps.async==(u32)gf && caps.heads==(u32)(gf?2:1) && caps.connectors==(u32)(gf?2:1) && caps.tier3d_hw==(u32)(gf?3:0) && caps.gart_bytes==(gf?8192u*1024:0) && caps.cpu_direct==(u32)!gf && caps.sync_mode==(u32)!gf);
    if (gf) REQUIRE("caps-gf",caps.max_w==4096 && caps.max_h==4096 && caps.pitch_align==64 && !caps.sync_profile[0]);
    {
        static const struct { u8 op,rop,flags,fmt,pool,expected_gf,expected_tr; } cases[]={
            {1,0xf0,0,1,1,2,1},{1,0x5a,GFX2_F_PAT,2,1,2,1},{1,0x5a,0,3,1,0,1},
            {2,0xcc,GFX2_F_SRCKEY,1,1,0,1},{2,0xcc,GFX2_F_SRCKEY,1,2,1,1},
            {3,0xcc,0,1,1,0,1},{3,0xcc,0,2,1,2,1},{6,0,0,1,1,2,1},
            {6,0,0,1,2,1,1},{5,0,0,1,1,2,1},{5,0,0,1,2,1,1},
            {9,0,0,1,1,0,1},{7,0,0,1,1,0,1},{8,0,0,1,1,0,1}
        };
        for (u32 i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
            const u32 pool=gf?cases[i].pool:GFX2_POOL_SYSMEM;
            GFX2_Op op={.op=cases[i].op,.rop=cases[i].rop,.flags=cases[i].flags,.dw=16,.dh=16,.sw=16,.sh=16};
            if (!gf && cases[i].pool==GFX2_POOL_GART) continue;
            CALL("check-op",api->gfx2_check_op(a,&op,cases[i].fmt,cases[i].fmt,pool,pool,&how));
            REQUIRE("check-how",how==(gf?cases[i].expected_gf:cases[i].expected_tr));
        }
    }
    {
        static const struct { u8 op,rop,pat; } cases[]={
            {1,0x5a,1},{1,0xa0,2},{1,0x0f,0},{1,0x55,0},
            {2,0x66,0},{2,0x33,0},{2,0xb8,1},{2,0xca,2}};
        for (u32 poolidx=0;poolidx<(u32)(gf?2:1);poolidx++) for (u32 fmt=1;fmt<=2;fmt++) {
            u32 pool=gf && !poolidx?GFX2_POOL_VRAM:GFX2_POOL_SYSMEM, cpp=fmt==1?1:2;
            u32 mask=fmt==1?255:65535, fg=fmt==1?0x2d:0xf81f, bg=fmt==1?0x96:0x07e0;
            CALL("rop-dst",api->gfx2_surface_create(a,64,16,fmt,pool,GFX2_USE_RENDER2D,&ids[0]));
            CALL("rop-src",api->gfx2_surface_create(a,64,16,fmt,pool,GFX2_USE_RENDER2D,&ids[1]));
            for (u32 i=0;i<64;i++) color[i]=fmt==1?(i*37+11)&255:(i*0x421+0x1234)&65535;
            for (u32 y=0;y<16;y++) for (u32 x=0;x<64;x++) put(source,y*64+x,cpp,initial(x,y,fmt,1));
            CALL("rop-source",transfer(api,a,gen,ids[1],64,16,cpp,source,0));
            for (u32 c=0;c<8;c++) {
                GFX2_Op ops[2]={{0}}; u32 k=cases[c].pat?1:0;
                for (u32 y=0;y<16;y++) for (u32 x=0;x<64;x++) put(data,y*64+x,cpp,initial(x,y,fmt,0));
                CALL("rop-init",transfer(api,a,gen,ids[0],64,16,cpp,data,0));
                if (k) ops[0]=(GFX2_Op){.op=GFX2_OP_SET_PATTERN,.flags=cases[c].pat==2?GFX2_F_PAT_COLOR:0,.data=(u32)(cases[c].pat==2?(void *)color:(void *)mono),.fg={.v=fg},.bg={.v=bg}};
                ops[k]=(GFX2_Op){.op=cases[c].op,.rop=cases[c].rop,.flags=k?GFX2_F_PAT:0,.dst=ids[0],.src=cases[c].op==2?ids[1]:0,.dx=3,.dy=5,.dw=37,.dh=9,.fg={.v=fmt==1?0x4b:0x1234}};
                if (cases[c].op==2) { ops[k].sx=9; ops[k].sy=2; ops[k].sw=37; ops[k].sh=9; }
                CALL("rop-submit",submit(api,a,gen,ops,k+1,&fence));
                CALL("rop-read",transfer(api,a,gen,ids[0],64,16,cpp,buf,1));
                for (u32 y=0;y<16;y++) for (u32 x=0;x<64;x++) {
                    u32 want=initial(x,y,fmt,0), got=get(buf,y*64+x,cpp);
                    if (x>=3 && x<40 && y>=5 && y<14) {
                        u32 p=k?ref_pat(x,y,cases[c].pat,fg,bg):ops[k].fg.v;
                        want=ref_rop3(p,initial(x-3+9,y-5+2,fmt,1),want,cases[c].rop)&mask;
                    }
                    if (got!=want) { api->kprintf(ATTR,"gfx2_rh pixel x=%u y=%u got=%x want=%x\n",x,y,got,want); fail(api,"rop-pixel",c); goto out; }
                }
            }
            CALL("rop-free-d",api->gfx2_surface_free(ids[0])); ids[0]=0;
            CALL("rop-free-s",api->gfx2_surface_free(ids[1])); ids[1]=0;
        }
        CALL("rop32-create",api->gfx2_surface_create(a,64,16,GFX2_FMT_XRGB8888,gf?GFX2_POOL_VRAM:GFX2_POOL_SYSMEM,GFX2_USE_RENDER2D,&ids[0]));
        GFX2_Op ops[2]={{.op=GFX2_OP_SET_PATTERN,.data=(u32)mono,.fg={.v=0x2d},.bg={.v=0x96}}, {.op=GFX2_OP_FILL,.rop=0x5a,.flags=GFX2_F_PAT,.dst=ids[0],.dw=37,.dh=9}};
        rc=api->gfx2_submit(a,ops,2,gen,&done,&fence);
        REQUIRE("rop32",gf?rc==OS32_ERR_NOSYS:rc==0 && done==2);
        CALL("rop32-free",api->gfx2_surface_free(ids[0])); ids[0]=0;
    }
    {
        GFX2_Op ops[64]; u32 prev=0, completed, failed, start;
        CALL("fence-create",api->gfx2_surface_create(a,64,8,1,gf?1:3,GFX2_USE_RENDER2D,&ids[0]));
        CALL("fence-initial",submit(api,a,gen,ops,0,&prev));
        start=api->get_tick();
        for (u32 b=0;b<8;b++) {
            for (u32 k=0;k<64;k++) ops[k]=(GFX2_Op){.op=GFX2_OP_FILL,.rop=0xf0,.dst=ids[0],.dx=k,.dy=b,.dw=1,.dh=1,.fg={.v=((b*64+k)*7+1)&255}};
            CALL("fence-submit",submit(api,a,gen,ops,64,&fence));
            REQUIRE("fence-sequence",gf?fence && fence==prev+64:fence==prev); prev=fence;
        }
        CALL("fence-wait",api->gfx2_fence_wait(a,fence,WAIT_MS));
        u32 ticks=api->get_tick()-start;
        api->kprintf(ATTR,"gfx2_rh fence n=512 ticks=%u us_per=%u\n",ticks,ticks*10000/512);
        CALL("fence-query",api->gfx2_fence_query(a,&completed,&failed));
        REQUIRE("fence-state",!failed && (i32)(completed-fence)>=0);
        CALL("fence-read",transfer(api,a,gen,ids[0],64,8,1,buf,1));
        for (u32 i=0;i<512;i++) REQUIRE("fence-pixel",buf[i]==((i*7+1)&255));
        CALL("fence-free",api->gfx2_surface_free(ids[0])); ids[0]=0;
    }
    if (gf) {
        CALL("gart-vram",api->gfx2_surface_create(a,256,64,1,1,GFX2_USE_RENDER2D,&ids[1]));
        CALL("gart-vram2",api->gfx2_surface_create(a,256,64,1,1,GFX2_USE_RENDER2D,&ids[2]));
        for (u32 round=0;round<2;round++) {
            CALL("gart-create",api->gfx2_surface_create(a,256,64,1,2,GFX2_USE_RENDER2D,&ids[0]));
            for (u32 y=0;y<64;y++) for (u32 x=0;x<256;x++) data[y*256+x]=(x^(y*3))&255;
            CALL("gart-upload",transfer(api,a,gen,ids[0],256,64,1,data,0));
            GFX2_Op op={.op=GFX2_OP_COPY,.rop=0xcc,.src=ids[0],.dst=ids[1],.sw=256,.sh=64,.dw=256,.dh=64};
            CALL("gart-to-vram",submit(api,a,gen,&op,1,&fence));
            CALL("gart-read-v",transfer(api,a,gen,ids[1],256,64,1,buf,1));
            for (u32 i=0;i<256*64;i++) REQUIRE("gart-pattern",buf[i]==data[i]);
            op=(GFX2_Op){.op=GFX2_OP_FILL,.rop=0xf0,.dst=ids[1],.dw=256,.dh=64,.fg={.v=0x44}};
            CALL("gart-fill",submit(api,a,gen,&op,1,&fence));
            op=(GFX2_Op){.op=GFX2_OP_COPY,.rop=0xcc,.src=ids[1],.dst=ids[0],.sw=256,.sh=64,.dw=256,.dh=64};
            CALL("gart-from-vram",submit(api,a,gen,&op,1,&fence));
            CALL("gart-read-g",transfer(api,a,gen,ids[0],256,64,1,buf,1));
            for (u32 i=0;i<256*64;i++) REQUIRE("gart-fill-pixel",buf[i]==0x44);
            op.dst=ids[2];
            CALL("gart-rebind-vram",submit(api,a,gen,&op,1,&fence));
            CALL("gart-read-v2",transfer(api,a,gen,ids[2],256,64,1,buf,1));
            for (u32 i=0;i<256*64;i++) REQUIRE("gart-vram-pixel",buf[i]==0x44);
            CALL("gart-free",api->gfx2_surface_free(ids[0])); ids[0]=0;
        }
        CALL("gart-free-v",api->gfx2_surface_free(ids[1])); ids[1]=0;
        CALL("gart-free-v2",api->gfx2_surface_free(ids[2])); ids[2]=0;
    } else api->kprintf(ATTR,"gfx2_rh g8=n/a gart=0\n");
    {
        u32 pool=gf?GFX2_POOL_GART:GFX2_POOL_SYSMEM;
        CALL("mixed-g",api->gfx2_surface_create(a,64,16,1,pool,GFX2_USE_RENDER2D,&ids[0]));
        CALL("mixed-g2",api->gfx2_surface_create(a,64,16,1,pool,GFX2_USE_RENDER2D,&ids[1]));
        CALL("mixed-c",api->gfx2_surface_create(a,64,16,1,gf?1:3,GFX2_USE_RENDER2D,&ids[2]));
        for (u32 i=0;i<1024;i++) { data[i]=0x33; source[i]=0x22; }
        GFX2_Op ops[6]={
            {.op=GFX2_OP_HOSTDATA,.dst=ids[0],.dw=64,.dh=16,.pitch=64,.data=(u32)data},
            {.op=GFX2_OP_HOSTDATA,.dst=ids[1],.dw=64,.dh=16,.pitch=64,.data=(u32)source},
            {.op=GFX2_OP_FILL,.rop=0xf0,.dst=ids[0],.dw=32,.dh=16,.fg={.v=0x11}},
            {.op=GFX2_OP_COPY,.rop=0xcc,.flags=GFX2_F_SRCKEY,.src=ids[0],.dst=ids[1],.sw=64,.sh=16,.dw=64,.dh=16,.skey={.v=0x33,.x=0x33}},
            {.op=GFX2_OP_COPY,.rop=0xcc,.src=ids[1],.dst=ids[2],.sw=64,.sh=16,.dw=64,.dh=16},
            {.op=GFX2_OP_READBACK,.src=ids[2],.sw=64,.sh=16,.pitch=64,.data=(u32)buf}};
        CALL("mixed-submit",submit(api,a,gen,ops,6,&fence));
        for (u32 i=0;i<1024;i++) REQUIRE("mixed-pixel",buf[i]==(i%64<32?0x11:0x22));
        for (u32 i=0;i<3;i++) { CALL("mixed-free",api->gfx2_surface_free(ids[i])); ids[i]=0; }
    }
    {
        OS32_SurfaceQueryResult q; OS32_LeaseView lease; GFX2_SurfaceDesc desc;
        u32 display, start, fillticks, allticks, seen=0, first=0;
        CALL("display-query",api->surface_query(OS32_SURFACE_DISPLAY,&q));
        REQUIRE("display-count",q.count==1);
        CALL("display-lease",api->surface_lease(OS32_SURFACE_DISPLAY,&q.desc[0].ref,OS32_SURFACE_RW,&lease)); token=lease.token;
        CALL("display",api->gfx2_screen_surface(a,0,GFX2_ROLE_DISPLAY,&display));
        CALL("display-desc",api->gfx2_surface_desc(display,&desc));
        GFX2_Op op={.op=GFX2_OP_FILL,.rop=0xf0,.dst=display,.dw=desc.w,.dh=desc.h,.fg={.v=5}};
        start=api->get_tick();
        for (u32 i=0;i<10;i++) CALL("present-fill",submit(api,a,gen,&op,1,&fence));
        fillticks=api->get_tick()-start; start=api->get_tick();
        for (u32 i=0;i<10;i++) { CALL("present-fill-all",submit(api,a,gen,&op,1,&fence)); CALL("present",api->gfx2_present(a,0,0,0,desc.w,desc.h)); }
        allticks=api->get_tick()-start;
        u32 bytes=desc.w*desc.h*(desc.fmt==1?1:desc.fmt==2?2:4);
        u32 us=allticks>=fillticks?(allticks-fillticks)*1000:0;
        api->kprintf(ATTR,"gfx2_rh present n=10 bytes=%u fill_ticks=%u all_ticks=%u us_per=%u ns_per_byte=%u ref_us=%u%s\n",bytes,fillticks,allticks,us,us<=4000000?us*1000/bytes:0,bytes/4*80/1000,gf?" sync=none":"");
        start=api->get_tick();
        for (u32 i=0;i<16;i++) {
            u32 old=seen; rc=api->gfx2_vblank_wait(a,0,seen,WAIT_MS,&seen);
            if (!gf) { REQUIRE("vblank-nosys",rc==OS32_ERR_NOSYS); break; }
            REQUIRE("vblank",!rc && (i32)(seen-old)>0); if (!i) first=old;
        }
        if (gf) { u32 ticks=api->get_tick()-start; api->kprintf(ATTR,"gfx2_rh vblank n=16 ticks=%u us_per=%u seen_delta=%u\n",ticks,ticks*10000/16,seen-first); }
        CALL("display-unlease",api->surface_unlease(token)); token=0;
    }
    oldgen=gen; api->gfx_shutdown(); api->gfx_init();
    {
        int found=-1, n=api->gfx2_adapter_count(); GFX2_AdapterInfo next;
        for (int i=0;i<n;i++) { CALL("reinit-info",api->gfx2_adapter_info(i,&next)); if (next.flags&GFX2_AF_ACTIVE) { found=i; break; } }
        REQUIRE("reinit-adapter",found==(int)a && next.backend_kind==info->backend_kind);
        CALL("reinit-gen",api->gfx2_adapter_gen(a,&gen));
        api->kprintf(ATTR,"gfx2_rh reinit gen=%u->%u\n",oldgen,gen);
        if (gf) REQUIRE("reinit-generation",gen!=oldgen);
        CALL("reinit-create",api->gfx2_surface_create(a,64,8,1,gf?1:3,GFX2_USE_RENDER2D,&ids[0]));
        GFX2_Op op={.op=GFX2_OP_FILL,.rop=0xf0,.dst=ids[0],.dw=64,.dh=8,.fg={.v=0x5a}};
        if (gf) {
            REQUIRE("reinit-stale",api->gfx2_submit(a,&op,1,oldgen,&done,&fence)==OS32_ERR_STALE);
            CALL("reinit-check",api->gfx2_check_op(a,&op,1,1,1,1,&how)); REQUIRE("reinit-hw",how==GFX2_HOW_HW);
        }
        CALL("reinit-fill",submit(api,a,gen,&op,1,&fence));
        CALL("reinit-read",transfer(api,a,gen,ids[0],64,8,1,buf,1));
        for (u32 i=0;i<512;i++) REQUIRE("reinit-pixel",buf[i]==0x5a);
        CALL("reinit-free",api->gfx2_surface_free(ids[0])); ids[0]=0;
    }
    result=0;
out:
    if (token && (rc=api->surface_unlease(token))) result=fail(api,"cleanup-lease",rc);
    for (u32 i=0;i<4;i++) if (ids[i] && (rc=api->gfx2_surface_free(ids[i]))) result=fail(api,"cleanup-surface",rc);
    if (api->mem_stat(0,&after,sizeof(after))!=(int)sizeof(after) || after.audit_fail!=before.audit_fail) result=fail(api,"mem-audit",1);
    return result;
}
