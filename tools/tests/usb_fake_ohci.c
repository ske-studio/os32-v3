/* Independent frame model: OpenHCI 1.0a §§4.2,4.3.1,5.2; USB_DESIGN §11.
 * No libc. Device accesses use bus addresses and LE accessors, never shadows.
 * A frame retains the EDs it fetched, including their original successors.
 * The small core below supplies only the frozen 1A API used by this HCD test.
 */
#include "drivers/usb/ohci_priv.h"
#define PAGE 4096u
#define NPAGES 8u
#define MAX_HELD 96u
#define MEM_BYTES (256u * 1024u)
static u8 pages[NPAGES][PAGE] __attribute__((aligned(4096)));
static u8 mem[MEM_BYTES] __attribute__((aligned(16)));
static u32 mem_top, mem_frees, page_frees, quarantines, now_us;
static u8 page_state[NPAGES];
static u32 page_limit, bad_boundary, fail_mem, bm_fail, fail_after_bm;
static u32 writes, early_mie, gone_count, notifications, status_count;
static u32 data_bytes;
static u32 alloc_off[512], alloc_size[512], alloc_count;
static u8 alloc_freed[512];
static u32 held_bus[MAX_HELD], held_next[MAX_HELD], held_count;
static u8 retained[NPAGES][PAGE / 16u];
static int bad_dma, hold_active;
static struct usb_hc hcs[2];
static unsigned int regs[2][64];
static u32 event_td, event_cc, event_len, event_status_cc, event_no_response;
static u32 no_hcr, no_ownership, no_pes, no_clock;
static u32 actual_reads, sync_dev, sync_cpu, op_seen;
static int failure;

static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) ++n;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void number(u32 v)
{
    char b[12]; u32 i = sizeof(b) - 1u;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    say(b + i);
}
#define REQUIRE(v) do { if (!(v)) { say("FAIL line "); number(__LINE__); \
    say("\n"); failure = 1; return; } } while (0)
static void zero(void *v, u32 n) { u8 *p = v; while (n--) *p++ = 0; }
static u32 rget(u32 h, u32 off) { return usb_fake_native_rd((u8 *)regs[h] + off); }
static void rput(u32 h, u32 off, u32 v) { usb_fake_native_wr((u8 *)regs[h] + off, v); }
static int which(const volatile void *p)
{
    u32 i;
    for (i = 0; i < 2; ++i)
        if ((const volatile u8 *)p >= (const volatile u8 *)regs[i] &&
            (const volatile u8 *)p < (const volatile u8 *)(regs[i] + 64)) return (int)i;
    return -1;
}
static u32 page_bus(u32 i) { return 0x10f000u + i * PAGE; }
static void *bus_va(u32 bus)
{
    u32 i;
    for (i = 0; i < NPAGES; ++i)
        if (bus >= page_bus(i) && bus - page_bus(i) < PAGE) {
            if (!page_state[i]) bad_dma = 1;
            return pages[i] + bus - page_bus(i);
        }
    bad_dma = 1;
    return pages[0];
}
static void retain(u32 bus)
{
    u32 i;
    if (!bus) return;
    (void)bus_va(bus);
    for (i = 0; i < NPAGES; ++i)
        if (bus >= page_bus(i) && bus - page_bus(i) < PAGE)
            retained[i][(bus - page_bus(i)) / 16u] = 1;
}
static int is_retained(u32 bus)
{
    u32 i;
    for (i = 0; i < NPAGES; ++i)
        if (bus >= page_bus(i) && bus - page_bus(i) < PAGE)
            return retained[i][(bus - page_bus(i)) / 16u];
    return 0;
}

