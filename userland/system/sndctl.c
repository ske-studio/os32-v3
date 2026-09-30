/* ======================================================================== */
/*  SNDCTL.C — サウンドエンジン制御コマンド                                 */
/*                                                                          */
/*  使い方:                                                                 */
/*    sndctl play "T120 O4 [CDEFGAB>C<]"   BGM再生 (非ブロッキング)         */
/*    sndctl stop                           BGM停止                         */
/*    sndctl status                         再生状態表示                     */
/*    sndctl se <id>                        SE再生 (0-15)                    */
/*    sndctl mute                           マスターミュート                 */
/*    sndctl unmute                         マスターミュート解除             */
/* ======================================================================== */

#include "os32api.h"
#include "os32_kapi_shared.h"

static KernelAPI *kapi;

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int str_to_int(const char *s)
{
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

static void show_usage(void)
{
    kapi->kprintf(0x07, "sndctl - Sound Engine Control\n");
    kapi->kprintf(0x07, "Usage:\n");
    kapi->kprintf(0x07, "  sndctl play \"MML\"  BGM play (non-blocking)\n");
    kapi->kprintf(0x07, "  sndctl stop        BGM stop\n");
    kapi->kprintf(0x07, "  sndctl status      Show playback status\n");
    kapi->kprintf(0x07, "  sndctl se <id>     Play SE (0-15)\n");
    kapi->kprintf(0x07, "  sndctl mute        Master mute\n");
    kapi->kprintf(0x07, "  sndctl unmute      Master unmute\n");
    kapi->kprintf(0x07, "\nSE IDs:\n");
    kapi->kprintf(0x07, "  0: Cursor  1: Select  2: Cancel\n");
    kapi->kprintf(0x07, "  3: Error   4: Coin    5: Beep\n");
}

int main(int argc, char **argv, KernelAPI *api)
{
    kapi = api;

    if (argc < 2) {
        show_usage();
        return 0;
    }

    if (str_eq(argv[1], "play")) {
        if (argc < 3) {
            kapi->kprintf(0x04, "Error: MML string required\n");
            return 1;
        }
        kapi->snd_bgm_set_persist(1);
        kapi->snd_bgm_play(argv[2]);
        kapi->kprintf(0x02, "BGM started\n");
        return 0;
    }

    if (str_eq(argv[1], "stop")) {
        kapi->snd_bgm_set_persist(0);
        kapi->snd_bgm_stop();
        kapi->kprintf(0x02, "BGM stopped\n");
        return 0;
    }

    if (str_eq(argv[1], "status")) {
        int playing = kapi->snd_bgm_is_playing();
        kapi->kprintf(0x07, "BGM: %s\n", playing ? "playing" : "stopped");
        return 0;
    }

    if (str_eq(argv[1], "se")) {
        int id;
        if (argc < 3) {
            kapi->kprintf(0x04, "Error: SE ID required (0-15)\n");
            return 1;
        }
        id = str_to_int(argv[2]);
        kapi->snd_se_play(id);
        kapi->kprintf(0x02, "SE %d fired\n", id);
        return 0;
    }

    if (str_eq(argv[1], "mute")) {
        kapi->snd_set_master(0);
        kapi->kprintf(0x02, "Sound muted\n");
        return 0;
    }

    if (str_eq(argv[1], "unmute")) {
        kapi->snd_set_master(1);
        kapi->kprintf(0x02, "Sound unmuted\n");
        return 0;
    }

    kapi->kprintf(0x04, "Unknown subcommand: %s\n", argv[1]);
    show_usage();
    return 1;
}
