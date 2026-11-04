/* X1-X3/N1 service boundary integration, A1 UNIT14 (no service/V0 stubs).
 * Reuse the real F1/F2/F3/V0 and CPU-MMIO/window fixture from C1. Build
 * with its kernel include flags and accel_copy.c, -m32 -nostdlib -static
 * -ffunction-sections -fdata-sections -Wl,--gc-sections. No libc or refs/.
 * Hardware ordering oracle: nouveau c03c1a1 nvkm/engine/fifo/uchan.c:272-298
 * (block before unbind, bind before allow); nouveau_bo.c:594-629,669-689
 * (pin ownership survives use/map). Value loans, diagnostic dispatch and
 * absolute dl are OS32 X1-X3/N1 contracts, with no nouveau magic values.
 * F4 full start/stop/format-hook integration belongs to C2-service/C1. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"

/* Arbitrary small legal geometries, independent of service placement.
 * cpp=2/4: dispnv04/crtc.c:838-903; pitch 64: F4 X1/N4 contract. */
#define LOAN_PITCH 64U
#define LOAN_WIDTH 16U
#define LOAN_HEIGHT 16U
#define TEST_ROWS (2U * MEM_PAGE_SIZE / LOAN_PITCH)
#define REQUEST_DL 0x12345678U /* distinguish forwarded dl from renewed clock */
static struct gf_vram_reservation_node scan_node, test_node;
static struct gf3_surface scan_face, test_face;
static u32 recover_calls, recover_dl;
static i32 recover_rc;
static int owner_cookie;

static i32 unused_owner_call(void *owner, u32 dl)
{
    (void)owner; (void)dl;
    assert(0); /* standalone start/stop must never delegate */
    return GF3_ESTATE;
}
static i32 owner_recover(void *owner, u32 dl)
{
    assert(owner == &owner_cookie);
    ++recover_calls; recover_dl = dl;
    return recover_rc;
}
static const struct gf3_service_owner owner_table = {
    &owner_cookie, unused_owner_call, unused_owner_call, owner_recover
};

static void assert_face(const struct gf3_surface *a, const struct gf3_surface *b)
{
    assert(a->off == b->off && a->bytes == b->bytes && a->pitch == b->pitch);
    assert(a->width == b->width && a->height == b->height && a->fmt == b->fmt);
    assert(a->reservation_id == b->reservation_id && a->generation == b->generation);
}
static void external_reserve(void)
{
    struct gf2_chan *ch = &cint_prod.chan;
    assert(!gf_vram_registry_init(ch));
    assert(!gf_vram_reserve(ch, &scan_node, 0, MEM_PAGE_SIZE, &scan_node));
    assert(!gf_vram_reserve(ch, &test_node, CINT_TEST_OFF,
                            2U * MEM_PAGE_SIZE, &test_node));
    scan_face = (struct gf3_surface){
        .off = scan_node.value.off, .bytes = scan_node.value.bytes,
        .pitch = LOAN_PITCH, .width = LOAN_WIDTH, .height = LOAN_HEIGHT,
        .fmt = GF3_RGB565, .reservation_id = scan_node.value.reservation_id,
        .generation = scan_node.value.generation
    };
    test_face = (struct gf3_surface){
        .off = test_node.value.off, .bytes = test_node.value.bytes,
        .pitch = LOAN_PITCH, .width = LOAN_PITCH, .height = TEST_ROWS,
        .fmt = GF3_INDEX8, .reservation_id = test_node.value.reservation_id,
        .generation = test_node.value.generation
    };
}
static void external_keys_live(void)
{
    struct gf_vram_reservation key;
    const struct gf_vram_reservation *keys[] = {&scan_node.value, &test_node.value};
    for (u32 i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const struct gf_vram_reservation *k = keys[i];
        assert(!gf_vram_match(&cint_prod.chan, k->owner, k->reservation_id,
                              k->generation, k->off, k->bytes, &key));
    }
}
static void finish_external(void)
{
    assert(!gf3_service_close(cint_service));
    cint_expect_closed = true;
    assert(!gf3_service_fini(cint_service, cint_deadline()));
    assert(!gf2_stop(&cint_prod.chan) && cint_prod.chan.state == GF2_OFF);
    gf3_service_invalidate(cint_service);
    assert(!gf_foundation_stop(&cint_prod.foundation));
    cint_prod.display.engaged = cint_prod.display.active = false;
    cint_prod.display.stop_ok = true;
    /* F4 owns and returns its own nodes, never service_release. */
    external_keys_live();
    assert(gf3_service_owner_bind(cint_service, 0) == GF3_ESTATE);
    assert(!gf_vram_release(&cint_prod.chan, &test_node.value));
    assert(!gf_vram_release(&cint_prod.chan, &scan_node.value));
    assert(!gf3_service_owner_bind(cint_service, 0));
    cint_service->scan = cint_service->test = (struct gf3_surface){0};
    cint_no_resources();
}

