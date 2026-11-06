/* B8: one product TU (accel_transfer.c), A1's UNIT=8 dependencies.
 * CPU images describe the whole operation, independently of fragmentation.
 * Fragment examples come from GF_FEAT3_DESIGN section 7/G2/G3/N1, based on
 * MIT xf86-video-nouveau/src/nouveau_exa.c:256-281,320,341-365.
 * Link -m32 -march=i386 -std=gnu11 -O2 -nostdlib -static -no-pie.
 * The fixture performs no file access and needs no libc or libgcc. */
#define GF3_HOST_UNIT 8
#include "gf_accel_host.h"

/* Enough for a surface with two >16-bit pitch rows and its surrounding
 * sentinels; these are fixture capacities, not product limits. */
#define IMAGE_BYTES (64U * MEM_PAGE_SIZE)
#define EVENT_CAPACITY 512U
#define TEST_DEADLINE 10000U
static struct gf3_host host;
static struct gf3_host_event events[EVENT_CAPACITY];
static struct gf_vram_reservation_node reservation;
static struct gf3_surface surface;
static struct gf3_rect rect;
static struct gf3_ram ram;
static u8 vram[IMAGE_BYTES], memory[IMAGE_BYTES];
static u8 expected_vram[IMAGE_BYTES], expected_ram[IMAGE_BYTES];
static u8 wait_memory[IMAGE_BYTES], wait_staging[GF3_STAGING_BYTES];
static bool uploading, pending;
static u32 active_deadline, next_deadline, first_fragment;

static void observe(struct gf3_host *h, const struct gf3_host_event *e)
{
    if (e->op == H_CLOCK && !h->dma_pending && h->calls[H_M2MF] > first_fragment)
        next_deadline = h->now + GF3_TIMEOUT_US;
    if (e->op == H_SURFACE_CHECK) assert(e->a[4] == GF3_USE_BYTES);
    if (e->op == H_IFC || e->op == H_M2MF) {
        assert(h->calls[H_SURFACE_CHECK] == 1 && h->calls[H_RAM_CHECK] == 1);
        assert(e->p[0] == &h->ctx);
        u32 expected = e->op == H_M2MF ? next_deadline : TEST_DEADLINE;
        assert(e->a[e->op == H_IFC ? 4 : 8] == expected);
    }
    if (e->op == H_IFC) {
        assert(e->p[1] == &surface && e->p[2] == &ram);
        assert(e->a[0] == rect.x && e->a[1] == rect.y &&
               e->a[2] == rect.w && e->a[3] == rect.h);
    }
    if (e->op == H_FENCE) {
        active_deadline = next_deadline;
        assert(e->a[0] == active_deadline);
        if (pending)
            assert(!gf2_host_memcmp(h->staging, wait_staging, sizeof(wait_staging)));
        pending = true;
        gf2_host_memcpy(wait_staging, h->staging, sizeof(wait_staging));
        gf2_host_memcpy(wait_memory, ram.ptr, ram.bytes);
    }
    if (e->op == H_WAIT) assert(e->a[2] == active_deadline);
    if (pending && (e->op == H_SLEEP || e->op == H_WAIT || e->op == H_M2MF || e->op == H_IFC)) {
        /* A retired download may change staging on successful recovery. */
        if (uploading) {
            if (e->op != H_WAIT || e->rc)
                assert(!gf2_host_memcmp(h->staging, wait_staging, sizeof(wait_staging)));
        } else
            assert(!gf2_host_memcmp(ram.ptr, wait_memory, ram.bytes));
    }
    if ((e->op == H_WAIT && !e->rc) || e->op == H_M2MF || e->op == H_IFC)
        pending = false;
}