u32 mmio_rd32_le(const volatile void *p)
{
    ++actual_reads;
    return usb_fake_native_rd(p);
}
void mmio_wr32_le(volatile void *p, u32 v)
{
    int h = which(p);
    u32 off;
    if (h < 0) { bad_dma = 1; return; }
    off = (u32)((volatile u8 *)p - (volatile u8 *)regs[h]);
    ++writes;
    if (off == OHCI_INTERRUPT_ENABLE && (v & OHCI_INTR_MIE) &&
        hcs[h].state == HC_STARTING) ++early_mie;
    if (off == OHCI_INTERRUPT_STATUS) rput((u32)h, off, rget((u32)h, off) & ~v);
    else if (off == OHCI_INTERRUPT_DISABLE)
        rput((u32)h, OHCI_INTERRUPT_ENABLE, rget((u32)h, OHCI_INTERRUPT_ENABLE) & ~v);
    else if (off == OHCI_INTERRUPT_ENABLE)
        rput((u32)h, off, rget((u32)h, off) | v);
    else if (off >= OHCI_RH_PORT_STATUS(1) && off <= OHCI_RH_PORT_STATUS(15)) {
        u32 old = rget((u32)h, off);
        old &= ~(v & 0x1f0000u);
        if (v & OHCI_PORT_CCS) { if (!no_pes) old &= ~OHCI_PORT_PES; }
        if (v & OHCI_PORT_PES) old |= OHCI_PORT_PES;
        if (v & OHCI_PORT_PPS) old |= OHCI_PORT_PPS;
        if (v & OHCI_PORT_PRS) old |= OHCI_PORT_PRS;
        rput((u32)h, off, old);
    } else {
        rput((u32)h, off, v);
        if (off == OHCI_CONTROL && (v & OHCI_CTL_HCFS_MASK) == OHCI_CTL_OPERATIONAL) {
            struct ohci_hcca *hcca = bus_va(rget((u32)h, OHCI_HCCA));
            u32 i;
            ++op_seen;
            if (!sync_dev) bad_dma = 1;
            for (i = 0; i < 32; ++i) {
                u32 bus = dma_le32_get(&hcca->interrupt[i]), steps = 0;
                if (!bus) bad_dma = 1;
                while (bus && steps++ < MAX_HELD) {
                    struct ohci_ed_hw *ed = bus_va(bus);
                    if (!(dma_le32_get(&ed->flags) & OHCI_ED_SKIP)) bad_dma = 1;
                    bus = dma_le32_get(&ed->nexted);
                }
                if (steps >= MAX_HELD) bad_dma = 1;
            }
        }
    }
}
/* Poison every returned suballocation, not just whole platform pages. */
void __real_usb_dmamem_free(struct usb_hc *, struct usb_dma *);
void __wrap_usb_dmamem_free(struct usb_hc *hc, struct usb_dma *d)
{
    u32 i;
    if (hc->state != HC_DEAD && d->va) {
        for (i = 0; i < d->size; i += 16u) if (is_retained(d->bus + i)) bad_dma = 1;
        for (i = 0; i < d->size; ++i) ((u8 *)d->va)[i] = 0xa5;
    }
    __real_usb_dmamem_free(hc, d);
}
int usbp_dma_alloc(u32 size, u32 align, struct usb_dma *d)
{
    u32 i;
    if (size != PAGE || align != PAGE) return -1;
    for (i = 0; i < page_limit; ++i) if (!page_state[i]) {
        page_state[i] = 1;
        d->bus = page_bus(i) + (bad_boundary ? 0xff0u : 0);
        d->va = pages[i]; d->size = size; d->cookie = i + 1u;
        return 0;
    }
    return -1;
}
int usbp_dma_free(struct usb_dma *d)
{
    u32 i = d->cookie - 1u, j;
    if (i >= NPAGES || page_state[i] != 1) { bad_dma = 1; return -1; }
    for (j = 0; j < PAGE / 16u; ++j) if (retained[i][j]) bad_dma = 1;
    page_state[i] = 0; ++page_frees;
    for (j = 0; j < PAGE; ++j) pages[i][j] = 0xa5;
    return 0;
}
int usbp_dma_quarantine(struct usb_dma *d)
{
    u32 i = d->cookie - 1u;
    if (i >= NPAGES || page_state[i] != 1) { bad_dma = 1; return -1; }
    page_state[i] = 2; ++quarantines; return 0;
}
void usbp_dma_sync(struct usb_dma *d, u32 off, u32 len, int dir)
{
    u32 bus = d->bus + off, i;
    if (off + len > d->size) bad_dma = 1;
    if (dir == USBP_TO_DEV) {
        ++sync_dev;
        if (hold_active) for (i = 0; i < len; i += 16u)
            if (is_retained(bus + i)) {
                /* SKIP is the only allowed CPU change to a held ED. */
                struct ohci_ed_hw *ed = bus_va(bus + i);
                if (!(dma_le32_get(&ed->flags) & OHCI_ED_SKIP)) bad_dma = 1;
            }
    } else ++sync_cpu;
}
void *usbp_mem_alloc(u32 bytes)
{
    void *p;
    if (fail_mem || bytes > MEM_BYTES - mem_top) return NULL;
    if (alloc_count == 512u) return NULL;
    alloc_off[alloc_count] = mem_top; alloc_size[alloc_count] = bytes;
    alloc_freed[alloc_count++] = 0;
    p = mem + mem_top; mem_top += (bytes + 15u) & ~15u; return p;
}
void usbp_mem_free(void *p)
{
    u32 i, j;
    if (!p) return;
    for (i = 0; i < alloc_count; ++i) if (p == mem + alloc_off[i]) {
        if (alloc_freed[i]) { bad_dma = 1; return; }
        alloc_freed[i] = 1; ++mem_frees;
        for (j = 0; j < alloc_size[i]; ++j) mem[alloc_off[i] + j] = 0xa5;
        return;
    }
    bad_dma = 1;
}
int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear)
{
    (void)f; (void)clear;
    if (set & 4u) {
        if (bm_fail) return -1;
        if (fail_after_bm) rput(0, OHCI_RH_DESCRIPTOR_A, 0);
    }
    return 0;
}
void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 bar) { (void)f; (void)bar; }
u32 usbp_ms(void) { return now_us / 1000u; }
void usbp_udelay(u32 us)
{
    u32 h;
    now_us += us;
    for (h = 0; h < 2; ++h) {
        if (!no_hcr) rput(h, OHCI_COMMAND_STATUS, rget(h, OHCI_COMMAND_STATUS) & ~OHCI_CMD_HCR);
        if (!no_ownership) rput(h, OHCI_CONTROL, rget(h, OHCI_CONTROL) & ~OHCI_CTL_IR);
    }
}
void usbp_log(const char *s, ...) { (void)s; }

