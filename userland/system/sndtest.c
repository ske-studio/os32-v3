/* sndtest.c — snd_bgm_play 非ブロッキングBGMテスト */
/* BGMを再生しながらカウンタを表示し、BGM中にSEを割り込ませる */

#include "os32api.h"
#include "os32_kapi_shared.h"

static KernelAPI *kapi;

int main(int argc, char **argv, KernelAPI *api)
{
    int i;
    u32 start;
    (void)argc; (void)argv;
    kapi = api;

    kapi->kprintf(0x07, "=== SND Engine Test ===\n");

    /* BGM再生開始 (非ブロッキング — 即座にリターン) */
    kapi->kprintf(0x07, "Starting BGM (non-blocking)...\n");
    kapi->snd_bgm_play("T120 @C0 @T0 O4 L8 [CDEFGAB>C<]");

    /* BGM再生中にカウンタを表示 */
    kapi->kprintf(0x07, "BGM playing, counting:\n");
    start = kapi->get_tick();
    for (i = 0; i < 30; i++) {
        u32 now = kapi->get_tick();
        kapi->kprintf(0x07, "  tick %d (elapsed %dms) playing=%d\n",
                      i, (now - start) * 10, kapi->snd_bgm_is_playing());

        /* 10カウント目でSEを割り込ませる */
        if (i == 10) {
            kapi->kprintf(0x0E, "  >> SE_SELECT fired!\n");
            kapi->snd_se_play(1);  /* SE_SELECT (FM) */
        }

        /* 20カウント目でSSG SEを割り込ませる */
        if (i == 20) {
            kapi->kprintf(0x0E, "  >> SE_CURSOR fired!\n");
            kapi->snd_se_play(0);  /* SE_CURSOR (SSG) */
        }

        /* 100ms待機 */
        {
            u32 wait_end = kapi->get_tick() + 10;
            while (kapi->get_tick() < wait_end) {
                kapi->sys_halt();
            }
        }
    }

    kapi->kprintf(0x07, "Stopping BGM...\n");
    kapi->snd_bgm_stop();

    kapi->kprintf(0x07, "Test complete!\n");
    return 0;
}
