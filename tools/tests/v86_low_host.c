/* T7a-2e: real paging/session/BIOS with a qemu-user low VA mapping.
 * CPU CR3/TLB and exception delivery are explicit substitutes; no signal stack
 * is claimed to emulate #PF. The low fixture executes actual x86 instructions.
 */
#include "types.h"
static u32 host_cr3;
static void flush_low(void);
#include "v86.h"
#include "v86_call.h"
#include "idt.h"
#include "tvram.h"
#include "paging.c"
#include "pgalloc.c"
#include "pgalloc_host_fixture.h"
#include "module.h"
#include "kstring.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static void say(const char *s, u32 n) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define SAY(s) say(s "\n", sizeof(s "\n") - 1)
#define CHECK(c, s) do { if (!(c)) { SAY("FAIL " s); die(1); } } while (0)
static u32 corrupt_restore_pde;
static u32 tlb[MEM_1MB / PAGE_SIZE], tlb_pde, tlb_root, flushes;
static void flush_low(void) {
    flushes++;
    if (corrupt_restore_pde) page_directory[0] ^= PTE_USER;
    tlb_root = host_cr3;
    if (!page_tables[0]) return;
    tlb_pde = ((u32 *)P2V(host_cr3))[0];
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++) tlb[i] = page_tables[0][i];
}
static u32 fail_map, maps, module_refs, refuse_module, snapshot_fail;
static u32 boundary_fail, boundaries, abort_jmp[5], stop_jmp[5], expect_stop;
static u8 mailbox_storage[MEM_AUTOPLAY_MAILBOX_SIZE];
static int missing_mailbox;
void *v86_call_mailbox(void) { return missing_mailbox ? 0 : mailbox_storage; }
static void boundary(void) {
    if (++boundaries == boundary_fail) __builtin_longjmp(abort_jmp, 1);
}
static void __attribute__((unused)) restore_stop(void) {
    CHECK(expect_stop, "restore must complete before caller return");
    CHECK(v86_restore_mismatch, "restore mismatch counted");
    static const char expected[][65] = {
        "V86 RESTORE: bad=00000100 PTE=00000100 PDE=00000000 PCD=00000000",
        "V86 RESTORE: bad=00000001 PTE=00000000 PDE=00000001 PCD=00000000",
        "V86 RESTORE: bad=00000001 PTE=00000000 PDE=00000000 PCD=00000001",
    };
    for (u32 col = 0; col < TVRAM_COLS; col++) {
        CHECK(((volatile u16 *)P2V_IO(TVRAM_BASE))[col] ==
              (col < sizeof(expected[0]) - 1 ? (u8)expected[expect_stop - 1][col] : ' ') &&
              ((volatile u16 *)P2V_IO(TVRAM_ATTR))[col] == TATTR_RED,
              "restore TVRAM diagnostic");
    }
    __builtin_longjmp(stop_jmp, 1);
}
void *kmemcpy(void *d, const void *s, u32 n) {
    volatile u8 *dst = d; const volatile u8 *src = s;
    for (u32 i = 0; i < n; i++) dst[i] = src[i];
    return d;
}
void *kmemset(void *d, int c, u32 n) {
    volatile u8 *dst = d;
    for (u32 i = 0; i < n; i++) dst[i] = (u8)c;
    return d;
}
int v86_module_session_acquire(void) {
    if (refuse_module) return 0;
    module_refs++;
    return 1;
}
void v86_module_session_release(void) { CHECK(module_refs == 1, "module pin"); module_refs--; }
static int checked_snapshot(struct v86_session_state *s) {
    return snapshot_fail ? -1 : paging_v86_snapshot(s);
}
static int checked_map(u32 base, u32 end, u32 phys, u32 flags) {
    if (++maps == fail_map) return -1;
    return paging_v86_map_range(base, end, phys, flags);
}
#include "v86_bios.c"
#include "v86_snapshot.c"
#include "clock.c"
#include "sysclk.c"
#include "bios_consumer_slice.inc"
static void checked_bios(void) {
    /* Whole conventional RAM except restored page0 must be freshly zero. */
    for (u32 a = PAGE_SIZE; a < MEM_CONV_END; a++)
        CHECK(*(volatile u8 *)P2V(a) == 0, "previous guest data zero");
    CHECK(*(volatile u32 *)P2V(V86_TEST_MAGIC_ADDR) == 0, "test word zero");
    boundary(); /* zero complete, before BIOS edits */
    v86_bios_setup();
    boundary(); /* BIOS complete, before I/O policy */
}
static u32 allocation_calls;
static u32 checked_alloc(u32 owner, int pages) __attribute__((unused));
static u32 checked_alloc(u32 owner, int pages) {
    allocation_calls++;
    return pgalloc_alloc_phys(owner, pages);
}
#define pgalloc_alloc_phys checked_alloc
#define paging_v86_snapshot checked_snapshot
#define paging_v86_map_range checked_map
#define v86_bios_setup checked_bios
#define _stop restore_stop
#include "emergency.c"
#include "v86_mem.c"
#undef _stop
#undef v86_bios_setup
#undef paging_v86_map_range
#undef paging_v86_snapshot
#undef pgalloc_alloc_phys
struct kdebug kdebug;
volatile u32 kctx_irq_depth, kctx_exc_depth;
static u32 releases;
void v86_io_apply_policy(void) { boundary(); }
void v86_io_reset_policy(void) {}
void v86_runtime_end(void) { v86_session.active = v86_session.running = 0; }
void gfx_v86_return(void) { CHECK(!kctx_exc_depth && !v86_session.open, "normal release"); releases++; }
void kselftest_audit_v86_return(void) {}
void loop_dev_detach(int slot) { (void)slot; }
/* BIOS disk HLE is real; loop/VFS are deterministic image-I/O substitutes. */
static struct {
    int format;
    u16 cyls, bps;
    u8 heads, spt;
} bios_images[V86_DRIVE_MAX];
static u32 bios_io_calls, bios_failures, bios_d88_cyl, bios_d88_head;
static u32 bios_frame[32];
#define BIOS_CHECK(c, s) do { if (!(c)) { SAY("FAIL " s); bios_failures++; } } while (0)
int loop_dev_attach(const char *path, int slot) {
    bios_images[slot].format = path[0] == 'd' ? LOOP_FMT_D88 :
                              path[0] == 'h' ? LOOP_FMT_HDI : LOOP_FMT_RAW;
    bios_images[slot].cyls = path[0] == 'h' ? 615 : 80;
    bios_images[slot].heads = path[0] == 'h' ? 4 : 2;
    bios_images[slot].spt = path[0] == 'h' ? 17 : 8;
    bios_images[slot].bps = 512;
    return 0;
}
int loop_dev_get_format(int slot) { return bios_images[slot].format; }
int loop_dev_is_hdd(int slot) { return bios_images[slot].format == LOOP_FMT_HDI; }
int loop_dev_is_writable(int slot) { (void)slot; return 1; }
int loop_dev_get_geometry(int slot, u16 *c, u8 *h, u8 *s, u16 *b, u32 *t) {
    *c = bios_images[slot].cyls; *h = bios_images[slot].heads;
    *s = bios_images[slot].spt; *b = bios_images[slot].bps;
    *t = (u32)*c * *h * *s;
    return 0;
}
u8 loop_dev_get_sec_n(int slot) {
    u8 n = 0;
    while ((128U << n) < bios_images[slot].bps) n++;
    return n;
}
int loop_dev_track_id_d88(int slot, u8 c, u8 h, u16 idx,
                          u8 *ic, u8 *ih, u8 *ir, u8 *in, u16 *spt) {
    bios_io_calls++;
    if (bios_images[slot].format != LOOP_FMT_D88 ||
        c >= bios_images[slot].cyls || h >= bios_images[slot].heads) return -1;
    *ic = c + 10; *ih = h ^ 1; *ir = idx + 3;
    *in = loop_dev_get_sec_n(slot); *spt = bios_images[slot].spt;
    return 0;
}
u32 loop_dev_seek_d88(int slot, u8 c, u8 h, u8 ic, u8 ih, u8 ir,
                      u32 *len, u16 *spt) {
    bios_io_calls++;
    if (bios_images[slot].format != LOOP_FMT_D88 || c != bios_d88_cyl ||
        h != bios_d88_head || ic != 0 || ih != 1 || ir != 1) return 0;
    *len = bios_images[slot].bps; *spt = bios_images[slot].spt;
    return 128;
}
int loop_dev_get_fd(int slot) { return slot; }
int vfs_seek(int fd, int offset, int whence) {
    (void)fd; (void)whence; return offset;
}
int vfs_read_fd(int fd, void *buf, u32 n) {
    (void)fd; bios_io_calls++; kmemset(buf, 0x5a, n); return (int)n;
}
int loop_dev_read_chs(int slot, u16 c, u8 h, u8 s, void *buf) {
    bios_io_calls++;
    if (c >= bios_images[slot].cyls || h >= bios_images[slot].heads ||
        s == 0 || s > bios_images[slot].spt) return -1;
    kmemset(buf, 0x5a, bios_images[slot].bps);
    return 0;
}
int loop_dev_write_chs(int slot, u16 c, u8 h, u8 s, const void *buf) {
    bios_io_calls++;
    BIOS_CHECK(((const u8 *)buf)[0] == 0x5a, "BIOS write payload");
    return c < bios_images[slot].cyls && h < bios_images[slot].heads &&
           s && s <= bios_images[slot].spt ? 0 : -1;
}
int loop_dev_read_lba(int slot, u32 lba, u32 n, void *buf) {
    (void)lba; bios_io_calls++; kmemset(buf, 0x5a, n * bios_images[slot].bps); return 0;
}
int loop_dev_write_lba(int slot, u32 lba, u32 n, const void *buf) {
    (void)slot; (void)lba; (void)n; (void)buf; bios_io_calls++; return 0;
}
static u32 *bios_call(u32 ax, u32 cx, u32 dx, u32 bx) {
    kmemset(bios_frame, 0, sizeof(bios_frame));
    bios_frame[V86F_EAX] = 0xabcd0000 | ax;
    bios_frame[V86F_ECX] = 0x12340000 | cx;
    bios_frame[V86F_EDX] = 0x56780000 | dx;
    bios_frame[V86F_EBX] = 0x9abc0000 | bx;
    bios_frame[V86F_ES] = 0x2000;
    bios_frame[V86F_EBP] = 0x40;
    v86_bios_dispatch(bios_frame, BIOS_INT_DISK);
    return &v86_disklog[((v86_disklog_w - 1) % V86_DISKLOG_N) * V86_DISKLOG_WORDS];
}
static int bios_ok(void) { return !(bios_frame[V86F_EFLAGS] & 1); }
static void bios_pair(const char *first, const char *second) {
    BIOS_CHECK(!v86_bios_attach_disk(first), "BIOS first attach");
    if (second) BIOS_CHECK(!v86_bios_attach_second(second), "BIOS second attach");
    v86_bios_setup();
}
static void bios_disk_cases(void) {
    u32 *log;
    /* Different formats in both orders, including an HDD boot image. */
    bios_pair("d88", "raw");
    bios_call(0x0a91, 0, 0, 0);
    BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xffff) == 0x200 &&
               (bios_frame[V86F_EDX] & 0xffff) == 1, "BIOS second RAW READ ID");
    bios_pair("raw", "d88");
    bios_call(0x0a91, 0, 0, 0);
    BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xffff) == 0x20a &&
               (bios_frame[V86F_EDX] & 0xffff) == 0x103, "BIOS second D88 READ ID");
    bios_pair("hdd", "d88");
    bios_call(0x0a90, 0, 0, 0);
    BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 10,
               "BIOS HDD boot second D88 READ ID");

    bios_pair("raw", "raw");
    bios_call(0x0090, 7, 0, 0);
    bios_call(0x0091, 3, 0, 0);
    bios_call(0x0791, 0, 0, 0);
    log = bios_call(0x0a90, 0, 0, 0);
    BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 7,
               "BIOS per-drive SEEK RECAL");
    BIOS_CHECK(log[V86_DISKLOG_PHYS] == 7, "BIOS per-drive physical log");
    /* Every implicit-seek command updates only the selected drive. */
    for (u32 cmd = 0; cmd < 4; cmd++) {
        static const u32 seek_cmds[] = {0x1691, 0x1191, 0x1591, 0x1a91};
        bios_call(seek_cmds[cmd], 0x205, 1, 0);
        bios_call(0x0a91, 0, 0, 0);
        BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 5,
                   "BIOS selected implicit seek");
        bios_call(0x0a90, 0, 0, 0);
        BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 7,
                   "BIOS implicit seek preserves other drive");
    }
    bios_pair("d88", "d88");
    bios_call(0x0090, 7, 0, 0);
    bios_call(0x0791, 0, 0, 0);
    bios_d88_cyl = 7; bios_d88_head = 1;
    bios_call(0x0690, 0x200, 0x101, 512);
    BIOS_CHECK(bios_ok() && *(u8 *)0x20040 == 0x5a,
               "BIOS per-drive D88 physical read");
    bios_call(0x0091, 9, 0, 0);
    v86_bios_setup();
    bios_call(0x0a90, 0, 0, 0);
    BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 10, "BIOS setup resets first cylinder");
    bios_call(0x0a91, 0, 0, 0);
    BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 10, "BIOS setup resets second cylinder");

    bios_pair("raw", 0);
    bios_images[0].format = LOOP_FMT_FDI;
    for (u32 c = 255; c <= 257; c++) {
        bios_images[0].cyls = c;
        bios_call(0x1a90, 0, 0, 0);
        BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 0, "BIOS FDI wide geometry first ID");
        bios_call(0x1a90, 254, 0, 0);
        BIOS_CHECK(bios_ok() && (bios_frame[V86F_ECX] & 0xff) == 254, "BIOS FDI wide geometry low ID");
    }
    bios_images[0].cyls = 255;
    bios_call(0x1a90, 255, 0, 0);
    BIOS_CHECK(!bios_ok() && ((bios_frame[V86F_EAX] >> 8) & 0xff) == 0xe0,
               "BIOS FDI true cylinder limit");

    bios_pair("raw", 0);
    for (u32 odd = 0; odd < 2; odd++) {
        for (u32 dh = 0; dh < 2; dh++) {
            for (u32 bit2 = 0; bit2 < 2; bit2++) {
                log = bios_call((odd ? 0x0190 : 0x0690) | (bit2 << 2),
                                0x200, (dh << 8) | 1, 512);
                BIOS_CHECK(bios_ok() && log[V86_DISKLOG_PHYS] == ((dh ^ bit2) << 8),
                           "BIOS physical log uses DH");
            }
        }
    }
    bios_pair("hdd", 0);
    log = bios_call(0x8480, 0x33, 0x44, 0x55);
    BIOS_CHECK(bios_ok() && (bios_frame[V86F_EBX] & 0xffff) == 512 &&
               (bios_frame[V86F_ECX] & 0xffff) == 615 &&
               (bios_frame[V86F_EDX] & 0xffff) == 0x411, "BIOS SASI SENSE output");
    BIOS_CHECK(log[V86_DISKLOG_AX] == 0x8480 && log[V86_DISKLOG_CX] == 0x33 &&
               log[V86_DISKLOG_DX] == 0x44 && log[V86_DISKLOG_BX] == 0x55 &&
               log[V86_DISKLOG_ADDRESS] == 0x20040, "BIOS SASI log input registers");
    bios_pair("raw", 0);
    log = bios_call(0x0a90, 0x3333, 0x4400, 0x55);
    BIOS_CHECK(bios_ok() && log[V86_DISKLOG_CX] == 0x3333 &&
               log[V86_DISKLOG_DX] == 0x4400 && log[V86_DISKLOG_BX] == 0x55,
               "BIOS READ ID log input registers");
    u32 logged = v86_disklog_w;
    bios_call(0x0a90, 0x3333, 0x4400, 0x55);
    BIOS_CHECK(v86_disklog_w == logged && log[V86_DISKLOG_RESULT] == 2 * V86_DISKLOG_REPEAT_ONE,
               "BIOS log repeats compare inputs");

    /* N=0..3 and masked N, real transfer, mismatch, partial and zero DONE. */
    for (u32 n = 0; n < 4; n++) {
        bios_pair("raw", 0);
        bios_images[0].bps = 128U << n;
        bios_images[0].format = (n & 1) ? LOOP_FMT_FDI : LOOP_FMT_RAW;
        for (u32 write = 0; write < 2; write++) {
            u32 ax = write ? 0x0590 : 0x0690;
            log = bios_call(ax, ((n | 0xfc) << 8), 1, (128U << n) + 1);
            BIOS_CHECK(bios_ok() && log[V86_DISKLOG_DONE] == (128U << n) + 1,
                       "BIOS N transfer and partial sector");
            log = bios_call(ax, n << 8, 1, 0);
            BIOS_CHECK(bios_ok() && log[V86_DISKLOG_DONE] == 0, "BIOS zero count DONE reset");
            log = bios_call(ax, n << 8, 8, (128U << n) + 1);
            BIOS_CHECK(!bios_ok() && log[V86_DISKLOG_DONE] == (128U << n),
                       "BIOS partial failure DONE");
            log = bios_call(ax, ((n + 1) & 3) << 8, 1, 1);
            BIOS_CHECK(!bios_ok() && log[V86_DISKLOG_DONE] == 0 &&
                       log[V86_DISKLOG_RESULT] % 256 == V86_DISK_FAIL_SECTOR_SIZE,
                       "BIOS bps mismatch DONE reset");
        }
        log = bios_call(0x0090, 0, 0, 0);
        BIOS_CHECK(bios_ok() && log[V86_DISKLOG_DONE] == 0, "BIOS SEEK DONE reset");
    }
    for (u32 attached = 0; attached < 2; attached++) {
        bios_pair("raw", 0);
        if (!attached) v86_bios_detach_disk();
        for (u32 cmd = 0; cmd < 3; cmd++) {
            static const u32 cmds[] = {0x0690, 0x0190, 0x0590};
            u32 fails = v86_disk_fail_n, calls = bios_io_calls;
            log = bios_call(cmds[cmd] | (attached ? 1 : 0), 0x200, 1, 512);
            BIOS_CHECK(!bios_ok() && v86_disk_fail_n == fails + 1 &&
                       v86_disk_fail_why == V86_DISK_FAIL_NO_DEVICE &&
                       log[V86_DISKLOG_DONE] == 0 && bios_io_calls == calls,
                       "BIOS absent unit rejected once before IO");
        }
        u32 fails = v86_disk_fail_n;
        bios_call(0x0390 | (attached ? 1 : 0), 0, 0, 0);
        BIOS_CHECK(bios_ok() && v86_disk_fail_n == fails, "BIOS absent INIT succeeds");
    }
    v86_bios_detach_disk();
    kmemcpy(P2V(0), v86_bios_real_lowmem(), PAGE_SIZE);
    CHECK(!bios_failures, "BIOS disk regressions");
    SAY("GREEN BIOS disk cases: formats / drive positions / geometry / call logs / transfers / absent units");
}