static void setup(enum gf3_fmt fmt, u32 width, u32 height, u32 pitch,
                   struct gf3_rect r, u32 ram_pitch, bool upload)
{
    gf3_host_init(&host);
    host.events = events; host.cevents = EVENT_CAPACITY;
    host.vram = vram; host.vram_bytes = sizeof(vram);
    host.ch.B = sizeof(vram);
    host.f.imem.vram_bytes = sizeof(vram) + MEM_PAGE_SIZE;
    host.f.imem.reserved = MEM_PAGE_SIZE;
    u32 cpp = gf3_host_cpp(fmt);
    surface = (struct gf3_surface){MEM_PAGE_SIZE,
        (height - 1U) * pitch + width * cpp, pitch, width, height, fmt, 1, 1};
    assert(surface.bytes <= sizeof(vram) - surface.off);
    reservation = (struct gf_vram_reservation_node){
        {&host, surface.off, surface.bytes, surface.reservation_id, surface.generation}, 0};
    host.reservations = &reservation;
    rect = r;
    ram = (struct gf3_ram){memory, (r.h - 1U) * ram_pitch + r.w * cpp, ram_pitch};
    assert(ram.bytes <= sizeof(memory));
    for (u32 i = 0; i < sizeof(vram); ++i) {
        vram[i] = (u8)(i * 13U + i / 251U);
        memory[i] = (u8)(i * 7U + i / 127U + 0x53U);
    }
    gf2_host_memset(host.staging, 0xa7, sizeof(host.staging));
    gf2_host_memcpy(expected_vram, vram, sizeof(vram));
    gf2_host_memcpy(expected_ram, memory, sizeof(memory));
    uploading = upload; pending = false; active_deadline = TEST_DEADLINE;
    host.observe = observe;
    host.complete_after = 2; /* Exercise staging/CPU ownership during waits. */
}

static void small(bool upload)
{
    setup(GF3_INDEX8, 21, 9, 29, (struct gf3_rect){3, 2, 13, 5}, 19, upload);
}

static int run(bool force)
{
    next_deadline = TEST_DEADLINE; first_fragment = host.calls[H_M2MF];
    int rc = uploading ? gf3_upload(&host.ctx, &surface, rect, &ram, force, TEST_DEADLINE) :
        gf3_download(&host.ctx, &surface, rect, &ram, TEST_DEADLINE);
    assert(host.ctx.last_rc == rc);
    return rc;
}

static void reference(void)
{
    u32 cpp = gf3_host_cpp(surface.fmt);
    u32 off = surface.off + rect.y * surface.pitch + rect.x * cpp;
    struct gf3_ram vr = {expected_vram, sizeof(expected_vram), surface.pitch};
    struct gf3_ram cpu = {expected_ram, sizeof(expected_ram), ram.pitch};
    if (uploading)
        gf3_host_ref_m2mf(&ram, 0, &vr, off, rect.w * cpp, rect.h);
    else {
        struct gf3_ram src = {vram, sizeof(vram), surface.pitch};
        gf3_host_ref_m2mf(&src, off, &cpu, 0, rect.w * cpp, rect.h);
    }
}

static void images(void)
{
    assert(!gf2_host_memcmp(vram, expected_vram, sizeof(vram)));
    assert(!gf2_host_memcmp(memory, expected_ram, sizeof(memory)));
}

static const struct gf3_host_event *fragment(u32 nth)
{
    for (u32 i = 0; i < host.nevents; ++i)
        if (events[i].op == H_M2MF && events[i].nth == nth) return &events[i];
    assert(0); return 0;
}

static void expect_fragment(u32 nth, u32 xbytes, u32 y, u32 bytes, u32 rows)
{
    const struct gf3_host_event *e = fragment(nth);
    u32 off = surface.off + (rect.y + y) * surface.pitch +
        rect.x * gf3_host_cpp(surface.fmt) + xbytes;
    assert(e->a[0] == (uploading ? GF3_HANDLE_PCI_DMA : GF3_HANDLE_VRAM_DMA));
    assert(e->a[1] == (uploading ? GF3_HANDLE_VRAM_DMA : GF3_HANDLE_PCI_DMA));
    assert(e->a[2] == (uploading ? 0 : off));
    assert(e->a[3] == (uploading ? off : 0));
    assert(e->a[4] == (uploading ? bytes : surface.pitch));
    assert(e->a[5] == (uploading ? surface.pitch : bytes));
    assert(e->a[6] == bytes && e->a[7] == rows && !e->rc);
}

