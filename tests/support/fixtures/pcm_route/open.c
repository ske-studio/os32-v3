#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void preempt(unsigned site);
#define PCM_PREEMPT(site) preempt(site)
#include "sys/dev/sound/cs4231/pcm_cs4231.c"

#include "sys/hal/sound/snd2.h"
#include "av_contract.h"

unsigned model_if = 1;
static unsigned frame;
static int playing, resync_wait, resync_masks, observed_pi;


static unsigned expected_irq, expected_route;
static unsigned bios, route, index_reg, regs[32], writes, route_writes;
static int registered, owned, irq_busy, dma_busy, reject_set, reject_restore;
static int retained, readback, stuck, drs_reads, abort_wait;
static int stuck_pi, invalid_readback, init, reserved_bit;
volatile u32 tick_count;

static void require(int ok, const char *message)
{
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}

unsigned inp(unsigned port)
{
    if (port == PCM_PORT_ROUTE) { readback = route == expected_route; return route; }
    if (port == PCM_PORT_WSSID) return PCM_WSSID_VALUE;
    if (port == PCM_PORT_R0) {
        if (stuck || (abort_wait && drs_reads < PCM_STOP_TICKS))
            require(model_if, "DRS wait with IF=0");
        return index_reg | init;
    }
    if (port == PCM_PORT_R1) {
        if (index_reg == PCM_I_ALTSTAT) {
            observed_pi = regs[index_reg] & PCM_ALT_PI;
            return regs[index_reg] | reserved_bit;
        }
        if (index_reg == PCM_I_MODEID) return regs[index_reg] | PCM_ID_VALUE;
        if (index_reg == PCM_I_VERSION) return PCM_VER_CS4231;
        if (index_reg == PCM_I_ERRSTAT && abort_wait) {
            require(!(regs[PCM_I_IFACE] & PCM_IFACE_PEN) &&
                    !(regs[PCM_I_PINCTL] & PCM_PINCTL_IEN), "DRS before halt");
            if (++drs_reads < PCM_STOP_TICKS) {
                ++tick_count;
                return PCM_ERR_DRS;
            }
        }
        if (index_reg == PCM_I_ERRSTAT && stuck) {
            ++tick_count;
            ++drs_reads;
            return PCM_ERR_DRS;
        }
        return regs[index_reg & 31];
    }
    return 0;
}

void outp(unsigned port, unsigned value)
{
    require(owned && registered, "unowned device write");
    ++writes;
    if (abort_wait) require(port != PCM_PORT_R2, "abort wrote R2");
    if (resync_wait) require(port != PCM_PORT_R2, "resync wrote R2");
    if (port == PCM_PORT_ROUTE) {
        ++route_writes;
        if (!(route_writes == 1 ? reject_set : reject_restore)) route = value;
        return;
    }
    require(readback, "codec before route readback");
    if (port == PCM_PORT_R0) index_reg = value;
    if (port == PCM_PORT_R1) {
        if (resync_wait && index_reg == PCM_I_ALTSTAT) {
            require(observed_pi && value == 0, "resync I24 write without observed PI");
            observed_pi = 0;
        }
        require(!model_if, "unlocked codec write");
        if (index_reg == PCM_I_ALTSTAT && stuck_pi) return;
        if (index_reg == PCM_I_ALTSTAT && invalid_readback) init = PCM_R0_INIT;
        regs[index_reg & 31] = value;
    }
}

