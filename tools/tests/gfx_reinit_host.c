/* e5 real lifecycle, ledger, paging and lease; three appslot-equivalent ASes. */
unsigned int host_arch_if = 0x202U;
#define run e4_run
#include "gfx_kernel_fb_host.c"
#undef run
static struct addrspace other, third;
static u32 master_image[64 * PTE_COUNT], third_image[64 * PTE_COUNT];
static struct as_lease third_leases[MEM_LEASE_MAX];
static u32 watching, caller_flags, caller_root, expected_gen[LEDGER_MAX_SURFACES], seen_bind;
extern volatile u32 gfx_reinit_fail_count;
static int inject_acquire;
static struct surface_ref gap_ref;
static struct lease_view gap_view;
static void pixels_prepare(void)
{
    for (u32 i=0;i<4;i++) kmemset(bb[i],0xa5,GFX_PLANE_SZ);
}
static void pixels_check(u32 size)
{
    for (u32 i=0;i<4;i++) {
        u8 *pixels=bb[i];
        for (u32 j=0;j<size;j++) CHECK(pixels[j] == 0);
        for (u32 j=size;j<GFX_PLANE_SZ;j++) CHECK(pixels[j] == 0xa5);
    }
}

static void image_tables(u32 root, u32 *image, int compare)
{
    u32 *pd=P2V(root), n=0;
    for (u32 i=0;i<PDE_COUNT;i++) {
        if (compare) CHECK(pd[i] == image[n]); else image[n]=pd[i];
        n++;
    }
    for (u32 i=0;i<PDE_COUNT;i++) if (pd[i]&PTE_PRESENT) {
        u32 *pt=P2V(pd[i]&~0xfffU);
        for (u32 j=0;j<PTE_COUNT;j++) {
            CHECK(n < 64*PTE_COUNT);
            if (compare) CHECK(pt[j] == image[n]); else image[n]=pt[j];
            n++;
        }
    }
}
void host_reinit_event(int event)
{
    struct surface_query_source source;
    if (!watching) return;
    CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&source) == OS32_ERR_INVAL && !source.ready);
    CHECK(surface_query_refs(&source,source.refs,1,LEDGER_PERM_RW) == OS32_ERR_INVAL);
    CHECK(surface_lease(&source,0,LEDGER_PERM_RW,0) == OS32_ERR_INVAL);
    if (event == 1) {
        CHECK(host_cr3 == caller_root && host_arch_if == caller_flags);
        for (u32 i=0;i<LEDGER_MAX_SURFACES;i++)
            if (ledger_surfaces[i].backend == LEDGER_SF_PC98) {
                CHECK(!ledger_surfaces[i].lease_count);
                CHECK(ledger_surfaces[i].gen == expected_gen[i]);
            }
        seen_bind=1;
        if (inject_acquire) {
            struct lease_authority auth={space.owner,LEDGER_SF_PC98,LEDGER_ROLE_CLIENT};
            CHECK(!lease_acquire(&space,&auth,&gap_ref,1,LEDGER_PERM_RW,&gap_view));
        }
    } else {
        CHECK(seen_bind);
        CHECK(host_cr3 == paging_kernel_pd_phys() && !(host_arch_if&X86_EFLAGS_IF));
    }
}
static struct surface_ref ref_of(struct ledger_surface *sf)
{
    return (struct surface_ref){(u32)(sf-ledger_surfaces),sf->gen};
}
static void borrow(struct addrspace *as, struct surface_ref *refs, u32 count, u32 role,
                   u32 backend, struct lease_view *views)
{
    struct lease_authority auth={as->owner,backend,role};
    CHECK(!lease_acquire(as,&auth,refs,count,LEDGER_PERM_RW,views));
}
static void run(void) __attribute__((used));
static void run(void)
{
    struct surface_query_source client, display, fresh;
    struct lease_view av, bv, dv[4], cv;
    u32 owner, phys, pages;
    CallerAccessFrame previous;
    map_host(0x10000,0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE,WAB_XE10_LINEARWIN_SIZE);
    boot(); gfx_boot_reserve();
    struct addrspace *spaces[3]={&space,&other,&third};
    for (u32 i=0;i<3;i++) {
        CHECK(ledger_owner_new(LEDGER_KIND_AS,i+2,"e5",&owner));
        CHECK(!paging_addrspace_create_lease(spaces[i],owner));
        host_slots[i+2].state=APP_STATE_RUNNING;
        host_slots[i+2].cpl3=1;host_slots[i+2].as=spaces[i];
        host_slots[i+2].hdr_flags=OS32X_FLAG_GFX;
    }
    host_cr3=space.pd_phys;
    CHECK(caller_access_enter(&previous,CALLER_USER));
    host_cr3=paging_kernel_pd_phys();
    gfx_set_backend_pref(GFX_PREF_PC98);gfx_init();
    struct ledger_surface *pc=ledger_surface_find(LEDGER_SF_PC98,LEDGER_ROLE_CLIENT);
    struct ledger_surface *pegc=ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_CLIENT);
    struct surface_ref unrelated=ref_of(pegc);
    borrow(&third,&unrelated,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PEGC,&cv);
    kmemcpy(third_leases,third.leases,sizeof(third_leases));
    image_tables(paging_kernel_pd_phys(),master_image,0);
    image_tables(third.pd_phys,third_image,0);
    phys=pc->first;pages=pc->npages;
    for (u32 mode=0;mode<3;mode++) {
        CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT,&client));
        CHECK(!gfx_surface_source(LEDGER_ROLE_DISPLAY,&display));
        borrow(&space,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&av);
        borrow(&other,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&bv);
        borrow(&other,display.refs,4,LEDGER_ROLE_DISPLAY,LEDGER_SF_PC98,dv);
        for (u32 i=0;i<LEDGER_MAX_SURFACES;i++) expected_gen[i]=ledger_surfaces[i].gen;
        caller_root=mode ? paging_kernel_pd_phys() : space.pd_phys;
        caller_flags=mode == 2 ? 2U : 0x202U;
        host_arch_if=caller_flags;
        host_cr3=caller_root;watching=1;seen_bind=0;
        pixels_prepare();
        gfx_init_200();
        watching=0;
        CHECK(host_cr3 == caller_root && host_arch_if == caller_flags);
        host_arch_if=0x202U;
        CHECK(pc->height == GFX_HEIGHT && pc->first == phys && pc->npages == pages);
        CHECK(pc->plane_offset[1] == GFX_PLANE_SZ);
        CHECK(bb_r == bb_b+GFX_PLANE_SZ);
        pixels_check(GFX_PLANE_SZ_200);
        CHECK(pc->gen == client.refs[0].generation+1 && !pc->lease_count);
        CHECK(!paging_lease_pte(&space,av.base));
        host_cr3=paging_kernel_pd_phys();
        CHECK(!paging_lease_pte(&other,bv.base));
        CHECK(lease_release(&space,av.token) == LEASE_INVAL);
        CHECK(lease_release(&other,bv.token) == LEASE_INVAL);
        for (u32 i=0;i<4;i++) {
            CHECK(!paging_lease_pte(&other,dv[i].base));
            CHECK(lease_release(&other,dv[i].token) == LEASE_INVAL);
            CHECK(ledger_surfaces[display.refs[i].sid].height == GFX_HEIGHT);
        }
        struct lease_authority auth={other.owner,LEDGER_SF_PC98,LEDGER_ROLE_CLIENT};
        CHECK(lease_acquire(&other,&auth,client.refs,1,LEDGER_PERM_RW,&bv) == LEASE_STALE);
        host_cr3=space.pd_phys;
        CHECK(surface_query_refs(&client,client.refs,1,LEDGER_PERM_RW) == OS32_ERR_STALE);
        CHECK(!gfx_surface_source(LEDGER_ROLE_DISPLAY,&fresh));
        CHECK(surface_query_refs(&fresh,display.refs,4,LEDGER_PERM_RW) == OS32_ERR_STALE);
        host_cr3=paging_kernel_pd_phys();
        image_tables(paging_kernel_pd_phys(),master_image,1);
        image_tables(third.pd_phys,third_image,1);
        CHECK(pegc->gen == unrelated.generation && pegc->lease_count == 1);
        for(u32 i=0;i<sizeof(third_leases);i++)
            CHECK(((u8 *)third_leases)[i] == ((u8 *)third.leases)[i]);
        pixels_prepare();
        gfx_init();
        pixels_check(GFX_PLANE_SZ);
        CHECK(pc->height == GFX_HEIGHT && pc->plane_offset[1] == GFX_PLANE_SZ);
    }
    /* Internal acquire bypasses the closed publisher between the two phases.
     * It can succeed with the old generation, but regen must refuse publication. */
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT,&client));
    borrow(&space,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&av);
    gap_ref=client.refs[0];
    for (u32 i=0;i<LEDGER_MAX_SURFACES;i++) expected_gen[i]=ledger_surfaces[i].gen;
    caller_root=space.pd_phys;caller_flags=0x202U;
    host_cr3=caller_root;host_arch_if=caller_flags;
    u32 gap_fails=gfx_reinit_fail_count;
    watching=1;seen_bind=0;inject_acquire=1;
    gfx_init_200();
    watching=0;inject_acquire=0;
    CHECK(host_cr3 == caller_root && host_arch_if == caller_flags);
    CHECK(pc->gen == gap_ref.generation && pc->lease_count == 1);
    CHECK(gfx_reinit_fail_count == gap_fails+1);
    CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&fresh) == OS32_ERR_INVAL);
    CHECK(lease_release(&space,av.token) == LEASE_INVAL);
    for (u32 i=0;i<MEM_LEASE_MAX;i++) CHECK(space.leases[i].token != av.token);
    CHECK(gap_view.token != av.token);
    CHECK(!lease_release(&space,gap_view.token));
    host_cr3=paging_kernel_pd_phys();gfx_init();
    /* A failed AS does not restore A's successfully revoked token. */
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT,&client));
    borrow(&space,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&av);
    borrow(&other,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&bv);
    u32 *bpd=P2V(other.pd_phys), di=MEM_LEASE_BASE>>22, saved=bpd[di];
    bpd[di]=0;
    u32 revoke_fails=lease_revoke_fail_count;
    u32 gfx_fails=gfx_reinit_fail_count;
    gfx_init();
    CHECK(gfx_reinit_fail_count == gfx_fails+1);
    CHECK(lease_revoke_fail_count == revoke_fails+1);
    CHECK(!space.leases[0].token);
    CHECK(lease_release(&space,av.token) == LEASE_INVAL);
    CHECK(pc->lease_count == 1 && pc->gen == client.refs[0].generation);
    CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&fresh) == OS32_ERR_INVAL);
    bpd[di]=saved;
    gfx_init();
    CHECK(!pc->lease_count && pc->gen == client.refs[0].generation+1);
    CHECK(lease_release(&other,bv.token) == LEASE_INVAL);
    /* A broken first lease must not suppress revocation of the next slot. */
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT,&client));
    borrow(&other,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&bv);
    borrow(&other,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&av);
    saved=other.leases[0].base;
    other.leases[0].base=MEM_LEASE_END-PAGE_SIZE;
    revoke_fails=lease_revoke_fail_count;
    CHECK(lease_revoke_all(&other) == 1);
    CHECK(lease_revoke_fail_count == revoke_fails+1);
    CHECK(other.leases[0].token == bv.token && !other.leases[1].token);
    CHECK(pc->lease_count == 1);
    other.leases[0].base=saved;
    CHECK(!lease_revoke_all(&other));
    /* Prepare, shutdown and post-init fallback renew both affected backends. */
    u32 before=pc->gen;
    gfx_prepare_backend();CHECK(pc->gen == before+1);
    CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&fresh) == OS32_ERR_INVAL);
    gfx_init();before=pc->gen;gfx_shutdown();CHECK(pc->gen == before+1);
    gfx_set_backend_pref(GFX_PREF_CIRRUS);gfx_init();
    struct ledger_surface *cir=ledger_surface_find(LEDGER_SF_CIRRUS,LEDGER_ROLE_CLIENT);
    u32 cirgen=cir->gen;before=pc->gen;fail_after_init=1;
    gfx_init();
    CHECK(gfx_sf_backend() == LEDGER_SF_PC98 && pc->gen == before+1 && cir->gen == cirgen+1);
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT,&fresh));
    fail_after_init=0;gfx_set_backend_pref(GFX_PREF_PC98);
    before=pc->gen;
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT,&client));
    borrow(&other,client.refs,1,LEDGER_ROLE_CLIENT,LEDGER_SF_PC98,&bv);
    host_cr3=space.pd_phys;host_arch_if=2U;
    gfx_client_to_gshell();
    CHECK(host_cr3 == space.pd_phys && host_arch_if == 2U);
    host_cr3=paging_kernel_pd_phys();host_arch_if=0x202U;
    CHECK(pc->owner == LEDGER_OWNER_GSHELL && pc->gen == before+1 && !pc->lease_count);
    CHECK(lease_release(&other,bv.token) == LEASE_INVAL);
    gfx_client_to_gshell();CHECK(pc->gen == before+1);
    /* Exhausted generation remains unpublishable; kernel alias stays usable. */
    pc->gen=0xffffffffU;
    u32 fails=gfx_reinit_fail_count;
    gfx_init();
    CHECK(pc->gen == 0xffffffffU && gfx_reinit_fail_count > fails);
    CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&client) == OS32_ERR_INVAL);
    CHECK(bb_b == P2V(phys*PAGE_SIZE));
    /* regen cannot change identity, free RAM, zero pixels, or run off master. */
    u32 sid=(u32)(pegc-ledger_surfaces), gen=pegc->gen;
    CHECK(!ledger_surface_regen(sid,0));
    CHECK(!lease_release(&third,cv.token));
    ((u8 *)P2V(pegc->first*PAGE_SIZE))[0]=0x5a;
    host_cr3=space.pd_phys;CHECK(!ledger_surface_regen(sid,0));
    host_cr3=paging_kernel_pd_phys();CHECK(ledger_surface_regen(sid,0));
    CHECK(pegc->gen == gen+1 && ((u8 *)P2V(pegc->first*PAGE_SIZE))[0] == 0x5a);
    struct ledger_surface bad=*pegc, prior=*pegc;
    bad.height=0;
    CHECK(!ledger_surface_regen(sid,&bad));
    for (u32 i=0;i<sizeof(prior);i++) CHECK(((u8 *)&prior)[i] == ((u8 *)pegc)[i]);
    pegc->closing=1;CHECK(!ledger_surface_regen(sid,0));pegc->closing=0;
    /* Packed reinit originates in CPL=3 caller AS, with IF restored. */
    host_cirrus_available=1;
    const int prefs[2]={GFX_PREF_PEGC,GFX_PREF_CIRRUS};
    const u32 backends[2]={LEDGER_SF_PEGC,LEDGER_SF_CIRRUS};
    for (u32 k=0;k<2;k++) {
        host_cr3=space.pd_phys;
        gfx_set_backend_pref(prefs[k]);gfx_init();
        host_cr3=paging_kernel_pd_phys();
        struct ledger_surface *sf=ledger_surface_find(backends[k],LEDGER_ROLE_CLIENT);
        CHECK(gfx_sf_backend() == backends[k]);
        struct surface_ref ref=ref_of(sf);
        borrow(&space,&ref,1,LEDGER_ROLE_CLIENT,backends[k],&av);
        borrow(&other,&ref,1,LEDGER_ROLE_CLIENT,backends[k],&bv);
        host_cr3=space.pd_phys;host_arch_if=0x202U;
        gfx_init();
        CHECK(host_cr3 == space.pd_phys && host_arch_if == 0x202U);
        CHECK(sf->gen == ref.generation+1 && !sf->lease_count);
        CHECK(!paging_lease_pte(&space,av.base));
        CHECK(lease_release(&space,av.token) == LEASE_INVAL);
        host_cr3=paging_kernel_pd_phys();
        CHECK(!paging_lease_pte(&other,bv.base));
        CHECK(lease_release(&other,bv.token) == LEASE_INVAL);
        /* Revoke failure (broken base) preserves owner and restores caller IF/CR3. */
        ref=ref_of(sf);owner=sf->owner;
        borrow(&other,&ref,1,LEDGER_ROLE_CLIENT,backends[k],&bv);
        saved=other.leases[0].base;other.leases[0].base=MEM_LEASE_END-PAGE_SIZE;
        host_cr3=space.pd_phys;host_arch_if=k ? 2U : 0x202U;
        gfx_client_to_gshell();
        CHECK(host_cr3 == space.pd_phys && host_arch_if == (k ? 2U : 0x202U));
        CHECK(sf->owner == owner && sf->gen == ref.generation);
        CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&fresh) == OS32_ERR_INVAL);
        host_cr3=paging_kernel_pd_phys();host_arch_if=0x202U;
        other.leases[0].base=saved;CHECK(!lease_revoke_all(&other));
        sf->gen=LEDGER_SURFACE_GEN_MAX;
        host_cr3=space.pd_phys;
        gfx_client_to_gshell();
        CHECK(host_cr3 == space.pd_phys && host_arch_if == 0x202U);
        CHECK(sf->owner == owner && sf->gen == LEDGER_SURFACE_GEN_MAX);
        CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&fresh) == OS32_ERR_INVAL);
        host_cr3=paging_kernel_pd_phys();
    }
    SAY("PASS gfx reinit: three ASes, order, not-ready, stale refs/tokens, geometry, exhaustion");
    die(0);
}
