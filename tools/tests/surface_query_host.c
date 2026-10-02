#define HOST_CALLER_COPY_TEST
#include "access_walk_host.c"
#include "surface_query_host_source.c"
static int gui, gfx_owner = 1;
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return gfx_owner; }
static struct surface_query_source source;
static struct surface_query_result *value;
static void query_expect(int rc)
{
    u32 root = host_cr3, flags = host_arch_if;
    struct ledger_surface before[LEDGER_MAX_SURFACES];
    struct addrspace as_before = space;
    kmemcpy(before, ledger_surfaces, sizeof(before));
    for (u32 i = 0; i < sizeof(*value); i++) ((u8 *)value)[i] = 0x55;
    CHECK(surface_query(&source, (void *)MEM_EXEC_LOAD_ADDR) == rc);
    if (rc) for (u32 i = 0; i < sizeof(*value); i++) CHECK(((u8 *)value)[i] == 0x55);
    for (u32 i = 0; i < sizeof(before); i++)
        CHECK(((u8 *)before)[i] == ((u8 *)ledger_surfaces)[i]);
    for (u32 i = 0; i < sizeof(space); i++) CHECK(((u8 *)&space)[i] == ((u8 *)&as_before)[i]);
    CHECK(host_cr3 == root && host_arch_if == flags);
}
static void caller_copy_tests(void)
{
    CallerAccessFrame previous;
    struct surface_ref refs[SURFACE_QUERY_MAX];
    struct caller_access auth;
    u32 backing = pgalloc_alloc_phys(space.owner, 4), sid[4];
    struct ledger_surface sf = {.first = backing / PAGE_SIZE, .npages = 1,
        .owner = space.owner, .backing = LEDGER_SB_RAM, .backend = LEDGER_SF_PC98,
        .role = LEDGER_ROLE_CLIENT, .format = 1, .width = 64, .height = 16,
        .pitch = 8, .planes = 4, .perm_max = LEDGER_PERM_RW,
        .plane_offset = {0, 128, 256, 384}};
    CHECK(backing);
    host_cr3 = paging_kernel_pd_phys();
    for (u32 i = 0; i < 4; i++) {
        sf.first = backing / PAGE_SIZE + i;
        CHECK(ledger_surface_create(&sf, &sid[i]));
        refs[i] = (struct surface_ref){sid[i], ledger_surfaces[sid[i]].gen};
    }
    host_cr3 = space.pd_phys;
    value = P2V(payload);
    source = (struct surface_query_source){LEDGER_ROLE_CLIENT, LEDGER_SF_PC98, 1, 1, {{0}}};
    source.refs[0] = refs[0];
    CHECK(caller_access_enter(&previous, CALLER_USER));
    for (u32 if_on = 0; if_on < 2; if_on++) {
        host_arch_if = if_on ? 0x202 : 2;
        for (u32 mode = 0; mode < 2; mode++) {
            gui = (int)mode;
            for (u32 g = 0; g < 2; g++) {
                slot.hdr_flags = g ? OS32X_FLAG_GFX : 0;
                for (u32 f = 0; f < 2; f++) {
                    gfx_owner = f ? current : 1;
                    for (u32 role = 1; role <= LEDGER_ROLE_UNICODE; role++) {
                        source.role = role;
                        source.backend = role <= LEDGER_ROLE_DISPLAY ? LEDGER_SF_PEGC : 0;
                        ledger_surfaces[sid[0]].role = role;
                        ledger_surfaces[sid[0]].backend = source.backend;
                        ledger_surfaces[sid[0]].perm_max = role == LEDGER_ROLE_UNICODE ? LEDGER_PERM_RO : LEDGER_PERM_RW;
                        int allowed = role == LEDGER_ROLE_CLIENT ? (!mode || !g || f) :
                            role == LEDGER_ROLE_DISPLAY ? (g && (!mode || f)) :
                            role == LEDGER_ROLE_TVRAM ? !mode : 1;
                        query_expect(allowed ? 0 : OS32_ERR_INVAL);
                        CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) ==
                              (allowed ? 0 : OS32_ERR_INVAL));
                        CHECK(host_arch_if == (if_on ? 0x202U : 2U));
                    }
                }
            }
        }
    }
    gui = 0; slot.hdr_flags = 0;
    source.role = LEDGER_ROLE_CLIENT; source.backend = LEDGER_SF_PC98;
    ledger_surfaces[sid[0]].role = LEDGER_ROLE_CLIENT;
    ledger_surfaces[sid[0]].backend = LEDGER_SF_PC98;
    ledger_surfaces[sid[0]].perm_max = LEDGER_PERM_RW;
    for (u32 backend = LEDGER_SF_PC98; backend <= LEDGER_SF_CIRRUS; backend++) {
        source.backend = backend; ledger_surfaces[sid[0]].backend = backend;
        query_expect(0);
    }
    /* Exercise authorize directly: snapshot/backend mismatch must not mask it. */
    unsigned int saved_irq = irq_save();
    for (u32 role = LEDGER_ROLE_CLIENT; role <= LEDGER_ROLE_UNICODE; role++) {
        source.role = role; slot.hdr_flags = OS32X_FLAG_GFX;
        source.backend = role <= LEDGER_ROLE_DISPLAY ? 0 : LEDGER_SF_PC98;
        CHECK(surface_query_authorize(&source, &auth) == OS32_ERR_INVAL);
        if (role <= LEDGER_ROLE_DISPLAY) {
            source.backend = LEDGER_SF_CIRRUS + 1;
            CHECK(surface_query_authorize(&source, &auth) == OS32_ERR_INVAL);
        }
    }
    irq_restore(saved_irq);
    source.role = LEDGER_ROLE_CLIENT; slot.hdr_flags = 0;
    source.backend = LEDGER_SF_PC98; ledger_surfaces[sid[0]].backend = LEDGER_SF_PC98;
    query_expect(0);
    CHECK(value->count == 1 && value->desc[0].ref.sid == sid[0]);
    CHECK(value->desc[0].ref.generation == refs[0].generation);
    CHECK(value->desc[0].role == source.role && value->desc[0].backend == source.backend);
    CHECK(value->desc[0].format == 1 && value->desc[0].width == 64 && value->desc[0].height == 16);
    CHECK(value->desc[0].pitch == 8 && value->desc[0].planes == 4);
    CHECK(value->desc[0].bytes == PAGE_SIZE && value->desc[0].access_max == LEDGER_PERM_RW);
    for (u32 i = 0; i < 4; i++) CHECK(value->desc[0].plane_offset[i] == i * 128);
    for (u32 i = sizeof(value->count) + sizeof(value->desc[0]); i < sizeof(*value); i++) CHECK(((u8 *)value)[i] == 0);