unsigned int pcm_module_irq_line(void) { return expected_irq; }
int pcm_module_bios_route(void) { return bios; }
int pcm_module_dma_acquire(const void *owner)
{
    require(registered, "DMA before IRQ");
    if (dma_busy) return -1;
    owned = 1;
    return 0;
}
void pcm_module_dma_release(const void *owner, int quarantine)
{
    require(owned, "release without owner");
    owned = 0;
    dma_busy = quarantine;
}
int irq_register(unsigned irq, irq_handler_fn fn, void *arg, unsigned flags)
{
    require(irq == expected_irq, "wrong PCM IRQ");
    if (irq_busy) return -1;
    registered = 1;
    return 0;
}
int irq_unregister(unsigned irq, irq_handler_fn fn, void *arg)
{
    require(irq == expected_irq, "wrong IRQ release");
    require(!owned, "IRQ returned before DMA");
    registered = 0;
    return 0;
}
void dma_chan_mask(unsigned ch, const void *owner)
{
    require(owned && ch == 1, "other owner mask");
    if (abort_wait) require(drs_reads >= PCM_STOP_TICKS, "abort mask before DRS");
    if (resync_wait) {
        require(!(regs[PCM_I_ERRSTAT] & PCM_ERR_DRS) || s_claimed, "mask before DRS");
        ++resync_masks;
    }
    ++writes;
}
void dma_chan_unmask(unsigned ch, const void *owner) { require(owned && ch == 1, "other owner unmask"); }
void dma_chan_ack_tc(unsigned ch, const void *owner) { require(owned && ch == 1, "other owner TC"); }
int dma_chan_setup(unsigned ch, const void *owner, u32 phys, u32 size, int dir, int mode)
{ require(owned && ch == 1, "other owner setup"); return 0; }
int dma_chan_remaining(unsigned ch, const void *owner, u32 *left, int *tc) {
    if (!playing) return -1;
    *left = (PCM_RING_FRAMES - frame) * PCM_FRAME_BYTES;
    *tc = 0;
    return 0;
}
int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *buf)
{ *buf = (struct dma_buf){0x10000, malloc(size), size}; return buf->va ? 0 : -1; }
int dma_free(const struct dma_buf *buf) { free(buf->va); return 0; }
int dma_mark_leaked(const struct dma_buf *buf) { ++retained; return 0; }
void *kmalloc(u32 n) { return malloc(n); }
void kfree(void *p) { free(p); }
void *kmemset(void *p, int v, u32 n) { return memset(p, v, n); }
void *kmemcpy(void *p, const void *s, u32 n) { return memcpy(p, s, n); }
void kprintf(u8 attr, const char *fmt, ...) {}
int sys_time_now(u32 *lo, u32 *hi) { *lo = tick_count * PCM_US_PER_TICK; *hi = 0; return 0; }
void tick_clock_jiffy64(u32 *lo, u32 *hi) { *lo = *hi = 0; }
int res_owner_get(void) { return 1; }

static void preempt(unsigned site)
{
    if (site != PCM_PP_WAIT || !playing) return;
    ++tick_count;
    unsigned next = (frame + 441) % PCM_RING_FRAMES;
    if (next / PCM_HALF_FRAMES != frame / PCM_HALF_FRAMES)
        regs[PCM_I_ALTSTAT] |= PCM_ALT_PI;
    frame = next;
    unsigned flags = irq_save();
    pcm_tick();
    irq_restore(flags);
}

void pcm_module_refill_tick(int dev) {}
u32 tick_clock_ms(void) { return tick_count * PCM_MS_PER_TICK; }
i32 exec_sys_yield(void) { preempt(PCM_PP_WAIT); return 0; }

static void exercise_hal(const char *scene)
{
    static u8 samples[PCM_RING_BYTES];
    SND2_StreamDesc desc = {.size = sizeof(desc),
                            .fmt = {44100, 16, 2, SND2_ENC_S16LE, 0}};
    u32 stream;
    require(!pcm_hw_register(), "register PCM backend");
    require(!snd2_stream_open(&desc, &stream), "HAL open");
    require(snd2_stream_write(stream, samples, sizeof(samples)) == sizeof(samples),
            "HAL write");
    playing = 1;
    require(!snd2_stream_start(stream), "HAL start");
    if (!strncmp(scene, "hal-resync", 10)) {
        int timeout = !strcmp(scene, "hal-resync-timeout");
        resync_wait = 1;
        regs[PCM_I_ERRSTAT] = PCM_ERR_DRS;
        tick_count += 100;
        unsigned flags = irq_save();
        pcm_tick();
        require(!(regs[PCM_I_IFACE] & PCM_IFACE_PEN) && !resync_masks, "pending DRS");
        for (unsigned i = 1; i <= PCM_STOP_TICKS; ++i) {
            ++tick_count;
            if (!timeout && i == PCM_STOP_TICKS) regs[PCM_I_ERRSTAT] = 0;
            regs[PCM_I_ALTSTAT] = PCM_ALT_PI;
            pcm_tick();
            require(!regs[PCM_I_ALTSTAT], "resync pending PI");
            if (i < PCM_STOP_TICKS) require(!resync_masks && !dma_busy, "early timeout");
        }
        irq_restore(flags);
        if (timeout) require(dma_busy && route_writes == 1, "resync timeout quarantine");
        else require(resync_masks && (regs[PCM_I_IFACE] & PCM_IFACE_PEN), "resync restart");
        return;
    }
    if (!strcmp(scene, "hal-abort")) {
        abort_wait = 1;
        regs[PCM_I_ALTSTAT] = PCM_ALT_PI;
        require(!snd2_stream_close(stream, SND2_CLOSE_ABORT) && !registered &&
                !owned && route == bios && !regs[PCM_I_ALTSTAT], "foreground HAL abort");
        return;
    }
    if (!strcmp(scene, "hal-drain")) {
        require(!snd2_stream_close(stream, SND2_CLOSE_DRAIN) &&
                !registered && !owned && route == bios, "foreground HAL drain");
        return;
    }
    stuck = !strcmp(scene, "hal-reclaim");
    stuck_pi = !strcmp(scene, "hal-pi");
    if (stuck_pi) regs[PCM_I_ALTSTAT] = PCM_ALT_PI;
    reject_restore = !stuck && !stuck_pi;
    if (stuck) snd2_reclaim(1);
    else require(snd2_stream_close(stream, SND2_CLOSE_ABORT) == OS32_ERR_IO,
                 "HAL close lost restore failure");
    require(!stuck_pi || route_writes == 1, "HAL restored route with PI");
    require(retained && !registered && dma_busy &&
            snd2_stream_open(&desc, &stream) == OS32_ERR_IO, "HAL quarantine");
    require(!stuck || (drs_reads >= PCM_STOP_TICKS && route_writes == 1),
            "HAL reclaim DRS deadline");
}

