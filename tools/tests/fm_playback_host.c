/* Real FM/snd sources. Only port I/O, ticks and STOP landing are replaced. */
#include "types.h"
#include "fm.h"
#include "io.h"
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); \
    finish(1); \
} } while (0)
static u32 now, owner = 1, reg, values[256], writes, keyoffs;
static int busy, stop, escaped;
static u32 busy_after;
static void *landing[5];
u32 ktime_ticks(void) { return now; }
int res_owner_get(void) { return owner; }
int ring3_wait_pending(void) { return stop && now >= 5; }
void ring3_abort_check(void)
{
    CHECK((values[OPN_REG_KEY_ONOFF] & OPN_KEY_ALL_SLOTS) == 0);
    escaped = 1;
    __builtin_longjmp(landing, 1);
}
static void port_write(u32 port, u32 value)
{
    if (port == OPN_ADDR) reg = value;
    if (port == OPN_DATA) {
        values[reg] = value; writes++;
        if (reg == OPN_REG_KEY_ONOFF && !(value & OPN_KEY_ALL_SLOTS)) keyoffs++;
    }
}
#undef inp
#undef outp
#undef io_wait
#undef io_wait_n
#undef _halt
#undef irq_save
#undef irq_restore
#define inp(p) ((void)(p), (busy || (busy_after && writes >= busy_after)) ? OPN_BUSY : 0)
#define outp(p,v) port_write(p,v)
#define io_wait() ((void)now++)
#define io_wait_n(n) ((void)(n))
#define _halt() ((void)now++)
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
#define OS32_FM_MODULE 1
#include "../../drivers/sound/fm/fm.c"
#undef tick_count
#include "../../drivers/sound/fm/snd_engine.c"
static void check_se_defaults(void)
{
    static const SndSE_Def expected[6] = {
        { 0, 0, 0, 5, 200, 12, 3 },
        { 1, 1, NOTE(6, N_C), 15, 0, 0, 0 },
        { 0, 0, 0, 8, 800, 15, 2 },
        { 0, 0, 0, 15, 1200, 15, 1 },
        { 1, 1, NOTE(7, N_E), 10, 0, 0, 0 },
        { 0, 0, 0, 10, 400, 15, 0 },
    };
    int i;
    for (i = 0; i < SND_SE_MAX; i++) {
        SndSE_Def *actual = &g_snd.se_table[i];
        SndSE_Def zero = { 0 };
        const SndSE_Def *want = i < 6 ? &expected[i] : &zero;
        CHECK(actual->use_fm == want->use_fm && actual->tone_num == want->tone_num);
        CHECK(actual->note == want->note && actual->duration == want->duration);
        CHECK(actual->ssg_period == want->ssg_period && actual->ssg_vol == want->ssg_vol);
        CHECK(actual->ssg_decay == want->ssg_decay);
    }
    g_snd.se_table[SND_SE_SELECT].duration = 42;
    CHECK(g_snd.se_table[SND_SE_SELECT].duration == 42);
    snd_init();
    CHECK(g_snd.se_table[SND_SE_SELECT].duration == 15);
    snd_se_play(6);
    CHECK(!g_snd.fm_se.active && !g_snd.ssg_se.active);
}