static void loans_and_publication(void)
{
    cint_reset();
    assert(gf3_service_surfaces(cint_service, 0, 0) == GF3_ESTATE);
    assert(!gf3_service_owner_bind(cint_service, &owner_table));
    assert(gf3_service_surfaces(cint_service, 0, 0) == GF3_ESTATE); /* !ready */
    assert(gf3_service_start(cint_service, REQUEST_DL) == GF3_ESTATE); /* owner, !ready */
    assert(!cint_host.nevents && !cint_service->scan_pitch && !cint_slot);
    external_reserve();
    u32 issued = registry.next_id;
    struct gf_vram_reservation_node *head = registry.head;
    assert(!gf3_service_objects_init(cint_service, cint_deadline()));
    assert(!cint_slot && !hook_active && cint_prod.accel.ready);
    assert(registry.head == head && registry.next_id == issued);
    assert(!cint_service->scan_node.value.owner && !cint_service->test_node.value.owner);
    assert(!cint_service->scan_pitch); /* no backend geometry fetch */
    assert(gf3_service_objects_init(cint_service, REQUEST_DL) == GF3_ESTATE);
    assert(!gf3_service_surfaces(cint_service, &scan_face, &test_face));
    assert_face(&cint_service->scan, &scan_face);
    assert_face(&cint_service->test, &test_face);
    struct gf3_surface bad = test_face;
    ++bad.generation;
    struct gf3_surface changed = scan_face;
    changed.fmt = GF3_XRGB8888;
    assert(gf3_service_surfaces(cint_service, &changed, &bad) == GF3_ESTATE);
    assert_face(&cint_service->scan, &scan_face); /* atomic, even if scan valid */
    assert_face(&cint_service->test, &test_face);
    bad = test_face; bad.pitch = 1;
    assert(gf3_service_surfaces(cint_service, &changed, &bad) == GF3_ERANGE);
    assert_face(&cint_service->scan, &scan_face);
    assert(!gf3_service_surfaces(cint_service, &changed, &test_face));
    assert_face(&cint_service->scan, &changed); /* all 32bit metadata retained */
    cint_trace_clear();
    assert(!gf3_service_admission(cint_service, true));
    assert(cint_slot == &cint_service->ops && cint_slot->ctx == &cint_prod.accel);
    assert(cint_slot->test_surface == &cint_service->test && !hook_active);
    assert(!cint_host.nevents); /* no waits or HW during publication */
    assert(!gf3_service_admission(cint_service, false));
    assert(!gf3_service_surfaces(cint_service, &cint_service->test, &cint_service->scan));
    assert_face(&cint_service->scan, &test_face); /* alias-safe pair copy */
    assert_face(&cint_service->test, &changed);
    assert(!gf3_service_surfaces(cint_service, 0, &test_face));
    assert(!gf3_service_admission(cint_service, true) && cint_slot && !hook_active);
    assert(!gf3_service_admission(cint_service, false));
    assert(!gf3_service_surfaces(cint_service, &scan_face, 0));
    assert(!gf3_service_admission(cint_service, true) && !cint_slot && !hook_active);
    assert(!gf3_service_surfaces(cint_service, 0, 0));
    assert(!gf3_service_admission(cint_service, true) && !cint_slot);
    assert(registry.head == head && registry.next_id == issued);
    external_keys_live();
    finish_external();
}

static void closed_windows_and_stale_loans(void)
{
    cint_reset();
    assert(!gf3_service_owner_bind(cint_service, &owner_table));
    external_reserve();
    assert(!gf3_service_objects_init(cint_service, cint_deadline()));
    assert(!gf3_service_surfaces(cint_service, &scan_face, &test_face));
    cint_open_windows();
    struct gf_inst *objects = cint_prod.foundation.imem.used;
    assert(!gf3_service_admission(cint_service, true));
    assert(!gf3_service_admission(cint_service, false));
    assert(views && cint_view[0].mapped && cint_view[1].mapped);
    assert(!cint_close_calls[0] && !cint_close_calls[1]);
    assert(cint_prod.accel.ready && cint_prod.foundation.imem.used == objects);
    assert_face(&cint_service->scan, &scan_face);
    ++cint_service->test.generation;
    cint_trace_clear();
    assert(gf3_service_admission(cint_service, true) == GF3_ESTATE);
    assert(!cint_slot && !hook_active && !cint_host.nevents);
    cint_service->test = test_face;
    u32 epoch = cint_prod.accel.epoch;
    cint_prod.accel.epoch = epoch + 1U;
    assert(gf3_service_admission(cint_service, true) == GF3_ESTATE);
    cint_prod.accel.epoch = epoch;
    cint_unmap_fail = CINT_MAP_VIEW0;
    assert(gf3_service_close(cint_service) == GF2_EMAP);
    assert(!cint_slot && !hook_active && cint_view[0].mapped && !cint_view[1].mapped);
    assert(cint_prod.accel.ready && cint_prod.foundation.imem.used == objects);
    external_keys_live();
    cint_unmap_fail = -1;
    assert(!gf3_service_close(cint_service) && !views);
    finish_external();
}

