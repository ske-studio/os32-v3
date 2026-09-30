/* sndtest2.c — BGM persist テスト */
/* BGM開始 + persist=1 で終了。BGMが後続プログラムでも鳴り続けるか確認 */

#include "os32api.h"
#include "os32_kapi_shared.h"

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc; (void)argv;

    api->kprintf(0x07, "=== BGM Persist Test ===\n");

    /* 直接MML文字列を渡す (クォートなし) */
    api->snd_bgm_set_persist(1);
    api->snd_bgm_play("T120 O4 L4 [CDEFGAB>C<]");

    api->kprintf(0x07, "playing=%d (should be 1)\n", api->snd_bgm_is_playing());
    api->kprintf(0x02, "BGM started with persist=1. Exiting...\n");

    return 0;
}