int main(int argc, char **argv)
{
    reserved_bit = argc > 3 && !strcmp(argv[3], "reserved-bit") ? 0x80 : 0;
    bios = route = strtoul(argv[1], 0, 0);
    expected_irq = argc > 3 && !strcmp(argv[3], "fm") ? 10 : 12;
    expected_route = expected_irq == 10 ? 0x1a : ((bios & 0xf8) | 2);
    irq_busy = !strcmp(argv[2], "irq");
    dma_busy = !strcmp(argv[2], "dma");
    reject_set = !strcmp(argv[2], "mismatch");
    reject_restore = !strcmp(argv[2], "restore");
    pcm_init();
    if (!strncmp(argv[2], "hal-", 4)) {
        exercise_hal(argv[2]);
        return 0;
    }
    int rc = pcm_open(44100);
    if (irq_busy || !strcmp(argv[2], "dma")) {
        require(rc && !writes && !registered, "busy open wrote hardware");
        return 0;
    }
    if (reject_set) {
        require(rc && retained && route_writes == 1, "unverified route recovery");
        return 0;
    }
    require(!rc && route == expected_route, "open route");
    if (!strcmp(argv[2], "drain")) {
        static u8 samples[PCM_RING_BYTES];
        playing = 1;
        require(pcm_write(samples, sizeof(samples)) == sizeof(samples), "start playback");
        for (unsigned i = 0; i < 8; ++i) preempt(PCM_PP_WAIT);
        require(g_pcm.half == 1 && !regs[PCM_I_ALTSTAT], "recover missed IRQ");
        rc = pcm_close();
        require(!rc && !retained && route == bios, "tick-only drain");
        return 0;
    }
    stuck = !strcmp(argv[2], "drs");
    stuck_pi = !strcmp(argv[2], "pi");
    invalid_readback = !strcmp(argv[2], "pi-readback");
    if (stuck_pi || invalid_readback || !strcmp(argv[2], "pi-clear"))
        regs[PCM_I_ALTSTAT] = PCM_ALT_PI;
    if (stuck) pcm_reclaim(1);
    else rc = pcm_close();
    if (stuck_pi || invalid_readback) require(rc == OS32_ERR_IO && route_writes == 1, "legacy close with PI");
    if (stuck || reject_restore || stuck_pi || invalid_readback) {
        require(retained && dma_busy && !registered, "quarantine");
        require(!stuck || (drs_reads >= PCM_STOP_TICKS && route_writes == 1), "DRS deadline");
        require(pcm_open(44100) == OS32_ERR_IO, "quarantine reopen");
    } else {
        require(!rc && route == bios && !retained && !registered, "restore BIOS route");
        require(!pcm_open(44100) && !pcm_close() && route == bios, "reopen BIOS route");
    }
    return 0;
}
