#ifdef SND2_WRITER_DIAG
#include "snd_writer_probe.h"
#endif
#include "../../sound/hal/snd2_refill_diag.h"
#include "os32api.h"
#include <string.h>

#define PERIOD 256
#define FRAMES (4 * PERIOD)
static short samples[2 * PERIOD];

static int playback(KernelAPI *api, u32 dev, const char *mode)
{
    SND2_Caps caps;
    SND2_Pos pos;
    u32 h;
    if (api->snd2_caps(dev, &caps, sizeof(caps)) || caps.dev != dev ||
        !caps.fmt_count || !caps.fmt[0].rate || caps.fmt[0].enc != SND2_ENC_S16LE ||
        caps.fmt[0].channels != 2) return 1;
    SND2_StreamDesc desc = {.size = sizeof(desc), .dev = dev, .fmt = caps.fmt[0],
                            .period_frames = PERIOD, .lead_periods = 3};
    if (api->snd2_stream_open(&desc, &h)) return 2;
    /* One cycle of a 1 kHz tone, sampled by phase at the device rate. */
    static const short sine[32] = {
        0, 2341, 4592, 6667, 8485, 9978, 11087, 11769,
        12000, 11769, 11087, 9978, 8485, 6667, 4592, 2341,
        0, -2341, -4592, -6667, -8485, -9978, -11087, -11769,
        -12000, -11769, -11087, -9978, -8485, -6667, -4592, -2341
    };
    int latency = !strcmp(mode, "latency");
    int epoch_test = !strcmp(mode, "epoch");
    int long_run = latency || !strcmp(mode, "wave") || !strcmp(mode, "stall") || epoch_test;
    u32 total = long_run ? 30 * desc.fmt.rate : FRAMES;
    const u32 step = 32000 / desc.fmt.rate, remainder = 32000 % desc.fmt.rate;
    u32 written = 0, index = 0, fraction = 0, frames = 0, sent = 0;
    int diag = (!strcmp(mode, "wave") || !strcmp(mode, "stall")) &&
               !api->snd2_caps(dev | SND2_CAPS_REFILL, &caps, sizeof(caps));
    u32 prev_us = 0, max_writer_us = 0;
    int have_time = 0;
    int stalled = 0, mark_pending = 0, marked = 0;
    u32 mark_frame = 0, write_lo = 0, write_hi = 0;
    SND2_Pos previous = {0}, recovered = {0};
    int armed = 0, changed = 0;
    unsigned full_waits = 0;
    while (written < total) {
        if (diag && written >= FRAMES) {
            u32 now, hi;
            if (api->sys_time_now(&now, &hi)) return 13;
            if (have_time && now - prev_us > max_writer_us) max_writer_us = now - prev_us;
            prev_us = now;
            have_time = 1;
        }
        if (sent == frames) {
            sent = 0;
            frames = total - written;
            if (frames > PERIOD) frames = PERIOD;
            for (u32 i = 0; i < frames; ++i) {
                samples[2*i] = sine[index];
                samples[2*i+1] = ((written+i) % PERIOD) ? 0 : 10000 + ((written+i)/PERIOD) % 16000;
                index += step;
                if (fraction >= desc.fmt.rate - remainder) {
                    fraction -= desc.fmt.rate - remainder;
                    ++index;
                } else {
                    fraction += remainder;
                }
                index &= 31;
            }
        }
        if (latency && written >= FRAMES && !marked) {
            if (!mark_pending) mark_pending = api->kbd_trygetchar() == 'x';
            if (mark_pending) {
                samples[2*sent+1] = -23456;
                if (api->sys_time_now(&write_lo, &write_hi)) return 13;
            }
        }
        int n = api->snd2_stream_write(h, samples + 2*sent, (frames - sent) * 4);
        if (n < 0 || (n & 3) || (u32)n > (frames - sent) * 4) return 3;
        if (!n) {
            /* played can advance inside a period before retirement frees space. */
            if (written < FRAMES || ++full_waits > PERIOD) return 3;
            u32 target = written - FRAMES + (frames - sent);
            target = (target + PERIOD - 1) / PERIOD * PERIOD;
            /* A short retirement can put even an aligned played target
             * inside the next segment. Block on its successor on retry. */
            if (full_waits > 1) target += PERIOD;
            if (api->snd2_stream_wait(h, target, 1000)) return 3;
            continue;
        }
        full_waits = 0;
        if (mark_pending) {
            mark_frame = written;
            marked = 1;
            mark_pending = 0;
        }
        sent += (u32)n / 4;
        u32 before = written;
        written += (u32)n / 4;
        if (before < FRAMES && written >= FRAMES && api->snd2_stream_start(h)) return 4;
        if (epoch_test && !changed && written >= desc.fmt.rate) {
            int key = api->kbd_trygetchar();
            if ((!armed && key == 'b') || (armed && key == 'a')) {
                if (api->snd2_stream_pos(h, &pos)) return 6;
                if (!armed) {
                    previous = pos;
                    armed = 1;
                    api->kprintf(7, "epoch armed epoch=%u played=%u ms=%u\n",
                                 pos.epoch, pos.played, pos.time_ms);
                } else {
                    if (pos.epoch != previous.epoch + 1U) return 6;
                    recovered = pos;
                    changed = 1;
                }
            }
        }
        if (!stalled && !strcmp(mode, "stall") && written >= desc.fmt.rate) {
            u32 start = api->get_tick();
            while (api->get_tick() - start < 20) api->sys_yield();
            if (api->snd2_stream_pos(h, &pos) || !pos.underruns || pos.played > written) return 6;
            stalled = 1;
        }
    }
    if (api->snd2_stream_wait(h, total - PERIOD, 1000)) return 5;
    if (api->snd2_stream_pos(h, &pos) || pos.written != total ||
        pos.played < total - PERIOD || pos.played > pos.written) return 6;
    api->kprintf(7, "written=%u played=%u underruns=%u epoch=%u\n",
                 pos.written, pos.played, pos.underruns, pos.epoch);
    if (diag &&
        !api->snd2_caps(dev | SND2_CAPS_REFILL, &caps, sizeof(caps))) {
        const unsigned long *v = caps.reserved;
#ifdef SND2_WRITER_DIAG
        static const char *const work[] = {
            "other/user", "display_restore", "gfx_poll", "abort_check",
            "park_stop", "io_end", "gui_pump", "idle_work"
        };
        unsigned which = v[SW_WORK] & 0xff, exit_work = v[SW_WORK] >> 8;
        api->kprintf(7, "wait_exit_us=%u exit_write_us=%u ready_write_us=%u pairs=%u exit_work=%s\n",
                     v[SW_WAIT_EXIT], v[SW_EXIT_WRITE], v[SW_TOTAL], v[SW_PAIRS],
                     exit_work < sizeof(work)/sizeof(work[0]) ? work[exit_work] : "unknown");
        api->kprintf(7, "worst_work=%s work_us=%u tsc_gaps_10ms=%u wake_irq=%u wake_ready_us=%u\n",
                     which < sizeof(work)/sizeof(work[0]) ? work[which] : "unknown",
                     v[SW_WORK_US], v[SW_JUMPS], v[SW_WAKE_IRQ], v[SW_WAKE_US]);
#endif
        api->kprintf(7, "max_writer_us=%u\n", max_writer_us);
        api->kprintf(7, "pi=%u tick=%u max_advance_us=%u max_dma_frames=%u min_stg=%u\n",
                     v[SND2_DIAG_PI], v[SND2_DIAG_TICK], v[SND2_DIAG_MAX_US],
                     v[SND2_DIAG_DMA_STEP], v[SND2_DIAG_MIN_STG]);
        if (pos.underruns)
            api->kprintf(7, "gap_frame=%u seg_n=%u next_first=%u event_stg=%u refill_tried=%u\n",
                         v[SND2_DIAG_FRAME], v[SND2_DIAG_SEG_N],
                         v[SND2_DIAG_NEXT_FIRST], v[SND2_DIAG_EVENT_STG], v[SND2_DIAG_REFILL_TRIED]);
    }
    if (!strcmp(mode, "wave") && pos.underruns) return 6;
    if (epoch_test && changed)
        api->kprintf(7, "epoch before=%u played=%u ms=%u after=%u played=%u ms=%u\n",
                     previous.epoch, previous.played, previous.time_ms,
                     recovered.epoch, recovered.played, recovered.time_ms);
    if (epoch_test && (!changed || pos.epoch != recovered.epoch ||
                       pos.played <= recovered.played)) return 6;
    if (latency) {
        if (!marked || pos.underruns) return 13;
        api->kprintf(7, "latency frame=%u write_us_hi=%u write_us_lo=%u\n",
                     mark_frame, write_hi, write_lo);
    }
    if (long_run) return api->snd2_stream_close(h, SND2_CLOSE_DRAIN);
    if (!strcmp(mode, "exit")) return 0;
    if (api->snd2_stream_pause(h)) return 7;
    return api->snd2_stream_close(h, SND2_CLOSE_ABORT);
}