/* Lists are fetched at frame begin. Processing follows saved links even after
 * unlink, while the same frame's TD writes can finish after stop. */
static void fetch_list(u32 bus)
{
    u32 steps = 0;
    while (bus && steps++ < MAX_HELD) {
        struct ohci_ed_hw *ed = bus_va(bus);
        u32 next = dma_le32_get(&ed->nexted);
        if (!(dma_le32_get(&ed->flags) & OHCI_ED_SKIP)) {
            u32 td = dma_le32_get(&ed->headp) & OHCI_PTR_MASK, count = 0;
            if (held_count == MAX_HELD) { bad_dma = 1; return; }
            held_bus[held_count] = bus; held_next[held_count++] = next;
            retain(bus);
            while (td && count++ < MAX_HELD) {
                struct ohci_td_hw *t = bus_va(td);
                retain(td); td = dma_le32_get(&t->nexttd) & OHCI_PTR_MASK;
            }
        }
        bus = next;
    }
    if (steps >= MAX_HELD) bad_dma = 1;
}
static void frame_begin(u32 h)
{
    u32 ctl = rget(h, OHCI_CONTROL);
    held_count = 0; zero(retained, sizeof(retained)); hold_active = 1;
    if ((ctl & OHCI_CTL_HCFS_MASK) != OHCI_CTL_OPERATIONAL) return;
    if (ctl & OHCI_CTL_CLE) fetch_list(rget(h, OHCI_CONTROL_HEAD_ED));
    if (ctl & OHCI_CTL_BLE) fetch_list(rget(h, OHCI_BULK_HEAD_ED));
    if (ctl & OHCI_CTL_PLE) {
        struct ohci_hcca *a = bus_va(rget(h, OHCI_HCCA));
        fetch_list(dma_le32_get(&a->interrupt[rget(h, OHCI_FM_NUMBER) & 31u]));
    }
}
static void frame_end(u32 h)
{
    u32 i, ordinal = 0;
    /* Stop writes retire the fetched list; software state alone cannot. */
    if ((rget(h, OHCI_CONTROL) & OHCI_CTL_HCFS_MASK) != OHCI_CTL_OPERATIONAL ||
        (rget(h, OHCI_COMMAND_STATUS) & OHCI_CMD_HCR)) held_count = 0;
    for (i = 0; i < held_count; ++i) {
        struct ohci_ed_hw *ed = bus_va(held_bus[i]);
        u32 head = dma_le32_get(&ed->headp), count = 0;
        u32 bus = head & OHCI_PTR_MASK, carry = head & OHCI_TOGGLECARRY;
        if (event_no_response || (head & OHCI_HALTED)) continue;
        /* The controller can continue through an unlinked ED's NextED. */
        if (dma_le32_get(&ed->nexted) != held_next[i]) bad_dma = 1;
        while (bus && count++ < MAX_HELD) {
            struct ohci_td_hw *td = bus_va(bus);
            u32 f = dma_le32_get(&td->flags), cbp = dma_le32_get(&td->cbp);
            u32 be = dma_le32_get(&td->be), next = dma_le32_get(&td->nexttd);
            u32 len = cbp ? be - cbp + 1u : 0, cc = 0;
            int setup = (f & (3u << 19)) == 0;
            ++ordinal;
            if (!len && !setup) { ++status_count; cc = event_status_cc; }
            if (ordinal == event_td) {
                cc = event_cc;
                if (event_len < len) {
                    cbp += event_len;
                    if (!cc && !(f & OHCI_TD_ROUND)) cc = OHCI_CC_DATA_UNDERRUN;
                } else cbp = 0;
            } else cbp = 0;
            if (len && !setup && (f & (3u << 19)) == OHCI_TD_IN) {
                u32 start = dma_le32_get(&td->cbp);
                u32 done = cbp ? cbp - start : len;
                data_bytes += done;
                for (u32 j = 0; j < done; ++j) *(u8 *)bus_va(start + j) = 0x5a;
            }
            for (u32 page = 0; page < NPAGES; ++page)
                if (bus >= page_bus(page) && bus - page_bus(page) < PAGE &&
                    page_state[page] == 2) bad_dma = 1;
            dma_le32_put(&td->cbp, cbp);
            dma_le32_put(&td->flags, (f & ~OHCI_TD_CC_MASK) | (cc << OHCI_TD_CC_SHIFT));
            /* Real OHCI replaces NextTD with the done-list link. */
            dma_le32_put(&td->nexttd, 0);
            if (!cc || cc == OHCI_CC_DATA_UNDERRUN) {
                u32 done = cbp ? cbp - dma_le32_get(&td->be) + len - 1u : len;
                u32 mps = dma_le32_get(&ed->flags) >> OHCI_ED_MPS_SHIFT;
                u32 packets = len ? (done / mps + ((done % mps) || cbp ? 1u : 0u)) : 1u;
                if ((f & (3u << 24)) == OHCI_TD_DATA0) carry = 0;
                if ((f & (3u << 24)) == OHCI_TD_DATA1) carry = OHCI_TOGGLECARRY;
                if (packets & 1u) carry ^= OHCI_TOGGLECARRY;
            }
            if (cc) {
                dma_le32_put(&ed->headp, next | carry | OHCI_HALTED);
                break;
            }
            dma_le32_put(&ed->headp, next | carry);
            bus = next;
        }
        if (count >= MAX_HELD) bad_dma = 1;
    }
    now_us += 1000;
    if (!(no_clock && h == 0)) rput(h, OHCI_FM_NUMBER, (rget(h, OHCI_FM_NUMBER) + 1u) & 0xffffu);
    hold_active = 0; zero(retained, sizeof(retained));
}
static void frame(u32 h) { frame_begin(h); frame_end(h); }