static void splitting(void)
{
    for (u32 direction = 0; direction < 2; ++direction) {
        bool upload = direction != 0;
        /* L=1 hits the method row limit before the staging byte limit. */
        setup(GF3_INDEX8, 3, GF3_M2MF_MAX_ROWS + 3U, 7,
              (struct gf3_rect){1, 1, 1, GF3_M2MF_MAX_ROWS + 1U}, 3, upload);
        reference(); assert(!run(true)); images();
        assert(host.calls[H_M2MF] == 2);
        expect_fragment(1, 0, 0, 1, GF3_M2MF_MAX_ROWS);
        expect_fragment(2, 0, GF3_M2MF_MAX_ROWS, 1, 1);
        assert(host.calls[H_FENCE] == 2 && host.calls[H_WAIT] == 2);

        /* L=1024: four rows fit, then the final row. */
        setup(GF3_INDEX8, 1030, 7, 1041, (struct gf3_rect){2, 1, 1024, 5}, 1033, upload);
        reference(); assert(!run(true)); images();
        assert(host.calls[H_M2MF] == 2);
        expect_fragment(1, 0, 0, 1024, 4); expect_fragment(2, 0, 4, 1024, 1);

        /* An exact staging-size row, and a 4097-byte row (4096+1). */
        for (u32 extra = 0; extra < 2; ++extra) {
            u32 length = GF3_STAGING_BYTES + extra;
            setup(GF3_INDEX8, length + 4U, 4, length + 11U,
                  (struct gf3_rect){1, 1, length, 2}, length + 7U, upload);
            reference(); assert(!run(true)); images();
            assert(host.calls[H_M2MF] == 2U * (1U + extra));
            expect_fragment(1, 0, 0, GF3_STAGING_BYTES, 1);
            expect_fragment(extra ? 3 : 2, 0, 1, GF3_STAGING_BYTES, 1);
            if (extra) {
                expect_fragment(2, GF3_STAGING_BYTES, 0, 1, 1);
                expect_fragment(4, GF3_STAGING_BYTES, 1, 1, 1);
            }
        }
        for (enum gf3_fmt fmt = GF3_RGB565; fmt <= GF3_ARGB8888; ++fmt) {
            u32 cpp = gf3_host_cpp(fmt), width = GF3_STAGING_BYTES / cpp + 1U;
            setup(fmt, width + 2U, 3, width * cpp + 13U,
                  (struct gf3_rect){1, 1, width, 1}, width * cpp + 9U, upload);
            reference(); assert(!run(true)); images();
            assert(host.calls[H_M2MF] == 2);
            expect_fragment(1, 0, 0, GF3_STAGING_BYTES, 1);
            expect_fragment(2, GF3_STAGING_BYTES, 0, cpp, 1);
        }
        /* BYTES accepts a pitch beyond the packed 2D field. */
        setup(GF3_Y8, 9, 2, U16_MAX + 1U, (struct gf3_rect){1, 0, 7, 2}, 11, upload);
        reference(); assert(!run(false)); images();
    }
}

static void selector_case(enum gf3_fmt fmt, u32 rowbytes, u32 rows,
                          bool force, bool ifc)
{
    u32 cpp = gf3_host_cpp(fmt), width = rowbytes / cpp;
    setup(fmt, width + 2U, rows + 2U, rowbytes + 17U,
          (struct gf3_rect){1, 1, width, rows}, rowbytes + 7U, true);
    reference(); assert(!run(force)); images();
    assert(host.calls[H_IFC] == (u32)ifc);
    assert((host.calls[H_M2MF] == 0) == ifc);
}

