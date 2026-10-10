/* App/SDK/kernel adapters are staged from gfx_attach_host.c. */
#define OS32_R1_FIXTURE
#include "r1_fixture.h"
volatile int ring3_in_syscall;
static void host_raise_ud(void) { CHECK(0); }
#undef slot
#include "fixture_source.c"
#define slot host_slots[2]
static int waits;
static void __cdecl wait_once(void) { waits++; }
static void run(void) __attribute__((used));
static void run(void)
{
    u32 owner;
    CallerAccessFrame prev;
    map_host(0x10000,0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE,WAB_XE10_LINEARWIN_SIZE);
    map_host(MEM_LEASE_BASE,0x100000);
    boot();gfx_boot_reserve();
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"r1-lease",&owner));
    CHECK(!paging_addrspace_create_lease(&space,owner));
    payload=pgalloc_alloc_phys(owner,1);
    CHECK(payload && !paging_addrspace_map_user(&space,MEM_EXEC_LOAD_ADDR,payload,PAGE_RW|PTE_USER));
    slot.state=APP_STATE_RUNNING;slot.cpl3=1;slot.as=&space;slot.hdr_flags=OS32X_FLAG_GFX;
    host_cr3=space.pd_phys;CHECK(caller_access_enter(&prev,CALLER_USER));
    api.mem_alloc=sdk_alloc;api.version=69;api.gfx_get_framebuffer=legacy_fb;api.gfx_screen_info=screen;
    api.gfx_add_dirty_rect=dirty;api.sys_halt=wait_once;
    gfx_api=&api;gfx_attach_port=&port;
    gfx_set_backend_pref(GFX_PREF_PC98);gfx_prepare_backend();gfx_init();
    /* No arm, wrong magic/id/generation/role/AS: normal acquisition succeeds. */
    r1_fixture_id[R1_FIXTURE_LEASE_FAIL]=2;
    r1_fixture_generation[R1_FIXTURE_LEASE_FAIL]=space.generation;
    for (u32 bad=0;bad<6;bad++) {
        r1_fixture_arm[R1_FIXTURE_LEASE_FAIL]=R1_FIXTURE_ARM;
        if(bad==0) r1_fixture_arm[R1_FIXTURE_LEASE_FAIL]=0;
        if(bad==1) r1_fixture_arm[R1_FIXTURE_LEASE_FAIL]=1;
        if(bad==2) r1_fixture_id[R1_FIXTURE_LEASE_FAIL]=3;
        if(bad==3) r1_fixture_generation[R1_FIXTURE_LEASE_FAIL]++;
        if(bad<4) {
            CHECK(!libos32gfx_attach_checked() && gfx_ready && live()==1);
            libos32gfx_detach(); CHECK(!live());
        } else if(bad==4) CHECK(!r1_fixture_lease(&space,LEDGER_ROLE_UNICODE));
        else CHECK(!r1_fixture_lease(&(struct addrspace){0},LEDGER_ROLE_CLIENT));
        if(bad>=2) CHECK(r1_fixture_arm[R1_FIXTURE_LEASE_FAIL]==R1_FIXTURE_ARM);
        r1_fixture_id[R1_FIXTURE_LEASE_FAIL]=2;
        r1_fixture_generation[R1_FIXTURE_LEASE_FAIL]=space.generation;
    }
    u32 pages=ledger_owner_pages(owner), free0=pgalloc_free_pages();
    r1_fixture_arm[R1_FIXTURE_LEASE_FAIL]=R1_FIXTURE_ARM;
    CHECK(libos32gfx_attach_checked()==OS32_ERR_FULL);
    CHECK(!r1_fixture_arm[R1_FIXTURE_LEASE_FAIL] && !gfx_ready && !live());
    CHECK(ledger_owner_pages(owner)==pages && pgalloc_free_pages()==free0);
    u32 old=draws, oldasm=asm_calls;
    sdk_gfx_clear(1); sdk_gfx_pixel(0,0,1);
    CHECK(draws==old && asm_calls==oldasm);
    libos32gfx_halt(); /* Real wait-return check retries attachment. */
    CHECK(waits==1 && gfx_ready && live()==1);
    sdk_gfx_pixel(0,0,3); CHECK(sdk_gfx_get_pixel(0,0)==3);
    u32 token=space.leases[0].token;
    libos32gfx_halt();
    CHECK(waits==2 && gfx_ready && live()==1 && space.leases[0].token==token);
    CHECK(!ledger_irq_ops && !ledger_exc_ops);
    SAY("r1 lease: one failure, drawing disabled, next wait recovers PASS");
    die(0);
}