/* Minimal 1C stand-ins. They maintain core-owned active/notification/delay
 * state through the exact fixed API. Integration with real 1C is a PM gate. */
int usb_xfer_start(struct usb_xfer *x)
{
    struct usb_hc *hc = x->dev->hc;
    int rc;
    if (hc->state != HC_RUNNING) return -USB_E_HC;
    x->curr_set ^= 1;
    x->did_dma_delay = 0;
    x->actual = 0; x->error = USB_X_OK;
    rc = hc->ops->xfer_enter(hc, x);
    if (rc) return rc;
    x->state = XS_ACTIVE;
    return 0;
}
void usb_xfer_stop(struct usb_xfer *x)
{
    struct usb_hc *hc = x->dev->hc;
    if (hc->state != HC_RUNNING || x->state != XS_ACTIVE) return;
    hc->ops->xfer_close(hc, x);
    x->error = USB_X_CANCELLED;
    if (!x->did_dma_delay) {
        x->dma_delay_frame = hc->ops->frame(hc);
        x->dma_delay_deadline_ms = usbp_ms() + USB_DMA_DELAY_MS;
        x->did_dma_delay = 1;
        x->state = XS_DMA_DELAY;
    } else x->state = XS_CB_PENDING;
}
void usb_xfer_notify(struct usb_xfer *x)
{
    if (x->state != XS_CB_PENDING || x->dev->hc->state != HC_RUNNING) return;
    x->state = XS_IDLE; ++notifications;
    if (x->done) x->done(x);
}
static void service(struct usb_hc *hc)
{
    struct usb_xfer *x;
    u32 st;
    if (hc->state != HC_RUNNING) return;
    st = hc->ops->intr_collect(hc);
    if (st & OHCI_INTR_UE) { usb_hc_dead(hc, USB_WHY_UE); return; }
    hc->ops->scan(hc, 32);
    for (x = hc->active; x; x = x->next) if (x->state == XS_DMA_DELAY) {
        if ((u16)(hc->ops->frame(hc) - x->dma_delay_frame) >= USB_DMA_DELAY_FRAMES)
            x->state = XS_CB_PENDING;
        else if ((i32)(usbp_ms() - x->dma_delay_deadline_ms) >= 0) {
            usb_hc_dead(hc, USB_WHY_DMA_DELAY); return;
        }
    }
    for (x = hc->active; x; x = x->next) usb_xfer_notify(x);
}
static void gone(struct usb_dev *d, struct usb_iface *i)
{ (void)d; (void)i; ++gone_count; }
static const struct usb_class cls = { .gone = gone };