#define BAD(expr, undo) do { expr; query_expect(OS32_ERR_INVAL); undo; } while (0)
    BAD(source.ready = 0, source.ready = 1);
    BAD(source.role = 99, source.role = LEDGER_ROLE_CLIENT);
    BAD(source.backend = LEDGER_SF_CIRRUS, source.backend = LEDGER_SF_PC98);
    BAD(source.count = 0, source.count = 1);
    BAD(source.count = 5, source.count = 1);
    BAD(source.refs[0].sid = LEDGER_MAX_SURFACES, source.refs[0] = refs[0]);
    BAD(source.refs[0].generation = 0, source.refs[0] = refs[0]);
    BAD(ledger_surfaces[sid[0]].perm_max = LEDGER_PERM_NONE, ledger_surfaces[sid[0]].perm_max = LEDGER_PERM_RW);
    BAD(ledger_surfaces[sid[0]].plane_offset[3] = PAGE_SIZE, ledger_surfaces[sid[0]].plane_offset[3] = 384);
    BAD(slot.state = APP_STATE_PARKED, slot.state = APP_STATE_RUNNING);
    BAD(current = 3, current = 2);
    BAD(resource_owner = 3, resource_owner = 2);
    BAD(host_cr3 = paging_kernel_pd_phys(), host_cr3 = space.pd_phys);
    BAD(space.owner++, space.owner--);
    BAD(space.generation++, space.generation--);
    BAD(slot.as = 0, slot.as = &space);
    BAD(slot.cpl3 = 0, slot.cpl3 = 1);
    BAD(ring3_wm_depth = 1, ring3_wm_depth = 0);
    BAD(kctx_irq_depth = 1, kctx_irq_depth = 0);
    BAD(kctx_exc_depth = 1, kctx_exc_depth = 0);
    source.role = LEDGER_ROLE_UNICODE; source.backend = 0;
    ledger_surfaces[sid[0]].role = LEDGER_ROLE_UNICODE; ledger_surfaces[sid[0]].backend = 0;
    query_expect(OS32_ERR_INVAL); /* Unicode publisher must offer RO only. */
    ledger_surfaces[sid[0]].perm_max = LEDGER_PERM_RO; query_expect(0);
    source.role = LEDGER_ROLE_CLIENT; source.backend = LEDGER_SF_PC98;
    ledger_surfaces[sid[0]].role = LEDGER_ROLE_CLIENT; ledger_surfaces[sid[0]].backend = LEDGER_SF_PC98;
    ledger_surfaces[sid[0]].perm_max = LEDGER_PERM_RW;
    source.refs[0].generation++; query_expect(OS32_ERR_STALE); source.refs[0] = refs[0];
    ledger_surfaces[sid[0]].closing = 1; query_expect(OS32_ERR_STALE); ledger_surfaces[sid[0]].closing = 0;
    ledger_surfaces[sid[0]].npages = 0; query_expect(OS32_ERR_STALE); ledger_surfaces[sid[0]].npages = 1;
    refs[0].generation++; CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RW) == OS32_ERR_STALE); refs[0] = source.refs[0];
    CHECK(surface_query_refs(&source, refs, 1, 3) == OS32_ERR_INVAL);
    CHECK(surface_query_refs(&source, refs, 0, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    CHECK(surface_query_refs(&source, 0, 1, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    ledger_surfaces[sid[0]].perm_max = LEDGER_PERM_RO;
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RW) == OS32_ERR_INVAL);
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) == 0);
    ledger_surfaces[sid[0]].perm_max = LEDGER_PERM_RW;
    /* Boot/gshell physical ownership does not grant or deny U. */
    BAD(caller_frame.access.owner = LEDGER_OWNER_BOOT, caller_frame.access.owner = space.owner);
    ledger_surfaces[sid[0]].owner = LEDGER_OWNER_BOOT; query_expect(0);
    ledger_surfaces[sid[0]].owner = LEDGER_OWNER_GSHELL; query_expect(0);
    ledger_surfaces[sid[0]].owner = space.owner;
    caller_access_invalidate(); query_expect(OS32_ERR_INVAL);
    CHECK(caller_access_enter(&previous, CALLER_TRUSTED)); query_expect(OS32_ERR_INVAL);
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    CHECK(caller_access_enter(&previous, CALLER_USER));
    /* B1 rejects NULL, overflow, RO and a tail crossing into NP before write. */
    CHECK(surface_query(&source, 0) == OS32_ERR_INVAL);
    CHECK(surface_query(&source, (void *)0xfffffffcU) == OS32_ERR_INVAL);
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 index = (MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT;
    pt[index] &= ~PTE_RW; query_expect(OS32_ERR_INVAL); pt[index] |= PTE_RW;
    u8 *tail = P2V(payload + PAGE_SIZE - 4);
    for (u32 i = 0; i < 4; i++) tail[i] = 0x55;
    CHECK(surface_query(&source, (void *)(MEM_EXEC_LOAD_ADDR + PAGE_SIZE - 4)) == OS32_ERR_INVAL);
    for (u32 i = 0; i < 4; i++) CHECK(tail[i] == 0x55);
    /* Four-desc snapshot; generation checks must include the last face. */
    source.role = LEDGER_ROLE_DISPLAY; source.count = 4; slot.hdr_flags = OS32X_FLAG_GFX;
    for (u32 i = 0; i < 4; i++) {
        source.refs[i] = refs[i]; ledger_surfaces[sid[i]].role = LEDGER_ROLE_DISPLAY;
    }
    query_expect(0); CHECK(value->count == 4);
    CHECK(surface_query_refs(&source, refs, 4, LEDGER_PERM_RW) == 0);
    for (u32 i = 0; i < 4; i++) {
        CHECK(value->desc[i].ref.sid == refs[i].sid);
        refs[i].generation++;
        CHECK(surface_query_refs(&source, refs, 4, LEDGER_PERM_RO) == OS32_ERR_STALE);
        refs[i] = source.refs[i];
    }
    refs[3] = refs[0]; CHECK(surface_query_refs(&source, refs, 4, LEDGER_PERM_RO) == OS32_ERR_INVAL); refs[3] = source.refs[3];
    source.refs[3] = refs[0]; query_expect(OS32_ERR_INVAL); source.refs[3] = refs[3];
    source.refs[3].generation++; query_expect(OS32_ERR_STALE); source.refs[3] = refs[3];
    source.count = 3; query_expect(OS32_ERR_INVAL); source.count = 4;
    source.backend = LEDGER_SF_PEGC;
    for (u32 i = 0; i < 4; i++) ledger_surfaces[sid[i]].backend = LEDGER_SF_PEGC;
    query_expect(OS32_ERR_INVAL); /* PEGC DISPLAY has one record, not four. */
    source.backend = LEDGER_SF_PC98;
    for (u32 i = 0; i < 4; i++) ledger_surfaces[sid[i]].backend = LEDGER_SF_PC98;
    unsigned int auth_irq = irq_save();
    CHECK(surface_query_authorize(&source, &auth) == 0 && auth.owner == space.owner);
    irq_restore(auth_irq);
    /* Reinit after freeing a lower slot really changes the selected sid. */
    struct surface_ref old = refs[3];
    host_cr3 = paging_kernel_pd_phys();
    CHECK(sid[0] < sid[3]);
    CHECK(ledger_surface_release(sid[0]));
    CHECK(ledger_surface_release(sid[3]));
    sf.first = pgalloc_alloc_phys(space.owner, 1) / PAGE_SIZE;
    sf.role = LEDGER_ROLE_DISPLAY;
    u32 recreated;
    CHECK(sf.first && ledger_surface_create(&sf, &recreated));
    CHECK(recreated == sid[0] && recreated != old.sid);
    host_cr3 = space.pd_phys;
    source.backend = LEDGER_SF_PEGC; source.count = 1;
    ledger_surfaces[recreated].backend = LEDGER_SF_PEGC;
    source.refs[0] = (struct surface_ref){recreated, ledger_surfaces[recreated].gen};
    CHECK(surface_query_refs(&source, &old, 1, LEDGER_PERM_RO) == OS32_ERR_STALE);
    ledger_surfaces[old.sid].closing = 0; /* npages=0 alone still identifies old. */
    CHECK(surface_query_refs(&source, &old, 1, LEDGER_PERM_RO) == OS32_ERR_STALE);
    refs[0] = (struct surface_ref){sid[1], ledger_surfaces[sid[1]].gen};
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    ledger_surfaces[sid[1]].closing = 1;
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) == OS32_ERR_STALE);
    ledger_surfaces[sid[1]].closing = 0;
    refs[0].generation++;
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) == OS32_ERR_STALE);
    refs[0] = (struct surface_ref){LEDGER_MAX_SURFACES, 1};
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    refs[0] = (struct surface_ref){old.sid, 0};
    CHECK(surface_query_refs(&source, refs, 1, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    /* Four records: all old => STALE; old + live alien => INVAL. */
    source.backend = LEDGER_SF_PC98; source.count = 4;
    ledger_surfaces[recreated].backend = LEDGER_SF_PC98;
    source.refs[0] = (struct surface_ref){recreated, ledger_surfaces[recreated].gen};
    source.refs[1] = (struct surface_ref){sid[1], ledger_surfaces[sid[1]].gen};
    source.refs[2] = (struct surface_ref){sid[2], ledger_surfaces[sid[2]].gen};
    host_cr3 = paging_kernel_pd_phys();
    sf.first = pgalloc_alloc_phys(space.owner, 1) / PAGE_SIZE;
    u32 fourth;
    CHECK(sf.first && ledger_surface_create(&sf, &fourth));
    CHECK(fourth == old.sid);
    source.refs[3] = (struct surface_ref){fourth, ledger_surfaces[fourth].gen};
    host_cr3 = space.pd_phys;
    refs[0] = old;
    for (u32 i = 1; i < 4; i++) {
        refs[i] = source.refs[i]; refs[i].generation++;
    }
    refs[3] = source.refs[0]; refs[3].generation++;
    CHECK(surface_query_refs(&source, refs, 4, LEDGER_PERM_RO) == OS32_ERR_STALE);
    refs[1] = source.refs[2]; refs[1].generation++;
    refs[2] = source.refs[1]; /* live face in wrong position, no duplicate sid */
    CHECK(surface_query_refs(&source, refs, 4, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    refs[2] = source.refs[2]; refs[2].generation++;
    refs[1] = old; /* duplicate old refs must still be INVAL */
    CHECK(surface_query_refs(&source, refs, 4, LEDGER_PERM_RO) == OS32_ERR_INVAL);
    CHECK(surface_query_error(0) == 0);
    CHECK(surface_query_error(LEASE_INVAL) == OS32_ERR_INVAL);
    CHECK(surface_query_error(LEASE_FULL) == OS32_ERR_FULL);
    CHECK(surface_query_error(LEASE_NOMEM) == OS32_ERR_NOSPC);
    CHECK(surface_query_error(-999) == OS32_ERR_INVAL);
    SAY("PASS: e1 query/auth/ref/B1/output/accounting/IF/CR3");
    die(0);
}