static void selection(void)
{
    for (enum gf3_fmt fmt = GF3_INDEX8; fmt <= GF3_UYVY; ++fmt)
        selector_case(fmt, 16, 2, false,
                      fmt == GF3_RGB565 || fmt == GF3_XRGB8888 || fmt == GF3_ARGB8888);
    selector_case(GF3_RGB565, 16, 2, true, false);
    selector_case(GF3_RGB565, 2, 1, false, false);
    selector_case(GF3_RGB565, 4, 1, false, true);
    selector_case(GF3_RGB565, 4, GF3_IFC_MAX_HEIGHT, false, true);
    selector_case(GF3_RGB565, 4, GF3_IFC_MAX_HEIGHT + 1U, false, false);
    selector_case(GF3_ARGB8888, GF3_SCRATCH_WORDS * sizeof(u32), 1, false, true);
    selector_case(GF3_ARGB8888, GF3_SCRATCH_WORDS * sizeof(u32) + sizeof(u32), 1, false, false);
    selector_case(GF3_RGB565, 1024, 15, false, true);
    selector_case(GF3_RGB565, 1024, 16, false, false); /* exactly 16 KiB */

    setup(GF3_RGB565, 12, 4, 29, (struct gf3_rect){1, 1, 8, 2}, 19, true);
    gf3_host_fail_at(&host, H_IFC, 1, GF3_EUNSUPPORTED);
    reference(); assert(!run(false)); images();
    assert(host.calls[H_IFC] == 1 && host.calls[H_M2MF] == 1);

    setup(GF3_RGB565, 12, 4, 29, (struct gf3_rect){1, 1, 8, 2}, 19, true);
    host.ch.staging_live = false; /* IFC remains usable without PCI staging. */
    reference(); assert(!run(false)); images(); assert(!host.calls[H_M2MF]);
}

static void no_submission(void)
{
    assert(!host.calls[H_IFC] && !host.calls[H_M2MF] && !host.calls[H_FENCE]);
    images();
}

static void validation(void)
{
    for (u32 direction = 0; direction < 2; ++direction) {
        bool upload = direction != 0;
        small(upload); rect.w = 0; surface.generation = 0; ram.ptr = 0;
        assert(!run(false)); no_submission();
        assert(!host.calls[H_SURFACE_CHECK] && !host.calls[H_RAM_CHECK]);
        small(upload); rect.h = 0; host.ctx.ready = false;
        assert(run(false) == GF3_ESTATE); no_submission();
        small(upload); ++host.ctx.epoch;
        assert(run(false) == GF3_ESTATE); no_submission();
        small(upload); host.ch.state = GF2_OFF;
        assert(run(false) == GF3_ESTATE); no_submission();
        small(upload); host.ch.fault.latched = true;
        assert(run(false) == GF2_EFAULT); no_submission();
        small(upload); ++surface.generation;
        assert(run(false) == GF3_ESTATE); no_submission();
        small(upload); ++surface.bytes; /* Adjacent reservation is not ours. */
        assert(run(false) == GF3_ERANGE); no_submission();
        small(upload); surface.fmt = (enum gf3_fmt)(GF3_UYVY + 1);
        assert(run(false) == GF3_EFORMAT); no_submission();
        small(upload); rect.x = U32_MAX;
        assert(run(false) == GF3_ERANGE); no_submission();
        small(upload); --ram.bytes;
        assert(run(false) == GF3_ERANGE); no_submission();
        small(upload); ram.pitch = rect.w - 1U;
        assert(run(false) == GF3_ERANGE); no_submission();
        small(upload); ram.ptr = 0;
        assert(run(false) == GF3_EINVAL); no_submission();
        small(upload); host.ch.staging_live = false;
        assert(run(false) == GF2_ENOSPC); no_submission();
    }
    small(true);
    assert(gf3_upload(0, &surface, rect, &ram, false, TEST_DEADLINE) == GF3_EINVAL);
    assert(gf3_upload(&host.ctx, 0, rect, &ram, false, TEST_DEADLINE) == GF3_EINVAL);
    assert(gf3_upload(&host.ctx, &surface, rect, 0, false, TEST_DEADLINE) == GF3_EINVAL);
    assert(gf3_download(&host.ctx, 0, rect, &ram, TEST_DEADLINE) == GF3_EINVAL);
    assert(gf3_download(&host.ctx, &surface, rect, 0, TEST_DEADLINE) == GF3_EINVAL);
    host.ctx.ch = 0; assert(run(false) == GF3_EINVAL); no_submission();
}

