/* Exercise the paired body and resident stream core through their normal entries. */
#define PCM_HOST_HEAP_BYTES (8192U * PCM_FRAME_BYTES)
#define main original_pcm_main
#include "pcm_cs4231_host.c"
#undef main
#include "sound/hal/snd2.h"
#include "pcm_module.h"

int pcm_module_init(PcmModuleApi *out);
u32 tick_clock_ms(void) { return (u32)(sim_now_us / 1000); }
i32 exec_sys_yield(void) { return 0; }

static void irq_after_failed_stop(void)
{
    u32 before = IO_ACCESSES;
    CHECK(pcm_irq(PCM_IRQ, 0) == IRQ_NONE);
    CHECK(IO_ACCESSES == before);
}

int main(void)
{
    PcmModuleApi api;
    SND2_Caps caps;
    SND2_DevInfo info;
    u32 id;
    reset_all();
    dev_present = 0;
    CHECK(!pcm_module_init(&api) && !snd2_dev_count());
    dev_present = 1;
    CHECK(!pcm_module_init(&api) && snd2_dev_count() == 1);
    CHECK(!snd2_dev_info(0, &info) && (info.flags & SND2_DEV_DEFAULT));
    CHECK(!snd2_caps(info.dev, &caps, sizeof(caps)));
    CHECK(caps.fmt_count == 2 && caps.fmt[0].rate == 44100 && caps.fmt[1].rate == 22050);
    CHECK(caps.period_min == 256 && caps.period_max == 2048 && caps.ring_frames == 4096);
    CHECK(caps.volume_steps == 64 && !caps.hw_mix_streams && !caps.hw_3d_streams && !caps.dsp_units);
    SND2_StreamDesc desc = {.size = sizeof(desc), .dev = info.dev, .fmt = caps.fmt[1],
                            .period_frames = 512, .lead_periods = 3};
    CHECK(!snd2_stream_open(&desc, &id));
    CHECK(sim_irq_registered && pool_used[0] && heap_used && !s_stg);
    CHECK(dev_reg[PCM_I_BASE_LO] == 255 && dev_reg[PCM_I_BASE_HI] == 1);
    CHECK(api.open(PCM_RATE_44100) == PCM_ERR_BUSY);
    CHECK(!snd2_stream_volume(id, 0, 1));
    CHECK(dev_reg[PCM_I_LDA] & PCM_DA_MUTE);
    CHECK(dev_reg[PCM_I_RDA] == PCM_DA_ATT_MAX);
    unsigned char data[512 * PCM_FRAME_BYTES];
    memset(data, 0x53, sizeof(data));
    CHECK(snd2_stream_write(id, data, sizeof(data)) == sizeof(data));
    sim_setup_fail = 1;
    CHECK(snd2_stream_start(id) == OS32_ERR_IO && sim_masked);
    CHECK(!snd2_device(info.dev)->stream.copied);
    sim_setup_fail = 0;
    CHECK(!snd2_stream_start(id) && !sim_masked && (dev_reg[PCM_I_IFACE] & PCM_IFACE_PEN));
    CHECK(!memcmp(pool_mem[0], data, sizeof(data)));
    sim_set_pos(40);
    CHECK(pcm_irq(PCM_IRQ, 0) == IRQ_NONE);
    CHECK(snd2_device(info.dev)->stream.last_hw == 0);
    dev_reg[PCM_I_ALTSTAT] = PCM_ALT_PI;
    CHECK(pcm_irq(PCM_IRQ, 0) == IRQ_HANDLED && !dev_reg[PCM_I_ALTSTAT]);
    CHECK(snd2_device(info.dev)->stream.last_hw == 40);
    sim_set_pos(80);
    api.tick();
    CHECK(snd2_device(info.dev)->stream.last_hw == 80);
    sim_set_pos(10);  /* Backward within the period: stop, prove and re-prime. */
    sim_setup_fail = 1;
    api.tick();
    CHECK(sim_masked && snd2_device(info.dev)->stream.restart_pending);
    sim_setup_fail = 0;
    api.tick();
    CHECK(!sim_masked && !snd2_device(info.dev)->stream.restart_pending);
    CHECK(dev_reg[PCM_I_BASE_LO] == 255 && dev_reg[PCM_I_BASE_HI] == 1);
    CHECK(!memcmp(pool_mem[0], data, sizeof(data)));
    CHECK(api.quiet() && !snd2_dev_count());
    CHECK(sim_masked && !sim_irq_registered && !heap_used && pool_freed[0] == 1);
    CHECK(!pcm_module_init(&api));
    CHECK(!snd2_stream_open(&desc, &id) && !snd2_stream_start(id));
    sim_tick_per_read = 1;
    dev_init_busy = 1;
    isr_site = SITE_IF_ON;
    isr_armed = 1;
    isr_fn = irq_after_failed_stop;
    CHECK(!api.quiet() && snd2_dev_count() == 1);
    CHECK(isr_fired == 1);
    CHECK(pool_leaked[0] == 1 && pool_used[0] && !heap_used && !sim_irq_registered);
    CHECK(snd2_stream_open(&desc, &id) == OS32_ERR_IO);
    CHECK(api.open(PCM_RATE_44100) == OS32_ERR_IO);
    return failed != 0;
}
