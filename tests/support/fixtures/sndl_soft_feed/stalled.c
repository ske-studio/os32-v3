/* Run the command through real partial publication, repeated refill and close. */
#include <stdio.h>
#include <stdlib.h>
#include "userland/lib/snd/pump.c"
#include "userland/lib/snd/chip.c"
int open_soft(const SNDL_ChipReq *, u32 *, SNDL_ChipGot *);
#define main v86_command_main
#define sndl_chip_open open_soft
#include "userland/cmds/v86.c"
#undef main
#undef sndl_chip_open

KernelAPI *sound_api;
static u32 accepted, ms, polls, short_writes, stop_after, closed, ended;
static void require(int ok, const char *why)
{
    if (!ok) {
        fprintf(stderr, "%s\n", why);
        exit(1);
    }
}
static int retained(void)
{
    if (buffered - sent < 128) return 0;
    for (unsigned i = 0; i < 128; ++i)
        if (scratch[2 * (sent + i)] != (int)(129 + i)) return 0;
    return 1;
}
static int begin(const V86_Begin *b, u32 *sid) { *sid = 1; return 0; }
static int run(u32 sid, const V86_RunIn *in, V86_Exit *ex)
{
    require(in->slice_ms == 0 && accepted == 1024 && short_writes == 1 && retained(),
            "partial refill entered DOS");
    require(++polls <= 5, "unbounded refill retry");
    ex->guest_us = ms * 1000;
    ex->reason = polls == stop_after ? V86S_END : V86S_SLICE;
    ex->code = polls == stop_after ? 5 : 0;
    return 0;
}
static int end(u32 sid)
{
    require(closed && !refs && !slots[0].fm, "refill cleanup");
    ++ended;
    return 0;
}
int sndl_init(KernelAPI *table) { sound_api = table; return 0; }
int open_soft(const SNDL_ChipReq *req, u32 *chip, SNDL_ChipGot *got)
{
    refs = stream = slots[0].id = *chip = 1;
    slots[0].fm = (FmVoice *)&slots[0];
    got->backend = SNDL_SOFT;
    accepted = written = 896;
    rate = 44100;
    pcm_period = buffered = 256;
    feed_mode = NORMAL_PUMP;
    sent = 0;
    for (unsigned i = 0; i < buffered; ++i) scratch[2 * i] = i + 1;
    return 0;
}
static int position(u32 id, SND2_Pos *pos)
{
    *pos = (SND2_Pos){.cap=1024, .state=SND2_ST_RUN, .written=accepted, .time_ms=++ms};
    return 0;
}
static int publish(u32 id, const void *src, u32 bytes)
{
    unsigned n = 1024 - accepted;
    if (n > bytes/4) n = bytes/4;
    if (n && n < bytes/4) ++short_writes;
    accepted += n;
    return 4*n;
}
static int close_pcm(u32 id, u32 how)
{
    require(how == SND2_CLOSE_ABORT && retained(), "refill abort close");
    ++closed;
    return 0;
}
int fmv_audio_due(const FmVoice *fm, uint32_t until)
{
    require(0, "refill abort close");
    return 0;
}
void fmv_delete(FmVoice *fm) { (void)fm; }
int fmv_write(FmVoice *fm, uint32_t at, unsigned reg, unsigned value) { return 0; }
int fmv_render_part(FmVoice *fm, uint32_t now, unsigned char *out,
                    uint32_t frames, uint32_t offset, uint32_t n) { return 0; }
static int clock_us(u32 *lo, u32 *hi) { *lo = ms * 1000; *hi = 0; return 0; }
static i32 yield(void) { return 0; }
int main(void)
{
    static KernelAPI table = {.v86s_begin=begin, .v86s_run=run, .v86s_end=end,
        .snd2_stream_pos=position, .snd2_stream_write=publish, .snd2_stream_close=close_pcm,
        .sys_time_now=clock_us, .sys_yield=yield};
    api = &table;
    for (stop_after = 0; stop_after <= 2; stop_after += 2) {
        polls = closed = ended = ms = short_writes = 0;
        int rc = boot_sound("tone.img", 0);
        require(rc == (stop_after ? 5 : OS32_ERR_TIMEOUT) && closed == 1 && ended == 1,
                "refill stop or deadline");
        require(stop_after ? polls == 2 : ms >= 67 && ms < 90, "refill absolute deadline");
        printf("stalled DMA: %s rc=%d polls=%lu elapsed=%lu ms suffix=128 close=%lu end=%lu\n",
               stop_after ? "CTRL+STOP" : "timeout", rc, polls, ms, closed, ended);
    }
    return 0;
}
