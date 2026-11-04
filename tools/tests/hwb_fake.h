/* Shell backend test substrate, libc-free ILP32. Test limits are capacities,
 * not product address allowlists: unknown port/phys writes create byte cells.
 * HW_BASIC_DESIGN §§2-4; PCI Status W1C: PCI Local Bus 2.2 §6.2.3. */
#ifndef TESTS_HWB_FAKE_H
#define TESTS_HWB_FAKE_H
#include "userland/shell/hwb.h"
#include "os32_kapi_shared.h"

static const char *hwb_test_name = "unnamed";
static inline void hwb_test_write(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(s), "d"(n) : "memory", "cc");
}
static inline __attribute__((noreturn)) void hwb_test_exit(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static inline void hwb_test_check(int ok, const char *expr)
{
    if (!ok) {
        hwb_test_write("FAIL:"); hwb_test_write(hwb_test_name);
        hwb_test_write(": "); hwb_test_write(expr); hwb_test_write("\n");
        hwb_test_exit(1);
    }
}
#define HWB_CHECK(expr) hwb_test_check(!!(expr), #expr)
#ifndef CHECK
#define CHECK(expr) HWB_CHECK(expr)
#endif
#define HWB_TEST(name) (hwb_test_name = (name))

/* Explicit host-fixture storage budgets, independent of product limits.
 * Override before inclusion for million-iteration traces. Exhaustion fails
 * CHECK rather than silently dropping events or restricting bus addresses. */
#ifndef HWBF_PCI_DEVICES
#define HWBF_PCI_DEVICES 16U
#endif
#ifndef HWBF_CELLS
#define HWBF_CELLS 4096U
#endif
#ifndef HWBF_REGISTERS
#define HWBF_REGISTERS 64U
#endif
#ifndef HWBF_EVENTS
#define HWBF_EVENTS 8192U
#endif
#ifndef HWBF_LINES
#define HWBF_LINES 256U
#endif
#ifndef HWBF_FILES
#define HWBF_FILES 16U
#endif
#define HWBF_STATUS_OFFSET 6U
#define HWBF_STATUS_W1C 0xF900U
struct hwbf_pci { u32 bdf, vendor, device; u8 config[HW_PCI_CONFIG_BYTES]; };
struct hwbf_cell { u32 space, addr; u8 value; };
struct hwbf_register { u32 space, bdf, addr, width, write_only; };
struct hwbf_file { const char *name, *data; u32 bytes; };
enum hwbf_event_kind { HWBF_ACCESS, HWBF_TIME, HWBF_YIELD, HWBF_POINT,
                       HWBF_LINE, HWBF_FILE, HWBF_FIND };
/* req is the exact input; result is output (valid only when rc==0). */
struct hwbf_event { enum hwbf_event_kind kind; struct hw_req req, result; int rc; u32 a, b; };
struct hwb_fake {
    struct hwb_backend backend;
    struct hwbf_pci pci[HWBF_PCI_DEVICES]; u32 pci_count;
    struct hwbf_cell cells[HWBF_CELLS]; u32 cell_count;
    struct hwbf_register registers[HWBF_REGISTERS]; u32 register_count;
    struct hwbf_event events[HWBF_EVENTS]; u32 event_count;
    char lines[HWBF_LINES][HWB_LOG_BYTES]; u32 line_count;
    struct hwbf_file files[HWBF_FILES]; u32 file_count;
    u32 mechanism, now_lo, now_hi, tick_us, reverse_clock, clock_fail_at, times;
    u32 points, point_at, pending_abort, yields;
    /* Inject raw call failure or status failure on the Nth access, zero=off. */
    u32 accesses, access_fail_at, status_fail_at, failure_status;
    u32 bus_accesses; /* dispatches reaching the fake bus after validation */
    int call_error, point_error, file_error, find_error;
};
static inline void hwbf_zero(void *p, u32 bytes)
{
    u8 *b = p;
    while (bytes--) *b++ = 0;
}
static inline int hwbf_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static inline struct hwbf_event *hwbf_event(struct hwb_fake *f, enum hwbf_event_kind k)
{
    CHECK(f->event_count < HWBF_EVENTS);
    struct hwbf_event *e = &f->events[f->event_count++];
    hwbf_zero(e, sizeof(*e)); e->kind = k;
    return e;
}
/* Fixture setup never adds an event. Newly registered config bytes start at 0. */
static inline struct hwbf_pci *hwbf_add_pci(struct hwb_fake *f, u32 bdf,
                                          u32 vendor, u32 device)
{
    CHECK(f->pci_count < HWBF_PCI_DEVICES);
    struct hwbf_pci *p = &f->pci[f->pci_count++];
    hwbf_zero(p, sizeof(*p)); p->bdf = bdf; p->vendor = vendor; p->device = device;
    p->config[0] = vendor; p->config[1] = vendor >> 8;
    p->config[2] = device; p->config[3] = device >> 8;
    return p;
}
static inline struct hwbf_pci *hwbf_pci(struct hwb_fake *f, u32 bdf)
{
    for (u32 i = 0; i < f->pci_count; i++) if (f->pci[i].bdf == bdf) return &f->pci[i];
    return 0;
}
static inline u8 *hwbf_byte(struct hwb_fake *f, u32 space, u32 addr)
{
    for (u32 i = 0; i < f->cell_count; i++)
        if (f->cells[i].space == space && f->cells[i].addr == addr) return &f->cells[i].value;
    CHECK(f->cell_count < HWBF_CELLS);
    struct hwbf_cell *c = &f->cells[f->cell_count++];
    c->space = space; c->addr = addr; c->value = 0;
    return &c->value;
}
static inline void hwbf_add_register(struct hwb_fake *f, u32 space, u32 bdf,
                                     u32 addr, u32 width, u32 write_only)
{
    CHECK(f->register_count < HWBF_REGISTERS);
    f->registers[f->register_count++] = (struct hwbf_register){space,bdf,addr,width,write_only};
}
static inline int hwbf_access_result(struct hwbf_event *e, const struct hw_req *r, int rc)
{
    e->result = *r; e->rc = rc;
    return rc;
}
static inline int hwbf_access(void *ctx, struct hw_req *r)
{
    struct hwb_fake *f = ctx;
    struct hwbf_event *e = hwbf_event(f, HWBF_ACCESS);
    e->req = *r; f->accesses++;
    if (f->access_fail_at == f->accesses) return hwbf_access_result(e, r, f->call_error);
    /* Wire shape errors leave every request byte intact. POINT has its own
     * backend adapter; access accepts only READ/WRITE, per hwb_backend. */
    if (r->version != HW_VERSION || r->status != 0 ||
        (r->action == HW_READ && r->value != 0) ||
        (r->action != HW_READ && r->action != HW_WRITE) ||
        r->space < HW_PCI || r->space > HW_PHYS ||
        (r->space != HW_PCI && r->bdf))
        return hwbf_access_result(e, r, OS32_ERR_INVAL);
    u32 value = r->value;
    if (r->action == HW_READ) r->value = 0;
    r->status = HW_OK;
    if ((r->width != 1 && r->width != 2 && r->width != 4) ||
        (r->space == HW_PCI && (r->bdf > HW_BDF_MASK || r->addr >= HW_PCI_CONFIG_BYTES ||
                                r->width > HW_PCI_CONFIG_BYTES - r->addr)) ||
        (r->space == HW_IO && (r->addr > U16_MAX || r->width - 1 > U16_MAX - r->addr)) ||
        (r->width - 1 > U32_MAX - r->addr) ||
        (r->space != HW_IO && (r->addr & (r->width - 1))) ||
        (r->action == HW_WRITE && r->width < 4 && value >> (r->width * BITS_PER_BYTE))) {
        r->status = HW_ARG; return hwbf_access_result(e, r, 0);
    }
    if (f->status_fail_at == f->accesses) { r->status = f->failure_status; return hwbf_access_result(e, r, 0); }
    if (r->space == HW_PCI && !f->mechanism) { r->status = HW_NOSYS; return hwbf_access_result(e, r, 0); }
    f->bus_accesses++;
    for (u32 i = 0; i < f->register_count; i++) {
        const struct hwbf_register *reg = &f->registers[i];
        if (reg->write_only && reg->space == r->space && reg->bdf == r->bdf &&
            r->addr <= reg->addr + reg->width - 1 && reg->addr <= r->addr + r->width - 1 &&
            r->action == HW_READ) { r->status = HW_IO_ERROR; return hwbf_access_result(e, r, 0); }
    }
    struct hwbf_pci *p = r->space == HW_PCI ? hwbf_pci(f, r->bdf) : 0;
    for (u32 lane = 0; lane < r->width; lane++) {
        u32 a = r->addr + lane;
        u8 *b = r->space == HW_PCI ? (p ? &p->config[a] : 0) : hwbf_byte(f, r->space, a);
        if (r->action == HW_READ) {
            r->value |= (u32)(b ? *b : U8_MAX) << (lane * BITS_PER_BYTE);
        } else if (b) {
            u8 v = value >> (lane * BITS_PER_BYTE);
            if (r->space == HW_PCI && a >= HWBF_STATUS_OFFSET && a < HWBF_STATUS_OFFSET + 2U) {
                u8 w1c = HWBF_STATUS_W1C >> ((a - HWBF_STATUS_OFFSET) * BITS_PER_BYTE);
                *b &= (u8)~(v & w1c);
            } else *b = v;
        }
    }
    return hwbf_access_result(e, r, 0);
}
static inline int hwbf_time(void *ctx, u32 *lo, u32 *hi)
{
    struct hwb_fake *f = ctx;
    struct hwbf_event *e = hwbf_event(f, HWBF_TIME);
    if (++f->times == f->clock_fail_at) return e->rc = OS32_ERR_IO;
    *lo = e->a = f->now_lo; *hi = e->b = f->now_hi;
    u32 before = f->now_lo;
    if (f->reverse_clock) {
        f->now_lo -= f->tick_us;
        if (f->now_lo > before) f->now_hi--;
    } else {
        f->now_lo += f->tick_us;
        if (f->now_lo < before) f->now_hi++;
    }
    return 0;
}
static inline void hwbf_yield(void *ctx)
{
    struct hwb_fake *f = ctx;
    hwbf_event(f, HWBF_YIELD); f->yields++;
}
static inline int hwbf_point(void *ctx, u32 *flag)
{
    struct hwb_fake *f = ctx;
    struct hwbf_event *e = hwbf_event(f, HWBF_POINT);
    f->points++;
    if (f->point_error) return e->rc = f->point_error;
    *flag = e->a = !!(f->pending_abort || f->points == f->point_at);
    f->pending_abort = 0;
    return 0;
}
static inline void hwbf_line(void *ctx, const char *line)
{
    struct hwb_fake *f = ctx;
    struct hwbf_event *e = hwbf_event(f, HWBF_LINE);
    CHECK(f->line_count < HWBF_LINES);
    e->a = f->line_count;
    u32 n = 0;
    do { CHECK(n < HWB_LOG_BYTES); f->lines[f->line_count][n] = line[n]; } while (line[n++]);
    f->line_count++;
}
static inline void hwbf_add_file(struct hwb_fake *f, const char *name,
                                 const char *data, u32 bytes)
{
    CHECK(f->file_count < HWBF_FILES);
    f->files[f->file_count++] = (struct hwbf_file){name,data,bytes};
}
static inline int hwbf_read_file(void *ctx, const char *path, char *buf, u32 cap, u32 *bytes)
{
    struct hwb_fake *f = ctx;
    struct hwbf_event *e = hwbf_event(f, HWBF_FILE);
    if (f->file_error) return e->rc = f->file_error;
    for (u32 i = 0; i < f->file_count; i++) if (hwbf_equal(path, f->files[i].name)) {
        const struct hwbf_file *p = &f->files[i];
        if (p->bytes > cap) return e->rc = OS32_ERR_INVAL;
        for (u32 j = 0; j < p->bytes; j++) buf[j] = p->data[j];
        *bytes = e->a = p->bytes; return 0;
    }
    return e->rc = OS32_ERR_NOTFOUND;
}
static inline int hwbf_find(void *ctx, u32 vendor, u32 device, u32 start, u32 *bdfs, u32 cap)
{
    struct hwb_fake *f = ctx;
    struct hwbf_event *e = hwbf_event(f, HWBF_FIND);
    e->a = vendor; e->b = device;
    if (f->find_error) return e->rc = f->find_error;
    u32 total = 0, stored = 0;
    for (u32 i = 0; i < f->pci_count; i++) {
        if (f->pci[i].vendor != vendor || f->pci[i].device != device) continue;
        if (total++ >= start && stored < cap) bdfs[stored++] = f->pci[i].bdf;
    }
    return e->rc = (int)total;
}
/* Zeroed fake: mechanism present, frozen zero clock, failures/injections off. */
static inline void hwbf_init(struct hwb_fake *f)
{
    hwbf_zero(f, sizeof(*f)); f->mechanism = 1; f->call_error = OS32_ERR_IO;
    f->failure_status = HW_IO_ERROR;
    f->backend = (struct hwb_backend){f, hwbf_access, hwbf_time, hwbf_yield,
                                    hwbf_point, hwbf_line, hwbf_read_file, hwbf_find};
}
/* Retain bus/files/clock/fault settings, clear observation buffers/counters. */
static inline void hwbf_clear_trace(struct hwb_fake *f)
{
    f->event_count = f->line_count = f->accesses = f->times = f->points = f->yields = 0;
}
#endif