static void failures(void)
{
    static const enum gf3_host_op ops[] = {H_FAULT, H_SURFACE_CHECK, H_RAM_CHECK, H_M2MF, H_FENCE, H_WAIT};
    for (u32 direction = 0; direction < 2; ++direction) {
        for (u32 i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
            small(direction != 0);
            gf3_host_fail_at(&host, ops[i], 1, GF2_EIO);
            assert(run(true) == GF2_EIO); images();
            assert(host.calls[H_M2MF] <= 1 && !host.calls[H_IFC]);
        }
        /* Failure on the second piece preserves the first piece only. */
        u32 length = GF3_STAGING_BYTES + 1U;
        setup(GF3_INDEX8, length, 1, length, (struct gf3_rect){0, 0, length, 1}, length, direction != 0);
        gf3_host_fail_at(&host, H_M2MF, 2, GF2_EFAULT);
        if (uploading) gf2_host_memcpy(expected_vram + surface.off, memory, GF3_STAGING_BYTES);
        else gf2_host_memcpy(expected_ram, vram + surface.off, GF3_STAGING_BYTES);
        assert(run(true) == GF2_EFAULT); images();
        assert(host.calls[H_M2MF] == 2 && host.calls[H_FENCE] == 1);
        small(direction != 0); host.now = TEST_DEADLINE;
        assert(run(true) == GF2_ETIME); images();

        /* The first batch completes just before its deadline. The next
         * batch gets a full budget and finishes past that original deadline. */
        setup(GF3_INDEX8, 1024, 5, 1031, (struct gf3_rect){0, 0, 1024, 5}, 1033, direction != 0);
        reference();
        host.now = TEST_DEADLINE - 3U;
        assert(!run(true)); images();
        assert(host.now >= TEST_DEADLINE);
        assert(host.calls[H_M2MF] == 2 && host.calls[H_WAIT] == 2);
    }
    static const int ifc_errors[] = {GF3_EFORMAT, GF2_EFAULT, GF2_ETIME, GF2_EIO};
    for (u32 i = 0; i < sizeof(ifc_errors) / sizeof(ifc_errors[0]); ++i) {
        setup(GF3_RGB565, 12, 4, 29, (struct gf3_rect){1, 1, 8, 2}, 19, true);
        gf3_host_fail_at(&host, H_IFC, 1, ifc_errors[i]);
        assert(run(false) == ifc_errors[i]);
        assert(host.calls[H_IFC] == 1 && !host.calls[H_M2MF]); images();
    }
}

/* A04: timeout leaves RUN and submitted DMA alive. A second transfer
 * must drain it before touching staging or submitting another fragment. */