static void fresh(void)
{
    zero(hcs, sizeof(hcs)); zero(regs, sizeof(regs));
    zero(page_state, sizeof(page_state)); zero(retained, sizeof(retained));
    mem_top = mem_frees = page_frees = quarantines = now_us = alloc_count = 0;
    bad_boundary = fail_mem = bm_fail = fail_after_bm = 0;
    no_hcr = no_ownership = no_pes = no_clock = 0;
    event_td = event_cc = event_len = event_status_cc = event_no_response = 0;
    notifications = status_count = gone_count = writes = early_mie = data_bytes = 0;
    actual_reads = sync_dev = sync_cpu = op_seen = 0;
    held_count = 0; hold_active = bad_dma = 0; page_limit = NPAGES;
    for (u32 i = 0; i < 2; ++i) {
        hcs[i].regs = regs[i]; hcs[i].ops = &ohci_ops;
        rput(i, OHCI_FM_INTERVAL, 11999);
        rput(i, OHCI_RH_DESCRIPTOR_A, 3);
    }
}
static void init_x(struct usb_xfer *x, struct usb_dev *d, u8 type, u32 len, int shrt)
{
    zero(d, sizeof(*d)); zero(x, sizeof(*x));
    d->hc = &hcs[0]; d->addr = 5; d->speed = USB_SPEED_FULL;
    x->dev = d; x->pipe.type = type; x->pipe.ep = USB_DIR_IN | 1u;
    x->pipe.mps = 8; x->pipe.interval = 1; x->short_ok = (u8)shrt;
    x->len = len;
    if (len) {
        /* Dedicated bus-backed payload pages for multi-TD tests. */
        page_state[4] = page_state[5] = page_state[6] = 1;
        x->buf.bus = page_bus(4); x->buf.va = pages[4]; x->buf.size = len;
        x->buf.cookie = 5;
    }
    x->setup.type = USB_DIR_IN;
    le16_wr(x->setup.length, (u16)len);
    x->open = 1;
    hcs[0].active = x;
}