static void objects_validation_and_failed_init(void)
{
    cint_reset();
    assert(!gf3_service_owner_bind(cint_service, &owner_table));
    external_reserve();
    u32 issued = registry.next_id, epoch = cint_prod.chan.epoch;
    struct gf2_chan alien = {0};
    cint_prod.accel.ch = &alien;
    assert(gf3_service_objects_init(cint_service, REQUEST_DL) == GF3_ESTATE);
    cint_prod.accel.ch = 0;
    cint_prod.chan.epoch = 0;
    assert(gf3_service_objects_init(cint_service, REQUEST_DL) == GF3_ESTATE);
    cint_prod.chan.epoch = epoch;
    cint_prod.chan.fault.latched = true;
    assert(gf3_service_objects_init(cint_service, REQUEST_DL) == GF3_ESTATE);
    cint_prod.chan.fault.latched = false;
    assert(!cint_host.nevents && !cint_slot && !hook_active);
    /* Real F3 sees an already expired absolute dl. No service-made clock
     * renews it, and failed initialization never publishes or reserves. */
    assert(gf3_service_objects_init(cint_service, cint_host.now) == GF2_ETIME);
    assert(!cint_slot && !hook_active && !cint_prod.accel.ready);
    assert(registry.next_id == issued);
    external_keys_live();
    finish_external();
}

static void owner_dispatch_and_legacy(void)
{
    cint_reset(); cint_start();
    struct gf3_service_owner invalid = owner_table;
    invalid.recover = 0;
    assert(gf3_service_owner_bind(cint_service, &invalid) == GF3_EINVAL);
    assert(!cint_service->owner);
    assert(!gf3_service_owner_bind(cint_service, &owner_table));
    assert(!gf3_service_owner_bind(cint_service, &owner_table));
    struct gf3_service_owner other = owner_table;
    assert(gf3_service_owner_bind(cint_service, &other) == GF3_ESTATE);
    struct gf_inst *objects = cint_prod.foundation.imem.used;
    u32 id = cint_service->scan.reservation_id;
    cint_trace_clear();
    assert(gf3_service_start(cint_service, REQUEST_DL) == GF3_ESTATE);
    assert(gf3_service_stop(cint_service) == GF3_ESTATE);
    assert(cint_slot && hook_active && cint_prod.accel.ready);
    assert(!cint_host.nevents && cint_prod.foundation.imem.used == objects);
    assert(gf3_service_surfaces(cint_service, 0, 0) == GF3_ESTATE); /* legacy nodes */
    assert(gf3_service_owner_bind(cint_service, 0) == GF3_ESTATE);
    recover_calls = 0; recover_rc = CINT_IO_FAILURE;
    assert(gf3_service_recover(cint_service, REQUEST_DL) == CINT_IO_FAILURE);
    assert(recover_calls == 1 && recover_dl == REQUEST_DL);
    assert(cint_prod.accel.last_rc == CINT_IO_FAILURE && !cint_host.nevents);
    assert(cint_slot && hook_active && cint_service->scan.reservation_id == id);
    recover_rc = 0;
    assert(!gf3_service_recover(cint_service, REQUEST_DL));
    assert(recover_calls == 2 && recover_dl == REQUEST_DL);
    cint_stop_display(); /* no display4: real legacy cleanup */
    assert(!gf3_service_owner_bind(cint_service, 0));
    cint_reset(); cint_start(); /* legacy hook/slot and nodes still work */
    cint_stop_display();
    cint_reset();
    /* End scan at push B: real channel layout remains unchanged, with no
     * page-aligned space for test rows after the scan. */
    framebuffer.phys = CINT_FB_PHYS + cint_prod.chan.B - framebuffer.bytes;
    assert(gf3_service_start(cint_service, cint_deadline()) == GF2_ENOSPC);
    assert(cint_prod.accel.ready && hook_active && !cint_slot);
    assert(cint_service->scan_node.value.owner && !cint_service->test_node.value.owner);
    cint_stop_display();
}

void _start(void)
{
    assert(gf3_service_owner_bind(0, &owner_table) == GF3_EINVAL);
    assert(gf3_service_objects_init(0, REQUEST_DL) == GF3_EINVAL);
    assert(gf3_service_surfaces(0, 0, 0) == GF3_EINVAL);
    assert(gf3_service_admission(0, false) == GF3_EINVAL);
    loans_and_publication();
    closed_windows_and_stale_loans();
    objects_validation_and_failed_init();
    owner_dispatch_and_legacy();
    gf2_host_exit(0);
}