static int chip(KernelAPI *api, u32 dev, int leave_open)
{
    SND2_ChipCaps caps;
    SND2_RegW mute = {.reg = 8, .val = 0};
    u32 h;
    if (api->snd2_chip_caps(dev, &caps, sizeof(caps)) || caps.dev != dev) return 8;
    if (api->snd2_chip_open(dev, &h)) return 9;
    if (api->snd2_chip_write(h, &mute, 1) != 1) return 10;
    if (api->snd2_chip_read(h, 0, 2, mute.reg) != mute.val) return 11;
    return leave_open ? 0 : api->snd2_chip_close(h);
}

int main(int argc, char **argv, KernelAPI *api)
{
    const char *mode = argc == 1 ? "" : argv[1];
    if (argc > 2 || (*mode && strcmp(mode, "exit") && strcmp(mode, "wave") &&
                     strcmp(mode, "latency") && strcmp(mode, "stall") && strcmp(mode, "epoch") && strcmp(mode, "nodev"))) {
        api->kprintf(7, "usage: snd2_test [exit|wave|latency|stall|epoch|nodev]\n");
        return 1;
    }
    int leave_open = !strcmp(mode, "exit");
    int count = api->snd2_dev_count();
    int rc = count < 0 ? count : ((!strcmp(mode, "nodev")) != (count == 0));
    api->kprintf(7, "snd2 devices=%d\n", count);
    for (int i = 0; !rc && i < count; ++i) {
        SND2_DevInfo info;
        rc = api->snd2_dev_info(i, &info);
        if (rc) break;
        api->kprintf(7, "dev=%u kind=%u %s\n", info.dev, info.kind, info.name);
        if (info.kind == SND2_KIND_PCM_OUT) rc = playback(api, info.dev, mode);
        else if (info.kind == SND2_KIND_FM || info.kind == SND2_KIND_PSG)
            rc = chip(api, info.dev, leave_open);
        else rc = 12;
    }
    api->kprintf(7, "snd2_test %s rc=%d\n", rc ? "FAIL" : "PASS", rc);
    return rc != 0;
}