static u32 saved_pt[MEM_1MB / PAGE_SIZE], saved_pde, caller_pd[PDE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u8 original(u32 a) { return (u8)(a ^ (a >> 8) ^ 0x39); }
static u8 mailbox_byte(u32 a) { return (u8)(a * 17 + 3); }
static u32 low_region_count, pool_free;
static struct ledger_region low_regions[LEDGER_MAX_REGIONS];
static void reservation_unchanged(void) {
    CHECK(ledger_region_count == low_region_count, "reservation count unchanged");
    for (u32 i = 0; i < sizeof(low_regions); i++)
        CHECK(((u8 *)ledger_regions)[i] == ((u8 *)low_regions)[i], "reservation bytes unchanged");
    CHECK(pgalloc_free_pages() == pool_free, "session pool unchanged");
    CHECK(!allocation_calls, "zero session backing allocations");
    for (u32 p = 0; p < 160; p++) {
        u32 hits = 0;
        CHECK(!bit(eligible, p), "low pool supply zero");
        for (u32 i = 0; i < ledger_region_count; i++) {
            const struct ledger_region *r = &ledger_regions[i];
            if (p >= r->first && p < r->end) {
                CHECK(r->first == 0 && r->end == 160 && r->type == LEDGER_R_FIXED &&
                      r->owner == LEDGER_OWNER_KERNEL && r->cache == LEDGER_CACHE_WB,
                      "single FIXED kernel WB all 160 PFNs");
                hits++;
            }
        }
        CHECK(hits == 1, "one low reservation");
    }
}
static void restored(void) {
    reservation_unchanged();
    CHECK(!v86_session.open && !paging_v86_session_open(), "session closed");
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++) {
        CHECK(page_tables[0][i] == saved_pt[i], "exact PTE including last ROM page");
        CHECK(tlb[i] == saved_pt[i], "TLB restored before caller");
    }
    CHECK(page_directory[0] == saved_pde && tlb_pde == saved_pde &&
          tlb_root == paging_kernel_pd_phys(), "master PDE CR3 TLB restored");
    CHECK(!(page_tables[0][0] & PTE_PRESENT), "normal page0 NP");
    for (u32 i = 0; i < PAGE_SIZE; i++) {
        CHECK(*(volatile u8 *)P2V(i) == original(i), "page0 restored all 4096 bytes");
        CHECK(v86_session.real_lowmem[i] == original(i), "immutable boot original");
    }
    for (u32 i = 0; i < MEM_AUTOPLAY_MAILBOX_SIZE; i++)
        CHECK(*(volatile u8 *)P2V(MEM_AUTOPLAY_MAILBOX_BASE + i) == mailbox_byte(i), "mailbox restored");
    paging_load_cr3(V2P(caller_pd));
    flush_low(); /* CR3 reload is a TLB flush on the CPU. */
    CHECK(tlb_root == V2P(caller_pd) && tlb_pde == caller_pd[0], "caller CR3 restored");
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++)
        CHECK(tlb[i] == saved_pt[i], "caller shared PT restored");
    paging_load_cr3(paging_kernel_pd_phys());
    flush_low();
}
static void before(void) {
    maps = boundaries = 0;
    for (u32 a = PAGE_SIZE; a < MEM_CONV_END; a++) *(volatile u8 *)P2V(a) = 0xa7;
    for (u32 i = 0; i < MEM_AUTOPLAY_MAILBOX_SIZE; i++)
        *(volatile u8 *)P2V(MEM_AUTOPLAY_MAILBOX_BASE + i) = mailbox_byte(i);
}
static void execute_low(void) {
    reservation_unchanged();
    for (u32 a = 0; a < MEM_CONV_END; a += PAGE_SIZE) {
        CHECK((tlb[a / PAGE_SIZE] & ~(PAGE_SIZE - 1)) == a, "direct physical PFN");
        CHECK((tlb[a / PAGE_SIZE] & (PAGE_RW | PTE_USER)) == (PAGE_RW | PTE_USER), "direct guest RW USER");
    }
    CHECK(tlb_pde & PTE_USER, "guest PDE USER");
    CHECK((*(volatile u8 *)P2V(0x501) & 7) == V86_GUEST_MEM_CODE, "BIOS 640KiB");
    /* mov byte [edi],5a; mov byte [esi],a5; ret. Executed inside direct low
     * RAM, writing the last conventional byte and the 0x8C000 test word.
     * vm86/IRET and actual hardware TLB operation remain NP21/W acceptance. */
    static const u8 code[] = {0xc6, 0x07, 0x5a, 0xc6, 0x06, 0xa5, 0xc3};
    kmemcpy(P2V(V86_TEST_CODE_ADDR), code, sizeof(code));
    void (*entry)(void) = P2V(V86_TEST_CODE_ADDR);
    __asm__ volatile("call *%0" : : "r"(entry), "D"(MEM_CONV_END - 1), "S"(V86_TEST_MAGIC_ADDR) : "memory");
    CHECK(*(volatile u8 *)P2V(MEM_CONV_END - 1) == 0x5a &&
          *(volatile u8 *)P2V(V86_TEST_MAGIC_ADDR) == 0xa5, "execute direct low fixture");
    kmemset(P2V(0), 0xcc, PAGE_SIZE);
    sysclk_detect();
    CHECK(sysclk_is_8mhz() == !!(original(BIOS_WORK_SYSCLK) & BIOS_SYSCLK_8MHZ), "clock boot original");
    CHECK(kbd_bios_shift_peek() == original(BIOS_WORK_KB_SHIFT), "keyboard boot original");
    kmemset(P2V(MEM_AUTOPLAY_MAILBOX_BASE), 0xdd, MEM_AUTOPLAY_MAILBOX_SIZE);
}
void _start(void) {
    u32 args[6] = {0, 16 * 1024 * 1024, 7, 0x32, ~0U, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(!result, "host low address mapping (qemu -B required)");
    host_map_fixed_paging();
    for (u32 i = 0; i < PAGE_SIZE; i++) *(volatile u8 *)P2V(i) = original(i);
    v86_bios_save_real();
    bios_disk_cases();
    paging_init(16384);
    host_pool_boot(16384);
#include "low_reservation.inc"
    for (u32 i = 0; i < sizeof(memory_boot_fixed) / sizeof(memory_boot_fixed[0]); i++) {
        CHECK(ledger_register_region(memory_boot_fixed[i].type, memory_boot_fixed[i].owner,
              memory_boot_fixed[i].first, memory_boot_fixed[i].end,
              memory_boot_fixed[i].cache, LEDGER_RF_PERMANENT), "boot low reservation registration");
    }
    low_region_count = ledger_region_count;
    kmemcpy(low_regions, ledger_regions, sizeof(low_regions));
    pool_free = pgalloc_free_pages();
    reservation_unchanged();
    paging_reclaim_conventional();
    CHECK(!(page_tables[0][0] & PTE_PRESENT) && memmap_want_at(0, 0) == MM_NP, "normal page0 NP");
    /* Shared SHM/trampoline PDE USER must survive all exits, including failure
     * before the final ROM mapping (last saved PTE is deliberately nontrivial). */
    page_directory[0] |= PTE_USER;
    for (u32 i = 0; i < PDE_COUNT; i++) caller_pd[i] = page_directory[i];
    live_addrspaces = 1;
    saved_pde = page_directory[0];
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++) saved_pt[i] = page_tables[0][i];
    flush_low();
    before();
    missing_mailbox = 1;
    CHECK(v86_session_begin(2) == -2 && !module_refs, "mailbox unavailable before writes");
    missing_mailbox = 0;
    refuse_module = 1;
    CHECK(v86_session_begin(2) == -1 && !module_refs, "module unavailable before writes");
    refuse_module = 0;
    snapshot_fail = 1;
    CHECK(v86_session_begin(2) == -1 && !module_refs, "snapshot failure releases pin");
    snapshot_fail = 0;
    restored();
    for (u32 n = 1; n <= V86_IDENT_MAP_N + 2; n++) {
        before(); fail_map = n;
        CHECK(v86_session_begin(2) == -3 && !module_refs, "every preparation map failure");
        restored();
    }
    fail_map = 0;
    for (u32 n = 1; n <= 3; n++) {
        before(); boundary_fail = n;
        if (!__builtin_setjmp(abort_jmp)) {
            v86_session_begin(2);
            CHECK(0, "preparation abort reached");
        }
        v86_session_end(); restored();
        CHECK(!module_refs, "preparation abort releases pin");
    }
    boundary_fail = 0;
    /* Normal, #PF, #GP, STOP share the mandatory RESTORING path. Exception
     * unwind is separately exercised by the real exec paths in shm_user. */
    for (u32 exit = 0; exit < 4; exit++) {
        before();
        CHECK(!v86_session_begin(2), "direct begin");
        execute_low();
        if (exit) { v86_session.aborting = 1; kctx_exc_depth = 1; }
        v86_session_end();
        restored();
        if (exit) {
            CHECK(v86_session.release_pending && module_refs, "exception retains module");
            kctx_exc_depth = 0; v86_session_end();
        }
        CHECK(!module_refs && !v86_session.release_pending, "all exits release");
    }
    /* Exhaust the real contiguous pool after module/staging preparation.
     * The direct product path still runs, and repeated sessions leak no pages.
     * Actual GUI transitions remain a guest acceptance (gfx is stubbed here). */
    u32 held[4096], held_n = 0, p;
    while ((p = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, 1))) held[held_n++] = p;
    CHECK(!pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, 159), "contiguous pool exhausted");
    pool_free = pgalloc_free_pages();
    CHECK(!pool_free, "pool completely exhausted");
    for (u32 cycle = 0; cycle < 20; cycle++) {
        before();
        CHECK(!v86_session_begin(LEDGER_OWNER_KERNEL), "V86 available with exhausted pool");
        execute_low(); v86_session_end(); restored();
        CHECK(!module_refs && !v86_session.release_pending, "20 sessions no pin leak");
    }
    while (held_n) CHECK(pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,
                       held[--held_n] / PAGE_SIZE, 1), "release exhaustion fixture");
    pool_free = pgalloc_free_pages();
    /* A failed restore must never reach caller copyout/unpin. */
    for (u32 kind = 1; kind <= 3; kind++) {
        before(); CHECK(!v86_session_begin(2), "fail-stop begin");
        expect_stop = kind;
        if (kind == 1) v86_map_session = 0;
        if (kind == 2) corrupt_restore_pde = 1;
        if (kind == 3) v86_session.low_pte[V86_VRAM_START / PAGE_SIZE] ^= PTE_PCD;
        kmemset(P2V_IO(TVRAM_BASE), 0, TVRAM_BPR);
        if (!__builtin_setjmp(stop_jmp)) {
            v86_session_end(); CHECK(0, "restore error fail-stop");
        }
        CHECK(module_refs && v86_session.open, "fail-stop retains pin");
        /* Only the fixture resumes a fail-stop, to exercise each diagnosis. */
        corrupt_restore_pde = v86_restore_mismatch = v86_session.closing = 0;
        v86_map_session = &v86_session;
        kmemcpy(v86_session.low_pte, saved_pt, sizeof(saved_pt));
        v86_session_end();
        restored();
    }
    SAY("GREEN single FIXED/kernel/WB 160 PFNs / 20 exhausted-pool sessions / backing alloc=0 (-636KiB) / direct 640KiB execution / page0 NP / mailbox / prepare failures / exits / TLB / fail-stop");
    die(0);
}