static void staging_retry(void)
{
    const int errors[] = {GF2_ETIME, GF2_EIO};
    for (u32 direction = 0; direction < 2; ++direction) {
        for (u32 retry = 0; retry < 2; ++retry) {
            for (u32 error = 0; error < sizeof(errors) / sizeof(errors[0]); ++error) {
                small(direction != 0);
                gf3_host_fail_at(&host, H_WAIT, 1, errors[error]);
                assert(run(true) == errors[error]);
                assert(host.ch.state == GF2_RUN && host.dma_pending);
                gf2_host_memcpy(wait_staging, host.staging, sizeof(wait_staging));
                /* Make the next upload's contents differ from the first. */
                for (u32 b = 0; b < ram.bytes; ++b) memory[b] ^= 0x5a;
                uploading = retry != 0;
                pending = false;
                gf3_host_trace_clear(&host);
                /* Failed re-drain must leave staging and CPU RAM untouched. */
                host.now = TEST_DEADLINE;
                host.fail_rc[H_WAIT] = 0;
                gf2_host_memcpy(expected_ram, memory, sizeof(memory));
                assert(run(true) == GF2_ETIME);
                assert(!host.calls[H_M2MF] && host.dma_pending);
                assert(!gf2_host_memcmp(host.staging, wait_staging, sizeof(wait_staging)));
                assert(!gf2_host_memcmp(memory, expected_ram, sizeof(memory)));
                /* A failed reference emission is also not completion. */
                pending = false;
                gf3_host_trace_clear(&host);
                host.now = 0;
                gf3_host_fail_at(&host, H_FENCE, 1, GF2_EIO);
                assert(run(true) == GF2_EIO);
                assert(!host.calls[H_M2MF] && host.dma_pending);
                assert(!gf2_host_memcmp(host.staging, wait_staging, sizeof(wait_staging)));
                /* Completion retires the old DMA before the new fragment. */
                host.fail_rc[H_FENCE] = 0;
                pending = false;
                gf3_host_trace_clear(&host);
                assert(!run(true));
                assert(host.calls[H_FENCE] == 2 && host.calls[H_WAIT] == 2);
                assert(host.calls[H_M2MF] == 1 && !host.dma_pending);
                if (uploading) {
                    u32 off = surface.off + rect.y * surface.pitch + rect.x;
                    for (u32 y = 0; y < rect.h; ++y)
                        assert(!gf2_host_memcmp(vram + off + y * surface.pitch,
                                               memory + y * ram.pitch, rect.w));
                } else {
                    u32 off = surface.off + rect.y * surface.pitch + rect.x;
                    for (u32 y = 0; y < rect.h; ++y)
                        assert(!gf2_host_memcmp(memory + y * ram.pitch,
                                               vram + off + y * surface.pitch, rect.w));
                }
                /* Normal following transfers add no recovery fence. */
                pending = false;
                gf3_host_trace_clear(&host);
                assert(!run(true));
                assert(host.calls[H_FENCE] == 1 && host.calls[H_WAIT] == 1);
            }
        }
    }
}

/* Linux i386 old_mmap/mprotect: put the last valid RAM byte immediately
 * before a PROT_NONE page. No libc mapping wrapper or external allocator. */
static void guard_boundary(void)
{
    enum { LINUX_MMAP = 90, LINUX_MPROTECT = 125,
           PROT_READ_WRITE = 3, MAP_PRIVATE_ANONYMOUS = 0x22,
           LINUX_MAX_ERRNO = 4095 };
    u32 args[6] = {0, 2U * MEM_PAGE_SIZE, PROT_READ_WRITE,
                   MAP_PRIVATE_ANONYMOUS, U32_MAX, 0};
    u32 address;
    __asm__ volatile("int $0x80" : "=a"(address) : "0"(LINUX_MMAP), "b"(args) : "memory", "cc");
    assert(address < (u32)-LINUX_MAX_ERRNO);
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(LINUX_MPROTECT), "b"(address + MEM_PAGE_SIZE),
                     "c"(MEM_PAGE_SIZE), "d"(0) : "memory", "cc");
    assert(!rc);
    for (u32 direction = 0; direction < 2; ++direction) {
        small(direction != 0);
        ram.ptr = (u8 *)(uptr)(address + MEM_PAGE_SIZE - ram.bytes);
        gf2_host_memcpy(ram.ptr, memory, ram.bytes);
        reference(); assert(!run(true));
        assert(!gf2_host_memcmp(ram.ptr, expected_ram, ram.bytes));
        assert(!gf2_host_memcmp(vram, expected_vram, sizeof(vram)));
    }
    setup(GF3_RGB565, 12, 4, 29, (struct gf3_rect){1, 1, 8, 2}, 19, true);
    ram.ptr = (u8 *)(uptr)(address + MEM_PAGE_SIZE - ram.bytes);
    gf2_host_memcpy(ram.ptr, memory, ram.bytes);
    reference(); assert(!run(false));
    assert(host.calls[H_IFC] == 1);
    assert(!gf2_host_memcmp(vram, expected_vram, sizeof(vram)));
}

void _start(void)
{
    splitting(); selection(); validation(); failures(); staging_retry(); guard_boundary();
    gf2_host_exit(0);
}