static void check_borrow(void)
{
    SndBorrowState *fm = &g_snd.fm_borrow, *ssg = &g_snd.ssg_borrow;
    snd_init();
    g_snd.bgm.playing = 1;
    g_snd.bgm.pos = 3;
    g_snd.bgm.notes[0] = (SndNote){ .channel = SND_FM_SE_CH, .tone = 1, .note = NOTE(4, N_C) };
    g_snd.bgm.notes[1] = (SndNote){ .channel = SND_SSG_SE_LOGICAL, .tone = 2, .note = NOTE(4, N_E) };
    g_snd.bgm.notes[2] = (SndNote){ .channel = SND_FM_SE_CH, .tone = 3, .note = NOTE(5, N_G) };
    g_snd.bgm.notes[3] = (SndNote){ .channel = 0, .tone = 0, .note = NOTE(4, N_C) };
    snd_se_play(SND_SE_SELECT);
    snd_se_play(SND_SE_CURSOR);
    CHECK(fm->borrowed && fm->saved_was_sounding && fm->saved_tone == 3 && fm->saved_note == NOTE(5, N_G));
    CHECK(ssg->borrowed && ssg->saved_was_sounding && ssg->saved_tone == 2 && ssg->saved_note == NOTE(4, N_E));
    g_snd.bgm.notes[2].note = SND_REST;
    g_snd.bgm.notes[1].note = SND_REST;
    snd_se_play(SND_SE_COIN);
    snd_se_play(SND_SE_BEEP);
    CHECK(fm->saved_was_sounding && fm->saved_note == NOTE(5, N_G));
    CHECK(ssg->saved_was_sounding && ssg->saved_note == NOTE(4, N_E));
    now += 20;
    snd_fm_se_tick(); snd_ssg_se_tick();
    CHECK(!fm->borrowed && !ssg->borrowed);
    CHECK((values[OPN_REG_KEY_ONOFF] & OPN_KEY_ALL_SLOTS) == OPN_KEY_ALL_SLOTS);
    CHECK(values[SSG_REG_VOL_C] == SSG_VOLUME_MAX);
    CHECK(values[SSG_REG_TONE_C_LO] == (ssg_period_from_note(NOTE(4, N_E)) & 0xff));
    snd_se_play(SND_SE_SELECT); snd_se_play(SND_SE_CURSOR);
    CHECK(!fm->saved_was_sounding && !ssg->saved_was_sounding);
    now += 20;
    snd_fm_se_tick(); snd_ssg_se_tick();
    CHECK(!(values[OPN_REG_KEY_ONOFF] & OPN_KEY_ALL_SLOTS));
    CHECK(values[SSG_REG_VOL_C] == 0);
    snd_init();
}

void _start(void)
{
    snd_init();
    check_se_defaults();
    check_borrow();
    snd_bgm_play("T120 O4 L4 CDE");
    CHECK(snd_bgm_is_playing() && (values[OPN_REG_KEY_ONOFF] & OPN_KEY_ALL_SLOTS));
    for (now = 0; now < 300; now++) snd_tick();
    CHECK(!snd_bgm_is_playing());
    snd_bgm_play("O4 C1");
    snd_se_play(SND_SE_SELECT);
    CHECK(g_snd.fm_se.active);
    now += 20; snd_tick(); CHECK(!g_snd.fm_se.active);
    snd_owner_exit(1);
    CHECK(!snd_bgm_is_playing() && !(values[OPN_REG_KEY_ONOFF] & OPN_KEY_ALL_SLOTS));
    now = 0; fm_play_mml_kapi("O4 C");
    CHECK(now >= 20 && !escaped);
    now = 0; stop = 1;
    if (!__builtin_setjmp(landing)) fm_play_mml_kapi("O4 CCCCCCCCC");
    CHECK(escaped && now == 5 && !(values[OPN_REG_KEY_ONOFF] & OPN_KEY_ALL_SLOTS));
    u32 before = keyoffs;
    CHECK(fm_quiet() && keyoffs == before + OPN_CHANNEL_COUNT);
    CHECK(!values[SSG_REG_VOL_A] && !values[SSG_REG_VOL_B] && !values[SSG_REG_VOL_C]);
    CHECK(values[SSG_REG_MIXER] == (SSG_MIXER_ALL_OFF | SSG_MIXER_IO_FLAG));
    before = writes;
    busy_after = writes + 7; /* BUSY after the final mixer write. */
    CHECK(!fm_quiet() && writes == before + 7);
    busy_after = 0;
    busy = 1; before = writes;
    CHECK(!fm_quiet() && writes == before);
    finish(0);
}