static void startup_pool(void)
{
    struct usb_dma d[USB_DMA_BLOCK_MAX], b;
    u32 i;
    fresh(); REQUIRE(ohci_start(&hcs[0]) == 0);
    REQUIRE(op_seen == 1 && sync_dev && !bad_dma);
    REQUIRE(!(rget(0, OHCI_INTERRUPT_ENABLE) & OHCI_INTR_MIE));
    REQUIRE(hcs[0].nports == 3);
    for (i = 0; i < USB_DMA_BLOCK_MAX; ++i) {
        REQUIRE(usb_dmamem_alloc(&hcs[0], 16, 16, &d[i]) == 0);
        REQUIRE(!(d[i].bus & 15u) && ((d[i].bus & 0xffffu) + d[i].size <= 0x10000u));
    }
    REQUIRE(usb_dmamem_alloc(&hcs[0], 16, 16, &b) == -USB_E_NOMEM);
    usb_dmamem_free(&hcs[0], &d[3]);
    REQUIRE(usb_dmamem_alloc(&hcs[0], 16, 16, &b) == 0);
    for (i = 0; i < USB_CFG_BUF_MAX; ++i) {
        REQUIRE(usb_dmamem_alloc(&hcs[0], 512, 512, &b) == 0);
        REQUIRE(!(b.bus & 511u));
    }
    REQUIRE(usb_dmamem_alloc(&hcs[0], 512, 512, &b) < 0);
    for (i = 0; i < USB_CTRL_BUF_MAX + USB_CLASS_BUF_MAX; ++i)
        REQUIRE(usb_dmamem_alloc(&hcs[0], 64, 64, &b) == 0);
    REQUIRE(usb_dmamem_alloc(&hcs[0], 64, 64, &b) < 0);
    fresh(); bad_boundary = 1;
    REQUIRE(usb_dmamem_init(&hcs[0]) < 0);
    REQUIRE(page_state[0] == 0 && !quarantines);
    fresh(); page_limit = 3;
    REQUIRE(ohci_start(&hcs[0]) == 0);
    REQUIRE(ohci_start(&hcs[1]) == -USB_E_NOMEM);
    REQUIRE(hcs[0].state == HC_RUNNING && page_state[0] == 1 && page_state[1] == 1);
    REQUIRE(page_state[2] == 0 && !bad_dma);
    fresh(); no_hcr = 1;
    REQUIRE(ohci_start(&hcs[0]) < 0);
    REQUIRE(hcs[0].state == HC_OFF && page_frees == 2 && !quarantines);
    fresh(); bm_fail = 1;
    REQUIRE(ohci_start(&hcs[0]) < 0);
    REQUIRE(hcs[0].state == HC_DEAD && quarantines == 2 && !page_frees);
    fresh(); fail_after_bm = 1;
    REQUIRE(ohci_start(&hcs[0]) < 0);
    REQUIRE(hcs[0].state == HC_DEAD && quarantines == 2 && !page_frees);
    fresh(); hcs[0].has_irq = 1;
    REQUIRE(ohci_start(&hcs[0]) == 0);
    REQUIRE(!early_mie && (rget(0, OHCI_INTERRUPT_ENABLE) & OHCI_INTR_MIE));
    REQUIRE(ohci_intr_collect(&hcs[0]) == 0); /* shared IRQ belongs to another device */
    rput(0, OHCI_INTERRUPT_STATUS, OHCI_INTR_WDH | OHCI_INTR_RHSC);
    REQUIRE(ohci_intr_collect(&hcs[0]) == (OHCI_INTR_WDH | OHCI_INTR_RHSC));
    REQUIRE(!rget(0, OHCI_INTERRUPT_STATUS));
    REQUIRE(!(rget(0, OHCI_INTERRUPT_ENABLE) & OHCI_INTR_RHSC));
    REQUIRE(sync_cpu && !bad_dma);
}

static void transfer(u8 type, u32 len, u32 at, u32 cc, u32 got, int shrt,
                     u32 status_cc, int result, u32 actual, u32 statuses)
{
    struct usb_xfer x; struct usb_dev d;
    u32 i;
    fresh(); REQUIRE(ohci_start(&hcs[0]) == 0);
    init_x(&x, &d, type, len, shrt);
    REQUIRE(ohci_xfer_alloc(&hcs[0], &x) == 0);
    REQUIRE(usb_xfer_start(&x) == 0);
    event_td = at; event_cc = cc; event_len = got; event_status_cc = status_cc;
    for (i = 0; i < 4 && !notifications; ++i) { frame(0); service(&hcs[0]); }
    REQUIRE(notifications == 1 && x.state == XS_IDLE);
    REQUIRE(x.error == result && x.actual == actual && status_count == statuses);
    REQUIRE(data_bytes == actual);
    if (type == USB_EP_CONTROL && statuses) REQUIRE(x.pipe.toggle == 0);
    if (type == USB_EP_CONTROL && at == 2 && cc == OHCI_CC_STALL) REQUIRE(x.pipe.toggle == 1);
    service(&hcs[0]); REQUIRE(notifications == 1 && !bad_dma);
    /* Normal completion may still be retained until frame end; unsetup waits. */
    frame(0); frame(0); ohci_xfer_free(&hcs[0], &x);
    REQUIRE(x.hcpriv == NULL && !bad_dma);
}
static void transfers(void)
{
    transfer(USB_EP_CONTROL, 8, 0, 0, 0, 0, 0, USB_X_OK, 8, 1);
    if (failure) return;
    transfer(USB_EP_CONTROL, 8, 2, OHCI_CC_STALL, 0, 0, 0, USB_X_STALL, 0, 0);
    if (failure) return;
    transfer(USB_EP_CONTROL, 8, 2, 0, 3, 0, 0, USB_X_SHORT_NG, 3, 1);
    if (failure) return;
    transfer(USB_EP_CONTROL, 8, 2, 0, 3, 0, OHCI_CC_STALL, USB_X_STALL, 3, 1);
    if (failure) return;
    transfer(USB_EP_CONTROL, 8, 2, 0, 3, 1, 0, USB_X_SHORT, 3, 1);
    if (failure) return;
    transfer(USB_EP_CONTROL, 8, 0, 0, 0, 1, 0, USB_X_OK, 8, 1);
    if (failure) return;
    transfer(USB_EP_BULK, 8, 1, 0, 3, 0, 0, USB_X_SHORT_NG, 3, 0);
    if (failure) return;
    transfer(USB_EP_BULK, PAGE * 3u, 2, 0, 7, 1, 0, USB_X_SHORT, PAGE + 7u, 0);
}
static void restart(struct usb_xfer *x) { x->done = NULL; if (usb_xfer_start(x)) bad_dma = 1; }
static void delay_unlink(void)
{
    struct usb_dev d, d2; struct usb_xfer x, y;
    struct ohci_xp *p;
    u32 oldset, oldbus, next, start, n;
    fresh(); REQUIRE(ohci_start(&hcs[0]) == 0);
    init_x(&x, &d, USB_EP_INTERRUPT, 8, 1);
    REQUIRE(ohci_xfer_alloc(&hcs[0], &x) == 0);
    REQUIRE(usb_xfer_start(&x) == 0);
    p = x.hcpriv; oldset = x.curr_set; oldbus = p->sets[oldset].ed->bus;
    x.done = restart;
    frame_begin(0); frame_end(0);
    /* Simulate HC retaining its completed ED until end of this frame. */
    retain(oldbus); retain(p->sets[oldset].first->bus); hold_active = 1;
    service(&hcs[0]);
    REQUIRE(x.curr_set != oldset && x.state == XS_ACTIVE && notifications == 1 && !bad_dma);
    hold_active = 0; zero(retained, sizeof(retained));
    for (n = 0; n < 2; ++n) {
        rput(0, OHCI_FM_NUMBER, 0xffffu);
        frame_begin(0); start = mem_frees;
        usb_xfer_stop(&x);
        REQUIRE(x.state == XS_DMA_DELAY && x.did_dma_delay);
        service(&hcs[0]); REQUIRE(mem_frees == start && notifications == n + 1u);
        frame_end(0); service(&hcs[0]); REQUIRE(notifications == n + 1u);
        frame(0); service(&hcs[0]); REQUIRE(notifications == n + 2u && !bad_dma);
        if (n == 0) REQUIRE(usb_xfer_start(&x) == 0);
    }
    fresh(); REQUIRE(ohci_start(&hcs[0]) == 0);
    init_x(&x, &d, USB_EP_BULK, 8, 1);
    REQUIRE(ohci_xfer_alloc(&hcs[0], &x) == 0); REQUIRE(usb_xfer_start(&x) == 0);
    init_x(&y, &d2, USB_EP_BULK, 8, 1);
    REQUIRE(ohci_xfer_alloc(&hcs[0], &y) == 0); REQUIRE(usb_xfer_start(&y) == 0);
    y.next = &x; p = y.hcpriv;
    next = dma_le32_get(&p->ed->nexted); REQUIRE(next != 0);
    frame_begin(0); usb_xfer_stop(&y);
    REQUIRE(dma_le32_get(&p->ed->nexted) == next);
    frame_end(0); REQUIRE(!bad_dma);
}
static void faults(void)
{
    struct usb_dev d; struct usb_xfer x; struct usb_dma out;
    u32 oldwrites, frees, oldsync, oldreads;
    for (u32 mode = 0; mode < 4; ++mode) {
        fresh(); REQUIRE(ohci_start(&hcs[0]) == 0); REQUIRE(ohci_start(&hcs[1]) == 0);
        init_x(&x, &d, USB_EP_INTERRUPT, 8, 1);
        d.ifs[0].cls = &cls; hcs[0].devices[d.addr] = &d;
        REQUIRE(ohci_xfer_alloc(&hcs[0], &x) == 0); REQUIRE(usb_xfer_start(&x) == 0);
        frame_begin(0); REQUIRE(held_count != 0);
        frees = mem_frees;
        if (mode == 0) {
            rput(0, OHCI_INTERRUPT_STATUS, OHCI_INTR_UE); service(&hcs[0]);
        } else if (mode == 1) {
            usb_xfer_stop(&x); no_clock = 1; now_us += 20000; service(&hcs[0]);
        } else if (mode == 2) {
            x.state = XS_CB_PENDING; usb_hc_dead(&hcs[0], USB_WHY_UNSETUP);
        } else usb_hc_dead(&hcs[0], USB_WHY_ABORT);
        REQUIRE(hcs[0].state == HC_DEAD && gone_count == 1 && quarantines == 2);
        frame_end(0); REQUIRE(!bad_dma);
        frame(0); REQUIRE(!bad_dma);
        oldwrites = writes; oldsync = sync_dev; oldreads = actual_reads;
        usb_hc_dead(&hcs[0], USB_WHY_UE); service(&hcs[0]);
        ohci_scan(&hcs[0], 32); ohci_xfer_close(&hcs[0], &x);
        ohci_xfer_free(&hcs[0], &x); usb_xfer_notify(&x);
        REQUIRE(ohci_intr_collect(&hcs[0]) == 0);
        REQUIRE(ohci_rh_port(&hcs[0], 1, USB_RH_RESET, &out.bus) == -USB_E_HC);
        REQUIRE(usb_dmamem_alloc(&hcs[0], 16, 16, &out) == -USB_E_HC);
        REQUIRE(usb_xfer_start(&x) == -USB_E_HC);
        REQUIRE(writes == oldwrites && sync_dev == oldsync && gone_count == 1 && notifications == 0);
        REQUIRE(actual_reads == oldreads);
        REQUIRE(mem_frees == frees && page_frees == 0 && x.hcpriv);
        REQUIRE(hcs[1].state == HC_RUNNING && !bad_dma);
        frame(1); REQUIRE(ohci_frame(&hcs[1]) == 1);
    }
}
static void periodic(void)
{
    struct usb_dev d; struct usb_xfer x;
    u32 interval, tick, last, count;
    for (interval = 1; interval <= 32; interval *= 2u) {
        fresh(); REQUIRE(!ohci_start(&hcs[0]));
        init_x(&x, &d, USB_EP_INTERRUPT, 8, 1); x.pipe.interval = (u8)interval;
        REQUIRE(!ohci_xfer_alloc(&hcs[0], &x)); REQUIRE(!usb_xfer_start(&x));
        count = 0; last = 0;
        for (tick = 0; tick < 65; ++tick) {
            frame(0); service(&hcs[0]);
            if (x.state == XS_IDLE) {
                if (count) REQUIRE(tick - last == interval);
                ++count; last = tick; REQUIRE(!usb_xfer_start(&x));
            }
        }
        REQUIRE(count == 64u / interval + 1u && !bad_dma);
    }
}
static void root_ports(void)
{
    u32 st;
    fresh(); REQUIRE(ohci_start(&hcs[0]) == 0);
    REQUIRE(ohci_rh_port(&hcs[0], 0, USB_RH_STATUS, &st) == -USB_E_INVAL);
    REQUIRE(ohci_rh_port(&hcs[0], 4, USB_RH_STATUS, &st) == -USB_E_INVAL);
    rput(0, OHCI_RH_PORT_STATUS(1), OHCI_PORT_CCS | OHCI_PORT_CSC);
    REQUIRE(!ohci_rh_port(&hcs[0], 1, USB_RH_CLEAR_CONNECT, &st));
    REQUIRE(st == OHCI_PORT_CCS);
    REQUIRE(!ohci_rh_port(&hcs[0], 1, USB_RH_SET_ENABLE, &st)); REQUIRE(st & OHCI_PORT_PES);
    REQUIRE(!ohci_rh_port(&hcs[0], 1, USB_RH_CLEAR_ENABLE, &st)); REQUIRE(!(st & OHCI_PORT_PES));
    REQUIRE(!ohci_rh_port(&hcs[0], 1, USB_RH_RESET, &st)); REQUIRE(st & OHCI_PORT_PRS);
}
void _start(void)
{
    startup_pool(); if (!failure) transfers();
    if (!failure) delay_unlink();
    if (!failure) faults();
    if (!failure) periodic();
    if (!failure) root_ports();
    if (!failure) say("usb fake OHCI: OK\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failure) : "memory");
    __builtin_unreachable();
}
